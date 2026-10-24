#include "camerapage.h"
#include "cameraproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"
#include "reporttable.h"

#include <QApplication>
#include <QClipboard>
#include <QGroupBox>
#include <QJsonArray>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>

CameraPage::CameraPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ Driver-reported camera capabilities snapshot"),
        tr("Queries only: no capture, format changes or measured frame rates. Reload properties to refresh. "
        "Rows preserve the format, size and frame interval relationship. Size ranges do not imply a common rate list."),
        tr("Copy camera properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void CameraPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void CameraPage::setSnapshot(const CameraProperties &data, const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto errorText = [&](int error) {
        if (error == EBUSY) return tr("Device busy");
        if (error == ETIMEDOUT) return tr("Query timed out");
        if (error == EAGAIN) return tr("Previous camera query has not exited; retry later");
        if (error == EOVERFLOW) return tr("Enumeration or report limit reached");
        if (error == EPROTO) return tr("Malformed driver response");
        const ReadState state = error == EACCES || error == EPERM ? ReadState::PermissionDenied
            : error == ENODEV || error == ENOENT || error == ENXIO ? ReadState::Removed
            : error == EINVAL || error == ENOTTY || error == EOPNOTSUPP ? ReadState::Unsupported : ReadState::Error;
        return propertyReadValue({state, {}, error});
    };
    const QJsonObject &report = data.report;
    const QJsonArray modes = report.value("modes").toArray();
    auto *identity = group(tr("Capture device"));
    const QString querySource = data.node + ": VIDIOC_QUERYCAP via libv4l2 native passthrough";
    field(identity, tr("Device node"), data.node.isEmpty() ? tr("Unavailable") : data.node, devicePath + " / udev devnode; verified sysfs dev and /sys/dev/char binding");
    QString status = data.state.state == ReadState::Available ? tr("Driver-reported snapshot")
        : data.state.error ? errorText(data.state.error) : propertyReadValue(data.state);
    if (data.state.state != ReadState::Available && !modes.isEmpty()) status = tr("Incomplete enumeration: %1").arg(status);
    field(identity, tr("Query status"), status, data.node + ": read-only capability enumeration; two-second worker wait budget");
    const QVector<QPair<QString, QString>> identityFields {{"card", tr("Device name")}, {"driver", tr("Driver")}, {"bus", tr("Bus information")}};
    for (const auto &entry : identityFields) {
        const QString value = report.value(entry.first).toString();
        if (report.contains(entry.first)) field(identity, entry.second, value.isEmpty() ? tr("Unavailable") : value, querySource);
    }
    auto *scope = group(tr("Collection scope"));
    field(scope, tr("Coverage"), tr("This capture node and current media pipeline. No capture or configuration changes. "
        "Reported modes do not guarantee throughput or compatibility with other simultaneous streams."), querySource);
    fieldsReport.alignCaptions();
    if (!modes.isEmpty()) {
        const QString title = tr("Format, size and frame rate combinations");
        auto *box = new QGroupBox(title, groups_->parentWidget());
        auto *layout = new QVBoxLayout(box);
        auto *table = new ReportTable(static_cast<int>(modes.size()), 4, box);
        const QStringList headers {tr("Capture queue"), tr("Pixel format"), tr("Resolution / range"), tr("Frame rates / intervals")};
        table->setHorizontalHeaderLabels(headers);
        table->setSelectionBehavior(QAbstractItemView::SelectItems);
        layout->addWidget(table); groups_->addWidget(box);
        copy.append(QString()); copy.append(title);
        const auto fractionText = [](const QJsonArray &f) { return QString::number(f.at(0).toInteger()) + '/' + QString::number(f.at(1).toInteger()); };
        const auto fps = [](const QJsonArray &f) { return static_cast<double>(f.at(1).toInteger()) / static_cast<double>(f.at(0).toInteger()); };
        for (qsizetype index = 0; index < modes.size(); ++index) {
            const QJsonObject row = modes.at(index).toObject();
            const quint32 code = static_cast<quint32>(row.value("format").toInteger());
            QString fourcc;
            for (int shift : {0, 8, 16, 24}) {
                const unsigned c = (code >> shift) & (shift == 24 ? 127 : 255);
                fourcc += c >= 32 && c <= 126 ? QString(QChar(static_cast<ushort>(c))) : QString("\\x%1").arg(c, 2, 16, QChar('0'));
            }
            QString format = fourcc + QString(" (0x%1)").arg(code, 8, 16, QChar('0'));
            if (code & 0x80000000U) format += tr(" (big endian)");
            const QString description = row.value("description").toString();
            if (!description.isEmpty()) format += " — " + description;
            const quint32 flags = static_cast<quint32>(row.value("flags").toInteger());
            if (flags & 1) format += tr(" (compressed)");
            if (flags & 2) format += tr(" (emulated, reported by driver)");
            // Keep the complete driver report for Copy, with concise names in the table.
            QString displayFormat;
            if (fourcc == "MJPG") displayFormat = "MJPEG";
            else if (fourcc == "YUYV") displayFormat = "YUYV 4:2:2";
            else if (description == fourcc || description.startsWith(fourcc + ' ')) displayFormat = description;
            else displayFormat = description.isEmpty() ? fourcc : fourcc + " — " + description;
            if (code & 0x80000000U) displayFormat += tr(" (big endian)");
            if ((flags & 1) && fourcc != "MJPG") displayFormat += tr(" (compressed)");
            if (flags & 2) displayFormat += tr(" (emulated, reported by driver)");
            const QString kind = row.value("size_kind").toString();
            QString size;
            if (row.contains("size_error")) size = errorText(row.value("size_error").toInt());
            else if (kind == "discrete") size = tr("%1 × %2").arg(row.value("width").toInteger()).arg(row.value("height").toInteger());
            else {
                size = tr("%1–%2 × %3–%4 pixels").arg(row.value("width").toInteger()).arg(row.value("max_width").toInteger())
                    .arg(row.value("height").toInteger()).arg(row.value("max_height").toInteger());
                size += kind == "continuous" ? tr(" (continuous)")
                    : tr(" (steps: %1 × %2 pixels)").arg(row.value("step_width").toInteger()).arg(row.value("step_height").toInteger());
            }
            QString rates;
            const QJsonObject interval = row.value("intervals").toObject();
            if (kind != "discrete") rates = tr("Not enumerated: intervals depend on the exact size");
            else if (interval.contains("error")) rates = errorText(interval.value("error").toInt());
            else if (interval.value("kind") == "discrete") {
                QStringList values;
                for (const QJsonValue &value : interval.value("values").toArray()) {
                    const QJsonArray f = value.toArray();
                    values.append(tr("%1 fps (%2 s)").arg(QLocale().toString(fps(f), 'g', 8), fractionText(f)));
                }
                rates = values.join('\n');
            } else {
                const QJsonArray minimum = interval.value("min").toArray(), maximum = interval.value("max").toArray();
                rates = tr("%1–%2 fps; intervals %3–%4 s").arg(QLocale().toString(fps(maximum), 'g', 8), QLocale().toString(fps(minimum), 'g', 8), fractionText(minimum), fractionText(maximum));
                rates += interval.value("kind") == "continuous" ? tr(" (continuous)")
                    : tr(" (interval step: %1 s)").arg(fractionText(interval.value("step").toArray()));
            }
            const QStringList values {row.value("queue") == "multi" ? tr("Multiple planes") : tr("Single plane"), displayFormat, size, rates};
            QStringList copyValues = values;
            copyValues[1] = format;
            const QString source = data.node + ": VIDIOC_ENUM_FMT / VIDIOC_ENUM_FRAMESIZES / VIDIOC_ENUM_FRAMEINTERVALS\n"
                + tr("Intervals queried only for this pixel format and exact discrete size. Ranges are not expanded.");
            copy.append(QString()); copy.append(tr("Mode %1").arg(index + 1));
            for (int column = 0; column < values.size(); ++column) {
                auto *item = new QTableWidgetItem(values.at(column));
                item->setToolTip(source); item->setTextAlignment(Qt::AlignLeading | Qt::AlignTop);
                table->setItem(static_cast<int>(index), column, item);
                copy.append(headers.at(column) + ": " + copyValues.at(column));
            }
        }
        table->fitContents();
    }
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
