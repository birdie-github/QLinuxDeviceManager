#include "devicesearch.h"
#include "devicefilter.h"
#include "devicemodel.h"
#include "resourceformat.h"
#include "propertytext.h"
#include "storagepresentation.h"
#include <QCoreApplication>
#include <QObject>
#include <cstdio>
#include <QPersistentModelIndex>
#include <QLocale>
#include <cerrno>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    Device device;
    device.path = "/sys/devices/fixture";
    device.incarnation = "fixture:1";
    device.name = "Visible device";
    device.category = "other";
    device.subsystem = "pci";
    device.properties.insert("ID_SERIAL", "CaseSensitiveSerial");
    DeviceProperties properties;
    properties.device = device;
    properties.state = ReadState::Available;
    properties.values.insert("sysfs/vendor", {ReadState::Available, "0x8086", 0});
    DeviceResource io;
    io.type = DeviceResource::Type::Io;
    io.start = 0x300; io.end = 0x31f;
    io.source = "/sys/devices/fixture/resource";
    properties.resources.items.append(io);
    DeviceProperties statusFixture = properties;
    statusFixture.device.properties.clear();
    statusFixture.values.remove("sysfs/vendor");
    statusFixture.values.insert("sysfs/model", {ReadState::Available, "Unavailable", 0});
    statusFixture.values.insert("sysfs/serial", {ReadState::Unavailable, {}, 0});
    statusFixture.values.insert("sysfs/revision", {ReadState::PermissionDenied, {}, 13});
    for (const PropertyEntry &entry : propertyEntries(statusFixture)) {
        if (entry.id == "sysfs/model")
            check(detailsPropertyStatus(entry).isEmpty(), "A literal status-like device value remains meaningful");
        if (entry.id == "sysfs/serial")
            check(entry.state == ReadState::Unavailable && detailsPropertyStatus(entry) == "unavailable",
                  "Unavailable raw properties retain status independently of displayed text");
        if (entry.id == "sysfs/revision")
            check(entry.state == ReadState::PermissionDenied && detailsPropertyStatus(entry) == "permission denied",
                  "Permission-denied properties remain distinct from unavailable properties");
        if (entry.id == "identifiers")
            check(entry.state == ReadState::Unavailable, "Missing derived hardware identifiers carry an unavailable status");
    }
    PropertyEntry failedProperty {"failed", "Failure", "localized explanation", {}, false, 2, ReadState::Error, 5};
    check(detailsPropertyStatus(failedProperty) == "read error (errno 5)", "Read error suffix preserves errno");
    DeviceProperties efiStatus = properties;
    efiStatus.device.subsystem = "efivarfs";
    for (const PropertyEntry &entry : propertyEntries(efiStatus))
        if (entry.id == "identifiers")
            check(entry.state == ReadState::NotApplicable && detailsPropertyStatus(entry) == "not applicable",
                  "UEFI hardware identifiers carry their derived not-applicable status");
    auto document = deviceSearchDocument({device, "Visible device", {}}, properties);
    check(!searchDocumentMatch(document, "casesensitiveserial").isEmpty(), "Raw metadata is case insensitive");
    check(!searchDocumentMatch(document, "32902").isEmpty(), "PCI vendor has decimal alias");
    check(!searchDocumentMatch(document, "0x8086").isEmpty(), "PCI vendor keeps hex value");
    check(searchDocumentMatch(document, "768") == "I/O range", "I/O range has decimal alias and reason");
    check(!searchDocumentMatch(document, "0000000000000300").isEmpty(), "Address keeps padded hex representation");
    Device efi = device;
    efi.subsystem = "efivarfs";
    DeviceProperties efiProperties;
    efiProperties.device = efi;
    efiProperties.state = ReadState::Available;
    efiProperties.efiBytes = QByteArray::fromHex("4142ff");
    efiProperties.values.insert("efi/hex", {ReadState::Available, {}, 0});
    const auto efiDocument = deviceSearchDocument({efi, "EFI fixture", {}}, efiProperties);
    check(!searchDocumentMatch(efiDocument, "41 42 ff").isEmpty(), "EFI hex bytes are searchable");
    check(!searchDocumentMatch(efiDocument, "AB.").isEmpty(), "EFI printable byte view is searchable");
    check(!searchDocumentMatch(efiDocument, "4142ff").isEmpty(), "EFI compact hex bytes are searchable");
    // Synthetic storage observations, not live hardware results. Missing health
    // must remain unavailable, while explicit cached evidence stays searchable.
    DeviceProperties storageFixture;
    storageFixture.device = device;
    storageFixture.state = ReadState::Available;
    storageFixture.storage.applicable = true;
    StorageEntity volume;
    volume.id = "/sys/devices/storage-fixture";
    volume.fields.append({"capacity", "Block capacity", {ReadState::Available, "18446744073709551615", 0},
                          "fixture/size", StorageField::Format::Bytes});
    volume.fields.append({"health", "Cached NVMe critical warnings", {}, "fixture/cache", StorageField::Format::NvmeHealth});
    volume.fields.append({"mounts", "Mount points", {ReadState::Available, "/fixture mount", 0}, "fixture/mountinfo"});
    storageFixture.storage.entities.append(volume);
    const auto storageEntries = propertyEntries(storageFixture);
    bool unavailableHealth = false;
    bool exactCapacity = false;
    for (const PropertyEntry &entry : storageEntries) {
        if (entry.id.endsWith("/health")) unavailableHealth = entry.value == "Unavailable";
        if (entry.id.endsWith("/capacity")) exactCapacity = entry.value.endsWith(" bytes") && !entry.value.contains('(');
    }
    check(unavailableHealth, "Missing health data cannot produce a healthy verdict");
    check(exactCapacity, "Capacity above signed 64-bit range keeps exact bytes without a clamped size");
    const auto storageDocument = deviceSearchDocument({device, "Visible device", {}}, storageFixture);
    check(!searchDocumentMatch(storageDocument, "/fixture mount").isEmpty(), "Storage mounts are searchable");
    storageFixture.state = ReadState::Removed;
    check(searchDocumentMatch(deviceSearchDocument({device, "Visible device", {}}, storageFixture), "/fixture mount").isEmpty(),
          "Removed instances cannot contribute partial storage results to search");
    properties.state = ReadState::Removed;
    document = deviceSearchDocument({device, "Visible device", {}}, properties);
    check(searchDocumentMatch(document, "32902").isEmpty(), "Invalidated properties do not contribute partial data");
    check(!searchDocumentMatch(document, "casesensitiveserial").isEmpty(), "Last inventory remains searchable after removal");

    Device hidden = device;
    hidden.path += "/hidden";
    hidden.hidden = true;
    {
        DeviceModel sorted;
        const QStringList expected {"cpu0", "cpu01", "cpu1", "cpu2", "cpu10",
            "cpu999999999999999999999999", "cpu1000000000000000000000000",
            "nvme0n1p2", "nvme0n1p10", "nvme0n2p1", "nvme0n10p1"};
        QVector<Device> numbered;
        for (auto it = expected.crbegin(); it != expected.crend(); ++it) {
            Device item;
            item.path = "/sys/devices/sort-fixture/" + *it;
            item.incarnation = item.path;
            item.name = item.sysname = *it;
            item.subsystem = "fixture";
            item.category = "other";
            numbered.append(item);
        }
        sorted.setInventory(numbered);
        for (const auto view : {DeviceModel::View::Type, DeviceModel::View::Connection}) {
            sorted.setView(view);
            QStringList actual;
            for (const SearchRecord &record : sorted.searchRecords()) actual.append(record.device.sysname);
            check(actual == expected,
                  "Tree order handles numeric runs, leading-zero ties and numbers beyond integer range");
        }
    }
    DeviceModel model;
    model.setInventory({device, hidden});
    const auto records = model.searchRecords();
    check(records.size() == 1, "Hidden devices are excluded from search inputs");
    if (records.isEmpty()) return 1;
    const auto &record = records.front();
    DeviceFilter filter;
    filter.setSourceModel(&model);
    filter.setQuery("casesensitiveserial", false);
    check(filter.visibleCount() == 0 && filter.rowCount() == 1
          && filter.rowCount(filter.index(0, 0)) == 0, "Name search ignores metadata and empty categories but retains computer root");
    filter.setQuery("casesensitiveserial", true);
    filter.addMatches({{record.device.path, record.device.generation + 1, "ID_SERIAL"}});
    check(filter.visibleCount() == 0, "A stale generation cannot match a reused path");
    filter.addMatches({{record.device.path, record.device.generation, "ID_SERIAL"}});
    check(filter.visibleCount() == 1 && filter.rowCount() == 1, "Deep match retains its category");
    filter.setQuery(record.label.toUpper(), false);
    check(filter.visibleCount() == 1, "Name search uses the exact displayed label, case insensitively");
    filter.setQuery({}, false);
    check(filter.visibleCount() == 1, "Clearing query restores eligible devices");
    model.setShowInternal(true);
    check(model.searchRecords().size() == 2 && filter.visibleCount() == 2, "Internal visibility applies to both modes");
    // Phase 3 projections: bus-qualified identity, real ancestry and synthetic rows.
    Device pci = device;
    pci.driver = "shared";
    pci.driverModule = "owner";
    Device usb = device;
    usb.path = "/sys/devices/usb-fixture";
    usb.name = "USB fixture";
    usb.subsystem = "usb";
    usb.driver = "shared";
    usb.driverModule = "owner";
    Device child = device;
    child.path = pci.path + "/child";
    child.parentPath = pci.path;
    child.name = "Child fixture";
    Device context = pci;
    context.hidden = true;
    model.setShowInternal(false);
    model.setInventory({context, child, usb});
    model.setView(DeviceModel::View::Connection);
    check(model.visibleCount() == 3, "Connection retains hidden ancestor context");
    // Find generations by record identity; presentation ordering is not ownership.
    quint64 childGeneration = 0;
    for (const auto &entry : model.searchRecords())
        if (entry.device.path == child.path) childGeneration = entry.device.generation;
    auto childIndex = model.findDevice(child.path, childGeneration);
    check(childIndex.parent().data(DeviceModel::PathRole).toString() == pci.path,
          "Connection uses actual parent record");
    filter.setQuery("Child fixture", false);
    check(filter.visibleCount() == 2, "Connection filter retains matching device ancestry");
    filter.setQuery({}, false);
    model.setInventory({pci, child, usb});
    for (const auto &entry : model.searchRecords())
        if (entry.device.path == child.path) childGeneration = entry.device.generation;
    model.setView(DeviceModel::View::DevicesByDriver);
    check(model.rowCount(model.index(0, 0)) == 3, "Same driver name on two buses stays distinct from unbound group");
    check(model.visibleCount() == 3 && filter.visibleCount() == 3,
          "Driver groups never inflate device count");
    model.setView(DeviceModel::View::DriversByDevice);
    childIndex = model.findDevice(child.path, childGeneration);
    check(model.rowCount(childIndex) == 2, "Unbound child has separate parent-driver relationship");
    for (int row = 0; row < model.rowCount(childIndex); ++row)
        check(!model.index(row, 0, childIndex).data(DeviceModel::PathRole).isValid(),
              "Synthetic driver rows cannot retarget device Properties");
    check(model.searchRecords().size() == 3 && filter.visibleCount() == 3,
          "Device-centric search/count ignores module rows");
    model.setView(DeviceModel::View::DriversByType);
    check(model.rowCount(model.index(0, 0)) == 1
          && model.rowCount(model.index(0, 0, model.index(0, 0))) == 3,
          "Drivers by type nests bus-qualified groups within category");
    check(model.findDevice(child.path, childGeneration).isValid(), "Instance selection survives view changes");
    Device unknown = pci;
    unknown.driverModule.clear();
    model.setInventory({unknown, child, usb});
    model.setView(DeviceModel::View::DriversByDevice);
    quint64 pciGeneration = 0;
    for (const auto &entry : model.searchRecords())
        if (entry.device.path == pci.path) pciGeneration = entry.device.generation;
    const QModelIndex pciIndex = model.findDevice(pci.path, pciGeneration);
    const QModelIndex driverIndex = model.index(0, 0, pciIndex);
    check(model.index(0, 0, driverIndex).data().toString().contains("undetermined"),
          "Missing module link does not establish built-in or modular status");
    filter.setQuery("Visible device", false);
    const QModelIndex filteredPci = filter.mapFromSource(pciIndex);
    check(filter.rowCount(filteredPci) == 1 && filter.rowCount(filter.index(0, 0, filteredPci)) == 1,
          "Filtering a device retains its driver and module-status rows");
    filter.setQuery({}, false);
    for (const auto mode : {DeviceModel::View::Type, DeviceModel::View::Connection,
                           DeviceModel::View::DevicesByDriver, DeviceModel::View::DriversByDevice,
                           DeviceModel::View::DriversByType}) {
        model.setView(mode);
        const QModelIndex computer = model.index(0, 0);
        check(model.rowCount() == 1 && computer.data(DeviceModel::NodeKeyRole).toString() == "computer",
              "Every projection has one computer root");
        check(!computer.data(DeviceModel::PathRole).isValid() && !computer.data().toString().isEmpty(),
              "Computer root has a caption and is not a device Properties target");
        check(model.visibleCount() == 3 && model.searchRecords().size() == 3,
              "Computer root is excluded from device counts and search records");
    }
    model.setView(DeviceModel::View::Type);
    check(model.visibleCount() == 3, "Returning to type projection retains same inventory");
    // Live updates keep persistent indexes and replace observed instances.
    int resets = 0;
    int changes = 0;
    QObject observationContext; // Disconnect before the captured counters leave scope.
    QObject::connect(&model, &QAbstractItemModel::modelReset, &observationContext, [&] { ++resets; });
    QObject::connect(&model, &QAbstractItemModel::dataChanged, &observationContext, [&] { ++changes; });
    model.setInventory({pci, child, usb});
    auto currentGeneration = [&](const QString &path) {
        for (const auto &entry : model.searchRecords())
            if (entry.device.path == path) return entry.device.generation;
        return quint64(0);
    };
    pciGeneration = currentGeneration(pci.path);
    QPersistentModelIndex retained(model.findDevice(pci.path, pciGeneration));
    changes = 0;
    model.setInventory({pci, child, usb});
    check(retained.isValid() && changes == 0 && resets == 0,
          "Unchanged live snapshots emit no row data changes or model resets");
    Device renamed = pci;
    renamed.name = "ZZ renamed fixture";
    model.setInventory({renamed, child, usb});
    check(retained.isValid() && retained.data().toString().contains("ZZ renamed"),
          "Rename/reorder preserves persistent instance index");
    QPersistentModelIndex oldChild(model.findDevice(child.path, currentGeneration(child.path)));
    model.setInventory({renamed, child, usb}, {pci.path});
    check(!retained.isValid() && !oldChild.isValid() && !model.device(pci.path, pciGeneration),
          "Remove/add replaces parent and descendant identities even with unchanged inode hints");
    QPersistentModelIndex lost(model.findDevice(usb.path, currentGeneration(usb.path)));
    model.setInventory({renamed, child, usb}, {}, true);
    check(!lost.isValid() && resets == 0, "Event loss invalidates identities through row replacement");
    for (const auto mode : {DeviceModel::View::Type, DeviceModel::View::Connection,
                           DeviceModel::View::DevicesByDriver, DeviceModel::View::DriversByDevice,
                           DeviceModel::View::DriversByType}) {
        model.setView(mode);
        QPersistentModelIndex stable(model.findDevice(usb.path, currentGeneration(usb.path)));
        model.setInventory({renamed, usb});
        check(stable.isValid() && model.visibleCount() == 2, "Removal retains unrelated rows in every projection");
        model.setInventory({renamed, child, usb});
        check(stable.isValid() && model.visibleCount() == 3, "Addition retains unrelated rows in every projection");
    }
    // Resource projections share formatting, retain real ancestry and count
    // unique devices rather than vectors, ranges or repeated type memberships.
    auto resources = std::make_shared<DeviceResources>();
    DeviceResource vector;
    vector.type = DeviceResource::Type::Irq;
    vector.start = vector.end = 121;
    vector.mode = "msix";
    vector.source = pci.path + "/msi_irqs/121";
    resources->items.append(vector);
    DeviceResource memory;
    memory.type = DeviceResource::Type::Memory;
    memory.start = 0x80000000;
    memory.end = 0x80000fff;
    memory.index = 0;
    memory.pci = true;
    memory.flags = 0x00102200;
    memory.source = pci.path + "/resource";
    resources->items.append(memory);
    const auto formatted = resourceRows(*resources);
    check(formatted.size() == 2 && formatted.at(0).setting == "121"
          && formatted.at(0).details.contains("MSI-X") && formatted.at(1).details.contains("BAR 0")
          && formatted.at(1).details.contains("Prefetchable"), "Shared resource formatter preserves vectors/BAR flags");
    Device bus = pci;
    bus.path = "/sys/devices/pci-root";
    bus.name = "Real parent context";
    bus.hidden = true;
    bus.resources.reset();
    pci.resources = resources;
    pci.parentPath = bus.path;
    auto childResources = std::make_shared<DeviceResources>(*resources);
    for (auto &entry : childResources->items)
        entry.source.replace(pci.path, child.path);
    child.resources = childResources;
    // Shared IRQ/range evidence stays on both devices, without conflict inference.
    model.setInventory({bus, pci, child, usb});
    model.setView(DeviceModel::View::ResourcesByType);
    check(model.rowCount(model.index(0, 0)) == 2 && model.visibleCount() == 2
          && model.searchRecords().size() == 2 && filter.visibleCount() == 2,
          "Type view groups memory/IRQ while deduplicating device counts and search inputs");
    pciGeneration = currentGeneration(pci.path);
    auto resourceOwner = model.findDevice(pci.path, pciGeneration);
    auto resourceIndex = model.index(0, 0, resourceOwner);
    check(resourceIndex.data(DeviceModel::ResourceRole).toBool()
          && !resourceIndex.data(DeviceModel::PathRole).isValid()
          && resourceIndex.data(DeviceModel::OwnerPathRole).toString() == pci.path
          && resourceIndex.data(DeviceModel::OwnerGenerationRole).toULongLong() == pciGeneration,
          "Resource rows carry an instance-scoped owner without becoming device records");
    const QString resourceKey = resourceIndex.data(DeviceModel::NodeKeyRole).toString();
    QPersistentModelIndex stableResource(resourceIndex);
    Device cloned = pci;
    cloned.resources = std::make_shared<const DeviceResources>(*resources);
    check(!model.setInventory({bus, cloned, child, usb}) && stableResource.isValid(),
          "Equivalent owned resource snapshots do not rebuild the model");
    check(model.findNode(resourceKey, pci.path, pciGeneration).isValid(),
          "Resource selection restores its exact node by device generation");
    auto changedResources = std::make_shared<DeviceResources>(*resources);
    changedResources->items[0].start = changedResources->items[0].end = 122;
    changedResources->items[0].source = pci.path + "/msi_irqs/122";
    Device reassigned = pci;
    reassigned.resources = changedResources;
    QPersistentModelIndex sameOwner(resourceOwner);
    check(model.setInventory({bus, reassigned, child, usb}) && sameOwner.isValid()
          && !stableResource.isValid() && currentGeneration(pci.path) == pciGeneration,
          "Resource-only changes update assignment rows without replacing the reporting device");
    model.setInventory({bus, pci, child, usb});
    filter.setQuery("121", false);
    check(filter.visibleCount() == 2, "Name filtering finds resource settings and retains both shared owners");
    filter.setQuery({}, false);
    model.setView(DeviceModel::View::ResourcesByConnection);
    resourceOwner = model.findDevice(pci.path, pciGeneration);
    check(resourceOwner.parent().data(DeviceModel::PathRole).toString() == bus.path
          && model.visibleCount() == 3 && model.searchRecords().size() == 3,
          "Connection view retains real hidden ancestry and excludes unrelated no-resource devices");
    childIndex = model.findDevice(child.path, currentGeneration(child.path));
    check(childIndex.parent() == resourceOwner && model.rowCount(childIndex) == 2,
          "Connection view follows recorded parents and adds direct resource children");
    filter.setQuery("121", false);
    check(filter.visibleCount() == 3, "Resource filtering retains connection-context ancestors");
    filter.setQuery({}, false);
    childGeneration = currentGeneration(child.path);
    model.setInventory({bus, pci, usb});
    check(model.visibleCount() == 2 && !model.findDevice(child.path, childGeneration).isValid(),
          "Resource connection updates remove vanished records");
    model.setView(DeviceModel::View::ResourcesByType);
    resourceOwner = model.findDevice(pci.path, pciGeneration);
    QPersistentModelIndex replacedResource(model.index(0, 0, resourceOwner));
    model.setInventory({bus, pci, usb}, {pci.path});
    check(!replacedResource.isValid() && !model.findNode(resourceKey, pci.path, pciGeneration).isValid(),
          "A replaced device cannot inherit resource selection or an old owner generation");
    // Storage presentation stays independent of widgets and live services.
    StorageProperties storage;
    storage.applicable = true;
    const auto sf = [](const QString &id, const QString &value, StorageField::Format format = StorageField::Format::Text) {
        return StorageField {id, "Fixture", {ReadState::Available, value, 0}, "fixture:" + id, format};
    };
    StorageEntity nativeBlock {"/sys/devices/fixture/block/sda", {
        sf("DEVNAME", "/dev/sda"), sf("dev", "8:0"), sf("capacity", "1024", StorageField::Format::Bytes),
        sf("ID_FS_TYPE", "ext4"), sf("ID_FS_LABEL", "data"), sf("mounts", "/data")}};
    StorageEntity cachedBlock {"/org/freedesktop/UDisks2/block_devices/sda", {
        sf("org.freedesktop.UDisks2.Block/IdType", "xfs")}};
    StorageEntity cachedDrive {"/org/freedesktop/UDisks2/drives/fixture", {
        sf("org.freedesktop.UDisks2.Drive/Model", "Fixture Drive"),
        sf("org.freedesktop.UDisks2.Drive/Size", "4096", StorageField::Format::Bytes),
        sf("org.freedesktop.UDisks2.Drive.Ata/SmartFailing", "0", StorageField::Format::AtaHealth)}};
    storage.entities = {nativeBlock, cachedBlock, cachedDrive};
    storage.links = {{nativeBlock.id, cachedBlock.id, "UDisks2 block metadata", "UDisks2 Block.DeviceNumber / sysfs dev"},
                     {cachedBlock.id, cachedDrive.id, "Storage relationship", "org.freedesktop.UDisks2.Block.Drive"}};
    auto presented = storagePresentation(storage, nativeBlock.id);
    check(presented.volumes.size() == 1 && presented.volumes.front().name == "/dev/sda — data",
          "An exact native/service block mapping produces one recognizable volume");
    check(storageFieldsText(presented.volumes.front().fields).contains("Conflicting metadata:"),
          "Conflicting block content metadata remains explicit with both sources");
    check(presented.health.size() == 1 && presented.health.front().name.contains("/dev/sda")
          && storageRowsText(presented.health).contains("not a guarantee of health"),
          "Health is per drive, names its mapped block and preserves cached scope");
    check(storageFieldsText(presented.overview).contains("Block capacity:")
          && storageFieldsText(presented.overview).contains("Drive capacity:"),
          "Physical drive capacity is separate from selected block capacity");
    check(presented.preferredHealthDrive == cachedDrive.id,
          "A unique explicit drive association determines the initial health selection");
    {
        // A related drive sorts before the selected device's drive (e.g. RAID/LVM).
        StorageProperties related = storage;
        StorageEntity otherDrive = cachedDrive;
        otherDrive.id = "/org/freedesktop/UDisks2/drives/aaa_related";
        related.entities.append(otherDrive);
        auto multiple = storagePresentation(related, nativeBlock.id);
        check(multiple.health.size() == 2 && multiple.health.front().id == otherDrive.id
              && multiple.preferredHealthDrive == cachedDrive.id,
              "A related drive's sort order cannot determine the selected device's preferred health drive");
        const QString controller = "/sys/devices/fixture";
        check(storagePresentation(related, controller).preferredHealthDrive == cachedDrive.id,
              "A controller can prefer the unique drive explicitly associated with its descendants");
        StorageEntity otherBlock {controller + "/block/sdb", {sf("DEVNAME", "/dev/sdb")}};
        related.entities.append(otherBlock);
        related.links.append({otherBlock.id, otherDrive.id, "Storage relationship", "org.freedesktop.UDisks2.Block.Drive"});
        check(storagePresentation(related, controller).preferredHealthDrive.isEmpty(),
              "Multiple explicitly associated drives do not establish a unique preferred health drive");
    }
    storage.links.removeFirst();
    check(storagePresentation(storage, nativeBlock.id).volumes.size() == 2,
          "Matching object basenames without an exact mapping never merge entities");
    check(storagePresentation(storage, nativeBlock.id).preferredHealthDrive.isEmpty(),
          "Without exact block mapping, object basenames cannot establish the preferred health drive");
    storage.entities = {nativeBlock};
    storage.links.clear();
    presented = storagePresentation(storage, nativeBlock.id);
    check(presented.health.size() == 1 && storageRowsText(presented.health).contains("Unavailable"),
          "Native-only snapshots explicitly report unavailable cached health");
    check(presented.preferredHealthDrive.isEmpty(),
          "Unavailable native-only health has no preferred UDisks2 drive");
    {
        StorageProperties health;
        const QLocale previousLocale;
        QLocale::setDefault(QLocale::c());
        const QString nvme = "org.freedesktop.UDisks2.NVMe.Controller/";
        const QString attributes = nvme + "SmartGetAttributes/";
        StorageEntity drive {"/org/freedesktop/UDisks2/drives/nvme_fixture", {
            sf("org.freedesktop.UDisks2.Drive/Model", "NVMe fixture"),
            sf(nvme + "SmartUpdated", "1720000000", StorageField::Format::Epoch),
            sf(nvme + "SmartCriticalWarning", "readonly\nfuture_warning", StorageField::Format::NvmeHealth),
            sf(attributes + "percent_used", "150", StorageField::Format::Percent),
            sf(attributes + "media_errors", "0", StorageField::Format::Count),
            sf(attributes + "temp_sensors/1", "306", StorageField::Format::Kelvin),
            {attributes + "temp_sensors/2", "Fixture", {}, "fixture:absent", StorageField::Format::Kelvin},
            {attributes + "temp_sensors/3", "Fixture", {ReadState::Error, {}, EPROTO}, "fixture:invalid", StorageField::Format::Kelvin},
            {attributes + "temp_sensors/4", "Fixture", {ReadState::Error, {}, EPROTO}, "fixture:invalid", StorageField::Format::Kelvin}
        }};
        health.entities = {drive};
        auto report = storagePresentation(health, drive.id);
        const QString copied = storageRowsText(report.health);
        check(report.health.front().healthGroups.front().title == "Health and endurance"
              && copied.contains("150%") && copied.contains("Media and data integrity errors: 0"),
              "Grouped NVMe health preserves over-100 endurance estimates and zero error counters");
        check(copied.contains("Temperature sensor 1: 33 °C") && !copied.contains("Temperature sensor 2")
              && copied.count("Temperature sensors:") == 1,
              "NVMe sensor precision, omitted unreported slots and retained sensor errors reach copied output");
        check(copied.contains("Media is in read-only mode") && copied.contains("future_warning"),
              "Known critical warnings are explained and unknown warning tokens remain visible");
        drive.id = "/org/freedesktop/UDisks2/drives/ata_fixture";
        drive.fields = {sf("org.freedesktop.UDisks2.Drive/Model", "ATA fixture"),
            sf("org.freedesktop.UDisks2.Drive.Ata/SmartFailing", "0", StorageField::Format::AtaHealth)};
        drive.ataAttributes = {{194, "temperature-celsius", 0, 100, 95, 0, 306000, 4},
            {9, "power-on-hours", 0, 0, -1, 0, 1234567, 0},
            {5, "reallocated-sector-count", 3, 100, 100, 36, 0, 3}};
        health.entities = {drive};
        report = storagePresentation(health, drive.id);
        const auto &table = report.health.front().ataAttributes;
        const QString ataCopy = storageRowsText(report.health);
        check(table.size() == 3 && table[0].id == "5" && table[1].current == "0"
              && table[1].worst == "Unavailable" && table[1].interpreted == "Unavailable"
              && table[2].interpreted == "32.9 °C",
              "ATA table preserves ID order, normalized zero, unknown values and millikelvin precision");
        check(ataCopy.contains("reallocated-sector-count (ID 5)") && ataCopy.contains("0 sectors")
              && ataCopy.contains("Flags: 0x0003") && ataCopy.contains("Source: " + drive.id),
              "Health copying includes ATA table values, zero counters, flags and provenance");
        QLocale::setDefault(previousLocale);
    }
    Device storageDevice = device;
    storageDevice.subsystem = "block";
    DeviceProperties storageSnapshot;
    storageSnapshot.device = storageDevice;
    storageSnapshot.storage = storage;
    storageSnapshot.values.insert("udev/ID_FS_UUID", {ReadState::Available, "uuid", 0});
    storageSnapshot.device.properties.insert("DM_UUID", "dm-uuid");
    storageSnapshot.device.attributes.insert("size", {ReadState::Available, "2", 0});
    int excludedStorageEntries = 0;
    for (const PropertyEntry &entry : propertyEntries(storageSnapshot)) {
        if (entry.tab == 3 || entry.id == "udev/ID_FS_UUID" || entry.id == "inventory/DM_UUID"
            || entry.id == "inventory/sysfs/size") {
            ++excludedStorageEntries;
            check(!isDetailsProperty(entry, storageDevice), "Storage fields never enter Details, including advanced fields");
        }
        if (entry.id == "name" || entry.id == "module/type")
            check(isDetailsProperty(entry, storageDevice), "Generic device and driver properties remain in Details");
    }
    check(excludedStorageEntries > 3, "Details exclusion fixture exercises storage and raw metadata");
    check(isDetailsProperty({"udev/ID_SERIAL", "Serial", "value", {}, true, 2}, device),
          "Raw non-storage device identity remains available");
    return failures ? 1 : 0;
}
