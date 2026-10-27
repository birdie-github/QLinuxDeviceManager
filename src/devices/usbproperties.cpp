#include "usbproperties.h"
#include "directoryscan.h"
#include <QFileInfo>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
constexpr int textLimit = 4096;
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
}

UsbProperties collectUsbProperties(int fd, const Device &device, int openError)
{
    if (device.subsystem != "usb" || device.devtype != "usb_device") return {};
    UsbProperties usb;
    if (fd < 0) {
        const Attribute error = failure(openError ? openError : EBADF);
        for (const char *key : {"version", "speed", "bConfigurationValue", "bNumConfigurations",
                               "configuration", "bNumInterfaces", "bmAttributes", "bMaxPower"})
            usb.values.insert(QLatin1String(key), error);
        usb.interfaceState = error;
        return usb;
    }
    usb.values.insert("version", readText(fd, "version"));
    usb.values.insert("speed", readText(fd, "speed"));
    for (const char *key : {"bConfigurationValue", "bNumConfigurations", "configuration",
                           "bNumInterfaces", "bmAttributes", "bMaxPower"})
        usb.values.insert(QLatin1String(key), readText(fd, key));
    const Attribute configuration = usb.values.value("bConfigurationValue");
    const bool unconfigured = configuration.state == ReadState::Available
        && (configuration.value.isEmpty() || configuration.value == "0");
    if (unconfigured) {
        for (const char *key : {"configuration", "bNumInterfaces", "bmAttributes", "bMaxPower"})
            usb.values.insert(QLatin1String(key), {ReadState::NotApplicable, {}, 0});
        usb.interfaceState = {ReadState::NotApplicable, {}, 0};
    }
    // Only direct USB interface children, never descendant devices behind hubs.
    const DirectoryNames names = unconfigured ? DirectoryNames()
        : boundedDirectoryNames(device.path, 256, {device.sysname + ":*"});
    if (!unconfigured) usb.interfaceState = names.error ? failure(names.error) : available(QString());
    for (const QString &name : names.names) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            usb.interfaces.clear();
            usb.interfaceState = failure(ECANCELED);
            break;
        }
        UsbInterfaceProperties interface;
        interface.name = name;
        interface.path = device.path + '/' + name;
        const QByteArray encoded = name.toUtf8();
        Descriptor child(openat(fd, encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
        struct stat before {};
        const int readError = child.fd < 0 ? errno : (fstat(child.fd, &before) != 0 ? errno : 0);
        if (readError) {
            interface.values.insert("state", failure(readError));
        } else {
            const Attribute driver = linkTarget(child.fd, "driver");
            for (const char *key : {"bInterfaceNumber", "bAlternateSetting", "bInterfaceClass",
                                   "bInterfaceSubClass", "bInterfaceProtocol", "interface"})
                interface.values.insert(QLatin1String(key), readText(child.fd, key));
            Attribute binding = driver;
            if (binding.state == ReadState::Available) binding.value = QFileInfo(binding.value).fileName();
            else if (binding.state == ReadState::Unavailable && binding.error == ENOENT) binding = available(QString());
            interface.values.insert("driver", binding);
            const Attribute afterDriver = linkTarget(child.fd, "driver");
            const Attribute afterSetting = readText(child.fd, "bAlternateSetting");
            const Attribute setting = interface.values.value("bAlternateSetting");
            struct stat after {};
            if (fstatat(fd, encoded.constData(), &after, AT_SYMLINK_NOFOLLOW) != 0) {
                const int error = errno;
                interface.values.clear();
                interface.values.insert("state", failure(error));
            } else if (inodeIdentity(before) != inodeIdentity(after)
                || driver.state != afterDriver.state || driver.value != afterDriver.value || driver.error != afterDriver.error
                || setting.state != afterSetting.state || setting.value != afterSetting.value || setting.error != afterSetting.error) {
                interface.values.clear();
                interface.values.insert("state", failure(EAGAIN));
            }
        }
        usb.interfaces.append(interface);
    }
    // A listing failure must not become a successful empty/partial result.
    const Attribute count = usb.values.value("bNumInterfaces");
    bool validCount = false;
    const uint expected = count.value.toUInt(&validCount);
    if (!unconfigured && !names.error && names.names.isEmpty() && count.state != ReadState::Available)
        usb.interfaceState = count;
    if (!unconfigured && !names.error && count.state == ReadState::Available
        && (!validCount || expected > 255 || expected != static_cast<uint>(names.names.size()))) {
        usb.interfaces.clear();
        usb.interfaceState = failure(validCount && expected <= 255 ? EAGAIN : EINVAL);
    }
    // Configuration changes invalidate both its declarations and interface list.
    const Attribute afterConfiguration = readText(fd, "bConfigurationValue");
    if (configuration.state != afterConfiguration.state || configuration.value != afterConfiguration.value
        || configuration.error != afterConfiguration.error) {
        usb.interfaces.clear();
        const Attribute changed = failure(EAGAIN);
        for (auto it = usb.values.begin(); it != usb.values.end(); ++it) it.value() = changed;
        usb.interfaceState = changed;
    }
    return usb;
}
