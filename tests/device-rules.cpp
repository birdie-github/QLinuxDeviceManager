// Fixture-only checks. No live hardware is touched and no GUI is launched.
#include "device.h"
#include "cpumetadata.h"
#include "devicepresentation.h"
#include <cstdio>

static int failures = 0;
static void check(bool result, const char *message)
{
    if (!result) { std::fprintf(stderr, "%s\n", message); ++failures; }
}
int main()
{
    Device keyboard;
    keyboard.path = "/sys/devices/pci0000:00/usb1/1-1/input/input7";
    keyboard.subsystem = "input";
    keyboard.sysname = "input7";
    keyboard.properties.insert("ID_INPUT_KEYBOARD", "1");
    classify(keyboard);
    check(keyboard.category == "keyboard" && !keyboard.hidden, "Composite keyboard function must remain visible");
    Device audio = keyboard;
    audio.subsystem = "sound";
    audio.sysname = "card1";
    classify(audio);
    check(audio.category == "audio" && !audio.hidden, "Composite audio function must remain independently visible");
    Device endpoint = keyboard;
    endpoint.sysname = "event3";
    classify(endpoint);
    check(endpoint.hidden, "Redundant input endpoint must be filtered");
    Device virtualNet;
    virtualNet.path = "/sys/devices/virtual/net/lo";
    virtualNet.subsystem = "net";
    classify(virtualNet);
    check(virtualNet.category == "network" && virtualNet.hidden, "Virtual device must retain its category");
    Device partition;
    partition.subsystem = "block";
    partition.devtype = "partition";
    classify(partition);
    check(partition.hidden, "Partitions must not masquerade as whole disks");
    Device pci;
    pci.subsystem = "pci";
    pci.properties.insert("PCI_CLASS", "0c0330");
    classify(pci);
    check(pci.category == "usb", "PCI USB class decoding");
    Device unknown;
    unknown.subsystem = "unknown_bus";
    unknown.driver = "example";
    unknown.sysname = "device0";
    classify(unknown);
    nameDevice(unknown);
    check(unknown.category == "other" && !unknown.hidden && unknown.name == QStringLiteral("example — device0"), "Unknown bound function needs a readable fallback");
    quint64 counter = 0;
    keyboard.incarnation = "first";
    QVector<Device> first {keyboard};
    reconcile(first, {}, counter);
    QVector<Device> next {keyboard};
    reconcile(next, first, counter);
    check(next[0].generation == first[0].generation, "Same instance must preserve selection identity");
    next[0].incarnation = "replacement";
    reconcile(next, first, counter);
    check(next[0].generation != first[0].generation, "Reused path must not retain old instance generation");
    next[0].properties.insert("ID_MODEL", "identical model");
    Device twin = next[0];
    twin.path += "/different";
    next.append(twin);
    reconcile(next, first, counter);
    check(next[0].generation != next[1].generation, "Equal model metadata must not merge different devices");
    const auto cpuModels = parseCpuModels(QStringLiteral(
        "processor : 1\nmodel name : Different CPU\n\n"
        "processor : 0\nmodel name : Intel(R) Core(TM) Ultra X7 358H\n\n"
        "processor : 2\nHardware : Board name\n"));
    check(cpuModels.value(0) == "Intel(R) Core(TM) Ultra X7 358H" && cpuModels.value(1) == "Different CPU",
          "CPU models must match processor numbers, not parse order or a global first model");
    check(!cpuModels.contains(2), "A global board name must not become a CPU model");
    Device cpu;
    cpu.subsystem = "cpu";
    cpu.sysname = "cpu0";
    Attribute model;
    model.state = ReadState::Available;
    model.value = cpuModels.value(0);
    cpu.attributes.insert("cpu_model", model);
    nameDevice(cpu);
    check(cpu.name == model.value && cpu.nameSource.contains("processor 0"), "CPU name must identify its metadata source");
    model.state = ReadState::PermissionDenied;
    cpu.attributes.insert("cpu_model", model);
    nameDevice(cpu);
    check(cpu.name == "cpu0", "Failed CPU metadata must not become an apparently resolved name");
    Device touchpad;
    touchpad.subsystem = "input";
    touchpad.sysname = "input14";
    touchpad.properties.insert("NAME", "\"SYNA3517:00 06CB:CFC6 Touchpad\"");
    touchpad.properties.insert("ID_MODEL", "Generic receiver");
    nameDevice(touchpad);
    check(touchpad.name == "SYNA3517:00 06CB:CFC6 Touchpad", "Input function name must take precedence over generic model names");

    // Paths/classes mirror the submitted HP laptop dump; no MAC/serial data.
    Device wifi;
    wifi.path = "/sys/devices/pci0000:00/0000:00:14.3";
    wifi.subsystem = "pci";
    wifi.properties.insert("PCI_CLASS", "28000");
    wifi.properties.insert("ID_MODEL_FROM_DATABASE", "Wi-Fi 7 BE211 320MHz");
    Device net;
    net.path = wifi.path + "/net/wlo1";
    net.parentPath = wifi.path;
    net.subsystem = "net";
    net.sysname = "wlo1";
    Device nvmePci;
    nvmePci.path = "/sys/devices/pci0000:00/0000:00:06.0/0000:01:00.0";
    nvmePci.subsystem = "pci";
    nvmePci.properties.insert("PCI_CLASS", "10802");
    Device nvme;
    nvme.path = nvmePci.path + "/nvme/nvme0";
    nvme.parentPath = nvmePci.path; // udev parent, not the intermediate nvme directory.
    nvme.subsystem = "nvme";
    Device disk;
    disk.path = nvme.path + "/nvme0n1";
    disk.parentPath = nvme.path;
    disk.subsystem = "block";
    disk.devtype = "disk";
    QVector<Device> laptop {wifi, net, nvmePci, nvme, disk};
    for (auto &d : laptop) classify(d);
    refinePresentation(laptop);
    check(!laptop[0].hidden && laptop[1].hidden && laptop[1].representedByPath == wifi.path,
          "One PCI Wi-Fi function and one interface must yield one default adapter row");
    check(!laptop[2].hidden && laptop[3].hidden && !laptop[4].hidden,
          "One NVMe PCI/controller pair must yield one controller without hiding the disk");
    check(laptop.size() == 5 && laptop[1].parentPath == wifi.path && laptop[1].driver.isEmpty(),
          "Grouping must retain every record, real ancestry and direct driver evidence");
    Device secondPort = net;
    secondPort.path = wifi.path + "/net/other0";
    QVector<Device> multiport {wifi, net, secondPort};
    for (auto &d : multiport) classify(d);
    refinePresentation(multiport);
    check(multiport[0].hidden && !multiport[1].hidden && !multiport[2].hidden,
          "Independent network functions must survive grouping");
    Device secondController = nvmePci;
    secondController.path = "/sys/devices/pci0000:00/0000:02:00.0";
    QVector<Device> twoControllers {nvmePci, nvme, secondController};
    for (auto &d : twoControllers) classify(d);
    refinePresentation(twoControllers);
    check(!twoControllers[0].hidden && !twoControllers[2].hidden,
          "A second real controller must never be merged by name or category");
    Device bridge = nvmePci;
    bridge.properties.insert("PCI_CLASS", "60400");
    QVector<Device> behindBridge {bridge, nvme};
    for (auto &d : behindBridge) classify(d);
    refinePresentation(behindBridge);
    check(!behindBridge[1].hidden, "A PCI bridge is not an NVMe controller duplicate");
    return failures ? 1 : 0;
}
