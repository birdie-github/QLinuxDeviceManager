#include "propertytext.h"
#include "devicelabel.h"
#include <QCoreApplication>
#include <QStringList>

QString propertyReadValue(const Attribute &a)
{
    switch (a.state) {
    case ReadState::Available:
        return a.value.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty value") : a.value;
    case ReadState::Unavailable: return QCoreApplication::translate("PropertiesDialog", "Unavailable");
    case ReadState::PermissionDenied: return QCoreApplication::translate("PropertiesDialog", "Permission denied");
    case ReadState::Removed: return QCoreApplication::translate("PropertiesDialog", "Device removed");
    case ReadState::Unsupported: return QCoreApplication::translate("PropertiesDialog", "Unsupported in this build");
    case ReadState::NotApplicable: return QCoreApplication::translate("PropertiesDialog", "Not applicable");
    case ReadState::Error: return QCoreApplication::translate("PropertiesDialog", "Read error (errno %1)").arg(a.error);
    }
    return {};
}
namespace {
QString hexDump(const QByteArray &bytes)
{
    QStringList lines;
    for (int offset = 0; offset < bytes.size(); offset += 16) {
        QStringList hex;
        QString printable;
        for (int i = offset; i < qMin(offset + 16, static_cast<int>(bytes.size())); ++i) {
            const unsigned char byte = static_cast<unsigned char>(bytes.at(i));
            hex.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
            printable += byte >= 32 && byte <= 126 ? QLatin1Char(static_cast<char>(byte)) : QLatin1Char('.');
        }
        lines.append(QStringLiteral("%1  %2  %3").arg(offset, 8, 16, QLatin1Char('0'))
            .arg(hex.join(' ').leftJustified(47), printable));
    }
    return lines.join('\n');
}

}

QVector<PropertyEntry> propertyEntries(const DeviceProperties &snapshot, bool removed, const QString &resourcesText)
{
    QVector<PropertyEntry> entries;
    const Device &d = snapshot.device;
    const bool efi = d.subsystem == "efivarfs";
    const Attribute notApplicable {ReadState::NotApplicable, {}, 0};
    const auto add = [&](const QString &id, const QString &label, const QString &value,
                           const QString &source = QString(), bool advanced = false, int tab = 2) {
        entries.append({id, label, value, source, advanced, tab});
    };
    const auto raw = [&](const QString &id) { return snapshot.values.value(id); };
    const auto addRaw = [&](const QString &id, const QString &label, int tab, const Attribute &fallback = Attribute()) {
        const Attribute a = snapshot.values.value(id, fallback);
        add(id, label, propertyReadValue(a), snapshot.sources.value(id), false, tab);
    };
    add("name", QCoreApplication::translate("PropertiesDialog", "Device name"), deviceDisplayName(d), d.nameSource, false, 0);
    add("category", QCoreApplication::translate("PropertiesDialog", "Category"), categoryLabel(d.category), QCoreApplication::translate("PropertiesDialog", "Application grouping"), false, 0);
    Attribute manufacturer;
    QString manufacturerSource;
    if (efi) manufacturer = notApplicable;
    else {
        for (const QString &id : {QStringLiteral("sysfs/manufacturer"), QStringLiteral("sysfs/vendor_name"),
                                 QStringLiteral("udev/ID_VENDOR_FROM_DATABASE"), QStringLiteral("udev/ID_VENDOR")}) {
            const Attribute candidate = raw(id);
            if (candidate.state == ReadState::Available && !candidate.value.isEmpty()) {
                manufacturer = candidate; manufacturerSource = snapshot.sources.value(id); break;
            }
            if (candidate.state == ReadState::PermissionDenied || candidate.state == ReadState::Error) {
                manufacturer = candidate; manufacturerSource = snapshot.sources.value(id); break;
            }
        }
        if (manufacturer.state == ReadState::Unavailable) {
            for (const QString &key : {QStringLiteral("ID_VENDOR_FROM_DATABASE"), QStringLiteral("ID_VENDOR")}) {
                if (d.properties.value(key).isEmpty()) continue;
                manufacturer = {ReadState::Available, d.properties.value(key), 0};
                manufacturerSource = d.propertySources.value(key, QStringLiteral("udev: %1 (%2)").arg(key, d.path));
                break;
            }
        }
    }
    add("manufacturer", QCoreApplication::translate("PropertiesDialog", "Manufacturer"), propertyReadValue(manufacturer), manufacturerSource, false, 0);
    Attribute bus = raw("udev/ID_BUS");
    if (bus.state != ReadState::Available) bus = raw("driver/bus");
    if (efi) bus = notApplicable;
    add("bus", QCoreApplication::translate("PropertiesDialog", "Bus / subsystem"), efi ? propertyReadValue(bus)
        : bus.state == ReadState::Available ? bus.value + " / " + d.subsystem : d.subsystem,
        QCoreApplication::translate("PropertiesDialog", "Device udev metadata and direct driver bus"), false, 0);
    add("location", QCoreApplication::translate("PropertiesDialog", "Location"), efi ? d.parentPath : d.path, QCoreApplication::translate("PropertiesDialog", "Kernel path"), false, 0);
    QString status;
    if (removed) status = QCoreApplication::translate("PropertiesDialog", "Removed or replaced; showing a snapshot.");
    else if (snapshot.state != ReadState::Available) status = QCoreApplication::translate("PropertiesDialog", "Present in the last inventory; properties have not been verified.");
    else {
        status = QCoreApplication::translate("PropertiesDialog", "Present at the last property read.");
        if (!efi) {
            const Attribute binding = raw("driver/name");
            status += '\n' + (binding.state == ReadState::Available
                ? binding.value.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "No directly bound driver; this alone is not a fault.") : QCoreApplication::translate("PropertiesDialog", "Driver bound: %1").arg(binding.value)
                : QCoreApplication::translate("PropertiesDialog", "Driver binding: %1").arg(propertyReadValue(binding)));
            const Attribute authorization = raw("sysfs/authorized");
            if (authorization.state == ReadState::Available && authorization.value == "0") status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB device/interface is unauthorized.");
            else if (authorization.state == ReadState::Available && authorization.value == "1") status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB device/interface is authorized.");
            else if (authorization.state == ReadState::PermissionDenied || authorization.state == ReadState::Error)
                status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB authorization: %1").arg(propertyReadValue(authorization));
        }
    }
    add("status", QCoreApplication::translate("PropertiesDialog", "Device status"), status, QCoreApplication::translate("PropertiesDialog", "Observed presence, binding and USB authorization only"), false, 0);
    Attribute binding = snapshot.values.value("driver/name", d.driver.isEmpty() ? Attribute()
        : Attribute{ReadState::Available, d.driver, 0});
    if (efi) binding = notApplicable;
    const QString driverName = !efi && binding.state == ReadState::Available && binding.value.isEmpty()
        ? QCoreApplication::translate("PropertiesDialog", "No directly bound driver") : propertyReadValue(binding);
    add("driver/name", QCoreApplication::translate("PropertiesDialog", "Bound kernel driver"), driverName, efi ? QString() : d.path + "/driver", false, 0);
    QStringList identifiers;
    for (const QString &key : {QStringLiteral("PCI_ID"), QStringLiteral("PCI_SUBSYS_ID"), QStringLiteral("PRODUCT"),
                             QStringLiteral("ID_VENDOR_ID"), QStringLiteral("ID_MODEL_ID"), QStringLiteral("ID_SERIAL_SHORT")}) {
        const Attribute a = snapshot.values.value("udev/" + key,
            d.properties.contains(key) ? Attribute{ReadState::Available, d.properties.value(key), 0} : Attribute());
        if (a.state == ReadState::Available && !a.value.isEmpty()) identifiers.append(key + ": " + a.value);
    }
    for (const QString &key : {QStringLiteral("vendor"), QStringLiteral("device"), QStringLiteral("idVendor"),
                             QStringLiteral("idProduct"), QStringLiteral("serial"), QStringLiteral("hid"), QStringLiteral("uid")}) {
        const Attribute a = raw("sysfs/" + key);
        if (a.state != ReadState::Unavailable && !(a.state == ReadState::Available && a.value.isEmpty()))
            identifiers.append(key + ": " + propertyReadValue(a));
    }
    add("identifiers", QCoreApplication::translate("PropertiesDialog", "Hardware identifiers"), efi ? propertyReadValue(notApplicable)
        : identifiers.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Unavailable") : identifiers.join('\n'), QCoreApplication::translate("PropertiesDialog", "Selected udev and direct sysfs identifiers"), false, 0);
    add("driver/binding", QCoreApplication::translate("PropertiesDialog", "Bound kernel driver"), driverName, efi ? QString() : d.path + "/driver", false, 1);
    const Attribute moduleFallback = efi || (binding.state == ReadState::Available && binding.value.isEmpty())
        ? notApplicable : Attribute();
    addRaw("driver/bus", QCoreApplication::translate("PropertiesDialog", "Driver bus"), 1, moduleFallback);
    addRaw("module/name", QCoreApplication::translate("PropertiesDialog", "Owning kernel module"), 1, moduleFallback);
    const Attribute type = snapshot.values.value("module/type", moduleFallback);
    add("module/type", QCoreApplication::translate("PropertiesDialog", "Built-in or modular"), type.state == ReadState::Available ? QCoreApplication::translate("PropertiesDialog", "Modular (kernel initstate evidence)")
        : type.state == ReadState::NotApplicable || type.state == ReadState::PermissionDenied || type.state == ReadState::Error
            ? propertyReadValue(type) : QCoreApplication::translate("PropertiesDialog", "Undetermined; a missing module link/version does not prove a built-in driver."),
        snapshot.sources.value("module/type"), false, 1);
    addRaw("module/runtime_version", QCoreApplication::translate("PropertiesDialog", "Running module version"), 1, moduleFallback);
    const QStringList fields {"filename", "version", "description", "author", "license", "firmware"};
    const QStringList labels {QCoreApplication::translate("PropertiesDialog", "Installed module filename"), QCoreApplication::translate("PropertiesDialog", "Installed module version"), QCoreApplication::translate("PropertiesDialog", "Installed module description"),
        QCoreApplication::translate("PropertiesDialog", "Installed module author"), QCoreApplication::translate("PropertiesDialog", "Installed module license"), QCoreApplication::translate("PropertiesDialog", "Declared firmware names")};
    for (int i = 0; i < fields.size(); ++i) addRaw("module/" + fields[i], labels[i], 1, moduleFallback);
    addRaw("sysfs/firmware_rev", QCoreApplication::translate("PropertiesDialog", "Reported device firmware revision"), 1,
        d.subsystem == "nvme" ? Attribute() : notApplicable);
    add("module/note", QCoreApplication::translate("PropertiesDialog", "Metadata scope"), efi ? propertyReadValue(notApplicable)
        : QCoreApplication::translate("PropertiesDialog", "Installed metadata may differ from code already loaded before an update. Declared firmware names do not prove firmware is loaded or report its version."), {}, false, 1);
    // Interpreted facts plus curated raw values, keeping provenance beside each.
    add("parent", QCoreApplication::translate("PropertiesDialog", "Parent device path"), d.parentPath.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Unavailable") : d.parentPath, QCoreApplication::translate("PropertiesDialog", "Recorded ancestry"), efi);
    add("raw_name", QCoreApplication::translate("PropertiesDialog", "Original name"), d.name, d.nameSource, true);
    add("name_source", QCoreApplication::translate("PropertiesDialog", "Name source"), d.nameSource, {}, true);
    add("sysname", QCoreApplication::translate("PropertiesDialog", "Kernel name"), d.sysname, {}, true);
    add("path", QCoreApplication::translate("PropertiesDialog", "Full kernel path"), d.path, {}, true);
    if (!efi) add("driver/path", QCoreApplication::translate("PropertiesDialog", "Direct driver path"), propertyReadValue(raw("driver/path")),
        snapshot.sources.value("driver/path"), true);
    QStringList keys = snapshot.values.keys();
    keys.sort();
    for (const QString &key : keys) {
        if (!key.startsWith("udev/") && !key.startsWith("sysfs/")) continue;
        if (key == "sysfs/firmware_rev") continue; // Already exposed on the Driver tab.
        add(key, key, propertyReadValue(raw(key)), snapshot.sources.value(key), true);
    }
    keys = d.properties.keys();
    keys.sort();
    for (const QString &key : keys) {
        if (snapshot.values.contains("udev/" + key)) continue;
        add("inventory/" + key, QCoreApplication::translate("PropertiesDialog", "Inventory: %1").arg(key), d.properties.value(key),
            d.propertySources.value(key, QStringLiteral("inventory udev metadata")), true);
    }
    keys = d.attributes.keys();
    keys.sort();
    for (const QString &key : keys)
        add("inventory/sysfs/" + key, QCoreApplication::translate("PropertiesDialog", "Inventory attribute: %1").arg(key), propertyReadValue(d.attributes.value(key)), QCoreApplication::translate("PropertiesDialog", "Last inventory; see name source for derived naming metadata"), true);
    if (efi) {
        const Attribute hex = raw("efi/hex");
        QString dump = propertyReadValue(hex);
        if (hex.state == ReadState::Available) {
            dump = snapshot.efiBytes.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty file") : hexDump(snapshot.efiBytes);
            if (snapshot.efiTruncated) dump += '\n' + QCoreApplication::translate("PropertiesDialog", "Truncated: first 65536 bytes shown.");
        }
        add("efi/hex", QCoreApplication::translate("PropertiesDialog", "UEFI variable — hex dump"), dump,
            QCoreApplication::translate("PropertiesDialog", "Complete efivarfs file bytes, including the attribute prefix; contents are not decoded."));
    }
    if (snapshot.resources.hasInformation())
        add("resources", QCoreApplication::translate("PropertiesDialog", "Resources"), resourcesText, QCoreApplication::translate("PropertiesDialog", "Direct device resource metadata"));
    return entries;
}
