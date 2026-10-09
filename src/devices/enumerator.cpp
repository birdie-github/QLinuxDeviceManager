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
Inventory Enumerator::takeResult()
{
    Q_ASSERT(!isRunning());
    return std::move(result_);
}
void Enumerator::run()
{
    Inventory result;
    result.request = request_;
    const auto finish = [&] { result_ = std::move(result); };
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    if (!context) {
        result.error = tr("Could not create the udev context.");
        finish();
        return;
    }
    std::unique_ptr<udev_enumerate, decltype(&udev_enumerate_unref)> scan(
        udev_enumerate_new(context.get()), &udev_enumerate_unref);
    if (!scan || udev_enumerate_scan_devices(scan.get()) < 0) {
        result.error = tr("Could not enumerate kernel devices. The previous inventory has been retained.");
        finish();
        return;
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
    finish();
}
