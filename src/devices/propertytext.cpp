#include "propertytext.h"
#include "devicelabel.h"
#include <QCoreApplication>
#include <QStringList>
#include <QDateTime>
#include <QLocale>
#include <cerrno>
#include <limits>

QString detailsPropertyStatus(const PropertyEntry &entry)
{
    if (entry.state == ReadState::Available)
        return entry.value.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty value").toLower() : QString();
    return propertyReadValue({entry.state, {}, entry.error}).toLower();
}
// Elapsed operating time has no calendar origin: use fixed 365-day years
// and 30-day months, then preserve the remaining days and clock time.
QString storageDuration(quint64 seconds)
{
    const quint64 days = seconds / 86400;
    return QCoreApplication::translate("Storage", "%1y %2m %3d %4:%5:%6")
        .arg(days / 365).arg((days % 365) / 30).arg((days % 365) % 30)
        .arg((seconds / 3600) % 24, 2, 10, QLatin1Char('0'))
        .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
        .arg(seconds % 60, 2, 10, QLatin1Char('0'));
}

QString storageFieldValue(const StorageField &field)
{
    const Attribute &value = field.value;
    if (field.format == StorageField::Format::HealthQueryStatus
        && value.state == ReadState::Unavailable && value.error == ETIMEDOUT)
        return QCoreApplication::translate("Storage", "Not queried: shared health-query time limit reached");
    if (value.state == ReadState::Unsupported) {
        if (field.id == "service")
            return QCoreApplication::translate("Storage", "UDisks2 support is not enabled in this build");
        if (field.id == "health")
            return QCoreApplication::translate("Storage", "Drive health is not exposed through a supported UDisks2 interface");
        if (field.id == "org.freedesktop.UDisks2.Drive.Ata/SmartGetAttributes"
            || field.id == "org.freedesktop.UDisks2.NVMe.Controller/SmartGetAttributes")
            return QCoreApplication::translate("Storage", "Detailed health query is not supported by UDisks2");
    }
    if (value.state != ReadState::Available) return propertyReadValue(value);
    using Format = StorageField::Format;
    switch (field.format) {
    case Format::HealthQueryStatus:
    case Format::Text:
        if (field.id == "service" && value.value == "cached") return QCoreApplication::translate("Storage", "Available; cached properties only");
        if (field.id == "mountinfo" && value.value == "application-namespace")
            return QCoreApplication::translate("Storage", "Application mount namespace only; other namespaces may differ. No filesystem is mounted or unlocked.");
        if (field.id == "mounts" && value.value == "no-observed-mount")
            return QCoreApplication::translate("Storage", "No mount observed in the application namespace");
        return propertyReadValue(value);
    case Format::Bytes: {
        bool valid = false;
        const quint64 bytes = value.value.toULongLong(&valid);
        if (!valid) return QCoreApplication::translate("Storage", "Invalid capacity or sector-size metadata");
        if (bytes > static_cast<quint64>(std::numeric_limits<qint64>::max())) return QCoreApplication::translate("Storage", "%1 bytes").arg(QLocale().toString(bytes));
        return QCoreApplication::translate("Storage", "%1 bytes (%2)").arg(QLocale().toString(bytes), QLocale().formattedDataSize(static_cast<qint64>(bytes)));
    }
    case Format::Boolean: return value.value == "1" ? QCoreApplication::translate("Storage", "Yes") : value.value == "0" ? QCoreApplication::translate("Storage", "No") : QCoreApplication::translate("Storage", "Invalid boolean metadata");
    case Format::Removable: return value.value == "1" ? QCoreApplication::translate("Storage", "Removable (UDisks2 hint)") : value.value == "0" ? QCoreApplication::translate("Storage", "Fixed (UDisks2 hint)") : QCoreApplication::translate("Storage", "Invalid removable metadata");
    case Format::Epoch: {
        bool valid = false;
        const quint64 seconds = value.value.toULongLong(&valid);
        if (!valid || !seconds || seconds > static_cast<quint64>(std::numeric_limits<qint64>::max())) return QCoreApplication::translate("Storage", "Unavailable");
        const QDateTime time = QDateTime::fromSecsSinceEpoch(static_cast<qint64>(seconds));
        return time.isValid() ? time.toString(Qt::ISODate) : QCoreApplication::translate("Storage", "Unavailable");
    }
    case Format::Kelvin: {
        bool valid = false;
        const double kelvin = value.value.toDouble(&valid);
        const int precision = field.id.startsWith("org.freedesktop.UDisks2.NVMe.Controller/") ? 0 : 1;
        return valid && kelvin > 0 ? QCoreApplication::translate("Storage", "%1 °C").arg(QLocale().toString(kelvin - 273.15, 'f', precision)) : QCoreApplication::translate("Storage", "Unavailable");
    }
    case Format::Percent:
    case Format::Hours:
    case Format::Seconds:
    case Format::Minutes:
    case Format::Count: {
        bool valid = false;
        const quint64 number = value.value.toULongLong(&valid);
        if (!valid) return QCoreApplication::translate("Storage", "Invalid health metadata");
        const QString text = QLocale().toString(number);
        if (field.format == Format::Percent) return QCoreApplication::translate("Storage", "%1%").arg(text);
        if (field.format == Format::Seconds) return storageDuration(number);
        if (field.format == Format::Hours) return QCoreApplication::translate("Storage", "%1 hours").arg(text);
        if (field.format == Format::Minutes) return QCoreApplication::translate("Storage", "%1 minutes").arg(text);
        return text;
    }
    case Format::AtaHealth: return value.value == "1" ? QCoreApplication::translate("Storage", "Failure predicted by drive")
        : value.value == "0" ? QCoreApplication::translate("Storage", "No failure predicted by drive (not a guarantee of health)") : QCoreApplication::translate("Storage", "Unavailable");
    case Format::NvmeHealth: {
        if (value.value.isEmpty()) return QCoreApplication::translate("Storage", "No critical warnings reported (not a guarantee of health)");
        QStringList warnings;
        for (const QString &warning : value.value.split('\n', Qt::SkipEmptyParts)) {
            const char *text = nullptr;
            if (warning == "spare") text = QT_TRANSLATE_NOOP("Storage", "Available spare is below its threshold");
            else if (warning == "temperature") text = QT_TRANSLATE_NOOP("Storage", "Temperature is outside a permitted threshold");
            else if (warning == "degraded") text = QT_TRANSLATE_NOOP("Storage", "NVM subsystem reliability is degraded");
            else if (warning == "readonly") text = QT_TRANSLATE_NOOP("Storage", "Media is in read-only mode");
            else if (warning == "volatile_mem") text = QT_TRANSLATE_NOOP("Storage", "Volatile-memory backup has failed");
            else if (warning == "pmr_readonly") text = QT_TRANSLATE_NOOP("Storage", "Persistent memory region is read-only or unreliable");
            warnings.append(text ? QCoreApplication::translate("Storage", text)
                : QCoreApplication::translate("Storage", "Unrecognized warning: %1").arg(warning));
        }
        return warnings.join('\n');
    }
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

QVector<PropertyEntry> propertyEntries(const DeviceProperties &snapshot, bool removed)
{
    QVector<PropertyEntry> entries;
    const Device &d = snapshot.device;
    const bool efi = d.subsystem == "efivarfs";
    const auto add = [&](const QString &id, const QString &label, const QString &value,
                           const QString &source = QString(), bool advanced = false, int tab = 2,
                           ReadState state = ReadState::Available, int error = 0) {
        entries.append({id, label, value, source, advanced, tab, state, error});
    };
    const auto raw = [&](const QString &id) { return snapshot.values.value(id); };
    if (snapshot.storage.applicable) {
        add("storage/scope", QCoreApplication::translate("Storage", "Storage snapshot"),
            QCoreApplication::translate("Storage", "Each entity is a separate storage layer; capacities are not added. Health is cached UDisks2 evidence, separate from operational status. Reload never requests SMART updates or self-tests. Deep search includes native metadata only."), {}, false, 3);
        for (const StorageField &note : snapshot.storage.notes)
            add("storage/note/" + note.id, QCoreApplication::translate("Storage", note.label), storageFieldValue(note), note.source, false, 3);
        for (const StorageEntity &entity : snapshot.storage.entities) {
            const QString prefix = "storage/entity/" + entity.id;
            add(prefix, QCoreApplication::translate("Storage", "Storage entity"), entity.id, {}, false, 3);
            for (const StorageField &field : entity.fields)
                add(prefix + '/' + field.id, QCoreApplication::translate("Storage", field.label), storageFieldValue(field), field.source, false, 3);
        }
        int index = 0;
        for (const StorageLink &edge : snapshot.storage.links)
            add("storage/link/" + QString::number(index++), QCoreApplication::translate("Storage", edge.label),
                edge.from + "\n→ " + edge.to, edge.source, false, 3);
    }
    entries += commonPropertyEntries({snapshot.device, snapshot.values, snapshot.sources, snapshot.state}, removed);
    // Interpreted facts plus curated raw values, keeping provenance beside each.
    add("parent", QCoreApplication::translate("PropertiesDialog", "Parent device path"), d.parentPath.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Unavailable") : d.parentPath, QCoreApplication::translate("PropertiesDialog", "Recorded ancestry"), efi, 2, d.parentPath.isEmpty() ? ReadState::Unavailable : ReadState::Available);
    add("raw_name", QCoreApplication::translate("PropertiesDialog", "Original name"), d.name, d.nameSource, true);
    add("name_source", QCoreApplication::translate("PropertiesDialog", "Name source"), d.nameSource, {}, true);
    add("sysname", QCoreApplication::translate("PropertiesDialog", "Kernel name"), d.sysname, {}, true);
    add("path", QCoreApplication::translate("PropertiesDialog", "Full kernel path"), d.path, {}, true);
    if (!efi) add("driver/path", QCoreApplication::translate("PropertiesDialog", "Direct driver path"), propertyReadValue(raw("driver/path")),
        snapshot.sources.value("driver/path"), true, 2, raw("driver/path").state, raw("driver/path").error);
    QStringList keys = snapshot.values.keys();
    keys.sort();
    for (const QString &key : keys) {
        if (!key.startsWith("udev/") && !key.startsWith("sysfs/")) continue;
        if (key == "sysfs/firmware_rev") continue; // Already exposed on the Driver tab.
        add(key, key, propertyReadValue(raw(key)), snapshot.sources.value(key), true, 2, raw(key).state, raw(key).error);
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
        add("inventory/sysfs/" + key, QCoreApplication::translate("PropertiesDialog", "Inventory attribute: %1").arg(key), propertyReadValue(d.attributes.value(key)), QCoreApplication::translate("PropertiesDialog", "Last inventory; see name source for derived naming metadata"), true, 2, d.attributes.value(key).state, d.attributes.value(key).error);
    if (efi) {
        const Attribute hex = raw("efi/hex");
        QString dump = propertyReadValue(hex);
        if (hex.state == ReadState::Available) {
            dump = snapshot.efiBytes.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty file") : hexDump(snapshot.efiBytes);
            if (snapshot.efiTruncated) dump += '\n' + QCoreApplication::translate("PropertiesDialog", "Truncated: first 65536 bytes shown.");
        }
        add("efi/hex", QCoreApplication::translate("PropertiesDialog", "UEFI variable — hex dump"), dump,
            QCoreApplication::translate("PropertiesDialog", "Complete efivarfs file bytes, including the attribute prefix; contents are not decoded."), false, 2, hex.state, hex.error);
    }
    return entries;
}

// Storage identity and content belong to the storage tabs even in advanced mode.
// Generic device/bus/driver facts remain available for all device classes.
bool isDetailsProperty(const PropertyEntry &entry, const Device &device)
{
    if (entry.tab == 3 || entry.id.startsWith("storage/")) return false;
    if (device.subsystem != "block" && device.subsystem != "nvme") return true;
    QString key = entry.id;
    for (const QString &prefix : {QStringLiteral("inventory/sysfs/"), QStringLiteral("inventory/"),
                                  QStringLiteral("udev/"), QStringLiteral("sysfs/")}) {
        if (key.startsWith(prefix)) { key.remove(0, prefix.size()); break; }
    }
    for (const QString &prefix : {QStringLiteral("ID_FS_"), QStringLiteral("ID_PART_"),
                                  QStringLiteral("ID_ATA"), QStringLiteral("ID_NVME"),
                                  QStringLiteral("ID_SCSI"), QStringLiteral("ID_DRIVE_"), QStringLiteral("ID_CDROM"),
                                  QStringLiteral("DM_"), QStringLiteral("MD_"),
                                  QStringLiteral("queue/"), QStringLiteral("dm/"), QStringLiteral("md/")})
        if (key.startsWith(prefix)) return false;
    const QStringList keys {"ID_MODEL", "ID_MODEL_ENC", "ID_VENDOR", "ID_VENDOR_ENC",
        "ID_SERIAL", "ID_SERIAL_SHORT", "ID_REVISION", "ID_BUS", "ID_WWN", "ID_WWN_WITH_EXTENSION",
        "DEVNAME", "DISKSEQ", "size", "dev", "partition", "start", "ro", "removable",
        "model", "vendor", "serial", "rev", "firmware_rev", "wwid"};
    return !keys.contains(key);
}
