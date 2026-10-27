#include "typeprojection.h"
#include <QCoreApplication>
#include <cstdio>

namespace {
int failures = 0;
void check(bool condition, const char *message)
{
    if (!condition) { std::fprintf(stderr, "%s\n", message); ++failures; }
}
Device record(const QString &path, const QString &parent, const QString &subsystem,
              const QString &category, bool hidden = false)
{
    Device device;
    device.path = path;
    device.parentPath = parent;
    device.sysname = path.section('/', -1);
    device.name = "Inherited model";
    device.subsystem = subsystem;
    device.category = category;
    device.hidden = hidden;
    return device;
}
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    const auto pci = record("/sys/devices/pci0/0000:05:00.0", {}, "pci", "network");
    auto net = record(pci.path + "/net/enp5s0", pci.path, "net", "network", true);
    net.representedByPath = pci.path;
    const auto gpu = record("/sys/devices/pci0/0000:09:00.0", {}, "pci", "display");
    const auto primary = record(gpu.path + "/drm/card0", gpu.path, "drm", "display", true);
    const auto render = record(gpu.path + "/drm/renderD128", gpu.path, "drm", "display", true);
    const auto output = record(primary.path + "/card0-DP-1", primary.path, "drm", "display-output");
    const auto audio = record("/sys/devices/pci0/0000:09:00.1", {}, "pci", "audio");
    const auto card = record(audio.path + "/sound/card0", audio.path, "sound", "audio", true);
    auto usb = record("/sys/devices/pci0/usb1", "/sys/devices/pci0", "usb", "usb");
    usb.devtype = "usb_device";
    auto interface = record(usb.path + "/1-0:1.0", usb.path, "usb", "usb", true);
    interface.devtype = "usb_interface";
    const auto hci = record(interface.path + "/bluetooth/hci0", interface.path, "bluetooth", "bluetooth");
    const auto connection = record(hci.path + "/hci0:256", hci.path, "bluetooth", "bluetooth", true);
    const auto root = record("/sys/devices/LNXSYSTM:00", {}, "acpi", "system", true);
    const auto bus = record(root.path + "/LNXSYBUS:00", root.path, "acpi", "system", true);
    const auto resource = record(bus.path + "/PNP0C02:0d", bus.path, "acpi", "system");
    const auto sibling = record(bus.path + "/AMDI0020:00", bus.path, "acpi", "system");
    const auto cpu = record(root.path + "/LNXCPU:05", root.path, "acpi", "processor");
    const QVector<Device> devices {pci, net, gpu, primary, render, output, audio, card,
                                 usb, interface, hci, connection, root, bus, resource, sibling, cpu};
    const auto all = typeProjection(devices, true);
    check(all.included.size() == devices.size(), "All records must remain present exactly once");
    check(all.parents.value(net.path) == pci.path && all.parents.value(card.path) == audio.path,
          "Network and ALSA objects must attach to their actual controllers");
    check(all.parents.value(primary.path) == gpu.path && all.parents.value(render.path) == gpu.path
          && all.parents.value(output.path) == primary.path,
          "DRM nodes and connectors must preserve their actual ancestry");
    check(all.parents.value(interface.path) == usb.path && all.parents.value(hci.path) == interface.path
          && all.parents.value(connection.path) == hci.path,
          "USB interfaces and Bluetooth objects must retain separate layers");
    check(!all.parents.contains(usb.path), "Absent controller records must not be synthesized");
    const auto normal = typeProjection(devices, false);
    check(!normal.included.contains(net.path) && !normal.parents.contains(net.path),
          "Normal-view endpoint visibility must remain unchanged");
    check(normal.included.contains(root.path) && normal.included.contains(bus.path)
          && normal.parents.value(resource.path) == bus.path && normal.parents.value(sibling.path) == bus.path
          && normal.parents.value(cpu.path) == root.path,
          "Hidden ACPI ancestors must be retained without inventing sibling ownership or CPU mappings");
    check(typeDeviceLabel(net, true, true) == "Network interface [enp5s0]"
          && typeDeviceLabel(card, true, true) == "ALSA sound card [card0]"
          && typeDeviceLabel(primary, true, true) == "DRM primary node [card0]"
          && typeDeviceLabel(render, true, true) == "DRM render node [renderD128]",
          "Nested labels must identify endpoint roles rather than repeat inherited model names");
    check(typeDeviceLabel(pci, false, true).contains("Inherited model"),
          "Physical root labels must retain their hardware names");
    auto first = root, second = bus;
    first.parentPath = second.path;
    second.parentPath = first.path;
    const auto cyclic = typeProjection({first, second}, true);
    check(!(cyclic.parents.value(first.path) == second.path && cyclic.parents.value(second.path) == first.path),
          "Cyclic parent metadata must be cut");
    auto bridged = net;
    bridged.parentPath = pci.path + "/unrecorded";
    bridged.path = bridged.parentPath + "/net/enp5s0";
    const auto bridge = typeProjection({pci, bridged}, true);
    check(bridge.parents.value(bridged.path) == pci.path,
          "Missing intermediate directories may bridge only to a recorded real ancestor");
    auto unrelated = net;
    unrelated.path = "/sys/devices/virtual/net/other";
    unrelated.parentPath.clear();
    const auto separate = typeProjection({pci, unrelated}, true);
    check(!separate.parents.contains(unrelated.path), "Model names and stale grouping hints must never invent ancestry");
    return failures ? 1 : 0;
}
