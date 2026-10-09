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
    check(filter.visibleCount() == 0 && filter.rowCount() == 0, "Name search ignores metadata and empty categories");
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
    return failures ? 1 : 0;
}
