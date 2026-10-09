// Fixture-only checks. No live hardware is touched and no GUI is launched.
#include "device.h"
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
    check(unknown.category == "other" && !unknown.hidden && unknown.name == "device0", "Unknown bound function needs a readable fallback");
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
    return failures ? 1 : 0;
}
