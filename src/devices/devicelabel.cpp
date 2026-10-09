#include "devicelabel.h"
#include "efivariables.h"
#include <QCoreApplication>
#include <QRegularExpression>
#include <QStringList>

namespace {
QString caption(const char *text)
{
    return QCoreApplication::translate("DeviceLabels", text);
}
QString unquote(QString value)
{
    if (value.size() >= 2 && value.startsWith('"') && value.endsWith('"'))
        value = value.mid(1, value.size() - 2);
    return value;
}
QString supplyField(const Device &d, const char *attribute, const char *property)
{
    const auto it = d.attributes.constFind(QLatin1String(attribute));
    if (it != d.attributes.cend())
        return it->state == ReadState::Available ? it->value.trimmed() : QString();
    return d.properties.value(QLatin1String(property)).trimmed();
}
QString friendlyLabel(const Device &d)
{
    if (d.subsystem == "power_supply") {
        const QString type = supplyField(d, "type", "POWER_SUPPLY_TYPE");
        if (type == "Mains") return caption(QT_TRANSLATE_NOOP("DeviceLabels", "AC power adapter"));
        if (type == "Battery") {
            const QString vendor = supplyField(d, "manufacturer", "POWER_SUPPLY_MANUFACTURER");
            QString model = supplyField(d, "model_name", "POWER_SUPPLY_MODEL_NAME");
            // Firmware role placeholders add no useful model identity.
            if (model.compare("Primary", Qt::CaseInsensitive) == 0
                || model.compare("Secondary", Qt::CaseInsensitive) == 0
                || model.compare("Battery", Qt::CaseInsensitive) == 0) model.clear();
            QString label = vendor.isEmpty() ? caption(QT_TRANSLATE_NOOP("DeviceLabels", "Battery"))
                : caption(QT_TRANSLATE_NOOP("DeviceLabels", "%1 battery")).arg(vendor);
            if (!model.isEmpty()) label += QStringLiteral(" (%1)").arg(model);
            return label;
        }
        if (type == "USB") {
            // UCSI names append the connector number to the controller's name.
            // Match the recorded direct parent, not an ambiguous trailing digit.
            const QString controller = d.parentPath.section('/', -1);
            const QString prefix = "ucsi-source-psy-" + controller;
            bool valid = false;
            const QString suffix = d.sysname.startsWith(prefix) ? d.sysname.mid(prefix.size()) : QString();
            const uint port = suffix.toUInt(&valid);
            if (!controller.isEmpty() && valid && port > 0)
                return caption(QT_TRANSLATE_NOOP("DeviceLabels", "USB-C power supply (port %1)")).arg(port);
            return caption(QT_TRANSLATE_NOOP("DeviceLabels", "USB power supply"));
        }
    }
    if (isAudioJackSwitch(d)) {
        const QString name = inputDeviceName(d);
        if (name.endsWith(" Headphone") || name == "Headphone")
            return caption(QT_TRANSLATE_NOOP("DeviceLabels", "Headphone jack detection"));
        if (name.endsWith(" Mic") || name == "Mic")
            return caption(QT_TRANSLATE_NOOP("DeviceLabels", "Microphone jack detection"));
        static const QRegularExpression pcm(QStringLiteral("HDMI/DP,pcm=([0-9]+)$"));
        return caption(QT_TRANSLATE_NOOP("DeviceLabels", "HDMI/DisplayPort jack detection (PCM %1)"))
            .arg(pcm.match(inputDeviceName(d)).captured(1));
    }
    if (d.subsystem == "input") {
        // Shorten only the kernel-generated I2C HID identity form. Product names
        // from USB/Bluetooth and descriptive input names remain untouched.
        static const QRegularExpression generated(QStringLiteral(
            "^([A-Z0-9]{4,8}:[0-9]{2}) ([0-9A-Fa-f]{4}):([0-9A-Fa-f]{4}) (Mouse|Touchpad)$"));
        const auto match = generated.match(inputDeviceName(d));
        const QStringList product = d.properties.value("PRODUCT").split('/');
        bool valid = false;
        const uint bus = product.size() == 4 ? product[0].toUInt(&valid, 16) : 0;
        if (match.hasMatch() && valid && bus == 0x18) {
            bool vendorValid = false, productValid = false;
            const uint vendor = product[1].toUInt(&vendorValid, 16);
            const uint id = product[2].toUInt(&productValid, 16);
            if (!vendorValid || !productValid || vendor != match.captured(2).toUInt(nullptr, 16)
                || id != match.captured(3).toUInt(nullptr, 16)) return {};
            if (match.captured(4) == "Touchpad" && d.properties.value("ID_INPUT_TOUCHPAD") == "1")
                return caption(QT_TRANSLATE_NOOP("DeviceLabels", "Touchpad (%1)")).arg(match.captured(1));
            if (match.captured(4) == "Mouse" && d.properties.value("ID_INPUT_MOUSE") == "1"
                && d.properties.value("ID_INPUT_POINTINGSTICK") != "1")
                return caption(QT_TRANSLATE_NOOP("DeviceLabels", "Mouse (%1)")).arg(match.captured(1));
        }
    }
    return {};
}
}
QString inputDeviceName(const Device &d)
{
    const auto it = d.attributes.constFind("name");
    if (it != d.attributes.cend() && it->state == ReadState::Available && !it->value.isEmpty()) return it->value;
    return unquote(d.properties.value("NAME"));
}
bool isAudioJackSwitch(const Device &d)
{
    static const QRegularExpression name(QStringLiteral("(?:^| )(?:HDMI/DP,pcm=[0-9]+|Headphone|Mic)$"));
    return d.subsystem == "input" && d.properties.value("ID_INPUT_SWITCH") == "1"
        && unquote(d.properties.value("PHYS")) == "ALSA"
        && d.properties.value("ID_INPUT_KEYBOARD") != "1"
        && d.properties.value("ID_INPUT_MOUSE") != "1"
        && d.properties.value("ID_INPUT_TOUCHPAD") != "1"
        && d.properties.value("ID_INPUT_POINTINGSTICK") != "1"
        && name.match(inputDeviceName(d)).hasMatch();
}
QString deviceDisplayName(const Device &d, bool includeIdentifiers)
{
    if (d.subsystem == "efivarfs") {
        const QString label = efiVariableLabel(d.name);
        return includeIdentifiers ? label + QStringLiteral(" [%1]").arg(d.properties.value("EFI_VENDOR_GUID")) : label;
    }
    QString label = friendlyLabel(d);
    if (!label.isEmpty())
        return includeIdentifiers ? label + QStringLiteral(" [%1]").arg(d.sysname) : label;
    if (d.name == d.sysname || d.nameSource == "direct kernel driver and kernel name") return d.name;
    label = d.nameTranslated ? QCoreApplication::translate("DeviceFunctions", d.name.toUtf8().constData()) : d.name;
    if (d.nameCandidate) label = caption(QT_TRANSLATE_NOOP("DeviceLabels", "%1 (module candidate)")).arg(label);
    return label + QStringLiteral(" [%1]").arg(d.sysname);
}
