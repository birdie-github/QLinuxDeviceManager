#include "monitorstate.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    MonitorState events;
    events.observe("/sys/devices/usb/device", true);
    events.observe("/sys/devices/usb/device", false);
    check(events.changed.size() == 1 && events.removed.size() == 1,
          "Coalescing remove/add retains replacement evidence");
    events.beginScan();
    check(!events.dirty && events.changed.isEmpty() && events.removed.size() == 1,
          "Fresh scans clear changes but retain removals until publication");
    events.observe("/sys/devices/usb/device", false);
    Device ancestor; ancestor.path = "/sys/devices/usb";
    Device target; target.path = ancestor.path + "/device";
    Device child; child.path = target.path + "/interface"; child.parentPath = target.path;
    Device peer; peer.path = ancestor.path + "/other"; peer.parentPath = ancestor.path;
    Device independent; independent.path = "/sys/devices/pci/card";
    Inventory inventory; inventory.devices = {ancestor, target, child, peer, independent};
    events.excludeUnsettled(inventory);
    check(inventory.devices.size() == 2 && inventory.devices.at(0).path == peer.path
          && inventory.devices.at(1).path == independent.path,
          "Unsettled ancestors and descendants are withheld without losing unrelated siblings");
    check(inventory.skipped == 3, "Withheld records are counted");
    events.lose();
    inventory.devices = {independent};
    events.excludeUnsettled(inventory);
    check(inventory.devices.isEmpty() && events.lost, "Loss during collection withholds untrusted snapshot");
    events.beginScan();
    inventory.devices = {independent};
    events.excludeUnsettled(inventory);
    check(inventory.devices.size() == 1 && events.lost,
          "A fresh loss-recovery scan can publish while forcing new generations");
    events = {};
    for (int i = 0; i <= MonitorState::Limit; ++i)
        events.observe(QStringLiteral("/sys/devices/fixture/%1").arg(i), true);
    check(events.lost && events.changed.size() <= MonitorState::Limit
          && events.removed.size() <= MonitorState::Limit, "Bounded hints degrade to full reconciliation");
    events = {};
    events.observe("/sys/class/usb", true);
    check(!events.dirty, "Non-canonical hints do not enter inventory reconciliation");
    return failures ? 1 : 0;
}
