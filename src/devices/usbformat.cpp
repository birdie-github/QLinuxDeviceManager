#include "usbproperties.h"
#include <QCoreApplication>
#include <QLocale>
#include <cerrno>
#include <cmath>

QVector<UsbSection> usbPropertySections(const UsbProperties &data, const Attribute &version,
                                      const Attribute &speed, const QString &devicePath)
{
    QVector<UsbSection> sections;
    const auto group = [&](const QString &title) {
        sections.append({title, {}});
        return sections.size() - 1;
    };
    const auto field = [&](qsizetype section, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        sections[section].fields.append({{}, caption, text, source});
    };
    const auto value = [&](const QString &key) { return propertyReadValue(data.values.value(key)); };
    const auto source = [&](const QString &key) { return devicePath + '/' + key; };
    auto connection = group(QCoreApplication::translate("PropertiesDialog", "Connection"));
    field(connection, QCoreApplication::translate("PropertiesDialog", "USB version"), propertyReadValue(version), source("version")
        + '\n' + QCoreApplication::translate("PropertiesDialog", "USB specification version reported by the device descriptor; not the port's maximum capability."));
    QString speedText = propertyReadValue(speed);
    if (speed.state == ReadState::Available) {
        bool valid = false;
        const double mbps = speed.value.toDouble(&valid);
        if (speed.value.compare("unknown", Qt::CaseInsensitive) == 0)
            speedText = propertyReadValue({ReadState::Unavailable, {}, 0});
        else if (!valid || !std::isfinite(mbps) || mbps <= 0)
            speedText = propertyReadValue({ReadState::Error, {}, EINVAL});
        else speedText = mbps >= 1000
            ? QCoreApplication::translate("PropertiesDialog", "%1 Gbps").arg(QLocale().toString(mbps / 1000, 'g', 6))
            : QCoreApplication::translate("PropertiesDialog", "%1 Mbps").arg(QLocale().toString(mbps, 'g', 6));
    }
    field(connection, QCoreApplication::translate("PropertiesDialog", "Connection speed"), speedText, source("speed")
        + '\n' + QCoreApplication::translate("PropertiesDialog", "Kernel-reported link rate, not measured throughput. For root hubs this describes the USB bus."));
    auto power = group(QCoreApplication::translate("PropertiesDialog", "Power declarations"));
    const Attribute attributes = data.values.value("bmAttributes");
    bool valid = false;
    const uint flags = attributes.value.toUInt(&valid, 16);
    QString supply = propertyReadValue(attributes), wake = supply;
    if (attributes.state == ReadState::Available) {
        if (!valid || flags > 255 || !(flags & 0x80)) {
            supply = wake = propertyReadValue({ReadState::Error, {}, EINVAL});
        } else {
            supply = flags & 0x40 ? QCoreApplication::translate("PropertiesDialog", "Self-powered") : QCoreApplication::translate("PropertiesDialog", "Bus-powered");
            wake = flags & 0x20 ? QCoreApplication::translate("PropertiesDialog", "Supported") : QCoreApplication::translate("PropertiesDialog", "Not supported");
        }
    }
    field(power, QCoreApplication::translate("PropertiesDialog", "Power source"), supply, source("bmAttributes"));
    field(power, QCoreApplication::translate("PropertiesDialog", "Declared maximum current"), value("bMaxPower"), source("bMaxPower"));
    field(power, QCoreApplication::translate("PropertiesDialog", "Remote wakeup capability"), wake, source("bmAttributes"));
    auto configuration = group(QCoreApplication::translate("PropertiesDialog", "Configuration"));
    const Attribute active = data.values.value("bConfigurationValue");
    field(configuration, QCoreApplication::translate("PropertiesDialog", "Active configuration"), active.state == ReadState::Available && (active.value.isEmpty() || active.value == "0")
        ? QCoreApplication::translate("PropertiesDialog", "Unconfigured") : propertyReadValue(active), source("bConfigurationValue"));
    field(configuration, QCoreApplication::translate("PropertiesDialog", "Available configurations"), value("bNumConfigurations"), source("bNumConfigurations"));
    const Attribute description = data.values.value("configuration");
    field(configuration, QCoreApplication::translate("PropertiesDialog", "Description"), description.state == ReadState::Available && description.value.isEmpty()
        ? QCoreApplication::translate("PropertiesDialog", "Not provided") : propertyReadValue(description), source("configuration"));
    field(configuration, QCoreApplication::translate("PropertiesDialog", "Interface count"), value("bNumInterfaces"), source("bNumInterfaces"));
    // Class descriptions are deliberately limited; retain numeric codes for
    // vendor-specific and unfamiliar functions rather than guessing a name.
    const auto classText = [&](const Attribute &attribute) {
        if (attribute.state != ReadState::Available) return propertyReadValue(attribute);
        bool ok = false;
        const uint code = attribute.value.toUInt(&ok, 16);
        if (!ok || code > 255) return propertyReadValue({ReadState::Error, {}, EINVAL});
        QString name;
        switch (code) {
        case 0x01: name = QCoreApplication::translate("PropertiesDialog", "Audio"); break;
        case 0x02: name = QCoreApplication::translate("PropertiesDialog", "Communications"); break;
        case 0x03: name = QCoreApplication::translate("PropertiesDialog", "Human interface device"); break;
        case 0x07: name = QCoreApplication::translate("PropertiesDialog", "Printer"); break;
        case 0x08: name = QCoreApplication::translate("PropertiesDialog", "Mass storage"); break;
        case 0x09: name = QCoreApplication::translate("PropertiesDialog", "Hub"); break;
        case 0x0a: name = QCoreApplication::translate("PropertiesDialog", "CDC data"); break;
        case 0x0b: name = QCoreApplication::translate("PropertiesDialog", "Smart card"); break;
        case 0x0e: name = QCoreApplication::translate("PropertiesDialog", "Video"); break;
        case 0xe0: name = QCoreApplication::translate("PropertiesDialog", "Wireless controller"); break;
        case 0xef: name = QCoreApplication::translate("PropertiesDialog", "Miscellaneous"); break;
        case 0xfe: name = QCoreApplication::translate("PropertiesDialog", "Application-specific"); break;
        case 0xff: name = QCoreApplication::translate("PropertiesDialog", "Vendor-specific"); break;
        }
        const QString hex = QStringLiteral("0x%1").arg(code, 2, 16, QLatin1Char('0'));
        return name.isEmpty() ? hex : QCoreApplication::translate("PropertiesDialog", "%1 (%2)").arg(name, hex);
    };
    if (data.interfaceState.state != ReadState::Available || data.interfaces.isEmpty()) {
        auto interfaces = group(QCoreApplication::translate("PropertiesDialog", "Interfaces"));
        field(interfaces, QCoreApplication::translate("PropertiesDialog", "Interfaces"), data.interfaceState.state == ReadState::Available
            ? QCoreApplication::translate("PropertiesDialog", "None observed") : propertyReadValue(data.interfaceState), devicePath);
    }
    for (const UsbInterfaceProperties &interface : data.interfaces) {
        auto form = group(QCoreApplication::translate("PropertiesDialog", "Interface %1").arg(interface.name));
        if (interface.values.contains("state")) {
            field(form, QCoreApplication::translate("PropertiesDialog", "Status"), propertyReadValue(interface.values.value("state")), interface.path);
            continue;
        }
        const auto entry = [&](const QString &key, const QString &caption) {
            field(form, caption, propertyReadValue(interface.values.value(key)), interface.path + '/' + key);
        };
        entry("bInterfaceNumber", QCoreApplication::translate("PropertiesDialog", "Number (hex)"));
        entry("bAlternateSetting", QCoreApplication::translate("PropertiesDialog", "Alternate setting"));
        const Attribute label = interface.values.value("interface");
        field(form, QCoreApplication::translate("PropertiesDialog", "Description"), label.state == ReadState::Available && label.value.isEmpty()
            ? QCoreApplication::translate("PropertiesDialog", "Not provided") : propertyReadValue(label), interface.path + "/interface");
        field(form, QCoreApplication::translate("PropertiesDialog", "Function class"), classText(interface.values.value("bInterfaceClass")), interface.path + "/bInterfaceClass");
        entry("bInterfaceSubClass", QCoreApplication::translate("PropertiesDialog", "Subclass (hex)"));
        entry("bInterfaceProtocol", QCoreApplication::translate("PropertiesDialog", "Protocol (hex)"));
        const Attribute driver = interface.values.value("driver");
        field(form, QCoreApplication::translate("PropertiesDialog", "Bound driver"), driver.state == ReadState::Available && driver.value.isEmpty()
            ? QCoreApplication::translate("PropertiesDialog", "Unbound") : propertyReadValue(driver), interface.path + "/driver");
    }
    return sections;
}
