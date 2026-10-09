#include "storageproperties.h"
#include "directoryscan.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <libudev.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <sys/stat.h>
#ifdef QLDM_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QEventLoop>
#include <QTimer>
#include <QVariant>
#include <sys/sysmacros.h>
using StorageInterfaces = QMap<QString, QVariantMap>;
using StorageObjects = QMap<QDBusObjectPath, StorageInterfaces>;
Q_DECLARE_METATYPE(StorageInterfaces)
Q_DECLARE_METATYPE(StorageObjects)
#endif

namespace {
constexpr int recordLimit = 4096;
constexpr int relatedLimit = 256;
constexpr int textLimit = 4096;
using Format = StorageField::Format;
Attribute ok(const QString &text) { return {ReadState::Available, text, 0}; }
Attribute failure(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENOENT || error == ENODATA ? ReadState::Unavailable
        : error == ENODEV ? ReadState::Removed : ReadState::Error, {}, error};
}
Attribute textFile(const QString &path, int limit = textLimit)
{
    QFile file(path);
    errno = 0;
    if (!file.open(QIODevice::ReadOnly)) return failure(errno ? errno : EIO);
    const QByteArray bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) return failure(EIO);
    if (bytes.size() > limit) return failure(EOVERFLOW);
    return ok(QString::fromUtf8(bytes).trimmed());
}
void field(QVector<StorageField> &fields, const QString &id, const char *label,
           const Attribute &value, const QString &source, Format format = Format::Text)
{
    fields.append({id, label, value, source, format});
}
void native(StorageEntity &entity, const QString &key, const char *label, Format format = Format::Text)
{
    field(entity.fields, key, label, textFile(entity.id + '/' + key), entity.id + '/' + key, format);
}
void metadata(StorageEntity &entity, udev_device *raw, const char *key, const char *label)
{
    const char *value = raw ? udev_device_get_property_value(raw, key) : nullptr;
    const Attribute data = !value ? Attribute() : strnlen(value, textLimit + 1) > textLimit
        ? failure(EOVERFLOW) : ok(QString::fromUtf8(value));
    field(entity.fields, QLatin1String(key), label, data,
          QStringLiteral("udev: %1 on %2").arg(QLatin1String(key), entity.id));
}
QString unescapeMount(QString value)
{
    // Decode once: replacing \\134 first would double-decode literal escapes.
    value.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
    return value;
}
void link(StorageProperties &result, const QString &from, const QString &to,
          const char *label, const QString &source)
{
    if (from.isEmpty() || to.isEmpty() || from == to || to == "/") return;
    for (const StorageLink &existing : result.links)
        if (existing.from == from && existing.to == to && existing.label == label) return;
    result.links.append({from, to, label, source});
}
#ifdef QLDM_HAVE_DBUS
QString objectPath(const QVariant &value)
{
    return value.value<QDBusObjectPath>().path();
}
StorageObjects serviceSnapshot(StorageProperties &result)
{
    static const int registered = [] {
        qDBusRegisterMetaType<StorageInterfaces>();
        qDBusRegisterMetaType<StorageObjects>();
        return 0;
    }();
    Q_UNUSED(registered);
    const auto bus = QDBusConnection::systemBus();
    const QString source = QStringLiteral("org.freedesktop.UDisks2: ObjectManager.GetManagedObjects");
    if (!bus.isConnected()) {
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), failure(ENOTCONN), source);
        return {};
    }
    auto message = QDBusMessage::createMethodCall("org.freedesktop.UDisks2", "/org/freedesktop/UDisks2",
                                                "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    message.setAutoStartService(false); // Read the existing cache; do not start a storage daemon.
    QDBusPendingCallWatcher watcher(bus.asyncCall(message, 2500));
    QEventLoop loop; // Runs only in the existing bounded property worker.
    QTimer cancellation;
    cancellation.setInterval(50);
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&loop] {
        if (QThread::currentThread()->isInterruptionRequested()) loop.quit();
    });
    cancellation.start();
    if (!watcher.isFinished()) loop.exec();
    if (QThread::currentThread()->isInterruptionRequested()) return {};
    QDBusPendingReply<StorageObjects> reply = watcher;
    if (reply.isError()) {
        const QDBusError error = reply.error();
        const ReadState state = error.type() == QDBusError::AccessDenied ? ReadState::PermissionDenied
            : error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::UnknownObject
                ? ReadState::Unavailable : ReadState::Error;
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"),
              {state, {}, error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout || error.type() == QDBusError::TimedOut ? ETIMEDOUT : EIO}, source);
        return {};
    }
    StorageObjects objects = reply.value();
    if (objects.size() > recordLimit) {
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), failure(EOVERFLOW), source);
        return {};
    }
    field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), ok("cached"), source);
    return objects;
}
void serviceField(StorageEntity &entity, const QVariantMap &properties, const QString &interface,
                  const char *key, const char *label, Format format = Format::Text, bool zeroUnknown = false)
{
    const QVariant value = properties.value(QLatin1String(key));
    Attribute data;
    if (value.isValid()) {
        QString text = format == Format::NvmeHealth ? value.toStringList().join('\n') : value.toString();
        if (format == Format::Boolean || format == Format::Removable || format == Format::AtaHealth) text = value.toBool() ? "1" : "0";
        if (text.size() > textLimit) data = failure(EOVERFLOW);
        else if (!(format == Format::Text && text.isEmpty()) && (!zeroUnknown || value.toDouble() != 0)) data = ok(text);
    }
    field(entity.fields, interface + '/' + QLatin1String(key), label, data,
          entity.id + " : " + interface + '.' + QLatin1String(key), format);
}
void addService(StorageProperties &result, const QSet<QString> &nativePaths)
{
    const StorageObjects objects = serviceSnapshot(result);
    if (objects.isEmpty()) return;
    const QString prefix = QStringLiteral("org.freedesktop.UDisks2.");
    QSet<QString> wanted;
    QMap<QString, QString> blocks;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariant value = it.value().value(prefix + "Block").value("DeviceNumber");
        if (!value.isValid()) continue;
        const dev_t number = static_cast<dev_t>(value.toULongLong());
        const QString path = QFileInfo(QStringLiteral("/sys/dev/block/%1:%2").arg(major(number)).arg(minor(number))).canonicalFilePath();
        if (nativePaths.contains(path)) {
            wanted.insert(it.key().path());
            blocks.insert(it.key().path(), path);
        }
    }
    // No guessed model/serial matching; each drive object is retained once.
    for (int pass = 0; pass < relatedLimit; ++pass) {
        const int before = wanted.size();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (!wanted.contains(it.key().path())) continue;
            for (const auto &pair : {qMakePair(QStringLiteral("Block"), QStringLiteral("Drive")),
                                    qMakePair(QStringLiteral("Block"), QStringLiteral("CryptoBackingDevice")),
                                    qMakePair(QStringLiteral("Block"), QStringLiteral("MDRaid")),
                                    qMakePair(QStringLiteral("Block"), QStringLiteral("MDRaidMember")),
                                    qMakePair(QStringLiteral("Partition"), QStringLiteral("Table")),
                                    qMakePair(QStringLiteral("Block.LVM2"), QStringLiteral("LogicalVolume")),
                                    qMakePair(QStringLiteral("LogicalVolume"), QStringLiteral("VolumeGroup"))}) {
                const QString target = objectPath(it.value().value(prefix + pair.first).value(pair.second));
                if (!target.isEmpty() && target != "/" && objects.contains(QDBusObjectPath(target))) wanted.insert(target);
            }
        }
        if (wanted.size() > relatedLimit) {
            field(result.notes, "service-limit", QT_TRANSLATE_NOOP("Storage", "Related storage metadata"), failure(EOVERFLOW), "UDisks2");
            return;
        }
        if (wanted.size() == before) break;
    }
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (!wanted.contains(it.key().path())) continue;
        StorageEntity entity;
        entity.id = it.key().path();
        if (blocks.contains(entity.id)) link(result, blocks.value(entity.id), entity.id,
            QT_TRANSLATE_NOOP("Storage", "UDisks2 block metadata"), "UDisks2 Block.DeviceNumber / sysfs dev");
        const auto &interfaces = it.value();
        const auto copy = [&](const char *iface, const char *key, const char *label,
                              Format format = Format::Text, bool zeroUnknown = false) {
            serviceField(entity, interfaces.value(prefix + QLatin1String(iface)), prefix + QLatin1String(iface), key, label, format, zeroUnknown);
        };
        if (interfaces.contains(prefix + "Drive")) {
            copy("Drive", "Vendor", QT_TRANSLATE_NOOP("Storage", "Drive manufacturer"));
            copy("Drive", "Model", QT_TRANSLATE_NOOP("Storage", "Drive model"));
            copy("Drive", "Serial", QT_TRANSLATE_NOOP("Storage", "Drive serial number"));
            copy("Drive", "Revision", QT_TRANSLATE_NOOP("Storage", "Drive firmware revision"));
            copy("Drive", "ConnectionBus", QT_TRANSLATE_NOOP("Storage", "Drive transport"));
            copy("Drive", "Size", QT_TRANSLATE_NOOP("Storage", "Drive capacity"), Format::Bytes, true);
            copy("Drive", "Removable", QT_TRANSLATE_NOOP("Storage", "Removable/fixed hint"), Format::Removable);
            copy("Drive", "MediaRemovable", QT_TRANSLATE_NOOP("Storage", "Drive media removable"), Format::Boolean);
            copy("Drive", "MediaAvailable", QT_TRANSLATE_NOOP("Storage", "Drive media available"), Format::Boolean);
        }
        if (interfaces.contains(prefix + "Block")) {
            copy("Block", "IdUsage", QT_TRANSLATE_NOOP("Storage", "Content usage"));
            copy("Block", "IdType", QT_TRANSLATE_NOOP("Storage", "Filesystem / content type"));
            copy("Block", "IdVersion", QT_TRANSLATE_NOOP("Storage", "Filesystem / content version"));
            copy("Block", "IdLabel", QT_TRANSLATE_NOOP("Storage", "Filesystem / content label"));
            copy("Block", "IdUUID", QT_TRANSLATE_NOOP("Storage", "Filesystem / content UUID"));
        }
        if (interfaces.contains(prefix + "Partition")) {
            copy("Partition", "Number", QT_TRANSLATE_NOOP("Storage", "Partition number"));
            copy("Partition", "Type", QT_TRANSLATE_NOOP("Storage", "Partition type"));
            copy("Partition", "Offset", QT_TRANSLATE_NOOP("Storage", "Partition offset"), Format::Bytes);
            copy("Partition", "Size", QT_TRANSLATE_NOOP("Storage", "Partition capacity"), Format::Bytes);
            copy("Partition", "Name", QT_TRANSLATE_NOOP("Storage", "Partition name"));
            copy("Partition", "UUID", QT_TRANSLATE_NOOP("Storage", "Partition UUID"));
        }
        if (interfaces.contains(prefix + "PartitionTable")) copy("PartitionTable", "Type", QT_TRANSLATE_NOOP("Storage", "Partition table type"));
        if (interfaces.contains(prefix + "MDRaid")) {
            copy("MDRaid", "Name", QT_TRANSLATE_NOOP("Storage", "RAID name"));
            copy("MDRaid", "Level", QT_TRANSLATE_NOOP("Storage", "RAID level"));
            copy("MDRaid", "UUID", QT_TRANSLATE_NOOP("Storage", "RAID UUID"));
            copy("MDRaid", "NumDevices", QT_TRANSLATE_NOOP("Storage", "RAID member count"));
        }
        if (interfaces.contains(prefix + "LogicalVolume")) {
            copy("LogicalVolume", "Name", QT_TRANSLATE_NOOP("Storage", "Logical volume name"));
            copy("LogicalVolume", "Type", QT_TRANSLATE_NOOP("Storage", "Logical volume type"));
        }
        if (interfaces.contains(prefix + "VolumeGroup")) copy("VolumeGroup", "Name", QT_TRANSLATE_NOOP("Storage", "Volume group name"));
        const bool ata = interfaces.contains(prefix + "Drive.Ata");
        const bool nvme = interfaces.contains(prefix + "NVMe.Controller");
        if (ata || nvme) {
            const QString iface = ata ? "Drive.Ata" : "NVMe.Controller";
            const QVariantMap health = interfaces.value(prefix + iface);
            if (ata) {
                copy("Drive.Ata", "SmartSupported", QT_TRANSLATE_NOOP("Storage", "ATA SMART supported"), Format::Boolean);
                copy("Drive.Ata", "SmartEnabled", QT_TRANSLATE_NOOP("Storage", "ATA SMART enabled"), Format::Boolean);
            }
            serviceField(entity, health, prefix + iface, "SmartUpdated", QT_TRANSLATE_NOOP("Storage", "Cached health updated"), Format::Epoch, true);
            // Missing/zero timestamps invalidate every health assessment field.
            if (health.value("SmartUpdated").toULongLong() != 0) {
                serviceField(entity, health, prefix + iface, "SmartTemperature", QT_TRANSLATE_NOOP("Storage", "Cached temperature"), Format::Kelvin, true);
                if (ata) {
                    copy("Drive.Ata", "SmartFailing", QT_TRANSLATE_NOOP("Storage", "Cached ATA failure prediction"), Format::AtaHealth);
                    for (const auto &pair : {qMakePair("SmartNumBadSectors", QT_TRANSLATE_NOOP("Storage", "Cached bad sectors")),
                                             qMakePair("SmartNumAttributesFailing", QT_TRANSLATE_NOOP("Storage", "Cached failing attributes"))}) {
                        if (health.value(pair.first, -1).toLongLong() >= 0) copy("Drive.Ata", pair.first, pair.second);
                    }
                } else copy("NVMe.Controller", "SmartCriticalWarning", QT_TRANSLATE_NOOP("Storage", "Cached NVMe critical warnings"), Format::NvmeHealth);
            } else {
                field(entity.fields, "health", QT_TRANSLATE_NOOP("Storage", "Cached health"), {}, entity.id + " : " + prefix + iface + ".SmartUpdated (never updated or unavailable)");
            }
        } else if (interfaces.contains(prefix + "Drive")) {
            field(entity.fields, "health", QT_TRANSLATE_NOOP("Storage", "Cached health"), {}, entity.id + " : UDisks2 (no supported health interface)");
        }
        for (auto iface = interfaces.cbegin(); iface != interfaces.cend(); ++iface) {
            for (const char *key : {"Drive", "CryptoBackingDevice", "MDRaid", "MDRaidMember", "Table", "LogicalVolume", "VolumeGroup"}) {
                const QString target = objectPath(iface.value().value(QLatin1String(key)));
                if (wanted.contains(target)) link(result, entity.id, target, QT_TRANSLATE_NOOP("Storage", "Storage relationship"), iface.key() + '.' + QLatin1String(key));
            }
        }
        result.entities.append(std::move(entity));
    }
}
#endif
}

StorageProperties collectStorageProperties(const Device &device, bool cachedService)
{
    StorageProperties result;
    result.applicable = device.subsystem == "block" || device.subsystem == "nvme";
    if (!result.applicable) return result;
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    const QDir directory("/sys/class/block");
    const DirectoryNames scanned = boundedDirectoryNames(directory.path(), recordLimit);
    if (scanned.error) {
        field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage inventory"), failure(scanned.error), "/sys/class/block");
        return result;
    }
    const QStringList &names = scanned.names;
    QMap<QString, QString> paths;
    for (const QString &name : names) {
        const QString path = QFileInfo(directory.filePath(name)).canonicalFilePath();
        if (path.startsWith("/sys/devices/")) paths.insert(name, path);
    }
    const QList<QString> pathValues = paths.values();
    const QSet<QString> allPaths(pathValues.cbegin(), pathValues.cend());
    QSet<QString> wanted;
    if (device.subsystem == "block") wanted.insert(device.path);
    else for (const QString &path : paths)
        if (path.startsWith(device.path + '/')) wanted.insert(path);
    QVector<StorageLink> nativeLinks;
    for (const QString &path : paths) {
        if (QThread::currentThread()->isInterruptionRequested()) return result;
        if (QFileInfo::exists(path + "/partition"))
            nativeLinks.append({path, QFileInfo(path).dir().canonicalPath(), QT_TRANSLATE_NOOP("Storage", "Partition of"), path + "/partition (kernel ancestry)"});
        for (const auto &pair : {qMakePair("slaves", QT_TRANSLATE_NOOP("Storage", "Backed by")), qMakePair("holders", QT_TRANSLATE_NOOP("Storage", "Used by"))}) {
            const QDir links(path + '/' + QLatin1String(pair.first));
            const DirectoryNames related = boundedDirectoryNames(links.path(), recordLimit);
            if (related.error) {
                field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage relationships"), failure(related.error), links.path());
                return result;
            }
            for (const QString &name : related.names) {
                const QString target = QFileInfo(links.filePath(name)).canonicalFilePath();
                if (allPaths.contains(target)) nativeLinks.append({path, target, pair.second, links.filePath(name)});
                if (nativeLinks.size() > recordLimit * 4) {
                    field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage relationships"), failure(EOVERFLOW), "/sys/class/block");
                    return result;
                }
            }
        }
    }
    for (int pass = 0; pass < relatedLimit; ++pass) {
        const int before = wanted.size();
        for (const StorageLink &edge : nativeLinks)
            if (wanted.contains(edge.from) || wanted.contains(edge.to)) { wanted.insert(edge.from); wanted.insert(edge.to); }
        if (wanted.size() > relatedLimit) {
            field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Related storage metadata"), failure(EOVERFLOW), "/sys/class/block");
            return result;
        }
        if (wanted.size() == before) break;
    }
    QMap<QString, QString> identities;
    const auto identity = [](const QString &path) {
        struct stat info {};
        if (lstat(path.toUtf8().constData(), &info) != 0) return QString();
        return QStringLiteral("%1:%2").arg(static_cast<qulonglong>(info.st_dev)).arg(static_cast<qulonglong>(info.st_ino));
    };
    for (const QString &path : wanted) identities.insert(path, identity(path));
    const Attribute mounts = textFile("/proc/self/mountinfo", 4 * 1024 * 1024);
    field(result.notes, "mountinfo", QT_TRANSLATE_NOOP("Storage", "Mount metadata"),
          mounts.state == ReadState::Available ? ok("application-namespace") : mounts, "/proc/self/mountinfo");
    QMap<QString, QStringList> mountPoints;
    if (mounts.state == ReadState::Available) {
        for (const QString &line : mounts.value.split('\n')) {
            const QStringList parts = line.split(' ');
            const int separator = parts.indexOf("-");
            if (parts.size() < 6 || separator < 6 || parts.size() <= separator + 2) continue;
            mountPoints[parts[2]].append(unescapeMount(parts[4]) + " [" + parts[separator + 1] + "; root=" + unescapeMount(parts[3]) + ']');
        }
    }
    // Sorting is deterministic and each canonical block path appears once.
    QStringList selected = wanted.values();
    selected.sort();
    for (const QString &path : selected) {
        if (QThread::currentThread()->isInterruptionRequested()) return result;
        StorageEntity entity;
        entity.id = path;
        const QByteArray encoded = path.toUtf8();
        std::unique_ptr<udev_device, decltype(&udev_device_unref)> raw(
            context ? udev_device_new_from_syspath(context.get(), encoded.constData()) : nullptr, &udev_device_unref);
        metadata(entity, raw.get(), "DEVNAME", QT_TRANSLATE_NOOP("Storage", "Device node"));
        const Attribute number = textFile(path + "/dev");
        field(entity.fields, "dev", QT_TRANSLATE_NOOP("Storage", "Major:minor"), number, path + "/dev");
        Attribute capacity = textFile(path + "/size");
        if (capacity.state == ReadState::Available) {
            bool valid = false;
            const quint64 sectors = capacity.value.toULongLong(&valid);
            capacity = !valid || sectors > std::numeric_limits<quint64>::max() / 512
                ? failure(EOVERFLOW) : ok(QString::number(sectors * 512));
        }
        field(entity.fields, "capacity", QT_TRANSLATE_NOOP("Storage", "Block capacity"), capacity, path + "/size (512-byte units)", Format::Bytes);
        const bool partition = QFileInfo::exists(path + "/partition");
        const QString queueOwner = partition ? QFileInfo(path).dir().canonicalPath() : path;
        for (const auto &pair : {qMakePair("logical_block_size", QT_TRANSLATE_NOOP("Storage", "Logical sector size")),
                                 qMakePair("physical_block_size", QT_TRANSLATE_NOOP("Storage", "Physical sector size"))}) {
            const QString source = queueOwner + "/queue/" + pair.first;
            field(entity.fields, QStringLiteral("queue/") + pair.first, pair.second, textFile(source), source, Format::Bytes);
        }
        native(entity, "removable", QT_TRANSLATE_NOOP("Storage", "Kernel removable media"), Format::Boolean);
        native(entity, "ro", QT_TRANSLATE_NOOP("Storage", "Block read-only"), Format::Boolean);
        if (QFileInfo::exists(path + "/partition")) {
            native(entity, "partition", QT_TRANSLATE_NOOP("Storage", "Partition number"));
            native(entity, "start", QT_TRANSLATE_NOOP("Storage", "Partition start (512-byte sectors)"));
        }
        for (const auto &pair : {qMakePair("ID_MODEL", QT_TRANSLATE_NOOP("Storage", "Disk model")), qMakePair("ID_VENDOR", QT_TRANSLATE_NOOP("Storage", "Manufacturer")),
                                 qMakePair("ID_SERIAL_SHORT", QT_TRANSLATE_NOOP("Storage", "Serial number")), qMakePair("ID_REVISION", QT_TRANSLATE_NOOP("Storage", "Firmware revision")),
                                 qMakePair("ID_BUS", QT_TRANSLATE_NOOP("Storage", "Transport")), qMakePair("ID_FS_TYPE", QT_TRANSLATE_NOOP("Storage", "Filesystem / content type")),
                                 qMakePair("ID_FS_USAGE", QT_TRANSLATE_NOOP("Storage", "Content usage")), qMakePair("ID_FS_LABEL", QT_TRANSLATE_NOOP("Storage", "Filesystem / content label")),
                                 qMakePair("ID_FS_UUID", QT_TRANSLATE_NOOP("Storage", "Filesystem / content UUID")), qMakePair("ID_PART_TABLE_TYPE", QT_TRANSLATE_NOOP("Storage", "Partition table type"))})
            metadata(entity, raw.get(), pair.first, pair.second);
        if (QFileInfo::exists(path + "/dm")) {
            native(entity, "dm/name", QT_TRANSLATE_NOOP("Storage", "Device-mapper name"));
            native(entity, "dm/uuid", QT_TRANSLATE_NOOP("Storage", "Device-mapper UUID"));
        }
        if (QFileInfo::exists(path + "/md")) {
            native(entity, "md/level", QT_TRANSLATE_NOOP("Storage", "RAID level"));
            native(entity, "md/array_state", QT_TRANSLATE_NOOP("Storage", "RAID state"));
            native(entity, "md/degraded", QT_TRANSLATE_NOOP("Storage", "RAID missing members"));
        }
        // Read only selected SCSI/NVMe identity metadata, with parent source kept explicit.
        const QString hardware = QFileInfo(path + "/device").canonicalFilePath();
        if (hardware.startsWith("/sys/devices/")) {
            const QString subsystem = QFileInfo(hardware + "/subsystem").canonicalFilePath();
            if (QFileInfo(subsystem).fileName() == "nvme")
                field(entity.fields, "native-transport", QT_TRANSLATE_NOOP("Storage", "Native transport"), ok("NVMe"), hardware + "/subsystem");
            else if (raw && udev_device_get_property_value(raw.get(), "ID_ATA_SATA")
                     && QByteArray(udev_device_get_property_value(raw.get(), "ID_ATA_SATA")) == "1")
                field(entity.fields, "native-transport", QT_TRANSLATE_NOOP("Storage", "Native transport"), ok("SATA"), "udev: ID_ATA_SATA on " + path);
            for (const auto &pair : {qMakePair("model", QT_TRANSLATE_NOOP("Storage", "Hardware model")), qMakePair("vendor", QT_TRANSLATE_NOOP("Storage", "Hardware vendor")),
                                     qMakePair("rev", QT_TRANSLATE_NOOP("Storage", "Hardware revision")), qMakePair("serial", QT_TRANSLATE_NOOP("Storage", "Hardware serial")),
                                     qMakePair("firmware_rev", QT_TRANSLATE_NOOP("Storage", "Hardware firmware revision"))})
                field(entity.fields, QStringLiteral("hardware/") + pair.first, pair.second,
                      textFile(hardware + '/' + pair.first), hardware + '/' + pair.first);
        }
        const QStringList points = mountPoints.value(number.value);
        field(entity.fields, "mounts", QT_TRANSLATE_NOOP("Storage", "Mount points (application namespace)"),
              mounts.state != ReadState::Available ? mounts : number.state != ReadState::Available ? number
                  : !points.isEmpty() ? (points.join('\n').size() > textLimit ? failure(EOVERFLOW) : ok(points.join('\n')))
                  : ok("no-observed-mount"), "/proc/self/mountinfo : " + number.value);
        result.entities.append(std::move(entity));
    }
    for (const StorageLink &edge : nativeLinks)
        if (wanted.contains(edge.from) && wanted.contains(edge.to)) link(result, edge.from, edge.to, edge.label, edge.source);
    if (device.subsystem == "nvme") {
        StorageEntity controller;
        controller.id = device.path;
        native(controller, "model", QT_TRANSLATE_NOOP("Storage", "Controller model"));
        native(controller, "serial", QT_TRANSLATE_NOOP("Storage", "Controller serial number"));
        native(controller, "firmware_rev", QT_TRANSLATE_NOOP("Storage", "Controller firmware revision"));
        result.entities.prepend(controller);
        for (const QString &path : selected)
            if (path.startsWith(device.path + '/'))
                link(result, device.path, path, QT_TRANSLATE_NOOP("Storage", "Controller namespace / partition"), "sysfs ancestry");
    }
#ifdef QLDM_HAVE_DBUS
    if (cachedService && !QThread::currentThread()->isInterruptionRequested()) addService(result, wanted);
#else
    Q_UNUSED(cachedService);
    field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), {ReadState::Unsupported, {}, 0}, "QtDBus not enabled in this build");
#endif
    // Related paths and device numbers can also be reused. Reject the whole
    // storage snapshot if a native entity changed while the service was queried.
    for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
        if (it.value().isEmpty() || identity(it.key()) != it.value()) {
            result.entities.clear();
            result.links.clear();
            result.notes.clear();
            field(result.notes, "changed", QT_TRANSLATE_NOOP("Storage", "Storage topology changed during collection"),
                  failure(EAGAIN), it.key());
            break;
        }
    }
    return result;
}
