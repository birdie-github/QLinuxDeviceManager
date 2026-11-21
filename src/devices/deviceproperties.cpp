#include "deviceproperties.h"
#include "efivalue.h"
#include "directoryscan.h"
#include "commonpropertyreader.h"
#include "propertyreadhelpers.h"
#include <QStringList>
#include <cerrno>
#include <fcntl.h>
#include <linux/pci_regs.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

using namespace PropertyRead;

namespace {
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
    if (d.subsystem == "efivarfs") {
        auto efi = readEfiSnapshot(d);
        if (!efi.instanceValidated) {
            result.state = efi.status.state;
            result.error = efi.status.error;
            return result;
        }
        result.state = ReadState::Available;
        result.efiBytes = std::move(efi.bytes);
        result.efiTruncated = efi.truncated;
        put(result, "efi/hex", efi.status, QStringLiteral("efivarfs: complete file bytes, no decoding"));
        return result;
    }
    collectCommonDeviceProperties(result, [&](int fd, const QString &devnode) {
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            pcieProperties(result, fd);
        if (!QThread::currentThread()->isInterruptionRequested()) result.resources = collectDeviceResources(fd, d);
        if (!QThread::currentThread()->isInterruptionRequested())
            result.storage = collectStorageProperties(d, cachedStorageService);
        if (!QThread::currentThread()->isInterruptionRequested()) networkProperties(result, fd, cachedStorageService);
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            result.usb = collectUsbProperties(fd, d);
        if (cachedStorageService && !QThread::currentThread()->isInterruptionRequested())
            batteryProperties(result, fd);
        if (cachedStorageService && d.subsystem == "video4linux" && d.sysname.startsWith("video")
            && !QThread::currentThread()->isInterruptionRequested())
            result.camera = collectCameraProperties(fd, d, devnode);
        // Desktop-session queries belong to explicit properties requests, not
        // deep search (which passes cachedStorageService=false).
        if (cachedStorageService && d.subsystem == "drm" && d.sysname.contains('-')
            && !QThread::currentThread()->isInterruptionRequested()) {
            result.monitor = collectMonitorProperties(fd, d.path);
            if (!QThread::currentThread()->isInterruptionRequested())
                result.display = collectDisplayProperties(d, readText(fd, "status"));
        }
        if (cachedStorageService && d.subsystem == "bluetooth" && bluetoothControllerName(d.sysname)
            && !QThread::currentThread()->isInterruptionRequested())
            result.bluetooth = collectBluetoothProperties(d);
        if (cachedStorageService && (d.category == "audio" || d.subsystem == "sound" || d.subsystem == "hdaudio")
            && !QThread::currentThread()->isInterruptionRequested())
            result.audio = collectAudioProperties(d);
    });
    if (result.state != ReadState::Available && result.bluetooth.applicable)
        result.bluetooth.invalidate({result.state, {}, result.error});
    if (result.state != ReadState::Available && result.audio.applicable) {
        result.audio.cards.clear();
        result.audio.codecs.clear();
        result.audio.codecStatus = {result.state, {}, result.error};
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
