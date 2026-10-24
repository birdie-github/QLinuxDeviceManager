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
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <memory>
#include <utility>
#include <sys/stat.h>
#ifdef QLDM_HAVE_DBUS
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QEventLoop>
#include <QElapsedTimer>
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
#ifdef QLDM_HAVE_DBUS
constexpr int healthQueryBudgetMs = 2500;
#endif
using Format = StorageField::Format;

Attribute ok(const QString &text)
{
    return {ReadState::Available, text, 0};
}

Attribute failure(int error)
{
    ReadState state = ReadState::Error;
    if (error == EACCES || error == EPERM)
        state = ReadState::PermissionDenied;
    else if (error == ENOENT || error == ENODATA)
        state = ReadState::Unavailable;
    else if (error == ENODEV)
        state = ReadState::Removed;
    return {state, {}, error};
}

Attribute textFile(const QString &path, int limit = textLimit)
{
    QFile file(path);
    errno = 0;
    if (!file.open(QIODevice::ReadOnly))
        return failure(errno ? errno : EIO);
    const QByteArray bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError)
        return failure(EIO);
    if (bytes.size() > limit)
        return failure(EOVERFLOW);
    return ok(QString::fromUtf8(bytes).trimmed());
}

void field(QVector<StorageField> &fields, const QString &id, const char *label, const Attribute &value,
           const QString &source, Format format = Format::Text)
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
    const Attribute data = !value                                      ? Attribute()
                           : strnlen(value, textLimit + 1) > textLimit ? failure(EOVERFLOW)
                                                                       : ok(QString::fromUtf8(value));
    field(entity.fields, QLatin1String(key), label, data,
          QStringLiteral("udev: %1 on %2").arg(QLatin1String(key), entity.id));
}

QString unescapeMount(QString value)
{
    // Decode once: replacing \\134 first would double-decode literal escapes.
    value.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
    return value;
}

void link(StorageProperties &result, const QString &from, const QString &to, const char *label,
          const QString &source)
{
    if (from.isEmpty() || to.isEmpty() || from == to || to == "/")
        return;
    for (const StorageLink &existing : result.links)
        if (existing.from == from && existing.to == to && existing.label == label)
            return;
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
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), failure(ENOTCONN),
              source);
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
        if (QThread::currentThread()->isInterruptionRequested())
            loop.quit();
    });
    cancellation.start();
    if (!watcher.isFinished())
        loop.exec();
    if (QThread::currentThread()->isInterruptionRequested())
        return {};
    QDBusPendingReply<StorageObjects> reply = watcher;
    if (reply.isError()) {
        const QDBusError error = reply.error();
        const ReadState state =
            error.type() == QDBusError::AccessDenied ? ReadState::PermissionDenied
            : error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::UnknownObject
                ? ReadState::Unavailable
                : ReadState::Error;
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"),
              {state,
               {},
               error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout ||
                       error.type() == QDBusError::TimedOut
                   ? ETIMEDOUT
                   : EIO},
              source);
        return {};
    }
    StorageObjects objects = reply.value();
    if (objects.size() > recordLimit) {
        field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"), failure(EOVERFLOW),
              source);
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
        if (format == Format::Boolean || format == Format::Removable || format == Format::AtaHealth)
            text = value.toBool() ? "1" : "0";
        if (text.size() > textLimit)
            data = failure(EOVERFLOW);
        else if (!(format == Format::Text && text.isEmpty()) && (!zeroUnknown || value.toDouble() != 0))
            data = ok(text);
    }
    field(entity.fields, interface + '/' + QLatin1String(key), label, data,
          entity.id + " : " + interface + '.' + QLatin1String(key), format);
}

// Shared cache-method wait budget; no service activation or authorization dialog.
QDBusMessage healthReply(const QString &path, const QString &iface, QElapsedTimer &budget, Attribute &state)
{
    const auto fail = [&](const Attribute &error) {
        state = error;
        return QDBusMessage();
    };
    if (QThread::currentThread()->isInterruptionRequested())
        return fail(failure(ECANCELED));
    if (!budget.isValid())
        budget.start(); // Begin at the first detailed health query.
    const qint64 remaining = healthQueryBudgetMs - budget.elapsed();
    // Unavailable + ETIMEDOUT on HealthQueryStatus means no request was sent.
    if (remaining <= 0)
        return fail({ReadState::Unavailable, {}, ETIMEDOUT});
    auto message =
        QDBusMessage::createMethodCall("org.freedesktop.UDisks2", path, iface, "SmartGetAttributes");
    message.setAutoStartService(false);
    message.setInteractiveAuthorizationAllowed(false);
    message << QVariant::fromValue(QVariantMap());
    QDBusPendingCallWatcher watcher(
        QDBusConnection::systemBus().asyncCall(message, static_cast<int>(remaining)));
    QEventLoop loop;
    QTimer cancellation;
    cancellation.setInterval(50);
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (QThread::currentThread()->isInterruptionRequested() || budget.elapsed() >= healthQueryBudgetMs)
            loop.quit();
    });
    cancellation.start();
    if (!watcher.isFinished())
        loop.exec();
    if (QThread::currentThread()->isInterruptionRequested())
        return fail(failure(ECANCELED));
    if (!watcher.isFinished())
        return fail(failure(ETIMEDOUT));
    const QDBusMessage reply = watcher.reply();
    if (reply.type() == QDBusMessage::ErrorMessage) {
        const QDBusError error(reply);
        const ReadState readState =
            error.type() == QDBusError::AccessDenied ? ReadState::PermissionDenied
            : error.type() == QDBusError::UnknownMethod || error.type() == QDBusError::UnknownInterface
                ? ReadState::Unsupported
            : error.type() == QDBusError::ServiceUnknown || error.type() == QDBusError::UnknownObject
                ? ReadState::Unavailable
                : ReadState::Error;
        return fail({readState,
                     {},
                     error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout ||
                             error.type() == QDBusError::TimedOut
                         ? ETIMEDOUT
                         : EIO});
    }
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() != 1)
        return fail(failure(EINVAL));
    state = ok(QString());
    return reply;
}

// Cache-only method: UDisks2 returns its stored log, without SmartUpdate or tests.
void nvmeAttributes(StorageEntity &entity, QElapsedTimer &budget)
{
    const QString iface = QStringLiteral("org.freedesktop.UDisks2.NVMe.Controller");
    const QString source = entity.id + " : " + iface + ".SmartGetAttributes";
    const auto note = [&](const Attribute &state) {
        field(entity.fields, iface + "/SmartGetAttributes",
              QT_TRANSLATE_NOOP("Storage", "Cached NVMe health details"), state, source,
              Format::HealthQueryStatus);
    };
    Attribute state;
    const QDBusMessage message = healthReply(entity.id, iface, budget, state);
    if (state.state != ReadState::Available) {
        note(state);
        return;
    }
    const QDBusReply<QVariantMap> reply(message);
    if (!reply.isValid()) {
        note(failure(EINVAL));
        return;
    }
    const QVariantMap values = reply.value();
    if (values.size() > 64) {
        note(failure(EOVERFLOW));
        return;
    }
    const auto add = [&](const char *key, const char *label, int type, Format format,
                         bool zeroUnknown = false) {
        const QVariant value = values.value(QLatin1String(key));
        Attribute data;
        if (value.isValid()) {
            if (value.metaType().id() != type)
                data = failure(EINVAL);
            else if (!zeroUnknown || value.toULongLong() != 0)
                data = ok(QString::number(value.toULongLong()));
        }
        field(entity.fields, iface + "/SmartGetAttributes/" + QLatin1String(key), label, data,
              source + " : " + QLatin1String(key), format);
    };
    add("avail_spare", QT_TRANSLATE_NOOP("Storage", "Cached available spare"), QMetaType::UChar,
        Format::Percent);
    add("spare_thresh", QT_TRANSLATE_NOOP("Storage", "Cached spare threshold"), QMetaType::UChar,
        Format::Percent);
    add("percent_used", QT_TRANSLATE_NOOP("Storage", "Cached estimated endurance used"), QMetaType::UChar,
        Format::Percent);
    add("total_data_read", QT_TRANSLATE_NOOP("Storage", "Cached total data read"), QMetaType::ULongLong,
        Format::Bytes);
    add("total_data_written", QT_TRANSLATE_NOOP("Storage", "Cached total data written"), QMetaType::ULongLong,
        Format::Bytes);
    add("ctrl_busy_time", QT_TRANSLATE_NOOP("Storage", "Cached controller busy time"), QMetaType::ULongLong,
        Format::Minutes);
    add("power_cycles", QT_TRANSLATE_NOOP("Storage", "Cached power cycles"), QMetaType::ULongLong,
        Format::Count);
    add("unsafe_shutdowns", QT_TRANSLATE_NOOP("Storage", "Cached unsafe shutdowns"), QMetaType::ULongLong,
        Format::Count);
    add("media_errors", QT_TRANSLATE_NOOP("Storage", "Cached media and data integrity errors"),
        QMetaType::ULongLong, Format::Count);
    add("num_err_log_entries", QT_TRANSLATE_NOOP("Storage", "Cached error information log entries"),
        QMetaType::ULongLong, Format::Count);
    add("wctemp", QT_TRANSLATE_NOOP("Storage", "Cached warning temperature threshold"), QMetaType::UShort,
        Format::Kelvin, true);
    add("cctemp", QT_TRANSLATE_NOOP("Storage", "Cached critical temperature threshold"), QMetaType::UShort,
        Format::Kelvin, true);
    add("warning_temp_time", QT_TRANSLATE_NOOP("Storage", "Cached time above warning temperature"),
        QMetaType::UInt, Format::Minutes);
    add("critical_temp_time", QT_TRANSLATE_NOOP("Storage", "Cached time above critical temperature"),
        QMetaType::UInt, Format::Minutes);

    const QVariant sensors = values.value("temp_sensors");
    const char *const labels[] = {QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 1"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 2"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 3"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 4"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 5"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 6"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 7"),
                                  QT_TRANSLATE_NOOP("Storage", "Cached temperature sensor 8")};
    // Accept Qt's typed list or a lazy D-Bus argument; decode at most eight items.
    Attribute sensorState;
    QVector<quint16> temperatures;
    if (sensors.isValid()) {
        if (sensors.metaType().id() == qMetaTypeId<QList<quint16>>()) {
            temperatures = sensors.value<QList<quint16>>();
            sensorState = temperatures.size() > 8 ? failure(EOVERFLOW) : ok(QString());
        } else if (sensors.metaType().id() != qMetaTypeId<QDBusArgument>())
            sensorState = failure(EINVAL);
        else {
            const QDBusArgument array = sensors.value<QDBusArgument>();
            if (array.currentSignature() != "aq")
                sensorState = failure(EINVAL);
            else {
                array.beginArray();
                while (!array.atEnd() && temperatures.size() < 8) {
                    quint16 kelvin = 0;
                    array >> kelvin;
                    temperatures.append(kelvin);
                }
                if (!array.atEnd())
                    sensorState = failure(EOVERFLOW);
                else
                    sensorState = ok(QString());
                array.endArray();
            }
        }
    }
    for (int i = 0; i < 8; ++i) {
        Attribute data = sensorState.state == ReadState::Available ? Attribute() : sensorState;
        if (sensorState.state == ReadState::Available && i < temperatures.size() && temperatures.at(i) != 0)
            data = ok(QString::number(temperatures.at(i)));
        field(entity.fields, iface + "/SmartGetAttributes/temp_sensors/" + QString::number(i + 1), labels[i],
              data, source + QStringLiteral(" : temp_sensors[%1]").arg(i), Format::Kelvin);
    }
}

void ataAttributes(StorageEntity &entity, QElapsedTimer &budget)
{
    const QString iface = QStringLiteral("org.freedesktop.UDisks2.Drive.Ata");
    const QString source = entity.id + " : " + iface + ".SmartGetAttributes";
    const auto note = [&](const Attribute &state) {
        field(entity.fields, iface + "/SmartGetAttributes",
              QT_TRANSLATE_NOOP("Storage", "Cached ATA SMART attributes"), state, source,
              Format::HealthQueryStatus);
    };
    Attribute state;
    const QDBusMessage reply = healthReply(entity.id, iface, budget, state);
    if (state.state != ReadState::Available) {
        note(state);
        return;
    }
    const QVariant value = reply.arguments().constFirst();
    if (value.metaType().id() != qMetaTypeId<QDBusArgument>()) {
        note(failure(EINVAL));
        return;
    }
    const QDBusArgument array = value.value<QDBusArgument>();
    if (array.currentSignature() != "a(ysqiiixia{sv})") {
        note(failure(EINVAL));
        return;
    }
    QVector<AtaSmartAttribute> attributes;
    QSet<quint8> ids;
    array.beginArray();
    while (!array.atEnd()) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            state = failure(ECANCELED);
            break;
        }
        if (attributes.size() >= 256) {
            state = failure(EOVERFLOW);
            break;
        }
        AtaSmartAttribute attribute;
        QVariantMap expansion;
        array.beginStructure();
        array >> attribute.id >> attribute.name >> attribute.flags >> attribute.current >> attribute.worst >>
            attribute.threshold >> attribute.interpreted >> attribute.unit >> expansion;
        array.endStructure();
        const auto normalized = [](qint32 number) { return number >= -1 && number <= 255; };
        if (attribute.name.toUtf8().size() > 128 || expansion.size() > 64) {
            state = failure(EOVERFLOW);
            break;
        }
        if (ids.contains(attribute.id) || !normalized(attribute.current) || !normalized(attribute.worst) ||
            !normalized(attribute.threshold) || attribute.unit < 0) {
            state = failure(EINVAL);
            break;
        }
        ids.insert(attribute.id);
        attributes.append(std::move(attribute));
    }
    array.endArray();
    if (state.state != ReadState::Available) {
        note(state);
        return;
    }
    if (attributes.isEmpty()) {
        note({});
        return;
    }
    entity.ataAttributes = std::move(attributes); // Publish complete, validated list only.
}
void addServiceFields(StorageEntity &entity, const StorageInterfaces &interfaces, const QString &prefix)
{
    const auto copy = [&](const char *iface, const char *key, const char *label, Format format = Format::Text,
                          bool zeroUnknown = false) {
        serviceField(entity, interfaces.value(prefix + QLatin1String(iface)), prefix + QLatin1String(iface),
                     key, label, format, zeroUnknown);
    };
    if (interfaces.contains(prefix + "Drive")) {
        copy("Drive", "Vendor", QT_TRANSLATE_NOOP("Storage", "Drive manufacturer"));
        copy("Drive", "Model", QT_TRANSLATE_NOOP("Storage", "Drive model"));
        copy("Drive", "Serial", QT_TRANSLATE_NOOP("Storage", "Drive serial number"));
        copy("Drive", "Revision", QT_TRANSLATE_NOOP("Storage", "Drive firmware revision"));
        copy("Drive", "ConnectionBus", QT_TRANSLATE_NOOP("Storage", "Drive transport"));
        copy("Drive", "Size", QT_TRANSLATE_NOOP("Storage", "Drive capacity"), Format::Bytes, true);
        copy("Drive", "Removable", QT_TRANSLATE_NOOP("Storage", "Removable/fixed hint"), Format::Removable);
        copy("Drive", "MediaRemovable", QT_TRANSLATE_NOOP("Storage", "Drive media removable"),
             Format::Boolean);
        copy("Drive", "MediaAvailable", QT_TRANSLATE_NOOP("Storage", "Drive media available"),
             Format::Boolean);
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
    if (interfaces.contains(prefix + "PartitionTable"))
        copy("PartitionTable", "Type", QT_TRANSLATE_NOOP("Storage", "Partition table type"));
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
    if (interfaces.contains(prefix + "VolumeGroup"))
        copy("VolumeGroup", "Name", QT_TRANSLATE_NOOP("Storage", "Volume group name"));
}

void addServiceHealth(StorageEntity &entity, const StorageInterfaces &interfaces, const QString &prefix,
                      QElapsedTimer &healthBudget)
{
    const auto copy = [&](const char *iface, const char *key, const char *label, Format format = Format::Text,
                          bool zeroUnknown = false) {
        serviceField(entity, interfaces.value(prefix + QLatin1String(iface)), prefix + QLatin1String(iface),
                     key, label, format, zeroUnknown);
    };
    const bool ata = interfaces.contains(prefix + "Drive.Ata");
    const bool nvme = interfaces.contains(prefix + "NVMe.Controller");
    if (ata || nvme) {
        const QString iface = ata ? "Drive.Ata" : "NVMe.Controller";
        const QVariantMap health = interfaces.value(prefix + iface);
        if (ata) {
            copy("Drive.Ata", "SmartSupported", QT_TRANSLATE_NOOP("Storage", "ATA SMART supported"),
                 Format::Boolean);
            copy("Drive.Ata", "SmartEnabled", QT_TRANSLATE_NOOP("Storage", "ATA SMART enabled"),
                 Format::Boolean);
        }
        serviceField(entity, health, prefix + iface, "SmartUpdated",
                     QT_TRANSLATE_NOOP("Storage", "Cached health updated"), Format::Epoch, true);
        // Missing/zero timestamps invalidate every health assessment field.
        if (health.value("SmartUpdated").toULongLong() != 0) {
            serviceField(entity, health, prefix + iface, "SmartTemperature",
                         QT_TRANSLATE_NOOP("Storage", "Cached temperature"), Format::Kelvin, true);
            if (ata) {
                copy("Drive.Ata", "SmartPowerOnSeconds", QT_TRANSLATE_NOOP("Storage", "Cached power-on time"),
                     Format::Seconds, true);
                copy("Drive.Ata", "SmartFailing",
                     QT_TRANSLATE_NOOP("Storage", "Cached ATA failure prediction"), Format::AtaHealth);
                for (const auto &pair :
                     {qMakePair("SmartNumBadSectors", QT_TRANSLATE_NOOP("Storage", "Cached bad sectors")),
                      qMakePair("SmartNumAttributesFailing",
                                QT_TRANSLATE_NOOP("Storage", "Cached failing attributes")),
                      qMakePair("SmartNumAttributesFailedInThePast",
                                QT_TRANSLATE_NOOP("Storage", "Cached attributes failed in the past"))}) {
                    const QVariant count = health.value(QLatin1String(pair.first));
                    const int type = QLatin1String(pair.first) == QLatin1String("SmartNumBadSectors")
                                         ? QMetaType::LongLong
                                         : QMetaType::Int;
                    Attribute data;
                    if (count.isValid()) {
                        if (count.metaType().id() != type || count.toLongLong() < -1)
                            data = failure(EINVAL);
                        else if (count.toLongLong() >= 0)
                            data = ok(QString::number(count.toLongLong()));
                    }
                    field(entity.fields, prefix + "Drive.Ata/" + QLatin1String(pair.first), pair.second, data,
                          entity.id + " : " + prefix + "Drive.Ata." + QLatin1String(pair.first),
                          Format::Count);
                }
                ataAttributes(entity, healthBudget);
            } else {
                copy("NVMe.Controller", "SmartCriticalWarning",
                     QT_TRANSLATE_NOOP("Storage", "Cached NVMe critical warnings"), Format::NvmeHealth);
                copy("NVMe.Controller", "SmartPowerOnHours",
                     QT_TRANSLATE_NOOP("Storage", "Cached power-on time"), Format::Hours, true);
                nvmeAttributes(entity, healthBudget);
            }
        } else {
            field(entity.fields, "health", QT_TRANSLATE_NOOP("Storage", "Cached health"), {},
                  entity.id + " : " + prefix + iface + ".SmartUpdated (never updated or unavailable)");
        }
    } else if (interfaces.contains(prefix + "Drive")) {
        field(entity.fields, "health", QT_TRANSLATE_NOOP("Storage", "Cached health"),
              {ReadState::Unsupported, {}, 0},
              entity.id + " : UDisks2 (no supported ATA or NVMe health interface)");
    }
}

// Worker-only optional read. Share a time budget across related drives, bound
// output, and never treat a JSON error envelope as a successful attribute read.
void smartctlCounters(StorageEntity &entity, const StorageObjects &objects,
                      const QMap<QString, QString> &blocks, QElapsedTimer &budget)
{
    const auto tr = [](const char *text) { return QCoreApplication::translate("Storage", text); };
    bool needed = false;
    for (const AtaSmartAttribute &attribute : entity.ataAttributes)
        needed = needed || attribute.name == "total-lbas-written" || attribute.name == "total-lbas-read";
    if (!needed) return;
    entity.ataCounterStatus = tr("No usable raw counters returned by smartctl.");
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("smartctl"),
        {QStringLiteral("/usr/sbin"), QStringLiteral("/usr/bin"), QStringLiteral("/sbin"), QStringLiteral("/bin")});
    if (executable.isEmpty()) {
        entity.ataCounterStatus = tr("smartctl is not installed (optional smartmontools package).");
        return;
    }
    QString device, nativePath;
    quint64 deviceNumber = 0;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const auto &interfaces = it.value();
        const QVariantMap block = interfaces.value("org.freedesktop.UDisks2.Block");
        if (!blocks.contains(it.key().path()) || interfaces.contains("org.freedesktop.UDisks2.Partition")
            || objectPath(block.value("Drive")) != entity.id) continue;
        QByteArray bytes = block.value("Device").toByteArray();
        if (bytes.endsWith('\0')) bytes.chop(1);
        if (bytes.contains('\0')) continue;
        device = QString::fromUtf8(bytes);
        nativePath = blocks.value(it.key().path());
        deviceNumber = block.value("DeviceNumber").toULongLong();
        break;
    }
    struct stat before{};
    if (!device.startsWith("/dev/") || stat(device.toUtf8().constData(), &before) != 0
        || !S_ISBLK(before.st_mode) || static_cast<quint64>(before.st_rdev) != deviceNumber) return;
    const auto sameDevice = [&] {
        struct stat now{};
        return stat(device.toUtf8().constData(), &now) == 0 && S_ISBLK(now.st_mode)
            && now.st_rdev == before.st_rdev && now.st_ino == before.st_ino && now.st_dev == before.st_dev
            && QFileInfo(QStringLiteral("/sys/dev/block/%1:%2").arg(major(now.st_rdev)).arg(minor(now.st_rdev)))
                .canonicalFilePath() == nativePath;
    };
    if (!sameDevice()) return;
    if (!budget.isValid()) budget.start();
    constexpr int timeoutMs = 5000, outputLimit = 1024 * 1024;
    if (budget.elapsed() >= timeoutMs || QThread::currentThread()->isInterruptionRequested()) return;
    QProcess process;
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    process.start(executable, {"-j", "-a", "-n", "standby", device});
    QByteArray output;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(50);
        output += process.readAllStandardOutput();
        if (output.size() > outputLimit || budget.elapsed() >= timeoutMs
            || QThread::currentThread()->isInterruptionRequested()) {
            process.kill();
            process.waitForFinished(1000);
            entity.ataCounterStatus = tr("smartctl query timed out, was cancelled, or exceeded its output limit.");
            return;
        }
    }
    output += process.readAllStandardOutput();
    if (output.size() > outputLimit || process.exitStatus() != QProcess::NormalExit || !sameDevice()) return;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return;
    const QJsonObject json = document.object();
    const int status = json.value("smartctl").toObject().value("exit_status").toInt(-1);
    // Bit 2 may concern an optional log requested by -a; a valid attributes
    // table can still be usable. Bits 3..7 are health findings, not read failures.
    if (status < 0 || status > 255 || (status & 3) || process.exitCode() != status) {
        entity.ataCounterStatus = tr("smartctl could not read the drive. Device permissions (root/sudo), "
                                   "standby mode, or an unsupported command may prevent access.");
        return;
    }
    const QJsonArray table = json.value("ata_smart_attributes").toObject().value("table").toArray();
    if (table.isEmpty() || table.size() > 256) return;
    QMap<int, QString> counters;
    for (const QJsonValue &entry : table) {
        const QJsonObject item = entry.toObject();
        const int id = item.value("id").toInt(-1);
        if (id != 241 && id != 242) continue;
        if (counters.contains(id)) return;
        const QJsonValue raw = item.value("raw").toObject().value("value");
        if (!raw.isDouble()) return;
        const double number = raw.toDouble(-1);
        // ATA raw counters have at most 48 bits, exactly representable in JSON doubles.
        if (!(number >= 0 && number <= 281474976710655.0)) return;
        const quint64 integer = static_cast<quint64>(number);
        if (static_cast<double>(integer) != number) return;
        counters.insert(id, QString::number(integer));
    }
    for (AtaSmartAttribute &attribute : entity.ataAttributes) {
        if (!((attribute.id == 241 && attribute.name == "total-lbas-written")
            || (attribute.id == 242 && attribute.name == "total-lbas-read"))) continue;
        if (!counters.contains(attribute.id)) continue;
        attribute.rawCounter = counters.value(attribute.id);
        attribute.rawSource = executable + " -j -a -n standby " + device
            + " : ata_smart_attributes.table : ID " + QString::number(attribute.id);
    }
}

void addService(StorageProperties &result, const QSet<QString> &nativePaths, const QString &selectedPath)
{
    const StorageObjects objects = serviceSnapshot(result);
    if (objects.isEmpty())
        return;
    const QString prefix = QStringLiteral("org.freedesktop.UDisks2.");
    QSet<QString> wanted;
    QSet<QString> selectedDrives;
    QMap<QString, QString> blocks;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        const QVariant value = it.value().value(prefix + "Block").value("DeviceNumber");
        if (!value.isValid())
            continue;
        const dev_t number = static_cast<dev_t>(value.toULongLong());
        const QString path =
            QFileInfo(QStringLiteral("/sys/dev/block/%1:%2").arg(major(number)).arg(minor(number)))
                .canonicalFilePath();
        if (nativePaths.contains(path)) {
            wanted.insert(it.key().path());
            blocks.insert(it.key().path(), path);
            if (path == selectedPath || path.startsWith(selectedPath + '/')) {
                const QString drive = objectPath(it.value().value(prefix + "Block").value("Drive"));
                if (!drive.isEmpty() && drive != "/")
                    selectedDrives.insert(drive);
            }
        }
    }
    // No guessed model/serial matching; each drive object is retained once.
    for (int pass = 0; pass < relatedLimit; ++pass) {
        const int before = wanted.size();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
            if (!wanted.contains(it.key().path()))
                continue;
            for (const auto &pair :
                 {qMakePair(QStringLiteral("Block"), QStringLiteral("Drive")),
                  qMakePair(QStringLiteral("Block"), QStringLiteral("CryptoBackingDevice")),
                  qMakePair(QStringLiteral("Block"), QStringLiteral("MDRaid")),
                  qMakePair(QStringLiteral("Block"), QStringLiteral("MDRaidMember")),
                  qMakePair(QStringLiteral("Partition"), QStringLiteral("Table")),
                  qMakePair(QStringLiteral("Block.LVM2"), QStringLiteral("LogicalVolume")),
                  qMakePair(QStringLiteral("LogicalVolume"), QStringLiteral("VolumeGroup"))}) {
                const QString target = objectPath(it.value().value(prefix + pair.first).value(pair.second));
                if (!target.isEmpty() && target != "/" && objects.contains(QDBusObjectPath(target)))
                    wanted.insert(target);
            }
        }
        if (wanted.size() > relatedLimit) {
            field(result.notes, "service-limit", QT_TRANSLATE_NOOP("Storage", "Related storage metadata"),
                  failure(EOVERFLOW), "UDisks2");
            return;
        }
        if (wanted.size() == before)
            break;
    }
    QList<QDBusObjectPath> ordered;
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        if (wanted.contains(it.key().path()))
            ordered.append(it.key());
    }
    // Prioritize explicit selected-device associations; retain object-path order
    // within each tier and do not guess a single drive for spanning volumes.
    std::stable_sort(ordered.begin(), ordered.end(), [&](const QDBusObjectPath &a, const QDBusObjectPath &b) {
        return selectedDrives.contains(a.path()) && !selectedDrives.contains(b.path());
    });
    QElapsedTimer smartctlBudget;
    QElapsedTimer healthBudget; // Shared by all related ATA and NVMe cache-method calls.
    for (const QDBusObjectPath &path : ordered) {
        if (QThread::currentThread()->isInterruptionRequested())
            return;
        StorageEntity entity;
        entity.id = path.path();
        if (blocks.contains(entity.id))
            link(result, blocks.value(entity.id), entity.id,
                 QT_TRANSLATE_NOOP("Storage", "UDisks2 block metadata"),
                 "UDisks2 Block.DeviceNumber / sysfs dev");
        const auto object = objects.constFind(path);
        const auto &interfaces = object.value();
        addServiceFields(entity, interfaces, prefix);
        addServiceHealth(entity, interfaces, prefix, healthBudget);
        smartctlCounters(entity, objects, blocks, smartctlBudget);
        for (auto iface = interfaces.cbegin(); iface != interfaces.cend(); ++iface) {
            for (const char *key : {"Drive", "CryptoBackingDevice", "MDRaid", "MDRaidMember", "Table",
                                    "LogicalVolume", "VolumeGroup"}) {
                const QString target = objectPath(iface.value().value(QLatin1String(key)));
                if (wanted.contains(target))
                    link(result, entity.id, target, QT_TRANSLATE_NOOP("Storage", "Storage relationship"),
                         iface.key() + '.' + QLatin1String(key));
            }
        }
        result.entities.append(std::move(entity));
    }
}
#endif

// Fill the bounded native graph before collecting any entity fields.
bool collectNativeTopology(const Device &device, StorageProperties &result, QSet<QString> &wanted,
                           QVector<StorageLink> &nativeLinks)
{
    const QDir directory("/sys/class/block");
    const DirectoryNames scanned = boundedDirectoryNames(directory.path(), recordLimit);
    if (scanned.error) {
        field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage inventory"),
              failure(scanned.error), "/sys/class/block");
        return false;
    }
    const QStringList &names = scanned.names;
    QMap<QString, QString> paths;
    for (const QString &name : names) {
        const QString path = QFileInfo(directory.filePath(name)).canonicalFilePath();
        if (path.startsWith("/sys/devices/"))
            paths.insert(name, path);
    }
    const QList<QString> pathValues = paths.values();
    const QSet<QString> allPaths(pathValues.cbegin(), pathValues.cend());
    if (device.subsystem == "block")
        wanted.insert(device.path);
    else
        for (const QString &path : paths)
            if (path.startsWith(device.path + '/'))
                wanted.insert(path);
    for (const QString &path : paths) {
        if (QThread::currentThread()->isInterruptionRequested())
            return false;
        if (QFileInfo::exists(path + "/partition"))
            nativeLinks.append({path, QFileInfo(path).dir().canonicalPath(),
                                QT_TRANSLATE_NOOP("Storage", "Partition of"),
                                path + "/partition (kernel ancestry)"});
        for (const auto &pair : {qMakePair("slaves", QT_TRANSLATE_NOOP("Storage", "Backed by")),
                                 qMakePair("holders", QT_TRANSLATE_NOOP("Storage", "Used by"))}) {
            const QDir links(path + '/' + QLatin1String(pair.first));
            const DirectoryNames related = boundedDirectoryNames(links.path(), recordLimit);
            if (related.error) {
                // Relationship directories are optional (e.g. partitions have
                // no slaves directory). Do not hide access/I/O failures, or
                // mistake a removed block device for an absent optional directory.
                int error = related.error;
                if (error == ENOENT) {
                    struct stat relationship{};
                    if (lstat(links.path().toUtf8().constData(), &relationship) != 0) {
                        error = errno;
                        if (error == ENOENT) {
                            struct stat owner{};
                            if (stat(path.toUtf8().constData(), &owner) == 0) {
                                if (S_ISDIR(owner.st_mode))
                                    continue;
                                error = ENOTDIR;
                            } else
                                error = errno;
                        }
                    }
                    // If the directory exists, ENOENT came from inside the
                    // enumeration. Preserve it rather than accepting a partial graph.
                }
                field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage relationships"),
                      failure(error), links.path());
                return false;
            }
            for (const QString &name : related.names) {
                const QString target = QFileInfo(links.filePath(name)).canonicalFilePath();
                if (allPaths.contains(target))
                    nativeLinks.append({path, target, pair.second, links.filePath(name)});
                if (nativeLinks.size() > recordLimit * 4) {
                    field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Storage relationships"),
                          failure(EOVERFLOW), "/sys/class/block");
                    return false;
                }
            }
        }
    }
    for (int pass = 0; pass < relatedLimit; ++pass) {
        const int before = wanted.size();
        for (const StorageLink &edge : nativeLinks)
            if (wanted.contains(edge.from) || wanted.contains(edge.to)) {
                wanted.insert(edge.from);
                wanted.insert(edge.to);
            }
        if (wanted.size() > relatedLimit) {
            field(result.notes, "limit", QT_TRANSLATE_NOOP("Storage", "Related storage metadata"),
                  failure(EOVERFLOW), "/sys/class/block");
            return false;
        }
        if (wanted.size() == before)
            break;
    }
    return true;
}

QString nativeIdentity(const QString &path)
{
    struct stat info{};
    if (lstat(path.toUtf8().constData(), &info) != 0)
        return QString();
    return QStringLiteral("%1:%2")
        .arg(static_cast<qulonglong>(info.st_dev))
        .arg(static_cast<qulonglong>(info.st_ino));
}

Attribute collectMountPoints(StorageProperties &result, QMap<QString, QStringList> &mountPoints)
{
    const Attribute mounts = textFile("/proc/self/mountinfo", 4 * 1024 * 1024);
    field(result.notes, "mountinfo", QT_TRANSLATE_NOOP("Storage", "Mount metadata"),
          mounts.state == ReadState::Available ? ok("application-namespace") : mounts,
          "/proc/self/mountinfo");
    if (mounts.state == ReadState::Available) {
        for (const QString &line : mounts.value.split('\n')) {
            const QStringList parts = line.split(' ');
            const int separator = parts.indexOf("-");
            if (parts.size() < 6 || separator < 6 || parts.size() <= separator + 2)
                continue;
            mountPoints[parts[2]].append(unescapeMount(parts[4]) + " [" + parts[separator + 1] +
                                         "; root=" + unescapeMount(parts[3]) + ']');
        }
    }
    return mounts;
}

StorageEntity collectNativeEntity(udev *context, const QString &path, const Attribute &mounts,
                                  const QMap<QString, QStringList> &mountPoints)
{
    StorageEntity entity;
    entity.id = path;
    const QByteArray encoded = path.toUtf8();
    std::unique_ptr<udev_device, decltype(&udev_device_unref)> raw(
        context ? udev_device_new_from_syspath(context, encoded.constData()) : nullptr, &udev_device_unref);
    metadata(entity, raw.get(), "DEVNAME", QT_TRANSLATE_NOOP("Storage", "Device node"));
    const Attribute number = textFile(path + "/dev");
    field(entity.fields, "dev", QT_TRANSLATE_NOOP("Storage", "Major:minor"), number, path + "/dev");
    Attribute capacity = textFile(path + "/size");
    if (capacity.state == ReadState::Available) {
        bool valid = false;
        const quint64 sectors = capacity.value.toULongLong(&valid);
        capacity = !valid || sectors > std::numeric_limits<quint64>::max() / 512
                       ? failure(EOVERFLOW)
                       : ok(QString::number(sectors * 512));
    }
    field(entity.fields, "capacity", QT_TRANSLATE_NOOP("Storage", "Block capacity"), capacity,
          path + "/size (512-byte units)", Format::Bytes);
    const bool partition = QFileInfo::exists(path + "/partition");
    const QString queueOwner = partition ? QFileInfo(path).dir().canonicalPath() : path;
    for (const auto &pair :
         {qMakePair("logical_block_size", QT_TRANSLATE_NOOP("Storage", "Logical sector size")),
          qMakePair("physical_block_size", QT_TRANSLATE_NOOP("Storage", "Physical sector size"))}) {
        const QString source = queueOwner + "/queue/" + pair.first;
        field(entity.fields, QStringLiteral("queue/") + pair.first, pair.second, textFile(source), source,
              Format::Bytes);
    }
    native(entity, "removable", QT_TRANSLATE_NOOP("Storage", "Kernel removable media"), Format::Boolean);
    native(entity, "ro", QT_TRANSLATE_NOOP("Storage", "Block read-only"), Format::Boolean);
    if (QFileInfo::exists(path + "/partition")) {
        native(entity, "partition", QT_TRANSLATE_NOOP("Storage", "Partition number"));
        native(entity, "start", QT_TRANSLATE_NOOP("Storage", "Partition start (512-byte sectors)"));
    }
    for (const auto &pair :
         {qMakePair("ID_MODEL", QT_TRANSLATE_NOOP("Storage", "Disk model")),
          qMakePair("ID_VENDOR", QT_TRANSLATE_NOOP("Storage", "Manufacturer")),
          qMakePair("ID_SERIAL_SHORT", QT_TRANSLATE_NOOP("Storage", "Serial number")),
          qMakePair("ID_REVISION", QT_TRANSLATE_NOOP("Storage", "Firmware revision")),
          qMakePair("ID_BUS", QT_TRANSLATE_NOOP("Storage", "Transport")),
          qMakePair("ID_FS_TYPE", QT_TRANSLATE_NOOP("Storage", "Filesystem / content type")),
          qMakePair("ID_FS_USAGE", QT_TRANSLATE_NOOP("Storage", "Content usage")),
          qMakePair("ID_FS_LABEL", QT_TRANSLATE_NOOP("Storage", "Filesystem / content label")),
          qMakePair("ID_FS_UUID", QT_TRANSLATE_NOOP("Storage", "Filesystem / content UUID")),
          qMakePair("ID_PART_TABLE_TYPE", QT_TRANSLATE_NOOP("Storage", "Partition table type"))})
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
            field(entity.fields, "native-transport", QT_TRANSLATE_NOOP("Storage", "Native transport"),
                  ok("NVMe"), hardware + "/subsystem");
        else if (raw && udev_device_get_property_value(raw.get(), "ID_ATA_SATA") &&
                 QByteArray(udev_device_get_property_value(raw.get(), "ID_ATA_SATA")) == "1")
            field(entity.fields, "native-transport", QT_TRANSLATE_NOOP("Storage", "Native transport"),
                  ok("SATA"), "udev: ID_ATA_SATA on " + path);
        for (const auto &pair :
             {qMakePair("model", QT_TRANSLATE_NOOP("Storage", "Hardware model")),
              qMakePair("vendor", QT_TRANSLATE_NOOP("Storage", "Hardware vendor")),
              qMakePair("rev", QT_TRANSLATE_NOOP("Storage", "Hardware revision")),
              qMakePair("serial", QT_TRANSLATE_NOOP("Storage", "Hardware serial")),
              qMakePair("firmware_rev", QT_TRANSLATE_NOOP("Storage", "Hardware firmware revision"))})
            field(entity.fields, QStringLiteral("hardware/") + pair.first, pair.second,
                  textFile(hardware + '/' + pair.first), hardware + '/' + pair.first);
    }
    const QStringList points = mountPoints.value(number.value);
    Attribute mountState;
    if (mounts.state != ReadState::Available)
        mountState = mounts;
    else if (number.state != ReadState::Available)
        mountState = number;
    else if (points.isEmpty())
        mountState = ok("no-observed-mount");
    else {
        const QString joined = points.join('\n');
        mountState = joined.size() > textLimit ? failure(EOVERFLOW) : ok(joined);
    }
    field(entity.fields, "mounts", QT_TRANSLATE_NOOP("Storage", "Mount points (application namespace)"),
          mountState, "/proc/self/mountinfo : " + number.value);
    return entity;
}

} // namespace

StorageProperties collectStorageProperties(const Device &device, bool cachedService)
{
    StorageProperties result;
    result.applicable = device.subsystem == "block" || device.subsystem == "nvme";
    if (!result.applicable)
        return result;
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    QSet<QString> wanted;
    QVector<StorageLink> nativeLinks;
    if (!collectNativeTopology(device, result, wanted, nativeLinks))
        return result;
    QMap<QString, QString> identities;
    for (const QString &path : wanted)
        identities.insert(path, nativeIdentity(path));
    QMap<QString, QStringList> mountPoints;
    const Attribute mounts = collectMountPoints(result, mountPoints);
    // Sorting is deterministic and each canonical block path appears once.
    QStringList selected = wanted.values();
    selected.sort();
    for (const QString &path : selected) {
        if (QThread::currentThread()->isInterruptionRequested())
            return result;
        result.entities.append(collectNativeEntity(context.get(), path, mounts, mountPoints));
    }
    for (const StorageLink &edge : nativeLinks)
        if (wanted.contains(edge.from) && wanted.contains(edge.to))
            link(result, edge.from, edge.to, edge.label, edge.source);
    if (device.subsystem == "nvme") {
        StorageEntity controller;
        controller.id = device.path;
        native(controller, "model", QT_TRANSLATE_NOOP("Storage", "Controller model"));
        native(controller, "serial", QT_TRANSLATE_NOOP("Storage", "Controller serial number"));
        native(controller, "firmware_rev", QT_TRANSLATE_NOOP("Storage", "Controller firmware revision"));
        result.entities.prepend(controller);
        for (const QString &path : selected)
            if (path.startsWith(device.path + '/'))
                link(result, device.path, path,
                     QT_TRANSLATE_NOOP("Storage", "Controller namespace / partition"), "sysfs ancestry");
    }
#ifdef QLDM_HAVE_DBUS
    if (cachedService && !QThread::currentThread()->isInterruptionRequested())
        addService(result, wanted, device.path);
#else
    Q_UNUSED(cachedService);
    field(result.notes, "service", QT_TRANSLATE_NOOP("Storage", "UDisks2 metadata"),
          {ReadState::Unsupported, {}, 0}, "QtDBus not enabled in this build");
#endif
    // Related paths and device numbers can also be reused. Reject the whole
    // storage snapshot if a native entity changed while the service was queried.
    for (auto it = identities.cbegin(); it != identities.cend(); ++it) {
        if (it.value().isEmpty() || nativeIdentity(it.key()) != it.value()) {
            result.entities.clear();
            result.links.clear();
            result.notes.clear();
            field(result.notes, "changed",
                  QT_TRANSLATE_NOOP("Storage", "Storage topology changed during collection"), failure(EAGAIN),
                  it.key());
            break;
        }
    }
    return result;
}
