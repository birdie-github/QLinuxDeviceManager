#include "deviceevents.h"
#include "device.h"
#include <QCoreApplication>
#include <cstdio>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const auto fixture = [](const QString &path, const QString &stamp) {
        Device device;
        device.path = path;
        device.incarnation = "inode:" + stamp;
        device.properties.insert("USEC_INITIALIZED", stamp);
        return device;
    };
    const auto observation = [](const Device &device, const QString &action, quint64 sequence) {
        DeviceEvent event;
        event.path = device.path;
        event.incarnation = action == "remove" ? QString() : device.incarnation;
        event.initialized = device.properties.value("USEC_INITIALIZED");
        event.action = action;
        event.sequence = sequence;
        event.observed = QDateTime::fromMSecsSinceEpoch(100000 + sequence);
        event.elapsedMs = sequence;
        return event;
    };
    DeviceEventHistory history(QDateTime::fromSecsSinceEpoch(100));
    Device parent = fixture("/sys/devices/usb/fixture", "old");
    Device child = fixture(parent.path + "/input", "old-child");
    QVector<Device> devices {parent, child};
    history.reconcile(devices);
    const quint64 old = devices.at(0).eventInstance;
    const quint64 oldChild = devices.at(1).eventInstance;
    check(old && oldChild && old != oldChild, "Distinct devices get distinct live-event identities");
    history.observe(observation(parent, "change", 1));
    history.observe(observation(parent, "remove", 2));
    history.observe(observation(child, "remove", 3));
    auto events = history.snapshot(true);
    check(events.records.at(0).instance == old && events.records.at(1).instance == old
          && events.records.at(2).instance == oldChild, "Removal events retain the observed parent/child instances");
    // Even identical inode/initialization hints cannot undo observed removal.
    history.observe(observation(parent, "add", 4));
    history.observe(observation(child, "add", 5));
    history.reconcile(devices);
    check(devices.at(0).eventInstance != old && devices.at(1).eventInstance != oldChild,
          "Remove/add at a reused path creates new event identities for descendants too");
    events = history.snapshot(true);
    check(events.records.at(3).instance == devices.at(0).eventInstance,
          "An observed add can be matched to the subsequent confirmed inventory");
    history.observe(observation(parent, "remove", 2));
    check(history.snapshot(true).records.back().instance == 0, "Reordered stale removal cannot retire a replacement");
    const quint64 replacement = devices.at(0).eventInstance;
    history.reconcile(devices);
    check(devices.at(0).eventInstance == replacement, "Stale event leaves current association intact");
    Device stale = fixture(parent.path, "stale-other-instance");
    history.observe(observation(stale, "change", 6));
    check(history.snapshot(true).records.back().instance == 0,
          "Conflicting initialization/inode evidence is not attached to current instance");
    history.lose();
    history.reconcile(devices);
    check(devices.at(0).eventInstance != replacement && history.snapshot(false).gaps == 1
          && !history.snapshot(false).monitoring, "Monitor loss retires old associations without deleting retained history");
    const quint64 stable = devices.at(0).eventInstance;
    history.reconcile(devices);
    check(devices.at(0).eventInstance == stable, "Ordinary snapshots neither rotate identity nor manufacture events");
    const int before = history.snapshot(true).records.size();
    DeviceEvent invalid = observation(parent, "change", 7);
    invalid.path = "/sys/class/usb";
    history.observe(invalid);
    check(history.snapshot(true).records.size() == before, "Noncanonical event paths are excluded");
    DeviceEventHistory bounded;
    QVector<Device> one {parent};
    bounded.reconcile(one);
    for (int i = 0; i < DeviceEventsSnapshot::Limit + 3; ++i)
        bounded.observe(observation(parent, "change", i + 1));
    events = bounded.snapshot(true);
    check(events.records.size() == DeviceEventsSnapshot::Limit && events.evicted == 3
          && events.records.front().sequence == 4, "Global history evicts oldest events at its explicit bound");
    DeviceEvent longValue = observation(parent, "change", 2000);
    longValue.driver = QString(10000, 'x');
    bounded.observe(longValue);
    check(bounded.snapshot(true).records.back().driver.size() == 4096, "Event payload copies are bounded");
    DeviceEventHistory pending;
    pending.observe(observation(parent, "add", 1));
    QVector<Device> unrelated {fixture(parent.path, "different")};
    pending.reconcile(unrelated);
    check(unrelated.front().eventInstance != pending.snapshot(true).records.front().instance,
          "A stale add payload cannot become authoritative inventory for a replacement");
    DeviceEventHistory capped;
    QVector<Device> many;
    many.reserve(DeviceEventHistory::IdentityLimit + 1);
    for (int i = 0; i <= DeviceEventHistory::IdentityLimit; ++i)
        many.append(fixture(QStringLiteral("/sys/devices/fixture/%1").arg(i), "stamp"));
    capped.reconcile(many);
    check(many.at(DeviceEventHistory::IdentityLimit - 1).eventInstance != 0 && many.back().eventInstance == 0,
          "Registry overflow leaves devices viewable with an explicit unassociated token");
    return failures ? 1 : 0;
}
