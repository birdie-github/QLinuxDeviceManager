#include "storagepresentation.h"
#include "propertytext.h"
#include "naturalsort.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QLocale>
#include <algorithm>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Storage", text); }
bool healthField(const StorageField &field)
{
    return field.id == "health" || field.id.contains(".Drive.Ata/") || field.id.contains(".NVMe.Controller/");
}
bool driveEntity(const StorageEntity &entity)
{
    for (const StorageField &field : entity.fields)
        if (field.id.startsWith("org.freedesktop.UDisks2.Drive/") || healthField(field)) return true;
    return false;
}
QString value(const StorageEntity &entity, const QStringList &keys)
{
    for (const QString &key : keys)
        for (const StorageField &field : entity.fields)
            if (field.id == key && field.value.state == ReadState::Available && !field.value.value.isEmpty())
                return storageFieldValue(field);
    return tr("Unavailable");
}
QString name(const StorageEntity &entity)
{
    for (const QString &key : {QStringLiteral("DEVNAME"), QStringLiteral("dm/name"),
            QStringLiteral("org.freedesktop.UDisks2.LogicalVolume/Name"),
            QStringLiteral("org.freedesktop.UDisks2.VolumeGroup/Name"),
            QStringLiteral("org.freedesktop.UDisks2.MDRaid/Name"),
            QStringLiteral("org.freedesktop.UDisks2.Drive/Model")}) {
        for (const StorageField &field : entity.fields)
            if (field.id == key && field.value.state == ReadState::Available && !field.value.value.isEmpty())
                return field.value.value;
    }
    // Kernel names/object basenames are display fallbacks, never identity keys.
    return QFileInfo(entity.id).fileName();
}
QString kind(const StorageEntity &entity)
{
    for (const StorageField &field : entity.fields) {
        if (field.id == "partition" || field.id.contains(".Partition/")) return tr("Partition");
        if (field.id == "dm/name") return tr("Device mapper");
        if (field.id == "md/level" || field.id.contains(".MDRaid/")) return tr("RAID");
        if (field.id.contains(".LogicalVolume/")) return tr("Logical volume");
        if (field.id.contains(".VolumeGroup/")) return tr("Volume group");
    }
    return tr("Block device");
}
StorageDisplayField display(const StorageField &field)
{
    return {tr(field.label), storageFieldValue(field), field.source};
}
// Aliases describe the same fact on this exact block entity. Preserve all sources;
// disagreeing available values are visible rather than silently overwritten.
void fact(QVector<StorageDisplayField> &output, const StorageEntity &entity,
          const char *caption, const QStringList &keys)
{
    QStringList values, sources;
    const StorageField *fallback = nullptr;
    for (const QString &key : keys) for (const StorageField &field : entity.fields) {
        if (field.id != key) continue;
        if (!fallback) fallback = &field;
        if (!sources.contains(field.source)) sources.append(field.source);
        if (field.value.state == ReadState::Available && !field.value.value.isEmpty()) {
            const QString text = storageFieldValue(field);
            if (!values.contains(text)) values.append(text);
        }
    }
    if (!fallback) return;
    QString text = values.isEmpty() ? storageFieldValue(*fallback) : values.join("\n");
    if (values.size() > 1) text = tr("Conflicting metadata:") + '\n' + text;
    output.append({tr(caption), text, sources.join('\n')});
}
QString ataAttributeName(const QString &identifier)
{
    QStringList words = identifier.split(QLatin1Char('-'), Qt::SkipEmptyParts);
    for (QString &word : words) {
        if (word.compare("lbas", Qt::CaseInsensitive) == 0) word = QStringLiteral("LBAs");
        else if (word.compare("udma", Qt::CaseInsensitive) == 0) word = QStringLiteral("UDMA");
        else if (word.compare("crc", Qt::CaseInsensitive) == 0) word = QStringLiteral("CRC");
        else if (!word.isEmpty()) word[0] = word[0].toUpper();
    }
    return words.join(QLatin1Char(' '));
}
StorageDisplayAtaAttribute ataAttribute(const AtaSmartAttribute &attribute, const QString &drive, const QString &counterStatus)
{
    const auto normalized = [](qint32 value) { return value < 0 ? tr("Unavailable") : QLocale().toString(value); };
    QString interpreted, tooltip;
    if (attribute.unit == 0 || (attribute.unit == 4 && attribute.interpreted == 0)) interpreted = tr("Unavailable");
    else if (attribute.interpreted < 0) interpreted = tr("Invalid health metadata");
    else switch (attribute.unit) {
    case 1: interpreted = QLocale().toString(attribute.interpreted); break;
    case 2:
        interpreted = storageDuration(static_cast<quint64>(attribute.interpreted / 1000));
        tooltip = tr("%1 seconds").arg(QLocale().toString(attribute.interpreted / 1000.0, 'f', 3));
        break;
    case 3: interpreted = tr("%1 sectors").arg(QLocale().toString(attribute.interpreted)); break;
    case 4: interpreted = tr("%1 °C").arg(QLocale().toString(attribute.interpreted / 1000.0 - 273.15, 'f', 1)); break;
    // libatasmart's extended unit enum: thousandths of a percent, percent, MB.
    // Keep MB as supplied; these are interpreted values, not raw LBA counts.
    case 5: interpreted = tr("%1%").arg(QLocale().toString(attribute.interpreted / 1000.0, 'f', 3)); break;
    case 6: interpreted = tr("%1%").arg(QLocale().toString(attribute.interpreted)); break;
    case 7: interpreted = tr("%1 MB").arg(QLocale().toString(attribute.interpreted)); break;
    default: interpreted = tr("Unavailable (unknown unit %1)").arg(attribute.unit); break;
    }
    QString counterNotice;
    QString source = drive + " : org.freedesktop.UDisks2.Drive.Ata.SmartGetAttributes : ID "
        + QString::number(attribute.id) + " (" + attribute.name + ")";
    if (attribute.name == "total-lbas-written" || attribute.name == "total-lbas-read") {
        if (!attribute.rawCounter.isEmpty()) {
            // The collector validates raw counters as unsigned 48-bit integers,
            // so multiplying by the conventional 512 bytes per LBA fits quint64.
            const quint64 raw = attribute.rawCounter.toULongLong();
            const quint64 bytes = raw * 512;
            interpreted = tr("%1 MB").arg(QLocale().toString(bytes / 1000000.0, 'f', 2));
            tooltip = tr("Raw smartctl counter: %1\nConversion assumes 512 bytes per LBA; 1 MB = 1,000,000 bytes. "
                         "Vendor-specific counter scales may differ.").arg(QLocale().toString(raw));
            source = attribute.rawSource + '\n' + source;
        } else {
            // Keep the UDisks2 value formatted above, but flag its uncertain scale.
            counterNotice = tr("This value is reported by UDisks2 and may be unreliable due to a known "
                "UDisks2/libatasmart scaling issue.\n\n"
                "To read the actual raw counter with smartctl, run the application as root or with sudo. "
                "The optional smartmontools package must be installed.");
            tooltip = counterNotice;
            if (!counterStatus.isEmpty()) tooltip += "\n\n" + counterStatus;
        }
    }
    return {QString::number(attribute.id), attribute.name.isEmpty() ? tr("Unnamed attribute") : ataAttributeName(attribute.name),
        normalized(attribute.current), normalized(attribute.worst), normalized(attribute.threshold), interpreted,
        QStringLiteral("0x%1").arg(attribute.flags, 4, 16, QLatin1Char('0')),
        source, tooltip, counterNotice};
}
void healthGroups(StorageDisplayRow &row, const StorageEntity &entity)
{
    QVector<StorageDisplayField> summary, usage, errors, temperatures;
    static const QMap<QString, const char *> captions {
        {"health", QT_TRANSLATE_NOOP("Storage", "Health reporting")},
        {"SmartUpdated", QT_TRANSLATE_NOOP("Storage", "Last health update")},
        {"SmartSupported", QT_TRANSLATE_NOOP("Storage", "ATA SMART supported")},
        {"SmartEnabled", QT_TRANSLATE_NOOP("Storage", "ATA SMART enabled")},
        {"SmartFailing", QT_TRANSLATE_NOOP("Storage", "Drive failure prediction")},
        {"SmartCriticalWarning", QT_TRANSLATE_NOOP("Storage", "Critical warnings")},
        {"SmartPowerOnSeconds", QT_TRANSLATE_NOOP("Storage", "Power-on time")},
        {"SmartPowerOnHours", QT_TRANSLATE_NOOP("Storage", "Power-on time")},
        {"SmartTemperature", QT_TRANSLATE_NOOP("Storage", "Temperature")},
        {"SmartNumBadSectors", QT_TRANSLATE_NOOP("Storage", "Bad sectors")},
        {"SmartNumAttributesFailing", QT_TRANSLATE_NOOP("Storage", "Failing attributes")},
        {"SmartNumAttributesFailedInThePast", QT_TRANSLATE_NOOP("Storage", "Attributes failed in the past")},
        {"avail_spare", QT_TRANSLATE_NOOP("Storage", "Available spare")},
        {"spare_thresh", QT_TRANSLATE_NOOP("Storage", "Spare threshold")},
        {"percent_used", QT_TRANSLATE_NOOP("Storage", "Estimated endurance used")},
        {"total_data_read", QT_TRANSLATE_NOOP("Storage", "Total data read")},
        {"total_data_written", QT_TRANSLATE_NOOP("Storage", "Total data written")},
        {"ctrl_busy_time", QT_TRANSLATE_NOOP("Storage", "Controller busy time")},
        {"power_cycles", QT_TRANSLATE_NOOP("Storage", "Power cycles")},
        {"unsafe_shutdowns", QT_TRANSLATE_NOOP("Storage", "Unsafe shutdowns")},
        {"media_errors", QT_TRANSLATE_NOOP("Storage", "Media and data integrity errors")},
        {"num_err_log_entries", QT_TRANSLATE_NOOP("Storage", "Error information log entries")},
        {"wctemp", QT_TRANSLATE_NOOP("Storage", "Warning temperature threshold")},
        {"cctemp", QT_TRANSLATE_NOOP("Storage", "Critical temperature threshold")},
        {"warning_temp_time", QT_TRANSLATE_NOOP("Storage", "Time above warning temperature")},
        {"critical_temp_time", QT_TRANSLATE_NOOP("Storage", "Time above critical temperature")}
    };
    bool nvme = false, sensorErrorShown = false;
    for (const StorageField &field : entity.fields) {
        if (!healthField(field)) continue;
        nvme = nvme || field.id.contains(".NVMe.Controller/");
        const QString key = field.id.section('/', -1);
        StorageDisplayField shown = display(field);
        if (key == "SmartPowerOnSeconds" && field.value.state == ReadState::Available) {
            bool valid = false;
            const quint64 seconds = field.value.value.toULongLong(&valid);
            if (valid) shown.tooltip = tr("%1 seconds").arg(QLocale().toString(seconds));
        }
        if (const char *caption = captions.value(key, nullptr)) shown.label = tr(caption);
        if (key == "SmartGetAttributes") shown.label = field.id.contains(".Drive.Ata/")
            ? tr("ATA SMART attributes") : tr("NVMe health details");
        if (field.id.contains("/temp_sensors/")) {
            // Zero/unreported slots are absent evidence, not failed reads.
            if (field.value.state == ReadState::Unavailable && !field.value.error) continue;
            if (field.value.state != ReadState::Available) {
                if (sensorErrorShown) continue;
                sensorErrorShown = true;
                shown.label = tr("Temperature sensors");
            } else shown.label = tr("Temperature sensor %1").arg(key);
            temperatures.append(shown);
        } else if (field.format == StorageField::Format::Kelvin || key.endsWith("temp_time"))
            temperatures.append(shown);
        else if (key == "SmartPowerOnSeconds" || key == "SmartPowerOnHours" || key == "total_data_read"
            || key == "total_data_written" || key == "ctrl_busy_time" || key == "power_cycles" || key == "unsafe_shutdowns")
            usage.append(shown);
        else if (key == "SmartNumBadSectors" || key == "SmartNumAttributesFailing"
            || key == "SmartNumAttributesFailedInThePast" || key == "media_errors" || key == "num_err_log_entries")
            errors.append(shown);
        else if (key == "SmartUpdated") summary.prepend(shown);
        else summary.append(shown);
    }
    if (!summary.isEmpty()) row.healthGroups.append({nvme ? tr("Health and endurance") : tr("Health"), summary});
    if (!usage.isEmpty()) row.healthGroups.append({tr("Usage"), usage});
    if (!errors.isEmpty()) row.healthGroups.append({tr("Errors"), errors});
    if (!temperatures.isEmpty()) row.healthGroups.append({tr("Temperatures"), temperatures});
    QVector<AtaSmartAttribute> attributes = entity.ataAttributes;
    std::sort(attributes.begin(), attributes.end(), [](const AtaSmartAttribute &a, const AtaSmartAttribute &b) {
        return a.id < b.id;
    });
    for (const AtaSmartAttribute &attribute : attributes) row.ataAttributes.append(ataAttribute(attribute, entity.id, entity.ataCounterStatus));
}
QString relation(const StorageLink &link)
{
    if (link.source.endsWith(".Table")) return tr("Partition of");
    if (link.source.endsWith(".CryptoBackingDevice")) return tr("Backed by");
    if (link.source.endsWith(".Drive")) return tr("Drive");
    if (link.source.endsWith(".MDRaidMember")) return tr("RAID member of");
    if (link.source.endsWith(".MDRaid")) return tr("RAID device");
    if (link.source.endsWith(".LogicalVolume")) return tr("Logical volume");
    if (link.source.endsWith(".VolumeGroup")) return tr("Volume group");
    return tr(link.label);
}
void overview(QVector<StorageDisplayField> &fields, const StorageEntity &entity)
{
    fields.append({tr("Device"), name(entity), entity.id});
    fact(fields, entity, "Model", {"model", "hardware/model", "ID_MODEL", "org.freedesktop.UDisks2.Drive/Model"});
    fact(fields, entity, "Manufacturer", {"hardware/vendor", "ID_VENDOR", "org.freedesktop.UDisks2.Drive/Vendor"});
    fact(fields, entity, "Serial number", {"serial", "hardware/serial", "ID_SERIAL_SHORT", "org.freedesktop.UDisks2.Drive/Serial"});
    fact(fields, entity, "Firmware revision", {"firmware_rev", "hardware/firmware_rev", "hardware/rev", "ID_REVISION", "org.freedesktop.UDisks2.Drive/Revision"});
    fact(fields, entity, "Transport", {"ID_BUS", "org.freedesktop.UDisks2.Drive/ConnectionBus"});
    fact(fields, entity, "Native transport", {"native-transport"});
    // A Drive.Size is a physical-drive fact, not a namespace/partition capacity alias.
    fact(fields, entity, "Block capacity", {"capacity"});
    fact(fields, entity, "Drive capacity", {"org.freedesktop.UDisks2.Drive/Size"});
    fact(fields, entity, "Logical sector size", {"queue/logical_block_size"});
    fact(fields, entity, "Physical sector size", {"queue/physical_block_size"});
    fact(fields, entity, "Kernel removable media", {"removable"});
    fact(fields, entity, "Block read-only", {"ro"});
    for (const StorageField &field : entity.fields)
        if (field.id.endsWith("/Removable") || field.id.endsWith("/MediaRemovable") || field.id.endsWith("/MediaAvailable"))
            fields.append(display(field));
}
}

StoragePresentation storagePresentation(const StorageProperties &snapshot, const QString &selectedPath)
{
    StoragePresentation result;
    QMap<QString, QString> canonical;
    QMap<QString, StorageEntity> entities;
    for (const StorageEntity &entity : snapshot.entities) entities.insert(entity.id, entity);
    for (const StorageLink &link : snapshot.links) {
        if (link.source != "UDisks2 Block.DeviceNumber / sysfs dev") continue;
        if (!entities.contains(link.from) || !entities.contains(link.to)) continue;
        canonical.insert(link.to, link.from);
        entities[link.from].fields += entities.value(link.to).fields;
    }
    for (auto it = canonical.cbegin(); it != canonical.cend(); ++it) entities.remove(it.key());
    const auto resolve = [&canonical](const QString &id) { return canonical.value(id, id); };

    QSet<QString> selectedDrives;
    for (const StorageLink &link : snapshot.links)
        if (resolve(link.from) == selectedPath && link.source.endsWith(".Drive")) selectedDrives.insert(resolve(link.to));
    // Controller dialogs cover their namespaces; this is an explicit descendant relation.
    if (selectedPath.startsWith("/sys/")) for (const StorageLink &link : snapshot.links)
        if (resolve(link.from).startsWith(selectedPath + '/') && link.source.endsWith(".Drive"))
            selectedDrives.insert(resolve(link.to));
    StorageEntity selected = entities.value(selectedPath);
    // Identity aliases from a single explicitly associated drive share the overview.
    // Drive capacity retains its own caption: it is never a block capacity alias.
    if (selectedDrives.size() == 1) {
        const QString drive = *selectedDrives.cbegin();
        if (entities.contains(drive)) {
            selected.fields += entities.value(drive).fields;
            result.overview.append({tr("Associated drive"), name(entities.value(drive)), drive});
        }
    }
    if (entities.contains(selectedPath)) overview(result.overview, selected);
    else result.overview.append({tr("Storage snapshot"), tr("Unavailable"), selectedPath});
    for (auto it = entities.cbegin(); it != entities.cend(); ++it) {
        const StorageEntity &entity = it.value();
        StorageDisplayRow row;
        row.id = entity.id;
        row.name = name(entity);
        row.source = entity.id;
        for (auto mapping = canonical.cbegin(); mapping != canonical.cend(); ++mapping)
            if (mapping.value() == entity.id) row.source += '\n' + mapping.key();
        if (driveEntity(entity)) {
            QStringList nodes;
            for (const StorageLink &link : snapshot.links) {
                if (resolve(link.to) == entity.id && link.source.endsWith(".Drive") && entities.contains(resolve(link.from))) {
                    const StorageEntity block = entities.value(resolve(link.from));
                    bool partition = false;
                    for (const StorageField &field : block.fields)
                        if (field.id == "partition" || field.id.contains(".Partition/")) partition = true;
                    if (!partition) {
                        const QString node = name(block);
                        if (!nodes.contains(node)) nodes.append(node);
                    }
                }
            }
            if (!nodes.isEmpty()) row.name += " — " + nodes.join(", ");
            row.kind = tr("Cached drive health");
            healthGroups(row, entity);
            if (row.healthGroups.isEmpty() && row.ataAttributes.isEmpty())
                row.fields.append({tr("Cached health"), tr("Unavailable"), entity.id});
            result.health.append(row);
            if (selectedDrives.size() == 1 && selectedDrives.contains(entity.id))
                result.preferredHealthDrive = entity.id;
            if (selectedDrives.size() > 1 && selectedDrives.contains(entity.id)) {
                result.overview.append({tr("Associated drive"), row.name, entity.id});
                overview(result.overview, entity);
            }
            continue;
        }
        // The selected NVMe controller is an overview, not another volume.
        bool block = false;
        for (const StorageField &field : entity.fields) if (field.id == "dev") block = true;
        if (entity.id == selectedPath && entity.id.startsWith("/sys/") && !block) {
            bool controller = false;
            for (const StorageField &field : entity.fields) if (field.id == "firmware_rev") controller = true;
            if (controller) continue;
        }
        row.kind = kind(entity);
        row.capacity = value(entity, {"capacity", "org.freedesktop.UDisks2.Partition/Size"});
        row.filesystem = value(entity, {"ID_FS_TYPE", "org.freedesktop.UDisks2.Block/IdType"});
        row.mounts = value(entity, {"mounts"});
        const QString label = value(entity, {"ID_FS_LABEL", "org.freedesktop.UDisks2.Block/IdLabel"});
        if (label != tr("Unavailable")) row.name += " — " + label;
        const QVector<QPair<const char *, QStringList>> facts {
            {"Block capacity", {"capacity", "org.freedesktop.UDisks2.Partition/Size"}},
            {"Filesystem / content type", {"ID_FS_TYPE", "org.freedesktop.UDisks2.Block/IdType"}},
            {"Content usage", {"ID_FS_USAGE", "org.freedesktop.UDisks2.Block/IdUsage"}},
            {"Filesystem / content label", {"ID_FS_LABEL", "org.freedesktop.UDisks2.Block/IdLabel"}},
            {"Filesystem / content UUID", {"ID_FS_UUID", "org.freedesktop.UDisks2.Block/IdUUID"}},
            {"Partition number", {"partition", "org.freedesktop.UDisks2.Partition/Number"}},
            {"Partition table type", {"ID_PART_TABLE_TYPE", "org.freedesktop.UDisks2.PartitionTable/Type"}}
        };
        QSet<QString> grouped;
        for (const auto &group : facts) {
            fact(row.fields, entity, group.first, group.second);
            for (const QString &key : group.second) grouped.insert(key);
        }
        for (const StorageField &field : entity.fields)
            if (!healthField(field) && !grouped.contains(field.id)) row.fields.append(display(field));
        for (const StorageLink &link : snapshot.links) {
            if (resolve(link.from) != entity.id || resolve(link.to) == entity.id) continue;
            const QString target = resolve(link.to);
            row.fields.append({relation(link), entities.contains(target) ? name(entities.value(target)) : QFileInfo(target).fileName(),
                               link.source + '\n' + target});
        }
        result.volumes.append(row);
    }
    if (result.health.isEmpty()) {
        StorageDisplayRow row;
        row.id = selectedPath;
        row.name = entities.contains(selectedPath) ? name(entities.value(selectedPath)) : QFileInfo(selectedPath).fileName();
        row.fields.append({tr("Cached health"), tr("Unavailable"), tr("No supported cached drive health in this snapshot")});
        result.health.append(row);
    }
    const auto byName = [](const StorageDisplayRow &a, const StorageDisplayRow &b) {
        const int comparison = naturalCompare(a.name, b.name);
        return comparison == 0 ? a.id < b.id : comparison < 0;
    };
    std::sort(result.volumes.begin(), result.volumes.end(), byName);
    std::sort(result.health.begin(), result.health.end(), byName);
    QStringList notes;
    for (const StorageField &note : snapshot.notes)
        notes.append(tr(note.label) + ": " + storageFieldValue(note) + '\n' + note.source);
    result.notes = notes.join("\n\n");
    return result;
}
QString storageFieldsText(const QVector<StorageDisplayField> &fields)
{
    QStringList text;
    for (const StorageDisplayField &field : fields)
        text.append(field.label + ": " + field.value + '\n' + QCoreApplication::translate("Storage", "Source: %1").arg(field.source));
    return text.join("\n\n");
}
QString storageRowsText(const QVector<StorageDisplayRow> &rows)
{
    QStringList text;
    for (const StorageDisplayRow &row : rows) {
        QStringList sections {row.name};
        if (!row.healthGroups.isEmpty() || !row.ataAttributes.isEmpty()) sections.append(tr("Cached drive health"));
        if (!row.fields.isEmpty()) sections.append(storageFieldsText(row.fields));
        for (const StorageDisplayGroup &group : row.healthGroups)
            sections.append(group.title + '\n' + storageFieldsText(group.fields));
        if (!row.ataAttributes.isEmpty()) {
            QStringList attributes {tr("ATA SMART attributes")};
            for (const StorageDisplayAtaAttribute &attribute : row.ataAttributes)
                attributes.append(tr("%1 (ID %2)\nNormalized value: %3\nWorst normalized value: %4\nThreshold: %5\nInterpreted value: %6\nFlags: %7\nSource: %8")
                    .arg(attribute.name, attribute.id, attribute.current, attribute.worst, attribute.threshold,
                         attribute.interpreted, attribute.flags, attribute.source)
                    + (attribute.interpretedTooltip.isEmpty() ? QString() : '\n' + attribute.interpretedTooltip));
            sections.append(attributes.join("\n\n"));
        }
        text.append(sections.join("\n\n"));
    }
    return text.join("\n\n");
}
