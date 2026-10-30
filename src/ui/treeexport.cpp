#include "treeexport.h"
#include "iconcredits.h"
#include <QAbstractItemModel>
#include <QBuffer>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QCoreApplication>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QSaveFile>
#include <QTreeView>

namespace {
QString text(const char *source)
{
    return QCoreApplication::translate("TreeExport", source);
}
QString singleLine(QString label)
{
    // A device name must not create extra rows or indentation in the text file.
    for (QChar &character : label)
        if (character.isSpace()) character = QLatin1Char(' ');
    return label;
}
TreeExportSnapshot capture(const QTreeView &tree, const QString &view, const QString &query, bool icons8)
{
    TreeExportSnapshot result {view, query, QDateTime::currentDateTimeUtc().toString(Qt::ISODate), {}};
    result.icons8 = icons8;
    const auto *model = tree.model();
    if (!model) return result;
    QHash<qint64, int> iconCache;
    QHash<QByteArray, int> images;
    const auto captureIcon = [&](const QModelIndex &index) {
        const QIcon icon = qvariant_cast<QIcon>(index.data(Qt::DecorationRole));
        if (icon.isNull()) return -1;
        const qint64 key = icon.cacheKey();
        const auto cached = iconCache.constFind(key);
        if (cached != iconCache.cend()) return cached.value();
        // Fixed 32px raster displayed at 16px: sharp on high-DPI screens and
        // independent of SVG plugins or theme files on the recipient's system.
        QImage image(32, 32, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        {
            QPainter painter(&image);
            icon.paint(&painter, QRect(0, 0, 32, 32), Qt::AlignCenter, QIcon::Normal, QIcon::Off);
        }
        QByteArray png;
        QBuffer buffer(&png);
        if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
            iconCache.insert(key, -1);
            return -1;
        }
        auto found = images.constFind(png);
        int id;
        if (found != images.cend()) id = found.value();
        else {
            id = static_cast<int>(result.icons.size());
            result.icons.append(png);
            images.insert(png, id);
        }
        iconCache.insert(key, id);
        return id;
    };
    struct Level { QModelIndex parent; int next = 0; };
    QVector<Level> stack {{tree.rootIndex(), 0}};
    while (!stack.isEmpty()) {
        auto &level = stack.last();
        if (level.next >= model->rowCount(level.parent)) { stack.removeLast(); continue; }
        const QModelIndex index = model->index(level.next++, 0, level.parent);
        const bool branch = model->rowCount(index) > 0;
        result.rows.append({singleLine(index.data(Qt::DisplayRole).toString()),
            static_cast<int>(stack.size() - 1), branch, tree.isExpanded(index), captureIcon(index)});
        if (branch) stack.append({index, 0});
    }
    return result;
}
}

QByteArray treeExportText(const TreeExportSnapshot &snapshot)
{
    QByteArray output;
    for (const auto &row : snapshot.rows) {
        output += QByteArray(row.depth * 2, ' ');
        output += singleLine(row.label).toUtf8();
        output += '\n';
    }
    return output;
}

QByteArray treeExportHtml(const TreeExportSnapshot &snapshot)
{
    const QString title = text("Device tree — %1").arg(snapshot.view);
    QString html = QStringLiteral("<!doctype html>\n<html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<title>%1</title>\n").arg(title.toHtmlEscaped());
    html += QStringLiteral(R"(<style>
:root { color-scheme: light dark; }
body { font-family: system-ui, sans-serif; margin: 1.5rem; color: CanvasText; background: Canvas; }
h1 { font-size: 1.4rem; }
p { overflow-wrap: anywhere; }
button { font: inherit; padding: .35rem .7rem; margin: 0 .5rem .8rem 0; cursor: pointer; }
ul { list-style: none; padding-left: 1.4rem; margin: 0; }
.tree { padding-left: 0; }
li { margin: .25rem 0; }
summary { cursor: pointer; }
summary, .leaf { white-space: pre-wrap; overflow-wrap: anywhere; }
.leaf { display: block; padding-left: 1rem; }
.icon { width: 16px; height: 16px; margin-right: .35rem; vertical-align: -.15em; }
.icon-definitions { position: absolute; width: 0; height: 0; overflow: hidden; }
footer { margin-top: 1.5rem; padding-top: .5rem; border-top: 1px solid; font-size: .875rem; }
@media print { button { display: none; } }
</style></head><body>
)");
    if (!snapshot.icons.isEmpty()) {
        // Inline SVG references reuse each embedded PNG without JavaScript or
        // repeating its base64 data for every device of the same type.
        html += QStringLiteral("<svg xmlns=\"http://www.w3.org/2000/svg\" class=\"icon-definitions\" aria-hidden=\"true\"><defs>\n");
        for (qsizetype id = 0; id < snapshot.icons.size(); ++id) {
            if (snapshot.icons[id].isEmpty()) continue;
            html += QStringLiteral("<image id=\"tree-icon-%1\" width=\"32\" height=\"32\" href=\"data:image/png;base64,%2\"/>\n")
                .arg(id).arg(QString::fromLatin1(snapshot.icons[id].toBase64()));
        }
        html += QStringLiteral("</defs></svg>\n");
    }
    html += QStringLiteral("<h1>%1</h1><p>%2</p>\n").arg(title.toHtmlEscaped(),
        text("Snapshot: %1").arg(snapshot.capturedAt).toHtmlEscaped());
    if (!snapshot.query.isEmpty())
        html += QStringLiteral("<p>%1</p>\n").arg(text("Search filter: %1").arg(snapshot.query).toHtmlEscaped());
    html += QStringLiteral("<button type=\"button\" id=\"expand\">%1</button>"
        "<button type=\"button\" id=\"collapse\">%2</button>\n<ul class=\"tree\">\n")
        .arg(text("Expand all").toHtmlEscaped(), text("Collapse all").toHtmlEscaped());
    int depth = 0;
    for (const auto &row : snapshot.rows) {
        while (depth > row.depth) { html += QStringLiteral("</ul></details></li>\n"); --depth; }
        QString label = singleLine(row.label).toHtmlEscaped();
        if (row.icon >= 0 && row.icon < snapshot.icons.size() && !snapshot.icons[row.icon].isEmpty())
            label.prepend(QStringLiteral("<svg class=\"icon\" viewBox=\"0 0 32 32\" aria-hidden=\"true\" focusable=\"false\"><use href=\"#tree-icon-%1\"/></svg>").arg(row.icon));
        if (row.branch) {
            html += QStringLiteral("<li><details%1><summary>%2</summary><ul>\n")
                .arg(row.expanded ? QStringLiteral(" open") : QString(), label);
            ++depth;
        } else html += QStringLiteral("<li><span class=\"leaf\">%1</span></li>\n").arg(label);
    }
    while (depth-- > 0) html += QStringLiteral("</ul></details></li>\n");
    html += QStringLiteral("</ul>\n");
    if (snapshot.icons8)
        html += QStringLiteral("<footer><p>%1</p></footer>\n").arg(icons8AttributionHtml());
    html += QStringLiteral(R"(<script>
for (const [id, open] of [['expand', true], ['collapse', false]]) {
    document.getElementById(id).addEventListener('click', () => {
        for (const branch of document.querySelectorAll('details')) branch.open = open;
    });
}
</script>
</body></html>
)");
    return html.toUtf8();
}

void exportDeviceTree(QTreeView *tree, const QString &view, const QString &query, bool icons8)
{
    // A modal file dialog processes live updates; own the snapshot before entering it.
    const TreeExportSnapshot snapshot = capture(*tree, view, query, icons8);
    const QString htmlFilter = text("Interactive HTML (*.html)");
    const QString textFilter = text("Plain text (*.txt)");
    QFileDialog dialog(tree, text("Export device tree"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setNameFilters({htmlFilter, textFilter});
    dialog.setDefaultSuffix("html");
    dialog.selectFile("device-tree");
    QObject::connect(&dialog, &QFileDialog::filterSelected, &dialog, [&dialog, textFilter](const QString &filter) {
        dialog.setDefaultSuffix(filter == textFilter ? "txt" : "html");
    });
    if (dialog.exec() != QDialog::Accepted || dialog.selectedFiles().isEmpty()) return;
    const QString path = dialog.selectedFiles().first();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const bool html = suffix == "html" || suffix == "htm"
        || (suffix != "txt" && dialog.selectedNameFilter() == htmlFilter);
    const QByteArray output = html ? treeExportHtml(snapshot) : treeExportText(snapshot);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(output) != output.size() || !file.commit())
        QMessageBox::warning(tree, text("Export device tree"),
            text("Could not save %1:\n%2").arg(path, file.errorString()));
}
