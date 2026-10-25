#include "enumerator.h"
#include "inventory.h"
#include "monitorstate.h"
#include <libudev.h>
#include <QElapsedTimer>
#include <memory>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <poll.h>
#include <utility>

namespace {
QString text(const char *value)
{
    return value ? QString::fromUtf8(value, static_cast<int>(strnlen(value, 4096))) : QString();
}
}
Inventory Enumerator::collect()
{
    const quint64 request = request_.load();
    Inventory result = collectInventory([this] { return isInterruptionRequested(); });
    result.request = request;
    return result;
}

void Enumerator::run()
{
    Q_ASSERT(isCurrentThread());
    using Monitor = std::unique_ptr<udev_monitor, decltype(&udev_monitor_unref)>;
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    Monitor monitor(nullptr, &udev_monitor_unref);
    MonitorState events;
    DeviceEventHistory history;
    bool historyGapPending = false;
    QElapsedTimer clock;
    clock.start();
    qint64 retryAt = 0;
    qint64 firstEvent = -1;
    qint64 lastEvent = 0;
    qint64 periodicAt = 30000;
    quint64 handledRequest = 0;
    bool initial = true;
    bool pending = true;
    bool awaitingGui = false;
    QString monitorNote;
    const auto failed = [&] {
        monitor.reset();
        history.lose();
        historyGapPending = true;
        events.lose();
        pending = true;
        retryAt = clock.elapsed() + 2000;
        monitorNote = tr("Live monitoring unavailable; retrying (F5 remains available)");
    };
    const auto openMonitor = [&] {
        if (!context) context.reset(udev_new());
        if (!context) { failed(); return; }
        monitor.reset(udev_monitor_new_from_netlink(context.get(), "udev"));
        if (!monitor) { failed(); return; }
        // Best effort, unprivileged. Failure to enlarge the socket is harmless;
        // ENOBUFS below triggers a fresh reconciliation and invalidates identities.
        udev_monitor_set_receive_buffer_size(monitor.get(), 1024 * 1024);
        if (udev_monitor_enable_receiving(monitor.get()) < 0
            || udev_monitor_get_fd(monitor.get()) < 0) { failed(); return; }
        monitorNote.clear();
        events.lose(); // A reopened monitor cannot vouch for instances during its gap.
        pending = true;
    };
    const auto drain = [&] {
        if (!monitor) return;
        // Bound each drain, even under a continuous stream. On overflow reopen
        // before scanning so an abandoned queue cannot masquerade as current state.
        for (int count = 0; count < MonitorState::Limit; ++count) {
            pollfd fd{udev_monitor_get_fd(monitor.get()), POLLIN, 0};
            const int ready = poll(&fd, 1, 0);
            if (ready < 0) { if (errno == EINTR) return; failed(); return; }
            if (fd.revents & (POLLERR | POLLHUP | POLLNVAL)) { failed(); return; }
            if (!ready || !(fd.revents & POLLIN)) return;
            errno = 0;
            std::unique_ptr<udev_device, decltype(&udev_device_unref)> event(
                udev_monitor_receive_device(monitor.get()), &udev_device_unref);
            if (!event) {
                if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
                failed();
                return;
            }
            const QString action = text(udev_device_get_action(event.get()));
            const QString path = text(udev_device_get_syspath(event.get()));
            DeviceEvent observation;
            observation.observed = QDateTime::currentDateTime();
            observation.elapsedMs = clock.elapsed();
            observation.sequence = udev_device_get_seqnum(event.get());
            observation.path = path;
            observation.action = action;
            observation.subsystem = text(udev_device_get_subsystem(event.get()));
            observation.devtype = text(udev_device_get_devtype(event.get()));
            observation.driver = text(udev_device_get_driver(event.get()));
            observation.initialized = text(udev_device_get_property_value(event.get(), "USEC_INITIALIZED"));
            // Do not read sysfs contents during event reception. An inode hint
            // is usable only while this path is still present; removals use the
            // already-observed instance, never a newly reused path's inode.
            struct stat identity {};
            if (action != "remove" && stat(path.toUtf8().constData(), &identity) == 0)
                observation.incarnation = QStringLiteral("%1:%2:%3")
                    .arg(static_cast<qulonglong>(identity.st_dev)).arg(static_cast<qulonglong>(identity.st_ino))
                    .arg(observation.initialized);
            const QString previous = text(udev_device_get_property_value(event.get(), "DEVPATH_OLD"));
            if (previous.startsWith("/devices/")) observation.oldPath = "/sys" + previous;
            history.observe(std::move(observation));
            events.observe(path, action == "remove");
            if (path.startsWith("/sys/devices/")) {
                lastEvent = clock.elapsed();
                if (firstEvent < 0) firstEvent = lastEvent;
            }
            const QString oldPath = text(udev_device_get_property_value(event.get(), "DEVPATH_OLD"));
            if (oldPath.startsWith("/devices/")) events.observe("/sys" + oldPath, true);
        }
        failed();
    };
    // Enable reception BEFORE the initial snapshot, and retain it across scans.
    openMonitor();
    while (!isInterruptionRequested()) {
        drain();
        const qint64 now = clock.elapsed();
        if (!monitor && now >= retryAt) openMonitor();
        if (events.dirty) {
            pending = true;
            if (firstEvent < 0) firstEvent = now;
            // Quiet-time deadline is updated only when drain() receives events.
        }
        if (awaitingGui && acknowledged_.load()) {
            awaitingGui = false;
        }
        const quint64 request = request_.load();
        const bool manual = request != handledRequest;
        const bool due = initial || manual || now >= periodicAt
            || (pending && (firstEvent < 0 || now - firstEvent >= 500 || now - lastEvent >= 100));
        if (!awaitingGui && due) {
            Inventory inventory;
            // At most two scans per publication. Continuous churn cannot keep
            // restarting a scan forever; uncertain dependency branches are withheld.
            for (int round = 0; round < 2; ++round) {
                events.beginScan();
                inventory = collect();
                drain();
                if (!events.dirty || isInterruptionRequested() || !inventory.error.isEmpty()) break;
                if (!monitor) openMonitor();
            }
            if (isInterruptionRequested()) break;
            events.excludeUnsettled(inventory);
            inventory.removedPaths = events.removed;
            inventory.identityLost = events.lost;
            inventory.monitorNote = monitorNote;
            // MonitorState can also detect bounded-hint loss independently of
            // socket failure. It must end event associations as well as GUI generations.
            if (inventory.identityLost && !initial && !historyGapPending) history.lose();
            history.reconcile(inventory.devices);
            inventory.events = history.snapshot(static_cast<bool>(monitor));
            if (events.dirty && inventory.monitorNote.isEmpty())
                inventory.monitorNote = tr("Changing device branches are being reconciled");
            handledRequest = inventory.request;
            pending = events.dirty;
            events.removed.clear();
            events.lost = false;
            historyGapPending = false;
            initial = false;
            firstEvent = pending ? clock.elapsed() : -1;
            lastEvent = clock.elapsed();
            periodicAt = clock.elapsed() + (monitor && inventory.error.isEmpty() ? 30000 : 2000);
            acknowledged_.store(false);
            awaitingGui = true;
            emit inventoryReady(inventory);
        }
        // Short interruptible polling also observes manual refresh and GUI ack.
        pollfd fd{monitor ? udev_monitor_get_fd(monitor.get()) : -1, POLLIN, 0};
        const int ready = poll(&fd, monitor ? 1 : 0, 50);
        if (ready > 0 && (fd.revents & POLLIN)) lastEvent = clock.elapsed();
        if (ready < 0 && errno != EINTR) failed();
    }
}
