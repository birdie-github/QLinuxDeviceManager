#include "storagepresentation.h"
#include "propertytext.h"
#include <QCoreApplication>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>

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
            for (const StorageField &field : entity.fields) if (healthField(field)) row.fields.append(display(field));
            if (row.fields.isEmpty()) row.fields.append({tr("Cached health"), tr("Unavailable"), entity.id});
            result.health.append(row);
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
    for (const StorageDisplayRow &row : rows)
        text.append(row.name + '\n' + storageFieldsText(row.fields));
    return text.join("\n\n");
}
