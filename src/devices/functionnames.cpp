#include "functionnames.h"
#include <QCoreApplication>
#include <QStringList>

FunctionName knownFunctionName(const Device &d)
{
    struct Entry { const char *id; const char *label; };
    if (d.subsystem == "usb" && d.devtype == "usb_device" && d.sysname.startsWith("usb")) {
        bool busValid = false, addressValid = false, vendorValid = false, productValid = false;
        const uint bus = d.sysname.mid(3).toUInt(&busValid);
        const uint address = d.properties.value("DEVNUM").toUInt(&addressValid);
        const QStringList product = d.properties.value("PRODUCT").split('/');
        if (product.size() == 3 && busValid && bus > 0 && addressValid && address == 1) {
            const uint vendor = product[0].toUInt(&vendorValid, 16);
            const uint id = product[1].toUInt(&productValid, 16);
            // Linux USB core's root-hub descriptor IDs; external hubs have port names.
            if (vendorValid && productValid && vendor == 0x1d6b) {
                const char *label = nullptr;
                if (id == 1) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 1.1 root hub");
                else if (id == 2) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 2.0 root hub");
                else if (id == 3) label = QT_TRANSLATE_NOOP("DeviceFunctions", "USB 3.x root hub");
                if (label) return {QString::fromLatin1(label), "Linux USB root-hub topology and descriptor identity"};
            }
        }
    }
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
    static const Entry platformAliases[] = {
        {"platform:coretemp", QT_TRANSLATE_NOOP("DeviceFunctions", "Intel CPU temperature monitor")},
        {"platform:rtc-efi", QT_TRANSLATE_NOOP("DeviceFunctions", "EFI real-time clock interface")},
        {"platform:alarmtimer", QT_TRANSLATE_NOOP("DeviceFunctions", "Kernel alarm timer")}
    };
    if (d.subsystem == "platform")
        for (const auto &entry : platformAliases)
            if (alias == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("documented kernel platform alias: %1").arg(alias)};
    static const Entry pciServices[] = {
        {"aer", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express advanced error reporting service")},
        {"pcie_bwctrl", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express bandwidth control service")},
        {"pcie_pme", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express power-management event service")},
        {"pciehp", QT_TRANSLATE_NOOP("DeviceFunctions", "PCI Express hot-plug service")}
    };
    if (d.subsystem == "pci_express")
        for (const auto &entry : pciServices)
            if (d.driver == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("PCI Express service; direct driver: %1").arg(d.driver)};
    static const Entry fauxFunctions[] = {
        {"microcode", QT_TRANSLATE_NOOP("DeviceFunctions", "CPU microcode interface")},
        {"reg-dummy", QT_TRANSLATE_NOOP("DeviceFunctions", "Dummy voltage regulator")},
        {"regulatory", QT_TRANSLATE_NOOP("DeviceFunctions", "Wireless regulatory interface")},
        {"snd-soc-dummy", QT_TRANSLATE_NOOP("DeviceFunctions", "Dummy SoC audio component")}
    };
    if (d.subsystem == "faux")
        for (const auto &entry : fauxFunctions)
            if (d.sysname == QLatin1String(entry.id))
                return {QString::fromLatin1(entry.label), QStringLiteral("documented kernel faux function: %1").arg(d.sysname)};
    if (d.subsystem == "serial-base" && d.driver == "port")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial port function")), "serial-base subsystem; direct port binding"};
    if (d.subsystem == "serial-base" && d.driver == "ctrl")
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Serial controller function")), "serial-base subsystem; direct ctrl binding"};
    if (d.subsystem == "bluetooth" && d.sysname.startsWith("hci") && !d.sysname.contains(':'))
        return {QStringLiteral(QT_TRANSLATE_NOOP("DeviceFunctions", "Bluetooth host controller")), "Bluetooth subsystem; host-controller interface"};
    return {};
}
