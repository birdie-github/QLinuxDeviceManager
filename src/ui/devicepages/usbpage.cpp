#include "usbpage.h"
#include "deviceproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"

#include <QApplication>
#include <QClipboard>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>
#include <cmath>

UsbPage::UsbPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ USB configuration snapshot"),
        tr("Power information is declared by the active USB configuration, not measured consumption. "
        "USB version and speed are also available as raw values on Details. Reload properties to refresh."),
        tr("Copy USB properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void UsbPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void UsbPage::setSnapshot(const UsbProperties &data, const Attribute &version,
                          const Attribute &speed, const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto value = [&](const QString &key) { return propertyReadValue(data.values.value(key)); };
    const auto source = [&](const QString &key) { return devicePath + '/' + key; };
    auto *connection = group(tr("Connection"));
    field(connection, tr("USB version"), propertyReadValue(version), source("version")
        + '\n' + tr("USB specification version reported by the device descriptor; not the port's maximum capability."));
    QString speedText = propertyReadValue(speed);
    if (speed.state == ReadState::Available) {
        bool valid = false;
        const double mbps = speed.value.toDouble(&valid);
        if (speed.value.compare("unknown", Qt::CaseInsensitive) == 0)
            speedText = propertyReadValue({ReadState::Unavailable, {}, 0});
        else if (!valid || !std::isfinite(mbps) || mbps <= 0)
            speedText = propertyReadValue({ReadState::Error, {}, EINVAL});
        else speedText = mbps >= 1000
            ? tr("%1 Gbps").arg(QLocale().toString(mbps / 1000, 'g', 6))
            : tr("%1 Mbps").arg(QLocale().toString(mbps, 'g', 6));
    }
    field(connection, tr("Connection speed"), speedText, source("speed")
        + '\n' + tr("Kernel-reported link rate, not measured throughput. For root hubs this describes the USB bus."));
    auto *power = group(tr("Power declarations"));
    const Attribute attributes = data.values.value("bmAttributes");
    bool valid = false;
    const uint flags = attributes.value.toUInt(&valid, 16);
    QString supply = propertyReadValue(attributes), wake = supply;
    if (attributes.state == ReadState::Available) {
        if (!valid || flags > 255 || !(flags & 0x80)) {
            supply = wake = propertyReadValue({ReadState::Error, {}, EINVAL});
        } else {
            supply = flags & 0x40 ? tr("Self-powered") : tr("Bus-powered");
            wake = flags & 0x20 ? tr("Supported") : tr("Not supported");
        }
    }
    field(power, tr("Power source"), supply, source("bmAttributes"));
    field(power, tr("Declared maximum current"), value("bMaxPower"), source("bMaxPower"));
    field(power, tr("Remote wakeup capability"), wake, source("bmAttributes"));
    auto *configuration = group(tr("Configuration"));
    const Attribute active = data.values.value("bConfigurationValue");
    field(configuration, tr("Active configuration"), active.state == ReadState::Available && (active.value.isEmpty() || active.value == "0")
        ? tr("Unconfigured") : propertyReadValue(active), source("bConfigurationValue"));
    field(configuration, tr("Available configurations"), value("bNumConfigurations"), source("bNumConfigurations"));
    const Attribute description = data.values.value("configuration");
    field(configuration, tr("Description"), description.state == ReadState::Available && description.value.isEmpty()
        ? tr("Not provided") : propertyReadValue(description), source("configuration"));
    field(configuration, tr("Interface count"), value("bNumInterfaces"), source("bNumInterfaces"));
    // Class descriptions are deliberately limited; retain numeric codes for
    // vendor-specific and unfamiliar functions rather than guessing a name.
    const auto classText = [&](const Attribute &attribute) {
        if (attribute.state != ReadState::Available) return propertyReadValue(attribute);
        bool ok = false;
        const uint code = attribute.value.toUInt(&ok, 16);
        if (!ok || code > 255) return propertyReadValue({ReadState::Error, {}, EINVAL});
        QString name;
        switch (code) {
        case 0x01: name = tr("Audio"); break;
        case 0x02: name = tr("Communications"); break;
        case 0x03: name = tr("Human interface device"); break;
        case 0x07: name = tr("Printer"); break;
        case 0x08: name = tr("Mass storage"); break;
        case 0x09: name = tr("Hub"); break;
        case 0x0a: name = tr("CDC data"); break;
        case 0x0b: name = tr("Smart card"); break;
        case 0x0e: name = tr("Video"); break;
        case 0xe0: name = tr("Wireless controller"); break;
        case 0xef: name = tr("Miscellaneous"); break;
        case 0xfe: name = tr("Application-specific"); break;
        case 0xff: name = tr("Vendor-specific"); break;
        }
        const QString hex = QStringLiteral("0x%1").arg(code, 2, 16, QLatin1Char('0'));
        return name.isEmpty() ? hex : tr("%1 (%2)").arg(name, hex);
    };
    if (data.interfaceState.state != ReadState::Available || data.interfaces.isEmpty()) {
        auto *interfaces = group(tr("Interfaces"));
        field(interfaces, tr("Interfaces"), data.interfaceState.state == ReadState::Available
            ? tr("None observed") : propertyReadValue(data.interfaceState), devicePath);
    }
    for (const UsbInterfaceProperties &interface : data.interfaces) {
        auto *form = group(tr("Interface %1").arg(interface.name));
        if (interface.values.contains("state")) {
            field(form, tr("Status"), propertyReadValue(interface.values.value("state")), interface.path);
            continue;
        }
        const auto entry = [&](const QString &key, const QString &caption) {
            field(form, caption, propertyReadValue(interface.values.value(key)), interface.path + '/' + key);
        };
        entry("bInterfaceNumber", tr("Number (hex)"));
        entry("bAlternateSetting", tr("Alternate setting"));
        const Attribute label = interface.values.value("interface");
        field(form, tr("Description"), label.state == ReadState::Available && label.value.isEmpty()
            ? tr("Not provided") : propertyReadValue(label), interface.path + "/interface");
        field(form, tr("Function class"), classText(interface.values.value("bInterfaceClass")), interface.path + "/bInterfaceClass");
        entry("bInterfaceSubClass", tr("Subclass (hex)"));
        entry("bInterfaceProtocol", tr("Protocol (hex)"));
        const Attribute driver = interface.values.value("driver");
        field(form, tr("Bound driver"), driver.state == ReadState::Available && driver.value.isEmpty()
            ? tr("Unbound") : propertyReadValue(driver), interface.path + "/driver");
    }
    fieldsReport.alignCaptions();
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
