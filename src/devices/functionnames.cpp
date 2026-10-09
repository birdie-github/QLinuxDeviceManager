#include "functionnames.h"
#include <QCoreApplication>
#include <QStringList>

FunctionName knownFunctionName(const Device &d)
{
    struct Entry { const char *id; const char *label; };
    // Standard ACPI/PNP identities, not vendor guesses or device-name prefixes.
    // Semantic reference: open-acpica/acpica source/common/ahids.c.
    static const Entry ids[] = {
        {"ACPI0003", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI power source")},
        {"ACPI0007", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI processor device")},
        {"ACPI0008", QT_TRANSLATE_NOOP("DeviceFunctions", "Ambient light sensor")},
        {"ACPI000C", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI processor aggregator")},
        {"ACPI000E", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI time and alarm device")},
        {"ACPI000F", QT_TRANSLATE_NOOP("DeviceFunctions", "User presence detector")},
        {"PNP0103", QT_TRANSLATE_NOOP("DeviceFunctions", "High precision event timer")},
        {"PNP0303", QT_TRANSLATE_NOOP("DeviceFunctions", "PS/2 keyboard interface")},
        {"PNP0A03", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI bus")},
        {"PNP0A08", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express bus")},
        {"PNP0B00", QT_TRANSLATE_NOOP("DeviceFunctions", "Real-time clock")},
        {"PNP0C02", QT_TRANSLATE_NOOP("DeviceFunctions", "Motherboard resources")},
        {"PNP0C09", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI embedded controller")},
        {"PNP0C0A", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI control-method battery")},
        {"PNP0C0B", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI fan")},
        {"PNP0C0C", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI power button")},
        {"PNP0C0D", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI lid switch")},
        {"PNP0C0E", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI sleep button")},
        {"PNP0C14", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI WMI interface")},
        {"PNP0C50", QT_TRANSLATE_NOOP("DeviceFunctions", "I2C human interface device")},
        {"PNP0C60", QT_TRANSLATE_NOOP("DeviceFunctions", "Display sensor")},
        {"LNXTHERM", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI thermal zone")},
        {"LNXVIDEO", QT_TRANSLATE_NOOP("DeviceFunctions", "ACPI video controller")}
    };
    const QString alias = d.properties.value("MODALIAS");
    QStringList identifiers;
    if ((d.subsystem == "acpi" || d.subsystem == "platform" || d.subsystem == "i2c") && alias.startsWith("acpi:"))
        identifiers = alias.mid(5).split(':', Qt::SkipEmptyParts);
    else if (d.subsystem == "pnp" && alias.startsWith("pnp:")) {
        // PNP modaliases encode each identifier as d<ID>, including compatible IDs.
        for (const auto &id : alias.mid(4).split('d', Qt::SkipEmptyParts)) identifiers.append(id);
    }
    for (const auto &id : identifiers)
        for (const auto &entry : ids)
            if (id.compare(QLatin1String(entry.id), Qt::CaseInsensitive) == 0)
                return {QString::fromLatin1(entry.label), QStringLiteral("standard ACPI/PNP ID: %1").arg(id)};
    if (d.subsystem == "serial-base" && d.driver == "port")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial port function")), "serial-base subsystem; direct port binding"};
    if (d.subsystem == "serial-base" && d.driver == "ctrl")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial controller function")), "serial-base subsystem; direct ctrl binding"};
    if (d.subsystem == "bluetooth" && d.sysname.startsWith("hci") && !d.sysname.contains(':'))
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Bluetooth host controller")), "Bluetooth subsystem; host-controller interface"};
    return {};
}
