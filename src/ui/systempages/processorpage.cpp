#include "processorpage.h"
#include "systemreport.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

SystemProcessorPage::SystemProcessorPage(QWidget *parent) : QWidget(parent)
{
    const auto page = reportPageLayout(this);
    auto *fields = new QWidget(this);
    form_ = new QGridLayout(fields);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setColumnStretch(1, 1);
    page.contents->addWidget(fields);
    page.contents->addStretch();
    const auto field = [&](QGridLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        const bool table = key == "core_groups" || key == "cache_topology";
        if (!table) captionsWidgets_.append(name);
        auto *value = propertyValueLabel(tr("Not collected"), form->parentWidget());
        const int row = form->count() ? form->rowCount() : 0;
        if (table) {
            QFont font = name->font(); font.setBold(true); name->setFont(font);
            form->addWidget(name, row, 0, 1, 2);
            delete value;
            auto *view = new ReportTable(form->parentWidget());
            view->setSelectionBehavior(QAbstractItemView::SelectItems);
            tables_.insert(key, view);
            form->addWidget(view, row + 1, 0, 1, 2);
        } else {
            form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, row, 1);
        }
        if (!fields_.contains(key)) {
            captions_.insert(key, caption);
            order_.append(key);
        }
        if (!table) fields_[key].append(value);
    };

    auto *processor = form_;
    field(processor, "cpu", systemFieldCaption(SystemPage::Processor, "cpu"));
    field(processor, "sockets", systemFieldCaption(SystemPage::Processor, "sockets"));
    field(processor, "cores", systemFieldCaption(SystemPage::Processor, "cores"));
    field(processor, "logical", systemFieldCaption(SystemPage::Processor, "logical"));
    field(processor, "smt", systemFieldCaption(SystemPage::Processor, "smt"));
    const QString featuresCaption = systemFieldCaption(SystemPage::Processor, "instruction_sets");
    auto *featuresLabel = new QLabel(featuresCaption, processor->parentWidget());
    captionsWidgets_.append(featuresLabel);
    featuresCopy_ = new QPushButton(tr("Copy"), processor->parentWidget());
    featuresCopy_->setAccessibleName(tr("Copy instruction sets and CPU features"));
    featuresCopy_->setEnabled(false);
    const int featuresRow = processor->rowCount();
    processor->addWidget(featuresLabel, featuresRow, 0);
    processor->addWidget(featuresCopy_, featuresRow, 1, Qt::AlignLeading);
    captions_.insert("instruction_sets", featuresCaption);
    order_.append("instruction_sets");
    connect(featuresCopy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(displayValue("instruction_sets", snapshot_.values.value("instruction_sets")));
    });
    field(processor, "core_groups", systemFieldCaption(SystemPage::Processor, "core_groups"));
    field(processor, "cache_topology", systemFieldCaption(SystemPage::Processor, "cache_topology"));
}
int SystemProcessorPage::captionWidthHint() const
{
    int width = 0;
    for (const QLabel *label : captionsWidgets_) width = qMax(width, label->sizeHint().width());
    return width;
}
void SystemProcessorPage::setCaptionWidth(int width)
{
    form_->setColumnMinimumWidth(0, width);
}
QString SystemProcessorPage::displayValue(const QString &key, const Attribute &value) const
{
    return systemProcessorValue(snapshot_, key, value);
}
void SystemProcessorPage::setSnapshot(const SystemProperties &snapshot)
{
    snapshot_ = snapshot;
    for (const QString &key : order_) {
        QString source = snapshot_.sources.value(key);
        if (key == "smt")
            source += '\n' + tr("SMT/HT support and policy reported by the kernel; core-group thread counts include online CPUs only.");
        if (key == "cores" || key == "sockets")
            source += '\n' + tr("Counts from kernel-reported topology for present CPUs.");
        if (key == "logical")
            source += '\n' + tr("Present and online counts are combined when equal; otherwise both are shown.");
        if (key == "core_groups")
            source += '\n' + tr("Maximum frequencies are kernel-reported limits, not live clocks. Core and thread counts include online CPUs only.");
        if (key == "instruction_sets")
            source += '\n' + tr("Kernel-exposed instruction-set and CPU-feature flags for processors listed in /proc/cpuinfo. "
                "These include non-instruction features and may be limited by the kernel, operating system or hypervisor. "
                "Different reported feature lists remain separate; this is not a complete raw hardware capability inventory.");
        const QString hint = "<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
        if (ReportTable *view = tables_.value(key, nullptr)) {
            view->clear();
            const auto table = systemProcessorTable(snapshot_, key, true);
            const bool hasRows = !table.rows.isEmpty();
            SystemProcessorTable detailed;
            if (key == "cache_topology" && hasRows)
                detailed = systemProcessorTable(snapshot_, key);
            view->setColumnCount(hasRows ? table.headers.size() : 1);
            view->setRowCount(hasRows ? table.rows.size() : 1);
            view->horizontalHeader()->setVisible(hasRows);
            if (hasRows) view->setHorizontalHeaderLabels(table.headers);
            for (int row = 0; row < view->rowCount(); ++row) {
                const QStringList cells = hasRows ? table.rows.at(row) : QStringList{table.status};
                for (int column = 0; column < cells.size(); ++column) {
                    auto *item = new QTableWidgetItem(cells[column]);
                    if (!detailed.rows.isEmpty() && column == 2)
                        item->setToolTip(detailed.rows.at(row).at(column));
                    view->setItem(row, column, item);
                }
            }
            view->fitContents();
            view->setToolTip(hint);
        }
        for (QLabel *value : fields_.value(key)) {
            value->setText(displayValue(key, snapshot_.values.value(key)));
            value->setToolTip(hint);
        }
    }
    const Attribute features = snapshot_.values.value("instruction_sets");
    featuresCopy_->setEnabled(features.state == ReadState::Available && !features.value.isEmpty());
    featuresCopy_->setToolTip(features.state == ReadState::Available
        ? tr("Copy kernel-reported instruction sets and CPU features to the clipboard.")
        : propertyReadValue(features));
}
QString SystemProcessorPage::copyText() const
{
    QStringList lines;
    for (const QString &key : order_) {
        if (tables_.contains(key)) {
            lines.append(captions_.value(key) + ':');
            const auto table = systemProcessorTable(snapshot_, key);
            if (table.rows.isEmpty()) {
                lines.append(table.status);
            } else {
                lines.append(table.headers.join('\t'));
                for (const auto &row : table.rows) lines.append(row.join('\t'));
            }
        } else {
            lines.append(captions_.value(key) + ": " + displayValue(key, snapshot_.values.value(key)));
        }
    }
    return lines.join('\n');
}
