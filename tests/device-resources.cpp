#include "deviceresources.h"
#include <cstdio>
#include <cstdlib>

namespace {
void require(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
}
int main()
{
    using Type = DeviceResource::Type;
    using Allocation = DeviceResource::Allocation;
    // Fixtures only. No real sysfs access, hardware actions, or GUI creation.
    const DeviceResources pci = parsePciResources(
        "0x00000007c0000000 0x00000007cfffffff 0x00102200\n"
        "0x000000000000ef00 0x000000000000efff 0x00000100\n"
        "0 0 0\n"
        "0 0xfff 0x20000200\n"
        "0x1000 0x1fff 0x10000200\n"
        "0 0 0x200\n", "fixture/resource");
    require(pci.issues.isEmpty() && pci.items.size() == 5, "PCI resources should preserve five typed entries");
    require(pci.items[0].start == 0x7c0000000ULL && pci.items[0].end == 0x7cfffffffULL,
            "64-bit memory addresses must not be truncated");
    require(pci.items[1].type == Type::Io && pci.items[1].start == 0xef00, "I/O type must come from flags");
    require(pci.items[2].index == 3 && pci.items[2].allocation == Allocation::Unassigned,
            "Empty slots must not shift BAR indices or turn unassigned resources into ranges");
    require(pci.items[3].allocation == Allocation::Disabled, "Disabled resources must not appear assigned");
    require(pci.items[4].allocation == Allocation::Unavailable, "Typed zeroed addresses must remain unavailable");
    require(pci.items[0].source == "fixture/resource", "Resource attribution must survive parsing");
    const DeviceResources bad = parsePciResources("0x1000 0x1fff 0x200\ninvalid\n", "fixture/resource");
    require(bad.items.isEmpty() && bad.issues.size() == 1, "Malformed input must not publish a shifted partial PCI table");
    require(!parsePciResources("0x2000 0x1000 0x200\n", "fixture/resource").issues.isEmpty(),
            "Reversed assigned ranges must be rejected");
    require(!parsePciResources("0x10000000000000000 0x1fff 0x200\n", "fixture/resource").issues.isEmpty(),
            "Overflowing addresses must be rejected");
    require(!parsePciResources(QString(65537, 'x'), "fixture/resource").issues.isEmpty(),
            "Oversized metadata must be rejected");
    const DeviceResources pnp = parsePnpResources(
        "state = active\nio 0x60-0x60\nirq 0\nmem 0xf0000000-0xffffffff window\nirq disabled\ndma 4\n", "fixture/resources");
    require(pnp.issues.isEmpty() && pnp.items.size() == 5, "PnP metadata should retain typed assignments");
    require(pnp.items[1].type == Type::Irq && pnp.items[1].start == 0
            && pnp.items[1].allocation == Allocation::Assigned, "PnP IRQ zero can be legitimate");
    require(pnp.items[2].flags == 0x00200000, "PnP windows must retain their explicit marker");
    require(pnp.items[3].allocation == Allocation::Disabled, "Disabled PnP IRQs must not become IRQ zero");
    const DeviceResources inactive = parsePnpResources("state = disabled\nio 0x60-0x64\n", "fixture/resources");
    require(inactive.items.size() == 1 && inactive.items[0].allocation == Allocation::Disabled,
            "Disabled PnP device assignments must not be shown as active");
    require(!parsePnpResources("state = active\nmem 0x20-0x10\n", "fixture/resources").issues.isEmpty(),
            "Invalid PnP ranges must be rejected");
    require(!parsePnpResources("irq 7\n", "fixture/resources").issues.isEmpty(),
            "Missing PnP state must be rejected");
    require(!parsePciResources("0 0 0\n", "fixture/resource").hasInformation(),
            "Empty slots alone must not enable a Resources tab");
    return 0;
}
