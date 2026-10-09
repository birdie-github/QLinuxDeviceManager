#include "devicesearch.h"
#include "devicefilter.h"
#include "devicemodel.h"
#include <QCoreApplication>
#include <cstdio>

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
    properties.state = ReadState::Removed;
    document = deviceSearchDocument({device, "Visible device", {}}, properties);
    check(searchDocumentMatch(document, "32902").isEmpty(), "Invalidated properties do not contribute partial data");
    check(!searchDocumentMatch(document, "casesensitiveserial").isEmpty(), "Last inventory remains searchable after removal");

    Device hidden = device;
    hidden.path += "/hidden";
    hidden.hidden = true;
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
    return failures ? 1 : 0;
}
