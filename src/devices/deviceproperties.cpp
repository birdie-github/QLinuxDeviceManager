#include "deviceproperties.h"
#include "directoryscan.h"
#include "acpivendors.h"
#include <libudev.h>
#ifdef QLDM_HAVE_KMOD
#include <libkmod.h>
#endif
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/pci_regs.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
constexpr int textLimit = 4096;
constexpr int efiLimit = 65536;
struct Descriptor {
    int fd = -1;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) close(fd); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};
Attribute failure(int error)
{
    Attribute a;
    a.error = error;
    a.state = error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENODEV ? ReadState::Removed
        : error == ENOENT || error == ENODATA ? ReadState::Unavailable : ReadState::Error;
    return a;
}
Attribute available(const QString &value) { return {ReadState::Available, value, 0}; }
Attribute boundedText(const char *value)
{
    if (!value) return {};
    const size_t length = strnlen(value, textLimit + 1);
    return length > textLimit ? failure(EOVERFLOW)
        : available(QString::fromUtf8(value, static_cast<int>(length)));
}
QByteArray readBytes(int fd, int limit, int &error)
{
    QByteArray bytes;
    char buffer[4096];
    error = 0;
    while (bytes.size() <= limit && !QThread::currentThread()->isInterruptionRequested()) {
        const int wanted = qMin(static_cast<int>(sizeof(buffer)), limit + 1 - static_cast<int>(bytes.size()));
        const ssize_t count = read(fd, buffer, wanted);
        if (count < 0) {
            if (errno == EINTR) continue;
            error = errno;
            return {};
        }
        if (count == 0) break;
        bytes.append(buffer, static_cast<int>(count));
    }
    if (QThread::currentThread()->isInterruptionRequested()) { error = ECANCELED; return {}; }
    return bytes;
}
Attribute readText(int directory, const char *name)
{
    Descriptor file(openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (file.fd < 0) return failure(errno);
    int error = 0;
    const QByteArray bytes = readBytes(file.fd, textLimit, error);
    if (error) return failure(error);
    if (bytes.size() > textLimit) return failure(EOVERFLOW);
    return available(QString::fromUtf8(bytes).trimmed());
}
Attribute linkTarget(int directory, const char *name)
{
    char buffer[textLimit + 1];
    const ssize_t size = readlinkat(directory, name, buffer, sizeof(buffer));
    if (size < 0) return failure(errno);
    if (size > textLimit) return failure(EOVERFLOW);
    return available(QString::fromUtf8(buffer, static_cast<int>(size)));
}
QString inodeIdentity(const struct stat &info)
{
    return QStringLiteral("%1:%2").arg(static_cast<qulonglong>(info.st_dev))
        .arg(static_cast<qulonglong>(info.st_ino));
}
void put(DeviceProperties &result, const QString &id, const Attribute &value, const QString &source)
{
    result.values.insert(id, value);
    result.sources.insert(id, source);
}
void sysfs(DeviceProperties &result, int fd, const char *key)
{
    const QString name = QLatin1String(key);
    put(result, "sysfs/" + name, readText(fd, key), result.device.path + '/' + name);
}
Attribute pcieDeviceType(int directory, QString &source)
{
    // Ordinary sysfs link reads remain usable without administrative privileges.
    // Never escalate or attempt a configuration-space read for a normal user.
    if (geteuid() != 0) return failure(EACCES);
    Descriptor config(openat(directory, "config", O_RDONLY | O_CLOEXEC | O_NOFOLLOW));
    if (config.fd < 0) return failure(errno);
    const auto readValue = [&](int offset, int size, unsigned int &value) -> Attribute {
        if (QThread::currentThread()->isInterruptionRequested()) return failure(ECANCELED);
        unsigned char bytes[2] {};
        const ssize_t count = pread(config.fd, bytes, size, offset);
        if (count < 0) return failure(errno);
        if (count != size) return failure(EMSGSIZE);
        value = bytes[0] | (size == 2 ? static_cast<unsigned int>(bytes[1]) << 8 : 0);
        return available(QString());
    };
    unsigned int status = 0, header = 0, pointer = 0;
    Attribute state = readValue(PCI_STATUS, 2, status);
    if (state.state != ReadState::Available) return state;
    if (status == 0xffff) return failure(ENODEV);
    if (!(status & PCI_STATUS_CAP_LIST)) return failure(ENODATA);
    state = readValue(PCI_HEADER_TYPE, 1, header);
    if (state.state != ReadState::Available) return state;
    header &= PCI_HEADER_TYPE_MASK;
    if (header != PCI_HEADER_TYPE_NORMAL && header != PCI_HEADER_TYPE_BRIDGE
        && header != PCI_HEADER_TYPE_CARDBUS) return failure(EPROTO);
    state = readValue(header == PCI_HEADER_TYPE_CARDBUS ? PCI_CB_CAPABILITY_LIST : PCI_CAPABILITY_LIST, 1, pointer);
    if (state.state != ReadState::Available) return state;
    // Follow only standard capability headers and the two-byte Express flags.
    // At most 48 aligned entries in bytes 0x40..0xff; no extended-space scan,
    // capability payload dump, BAR access, or write to the device.
    bool visited[256] {};
    for (int entries = 0; pointer && entries < 48; ++entries) {
        if (pointer < 0x40 || pointer > 0xfc || (pointer & 3) || visited[pointer])
            return failure(EPROTO);
        visited[pointer] = true;
        unsigned int capability = 0;
        state = readValue(static_cast<int>(pointer), 2, capability);
        if (state.state != ReadState::Available) return state;
        if ((capability & 0xff) == PCI_CAP_ID_EXP) {
            unsigned int flags = 0;
            state = readValue(static_cast<int>(pointer) + PCI_EXP_FLAGS, 2, flags);
            if (state.state != ReadState::Available) return state;
            if (flags == 0xffff || !(flags & PCI_EXP_FLAGS_VERS)) return failure(EPROTO);
            source += QStringLiteral("; PCI Express flags at offset 0x%1").arg(pointer + PCI_EXP_FLAGS, 2, 16, QLatin1Char('0'));
            return available(QString::number((flags & PCI_EXP_FLAGS_TYPE) >> 4));
        }
        pointer = capability >> 8;
    }
    return failure(pointer ? EPROTO : ENODATA);
}
void pcieProperties(DeviceProperties &result, int fd)
{
    if (result.device.subsystem != "pci") return;
    QHash<QString, Attribute> values;
    bool applicable = false;
    for (const char *key : {"current_link_speed", "current_link_width", "max_link_speed", "max_link_width"}) {
        if (QThread::currentThread()->isInterruptionRequested()) return;
        Attribute value = readText(fd, key);
        // Absent files alone do not establish PCIe applicability. Preserve actual
        // read failures so a present but unreadable PCIe device still has a tab.
        if (value.state == ReadState::Available || value.error != ENOENT)
            applicable = true;
        if (value.error == EOPNOTSUPP || value.error == ENOSYS)
            value.state = ReadState::Unsupported;
        values.insert(QLatin1String(key), value);
    }
    if (!applicable) return;
    result.pcieApplicable = true;
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        put(result, "sysfs/" + it.key(), it.value(), result.device.path + '/' + it.key());
    result.pcieTypeSource = result.device.path + "/config";
    result.pcieTypeRequiresRoot = geteuid() != 0;
    result.pcieType = pcieDeviceType(fd, result.pcieTypeSource);
}
void batteryProperties(DeviceProperties &result, int fd)
{
    if (result.device.subsystem != "power_supply") return;
    Attribute type = result.values.value("sysfs/type");
    if (type.error == EOPNOTSUPP || type.error == ENOSYS) type.state = ReadState::Unsupported;
    const Attribute previousType = result.device.attributes.value("type");
    // Inventory evidence controls only whether a failed snapshot gets a tab.
    // It must not replace an unreadable current type or supply battery values.
    if (type.state == ReadState::Available && type.value != "Battery") return;
    if (type.state != ReadState::Available
        && (previousType.state != ReadState::Available || previousType.value != "Battery")) return;
    BatteryProperties &battery = result.battery;
    battery.applicable = true;
    battery.values.insert("type", type);
    if (type.state != ReadState::Available) { battery.state = type; return; }
    battery.state = available(QString());
    for (const char *key : {"manufacturer", "model_name", "serial_number", "present"})
        battery.values.insert(QLatin1String(key), result.values.value(QStringLiteral("sysfs/") + QLatin1String(key)));
    const Attribute present = battery.values.value("present");
    if (present.state == ReadState::Available && present.value != "0" && present.value != "1") {
        battery.state = failure(EINVAL);
        return;
    }
    const bool absent = present.state == ReadState::Available && present.value == "0";
    for (const char *key : {"status", "technology", "cycle_count", "capacity", "voltage_now",
                           "energy_full_design", "energy_full", "energy_now",
                           "charge_full_design", "charge_full", "charge_now", "current_now", "power_now"}) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            battery.state = failure(ECANCELED);
            battery.values.clear();
            return;
        }
        Attribute value = absent ? Attribute {ReadState::NotApplicable, {}, 0} : readText(fd, key);
        if (value.error == EOPNOTSUPP || value.error == ENOSYS) value.state = ReadState::Unsupported;
        battery.values.insert(QLatin1String(key), value);
    }
    // A power_supply node can remain while a removable battery disappears.
    // Reject changed presence/type evidence rather than presenting mixed values.
    const auto same = [](const Attribute &a, const Attribute &b) {
        return a.state == b.state && a.value == b.value && a.error == b.error;
    };
    const Attribute afterType = readText(fd, "type");
    const Attribute afterPresent = readText(fd, "present");
    if (!same(type, afterType) || !same(present, afterPresent)) {
        battery.values.clear();
        if (afterType.state != ReadState::Available) battery.state = afterType;
        else if (!same(present, afterPresent) && afterPresent.state != ReadState::Available) battery.state = afterPresent;
        else battery.state = failure(EAGAIN);
        if (battery.state.error == EOPNOTSUPP || battery.state.error == ENOSYS)
            battery.state.state = ReadState::Unsupported;
    }
}

NetworkProperties networkInterface(int fd, const QString &path, const QString &name, bool ethernetDetails)
{
    const Attribute index = readText(fd, "ifindex");
    const Attribute type = readText(fd, "type");
    bool valid = false;
    const unsigned int number = index.value.toUInt(&valid);
    NetworkProperties result;
    if (index.state == ReadState::Available && valid && number)
        result = collectNetworkProperties(name, number, type.state == ReadState::Available && type.value == "1");
    else result.addressState = result.permanentAddress = index.state == ReadState::Available ? failure(EINVAL) : index;
    result.name = name;
    result.path = path;
    result.linkType = type;
    if (type.state != ReadState::Available) result.permanentAddress = type;
    result.currentAddress = readText(fd, "address");
    result.assignmentType = readText(fd, "addr_assign_type");
    result.mtu = readText(fd, "mtu");
    if (ethernetDetails && type.state == ReadState::Available && type.value == "1") {
        // ARPHRD_ETHER describes MAC framing and includes Wi-Fi. Check both
        // kernel wireless markers before issuing Ethernet-only ethtool queries.
        bool wireless = false;
        int scopeError = 0;
        for (const char *marker : {"phy80211", "wireless"}) {
            struct stat entry {};
            if (fstatat(fd, marker, &entry, AT_SYMLINK_NOFOLLOW) == 0) wireless = true;
            else if (errno != ENOENT) scopeError = errno;
        }
        if (wireless && index.state == ReadState::Available && valid && number
            && !QThread::currentThread()->isInterruptionRequested()) {
            result.wireless = collectWirelessProperties(name, number, path);
        } else if (!wireless && scopeError) {
            result.ethernet.scope = failure(scopeError);
            result.ethernet.source = path + "/phy80211; " + path + "/wireless";
        } else if (!wireless && index.state == ReadState::Available && valid && number
            && !QThread::currentThread()->isInterruptionRequested()) {
            result.ethernet = collectEthernetProperties(name, number);
        }
    }
    const Attribute afterIndex = readText(fd, "ifindex");
    const bool indexChanged = index.state == ReadState::Available
        && (afterIndex.state != ReadState::Available || afterIndex.value != index.value);
    if (indexChanged || result.addressState.state == ReadState::Removed || result.permanentAddress.state == ReadState::Removed
        || result.ethernet.scope.state == ReadState::Removed) {
        result.addresses.clear();
        const Attribute error = indexChanged
            ? (afterIndex.state != ReadState::Available ? afterIndex : failure(EAGAIN)) : failure(ENODEV);
        result.addressState = result.permanentAddress = result.currentAddress
            = result.assignmentType = result.mtu = error;
        if (result.ethernet.applicable || result.ethernet.scope.state != ReadState::NotApplicable)
            result.ethernet.invalidate(error);
        if (result.wireless.applicable) result.wireless.invalidate(error);
    }
    return result;
}
void networkProperties(DeviceProperties &result, int fd, bool ethernetDetails)
{
    const Device &device = result.device;
    if (device.subsystem == "net") {
        result.network.append(networkInterface(fd, device.path, device.sysname, ethernetDetails));
        result.networkState = available(QString());
        return;
    }
    if (device.category != "network") return;
    // A single net interface is represented by its PCI function in the device
    // tree. Read only that function's direct net directory, never ancestor ports.
    Descriptor directory(openat(fd, "net", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (directory.fd < 0) { result.networkState = failure(errno); return; }
    const QString path = device.path + "/net";
    const DirectoryNames entries = boundedDirectoryNames(path, 256);
    if (entries.error) { result.networkState = failure(entries.error); return; }
    result.networkState = available(QString());
    for (const QString &name : entries.names) {
        if (QThread::currentThread()->isInterruptionRequested()) break;
        const QByteArray encoded = name.toUtf8();
        Descriptor interface(openat(directory.fd, encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        if (interface.fd < 0) {
            const Attribute error = failure(errno);
            NetworkProperties unavailable;
            unavailable.name = name;
            unavailable.path = path + '/' + name;
            unavailable.addressState = unavailable.permanentAddress = unavailable.currentAddress
                = unavailable.assignmentType = unavailable.linkType = unavailable.mtu = error;
            result.network.append(unavailable);
        } else result.network.append(networkInterface(interface.fd, path + '/' + name, name, ethernetDetails));
    }
}
void moduleMetadata(DeviceProperties &result, int driverFd)
{
    Attribute owner = linkTarget(driverFd, "module");
    // ENOENT is absent evidence, not evidence of a built-in driver.
    if (owner.state == ReadState::Available) owner.value = QFileInfo(owner.value).fileName();
    put(result, "module/name", owner, result.values.value("driver/path").value + "/module");
    const QStringList fields {"filename", "version", "description", "author", "license", "firmware"};
    for (const QString &key : fields)
        put(result, "module/" + key, {}, QStringLiteral("installed module metadata"));
    put(result, "module/type", {}, QStringLiteral("kernel module initstate"));
    put(result, "module/runtime_version", {}, QStringLiteral("kernel module version"));
    if (owner.state != ReadState::Available || owner.value.isEmpty()) return;
    Descriptor moduleFd(openat(driverFd, "module", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (moduleFd.fd >= 0) {
        put(result, "module/runtime_version", readText(moduleFd.fd, "version"),
            QStringLiteral("/sys/module/%1/version").arg(owner.value));
        const Attribute state = readText(moduleFd.fd, "initstate");
        put(result, "module/type", state.state == ReadState::Available ? Attribute() : state,
            QStringLiteral("/sys/module/%1/initstate").arg(owner.value));
        if (state.state == ReadState::Available
            && (state.value == "live" || state.value == "coming" || state.value == "going"))
            put(result, "module/type", available("modular"), QStringLiteral("/sys/module/%1/initstate").arg(owner.value));
    } else {
        const Attribute error = failure(errno);
        put(result, "module/runtime_version", error, QStringLiteral("kernel module version"));
        put(result, "module/type", error, QStringLiteral("kernel module initstate"));
    }
#ifdef QLDM_HAVE_KMOD
    const char *const noConfiguration[] = {nullptr};
    std::unique_ptr<kmod_ctx, decltype(&kmod_unref)> context(kmod_new(nullptr, noConfiguration), &kmod_unref);
    kmod_module *raw = nullptr;
    const QByteArray name = owner.value.toUtf8();
    const int status = context ? kmod_module_new_from_name_lookup(context.get(), name.constData(), &raw) : -ENOMEM;
    std::unique_ptr<kmod_module, decltype(&kmod_module_unref)> module(raw, &kmod_module_unref);
    if (status < 0 || !module) {
        if (status < 0)
            for (const QString &key : fields) result.values["module/" + key] = failure(-status);
        return;
    }
    const Attribute filename = boundedText(kmod_module_get_path(module.get()));
    put(result, "module/filename", filename, QStringLiteral("libkmod: running-kernel installed module index"));
    kmod_list *info = nullptr;
    const int count = kmod_module_get_info(module.get(), &info);
    if (count < 0) {
        for (const QString &key : fields)
            if (key != "filename") result.values["module/" + key] = failure(-count);
    } else {
        kmod_list *entry = nullptr;
        int examined = 0;
        kmod_list_foreach(entry, info) {
            if (QThread::currentThread()->isInterruptionRequested()) break;
            if (++examined > 4096) {
                for (const QString &key : fields)
                    if (key != "filename") result.values["module/" + key] = failure(EOVERFLOW);
                break;
            }
            const Attribute keyText = boundedText(kmod_module_info_get_key(entry));
            const QString key = keyText.value;
            if (keyText.state != ReadState::Available || key == "filename" || !fields.contains(key)) continue;
            Attribute value = boundedText(kmod_module_info_get_value(entry));
            const Attribute previous = result.values.value("module/" + key);
            if (previous.state == ReadState::Error) continue;
            if (value.state == ReadState::Available && previous.state == ReadState::Available) {
                value.value = previous.value + '\n' + value.value;
                if (value.value.toUtf8().size() > textLimit) value = failure(EOVERFLOW);
            }
            put(result, "module/" + key, value,
                filename.state == ReadState::Available ? filename.value + ": .modinfo/" + key
                                                       : QStringLiteral("libkmod: installed metadata/%1").arg(key));
        }
    }
    kmod_module_info_free_list(info);
#else
    for (const QString &key : fields) result.values["module/" + key].state = ReadState::Unsupported;
#endif
}
}

void PropertiesReader::setRequest(const Device &device, quint64 request)
{
    Q_ASSERT(!isRunning());
    device_ = device;
    request_ = request;
}
DeviceProperties PropertiesReader::takeResult()
{
    Q_ASSERT(!isRunning());
    return std::move(result_);
}
DeviceProperties collectDeviceProperties(const Device &device, bool cachedStorageService)
{
    DeviceProperties result;
    result.device = device;
    const Device &d = result.device;
    const bool efi = d.subsystem == "efivarfs";
    const QByteArray path = d.path.toUtf8();
    if (efi) {
        struct stat entry {};
        if (lstat(path.constData(), &entry) != 0) {
            result.error = errno;
            result.state = errno == ENOENT || errno == ENODEV ? ReadState::Removed : failure(errno).state;
            return result;
        }
        if (inodeIdentity(entry) != d.incarnation) { result.state = ReadState::Removed; return result; }
        if (!S_ISREG(entry.st_mode) || !(entry.st_mode & S_IROTH)) {
            result.state = ReadState::PermissionDenied;
            result.error = EACCES;
            return result;
        }
    }
    Descriptor deviceFd(open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | (efi ? 0 : O_DIRECTORY)));
    if (deviceFd.fd < 0) {
        result.error = errno;
        result.state = errno == ENOENT || errno == ENODEV ? ReadState::Removed : failure(errno).state;
        return result;
    }
    struct stat before {};
    if (fstat(deviceFd.fd, &before) != 0) {
        result.error = errno; result.state = ReadState::Error; return result;
    }
    const QString identity = inodeIdentity(before);
    const bool sameIdentity = efi ? identity == d.incarnation : d.incarnation.startsWith(identity + ':');
    if (!sameIdentity) { result.state = ReadState::Removed; return result; }
    result.state = ReadState::Available;
    if (efi) {
        if (!S_ISREG(before.st_mode) || !(before.st_mode & S_IROTH)) {
            result.state = ReadState::PermissionDenied;
            result.error = EACCES;
            return result;
        }
        int error = 0;
        result.efiBytes = readBytes(deviceFd.fd, efiLimit, error);
        if (error) put(result, "efi/hex", failure(error), QStringLiteral("efivarfs: complete file bytes"));
        else {
            result.efiTruncated = result.efiBytes.size() > efiLimit;
            result.efiBytes.truncate(efiLimit);
            put(result, "efi/hex", available(QString()), QStringLiteral("efivarfs: complete file bytes, no decoding"));
        }
    } else {
        std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> raw(
            context ? udev_device_new_from_syspath(context.get(), path.constData()) : nullptr, &udev_device_unref);
        if (!raw) {
            result.state = ReadState::Error;
            result.error = context ? EIO : ENOMEM;
            return result;
        }
        const QString stamp = boundedText(udev_device_get_property_value(raw.get(), "USEC_INITIALIZED")).value;
        if (identity + ':' + stamp != d.incarnation) { result.state = ReadState::Removed; return result; }
        // Selected udev metadata only; no unbounded property dump.
        for (const char *key : {"ID_VENDOR_FROM_DATABASE", "ID_VENDOR", "ID_MODEL_FROM_DATABASE", "ID_MODEL",
                "ID_BUS", "ID_PATH", "ID_PATH_TAG", "ID_SERIAL", "ID_SERIAL_SHORT", "ID_REVISION",
                "ID_VENDOR_ID", "ID_MODEL_ID", "PCI_ID", "PCI_SUBSYS_ID", "PCI_CLASS", "PCI_SLOT_NAME",
                "PRODUCT", "PHYS", "MODALIAS", "DEVNAME", "DEVTYPE", "DRIVER", "ID_NET_DRIVER",
                "ID_USB_DRIVER", "ID_FIRMWARE_FROM_DATABASE"}) {
            const Attribute value = boundedText(udev_device_get_property_value(raw.get(), key));
            if (value.state != ReadState::Unavailable)
                put(result, QStringLiteral("udev/") + QLatin1String(key), value, QStringLiteral("udev: %1 on %2").arg(QLatin1String(key), d.path));
        }
        const Attribute alias = result.values.value("udev/MODALIAS");
        if (alias.state == ReadState::Available) {
            const AcpiVendor vendor = acpiVendor(d.subsystem, alias.value);
            if (!vendor.name.isEmpty())
                put(result, "acpi/vendor", {ReadState::Available, vendor.name, 0}, vendor.source);
        }
        const Attribute driverLink = linkTarget(deviceFd.fd, "driver");
        Attribute driverPath = driverLink;
        if (driverPath.state == ReadState::Available)
            driverPath.value = QDir::cleanPath(driverPath.value.startsWith('/') ? driverPath.value : d.path + '/' + driverPath.value);
        put(result, "driver/path", driverPath, d.path + "/driver");
        Attribute binding = driverLink;
        if (binding.state == ReadState::Available) binding.value = QFileInfo(binding.value).fileName();
        else if (binding.state == ReadState::Unavailable && binding.error == ENOENT) binding = available(QString());
        put(result, "driver/name", binding, d.path + "/driver");
        if (driverLink.state == ReadState::Available) {
            // Follow this one documented kernel link, never a guessed parent driver.
            Descriptor driverFd(openat(deviceFd.fd, "driver", O_RDONLY | O_DIRECTORY | O_CLOEXEC));
            if (driverFd.fd >= 0) {
                const Attribute subsystem = linkTarget(deviceFd.fd, "subsystem");
                put(result, "driver/bus", subsystem.state == ReadState::Available
                    ? available(QFileInfo(subsystem.value).fileName()) : subsystem, d.path + "/subsystem");
                moduleMetadata(result, driverFd.fd);
            } else put(result, "module/name", failure(errno), driverPath.value + "/module");
        }
        QStringList attributes;
        if (d.subsystem == "pci") attributes = {"vendor", "device", "subsystem_vendor", "subsystem_device", "class", "revision", "modalias"};
        else if (d.subsystem == "usb") attributes = {"idVendor", "idProduct", "bcdDevice", "manufacturer", "product", "serial", "authorized", "busnum", "devnum", "version", "speed", "modalias"};
        else if (d.subsystem == "input") attributes = {"name", "phys", "uniq", "modalias"};
        else if (d.subsystem == "nvme") attributes = {"model", "serial", "firmware_rev", "state"};
        else if (d.subsystem == "power_supply") attributes = {"type", "manufacturer", "model_name", "serial_number", "present"};
        else if (d.subsystem == "net") attributes = {"address", "addr_assign_type", "type", "mtu", "ifindex", "operstate", "flags"};
        else if (d.subsystem == "drm" && d.sysname.contains('-')) attributes = {"status", "enabled"};
        else if (d.subsystem == "hdaudio") attributes = {"vendor_name", "chip_name", "vendor_id", "subsystem_id", "revision_id"};
        else if (d.subsystem == "acpi") attributes = {"hid", "uid", "path", "modalias"};
        else if (d.subsystem == "pnp" || d.subsystem == "platform" || d.subsystem == "hid") attributes = {"modalias"};
        else if (d.subsystem == "video4linux") attributes = {"name", "index"};
        else if (d.subsystem == "sound" && d.sysname.startsWith("card")) attributes = {"id"};
        for (const QString &key : attributes) {
            if (QThread::currentThread()->isInterruptionRequested()) break;
            const QByteArray name = key.toLatin1();
            sysfs(result, deviceFd.fd, name.constData());
        }
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            pcieProperties(result, deviceFd.fd);
        if (!QThread::currentThread()->isInterruptionRequested()) result.resources = collectDeviceResources(deviceFd.fd, d);
        if (!QThread::currentThread()->isInterruptionRequested())
            result.storage = collectStorageProperties(d, cachedStorageService);
        if (!QThread::currentThread()->isInterruptionRequested()) networkProperties(result, deviceFd.fd, cachedStorageService);
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            result.usb = collectUsbProperties(deviceFd.fd, d);
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            batteryProperties(result, deviceFd.fd);
        if (cachedStorageService && d.subsystem == "video4linux" && d.sysname.startsWith("video")
            && !QThread::currentThread()->isInterruptionRequested())
            result.camera = collectCameraProperties(deviceFd.fd, d, boundedText(udev_device_get_devnode(raw.get())).value);
        // Desktop-session queries belong to explicit properties requests, not
        // deep search (which passes cachedStorageService=false).
        if (cachedStorageService && d.subsystem == "drm" && d.sysname.contains('-')
            && !QThread::currentThread()->isInterruptionRequested()) {
            result.monitor = collectMonitorProperties(deviceFd.fd, d.path);
            if (!QThread::currentThread()->isInterruptionRequested())
                result.display = collectDisplayProperties(d, readText(deviceFd.fd, "status"));
        }
        if (cachedStorageService && d.subsystem == "bluetooth" && bluetoothControllerName(d.sysname)
            && !QThread::currentThread()->isInterruptionRequested())
            result.bluetooth = collectBluetoothProperties(d);
        if (cachedStorageService && (d.category == "audio" || d.subsystem == "sound")
            && !QThread::currentThread()->isInterruptionRequested())
            result.audio = collectAudioProperties(d);
        // Binding can change during a read even if the device instance remains.
        const Attribute afterLink = linkTarget(deviceFd.fd, "driver");
        if (afterLink.state != driverLink.state || afterLink.value != driverLink.value || afterLink.error != driverLink.error) {
            result.state = ReadState::Error;
            result.error = EAGAIN;
        }
    }
    struct stat after {};
    if (lstat(path.constData(), &after) != 0) {
        result.error = errno;
        result.state = errno == ENOENT || errno == ENODEV ? ReadState::Removed : failure(errno).state;
    } else if (inodeIdentity(after) != identity) result.state = ReadState::Removed;
    else if (efi && !(after.st_mode & S_IROTH)) {
        result.state = ReadState::PermissionDenied;
        result.error = EACCES;
        result.efiBytes.clear();
    }
    if (QThread::currentThread()->isInterruptionRequested()) { result.state = ReadState::Error; result.error = ECANCELED; }
    if (result.state != ReadState::Available && result.bluetooth.applicable)
        result.bluetooth.invalidate({result.state, {}, result.error});
    if (result.state != ReadState::Available && result.audio.applicable) {
        result.audio.cards.clear();
        result.audio.status = {result.state, {}, result.error};
    }
    return result;
}

void PropertiesReader::run()
{
    Q_ASSERT(isCurrentThread());
    result_ = collectDeviceProperties(device_);
    result_.request = request_;
}
