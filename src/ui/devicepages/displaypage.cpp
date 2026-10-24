#include "displaypage.h"
#include "displayproperties.h"
#include "monitorproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"

#include <QApplication>
#include <QClipboard>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>

DisplayPage::DisplayPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ Monitor hardware and display configuration snapshot"),
        tr("Monitor information is reported by this connector’s cached EDID and kernel mode list; "
        "it does not guarantee capabilities. Active settings are shown separately below. Resolution and refresh rate describe the configured physical mode. "
        "Desktop color depth is not the panel or link depth. Scale excludes Qt application scaling. "
        "Wayland scale derived from logical size may include rounding; integer scale is a fallback. "
        "Reload properties to refresh. Unmatched or ambiguous connector names remain unavailable."),
        tr("Copy display properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void DisplayPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void DisplayPage::setSnapshot(const DisplayProperties &data, const MonitorProperties &monitor,
                              const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };

    if (monitor.applicable) {
        const auto hardwareValue = [&](const QString &key) { return propertyReadValue(monitor.values.value(key)); };
        const auto hardwareField = [&](QGridLayout *form, const QString &caption, const QString &key) {
            field(form, caption, hardwareValue(key), monitor.sources.value(key));
        };
        auto *identity = group(tr("Monitor hardware (reported)"));
        QString descriptorStatus;
        if (monitor.descriptor.state == ReadState::Available) {
            descriptorStatus = monitor.values.contains("warnings") ? tr("Decoded with conformance warnings") : tr("Decoded by libdisplay-info");
        } else if (monitor.descriptor.state == ReadState::Unsupported && monitor.descriptor.value.isEmpty()) {
            descriptorStatus = tr("EDID decoding unavailable: build with libdisplay-info support");
        } else if (monitor.descriptor.state == ReadState::Unsupported) {
            descriptorStatus = tr("Unsupported EDID version: %1").arg(monitor.descriptor.value);
        } else if (monitor.descriptor.state == ReadState::Error && monitor.descriptor.error == EPROTO) {
            descriptorStatus = tr("Malformed EDID (invalid header, length, checksum or parser rejection)");
        } else if (monitor.descriptor.state == ReadState::Error && monitor.descriptor.error == EAGAIN) {
            descriptorStatus = tr("Monitor report changed during collection; reload properties");
        } else descriptorStatus = propertyReadValue(monitor.descriptor);
        field(identity, tr("Descriptor"), descriptorStatus, monitor.sources.value("descriptor"));
        if (monitor.values.contains("version")) hardwareField(identity, tr("EDID version"), "version");
        if (monitor.descriptor.state == ReadState::Available) {
            hardwareField(identity, tr("Manufacturer"), "manufacturer");
            hardwareField(identity, tr("Manufacturer code"), "manufacturer_code");
            hardwareField(identity, tr("Model"), "model");
            hardwareField(identity, tr("Product code"), "product");
            hardwareField(identity, tr("Serial number"), "serial");
            const Attribute size = monitor.values.value("size");
            const QStringList dimensions = size.value.split(' ');
            field(identity, tr("Image dimensions"), size.state == ReadState::Available
                ? tr("%1 × %2 cm").arg(dimensions.value(0), dimensions.value(1)) : propertyReadValue(size), monitor.sources.value("size"));
            const QString ppi = monitorPixelsPerInch(monitor);
            if (!ppi.isEmpty()) field(identity, tr("PPI"), QLocale().toString(ppi.toLongLong()), tr("Pixels per inch"));
            if (monitor.values.contains("warnings")) hardwareField(identity, tr("Conformance warnings"), "warnings");
            auto *capabilities = group(tr("Monitor input (reported)"));
            const Attribute digital = monitor.values.value("digital");
            field(capabilities, tr("Signal type"), digital.state == ReadState::Available
                ? (digital.value == "1" ? tr("Digital") : tr("Analog")) : propertyReadValue(digital), monitor.sources.value("digital"));
            if (digital.state == ReadState::Available && digital.value == "1") {
                hardwareField(capabilities, tr("Input interface"), "interface");
                const Attribute depth = monitor.values.value("depth");
                field(capabilities, tr("Declared color depth"), depth.state == ReadState::Available
                    ? tr("%1 bits per color component").arg(depth.value) : propertyReadValue(depth), monitor.sources.value("depth"));
            }
        }
        auto *reportedModes = group(tr("Kernel-reported modes"));
        hardwareField(reportedModes, tr("Mode names"), "modes");
        field(reportedModes, tr("Scope"), tr("Cached mode names; refresh rates are not exposed here. "
            "The list may include driver additions or restrictions and does not guarantee monitor capabilities."), monitor.sources.value("modes"));
    }
    if (data.status.state == ReadState::Available && data.status.value == "disconnected") {
        auto *session = group(tr("Desktop session"));
        field(session, tr("Configuration"), tr("Disconnected"), data.sources.value("configuration"));
        fieldsReport.alignCaptions();
        groups_->addStretch();
        copyText_ = copy.join('\n');
        return;
    }
    const auto raw = [&](const QString &key) {
        return data.status.state == ReadState::Available ? data.values.value(key) : Attribute {data.status.state, {}, data.status.error};
    };
    const auto formatted = [&](const QString &key) {
        const Attribute value = raw(key);
        if (value.state != ReadState::Available) return propertyReadValue(value);
        if (key == "resolution") return QString(value.value).replace('x', QChar(0x00d7));
        if (key == "depth") return tr("%1 bits per pixel").arg(value.value);
        if (key == "refresh") return tr("%1 Hz").arg(QLocale().toString(value.value.toDouble(), 'f', 2));
        if (key == "rotation") return value.value == "0" ? tr("0°")
            : tr("%1° counterclockwise").arg(value.value);
        if (key == "reflection") return value.value == "xy" ? tr("X and Y axes") : value.value == "x" ? tr("X axis")
            : value.value == "y" ? tr("Y axis") : value.value == "vertical" ? tr("Vertical axis") : tr("None");
        if (key == "scale") {
            if (value.value == "custom") return tr("Custom transform");
            const QStringList axes = value.value.split(' ');
            const double x = axes.value(0).toDouble(), y = axes.value(1).toDouble();
            QString text = axes.value(0) == axes.value(1) ? tr("%1×").arg(QLocale().toString(x, 'g', 6))
                : tr("X: %1×, Y: %2×").arg(QLocale().toString(x, 'g', 6), QLocale().toString(y, 'g', 6));
            const QString kind = data.values.value("scale_kind").value;
            if (kind == "derived") text += tr(" (derived)");
            if (kind == "integer") text += tr(" (integer buffer scale)");
            return text;
        }
        return value.value;
    };
    auto *session = group(tr("Desktop session"));
    field(session, tr("Backend"), data.backend.isEmpty() ? tr("Unavailable") : data.backend);
    field(session, tr("Output"), data.output.isEmpty() ? tr("Unavailable") : data.output,
        data.sources.value("output", devicePath));
    field(session, tr("Configuration"), data.status.state == ReadState::Available
        ? (data.status.value == "inactive" ? tr("No active mode") : tr("Reported by desktop server"))
        : propertyReadValue(data.status), data.sources.value("configuration", tr("Read-only session query. Unavailable can mean missing runtime libraries, "
            "an output not exposed by this session, or a protocol without usable output names.")));
    auto *mode = group(tr("Active display mode"));
    field(mode, tr("Resolution"), formatted("resolution"), data.sources.value("resolution"));
    field(mode, tr("Desktop color depth"), formatted("depth"), data.sources.value("depth"));
    field(mode, tr("Refresh rate"), formatted("refresh"), data.sources.value("refresh"));
    auto *transform = group(tr("Orientation and scale"));
    field(transform, tr("Rotation"), formatted("rotation"), data.sources.value("rotation"));
    field(transform, tr("Reflection"), formatted("reflection"), data.sources.value("reflection"));
    field(transform, tr("Output scale"), formatted("scale"), data.sources.value("scale"));
    fieldsReport.alignCaptions();
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
