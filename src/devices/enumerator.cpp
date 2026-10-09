#include "enumerator.h"
#include "cpumetadata.h"
#include "devicepresentation.h"
#include "modulenames.h"
#include "efivariables.h"

#include <libudev.h>
#include <QFileInfo>
#include <memory>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <utility>
#include <poll.h>
#include <QElapsedTimer>
#include "monitorstate.h"

namespace {
QString text(const char *value)
{
    // All values are treated as plain text. Bound the owned copy.
    return value ? QString::fromUtf8(value, static_cast<int>(strnlen(value, 4096))) : QString();
}
Attribute readAttribute(const QString &path)
{
    Attribute result;
    const QByteArray encoded = path.toUtf8();
    const int fd = open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        result.error = errno;
        result.state = errno == EACCES || errno == EPERM ? ReadState::PermissionDenied
            : errno == ENOENT ? ReadState::Unavailable : ReadState::Error;
        return result;
    }
    char buffer[4097];
    ssize_t count;
    do { count = read(fd, buffer, sizeof(buffer)); } while (count < 0 && errno == EINTR);
    const int readError = errno;
    close(fd);
    if (count < 0) {
        result.error = readError;
        result.state = readError == ENODEV ? ReadState::Removed
            : readError == EACCES || readError == EPERM ? ReadState::PermissionDenied : ReadState::Error;
    } else if (count > 4096) {
        result.error = EOVERFLOW;
        result.state = ReadState::Error;
    } else {
        result.state = ReadState::Available;
        result.value = QString::fromUtf8(buffer, static_cast<int>(count)).trimmed();
    }
    return result;
}
}
Inventory Enumerator::collect()
{
    Inventory result;
    result.request = request_.load();
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    if (!context) {
        result.error = tr("Could not create the udev context.");
        return result;
    }
    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> scan(
        udev_enumerate_new(context.get()), &udev_enumerate_unref);
    if (!scan || udev_enumerate_scan_devices(scan.get()) < 0) {
        result.error = tr("Could not enumerate kernel devices.");
        return result;
    }
    // hwdb is an optional local naming database, still accessed through libudev.
    std::unique_ptr<udev_hwdb, decltype(&udev_hwdb_unref)> hwdb(udev_hwdb_new(context.get()), &udev_hwdb_unref);
    ModuleNames moduleNames; // Worker-owned context and per-refresh lookup cache.
    Attribute cpuInfo;
    QHash<int, QString> cpuModels;
    bool cpuInfoRead = false;
    udev_list_entry *entry = nullptr;
    udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(scan.get())) {
        if (isInterruptionRequested()) break;
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> raw(
            udev_device_new_from_syspath(context.get(), udev_list_entry_get_name(entry)), &udev_device_unref);
        if (!raw) { ++result.skipped; continue; }
        Device d;
        d.path = text(udev_device_get_syspath(raw.get()));
        if (!d.path.startsWith("/sys/devices/")) { ++result.skipped; continue; }
        d.sysname = text(udev_device_get_sysname(raw.get()));
        d.subsystem = text(udev_device_get_subsystem(raw.get()));
        d.devtype = text(udev_device_get_devtype(raw.get()));
        d.driver = text(udev_device_get_driver(raw.get()));
        if (!d.driver.isEmpty()) {
            const QString target = QFileInfo(d.path + "/driver/module").symLinkTarget();
            if (target.startsWith("/sys/module/")) d.driverModule = QFileInfo(target).fileName();
        }
        if (auto *parent = udev_device_get_parent(raw.get()))
            d.parentPath = text(udev_device_get_syspath(parent));
        // Explicit allowlist: no recursive sysfs reads, binary resources or hardware queries.
        for (const char *key : {"ID_MODEL_FROM_DATABASE", "ID_MODEL", "NAME", "ID_V4L_PRODUCT", "MODALIAS", "PCI_CLASS",
                              "PCI_ID", "ID_VENDOR_FROM_DATABASE", "ID_VENDOR", "ID_BUS",
                              "ID_INPUT_KEYBOARD", "ID_INPUT_MOUSE", "ID_INPUT_TOUCHPAD",
                              "ID_INPUT_POINTINGSTICK", "ID_INPUT_SWITCH", "PRODUCT", "PHYS", "DEVNUM",
                              "USEC_INITIALIZED"}) {
            const char *value = udev_device_get_property_value(raw.get(), key);
            if (value) d.properties.insert(QLatin1String(key), text(value));
        }
        if (hwdb && (d.subsystem == "pci" || d.subsystem == "usb")
            && d.properties.value("ID_MODEL_FROM_DATABASE").isEmpty()) {
            const QByteArray alias = d.properties.value("MODALIAS").toUtf8();
            if (!alias.isEmpty()) {
                udev_list_entry *property = nullptr;
                udev_list_entry_foreach(property, udev_hwdb_get_properties_list_entry(hwdb.get(), alias.constData(), 0)) {
                    const QString key = text(udev_list_entry_get_name(property));
                    if ((key == "ID_MODEL_FROM_DATABASE" || key == "ID_VENDOR_FROM_DATABASE")
                        && d.properties.value(key).isEmpty()) {
                        d.properties.insert(key, text(udev_list_entry_get_value(property)));
                        d.propertySources.insert(key, QStringLiteral("hwdb: %1 (%2)").arg(key, d.properties.value("MODALIAS")));
                    }
                }
            }
        }
        if (d.subsystem == "cpu") {
            if (!cpuInfoRead) {
                cpuInfo = readCpuInfo();
                if (cpuInfo.state == ReadState::Available) cpuModels = parseCpuModels(cpuInfo.value);
                cpuInfoRead = true;
                cpuInfo.value.clear(); // Keep only the selected per-processor model fields.
            }
            Attribute model;
            model.state = cpuInfo.state;
            model.error = cpuInfo.error;
            bool valid = false;
            const int number = d.sysname.startsWith("cpu") ? d.sysname.mid(3).toInt(&valid) : -1;
            if (valid && cpuModels.contains(number)) model.value = cpuModels.value(number);
            else if (model.state == ReadState::Available) model.state = ReadState::Unavailable;
            d.attributes.insert("cpu_model", model);
        }
        struct stat info {};
        const QByteArray path = d.path.toUtf8();
        if (stat(path.constData(), &info) != 0) { ++result.skipped; continue; }
        d.incarnation = QStringLiteral("%1:%2:%3")
            .arg(static_cast<qulonglong>(info.st_dev)).arg(static_cast<qulonglong>(info.st_ino))
            .arg(d.properties.value("USEC_INITIALIZED"));
        // Only documented text metadata on this device, never on a guessed parent.
        QString attribute;
        if (d.subsystem == "input" || d.subsystem == "video4linux") attribute = "name";
        else if (d.subsystem == "sound" && d.sysname.startsWith("card")) attribute = "id";
        else if (d.subsystem == "power_supply") attribute = "model_name";
        else if (d.subsystem == "nvme") attribute = "model";
        else if (d.subsystem == "usb" && d.devtype == "usb_device") attribute = "product";
        if (d.subsystem == "hdaudio") {
            d.attributes.insert("vendor_name", readAttribute(d.path + "/vendor_name"));
            d.attributes.insert("chip_name", readAttribute(d.path + "/chip_name"));
        }
        if (d.subsystem == "power_supply") {
            d.attributes.insert("type", readAttribute(d.path + "/type"));
            d.attributes.insert("manufacturer", readAttribute(d.path + "/manufacturer"));
        }
        if (!attribute.isEmpty()) d.attributes.insert(attribute, readAttribute(d.path + '/' + attribute));
        nameDevice(d);
        moduleNames.supplement(d);
        struct stat after {};
        if (stat(path.constData(), &after) != 0 || after.st_dev != info.st_dev || after.st_ino != info.st_ino) {
            ++result.skipped;
            continue;
        }
        classify(d);
        result.devices.append(std::move(d));
    }
    if (!isInterruptionRequested()) refinePresentation(result.devices);
    if (!isInterruptionRequested()) appendEfiVariables(result);
    return result;
}

void Enumerator::run()
{
    using Monitor = std::unique_ptr<udev_monitor, decltype(&udev_monitor_unref)>;
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    Monitor monitor(nullptr, &udev_monitor_unref);
    MonitorState events;
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
            if (events.dirty && inventory.monitorNote.isEmpty())
                inventory.monitorNote = tr("Changing device branches are being reconciled");
            handledRequest = inventory.request;
            pending = events.dirty;
            events.removed.clear();
            events.lost = false;
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
