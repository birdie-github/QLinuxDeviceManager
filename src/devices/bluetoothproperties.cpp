#include "bluetoothproperties.h"
#include "naturalsort.h"
#include "propertytext.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QLibrary>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QThread>
#include <QStringList>
#include <QMap>
#include <QtEndian>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef QLDM_HAVE_BLUEZ
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QTimer>
using BluezInterfaces = QMap<QString, QVariantMap>;
using BluezObjects = QMap<QDBusObjectPath, BluezInterfaces>;
Q_DECLARE_METATYPE(BluezInterfaces)
Q_DECLARE_METATYPE(BluezObjects)
#endif

namespace {
QString tr(const char *text) { return QCoreApplication::translate("Bluetooth", text); }
Attribute available(const QString &text) { return {ReadState::Available, text, 0}; }
Attribute failure(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENODEV ? ReadState::Removed
        : error == EOPNOTSUPP || error == EAFNOSUPPORT || error == EPROTONOSUPPORT ? ReadState::Unsupported
        : error == ENOENT || error == ENODATA ? ReadState::Unavailable : ReadState::Error, {}, error};
}
void put(BluetoothProperties &result, const QString &key, const Attribute &value, const QString &source)
{
    result.values.insert(key, value);
    result.sources.insert(key, source);
}
Attribute readText(const QString &path)
{
    const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return failure(errno);
    char buffer[257];
    ssize_t count;
    do { count = read(fd, buffer, sizeof(buffer)); } while (count < 0 && errno == EINTR);
    const int error = errno;
    close(fd);
    if (count < 0) return failure(error);
    if (count > 256) return failure(EOVERFLOW);
    return available(QString::fromUtf8(buffer, count).trimmed());
}
QString address(const QByteArray &bytes)
{
    QStringList parts;
    for (auto it = bytes.crbegin(); it != bytes.crend(); ++it)
        parts.append(QStringLiteral("%1").arg(static_cast<unsigned char>(*it), 2, 16, QLatin1Char('0')));
    return parts.join(':').toUpper();
}
QString company(quint16 id)
{
    // Optional system BlueZ library supplies its maintained SIG company database.
    static QLibrary library(QStringLiteral("bluetooth"), 3);
    using Lookup = const char *(*)(int);
    static const auto lookup = reinterpret_cast<Lookup>(library.resolve("bt_compidtostr"));
    const QString name = lookup ? QString::fromUtf8(lookup(id)) : QString();
    const QString code = QStringLiteral("0x%1").arg(id, 4, 16, QLatin1Char('0'));
    return name.isEmpty() ? tr("Company ID %1").arg(code) : name + " (" + code + ')';
}
QString serviceName(const QString &uuid)
{
    static const QHash<QString, QString> names = [] {
        QStringList paths {QCoreApplication::applicationDirPath() + "/bluetooth-services.ids",
            QCoreApplication::applicationDirPath() + "/../share/QLinuxDeviceManager/bluetooth-services.ids"};
        for (const QString &directory : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            paths.append(directory + "/QLinuxDeviceManager/bluetooth-services.ids");
#ifdef QLDM_INSTALLED_BLUETOOTH_SERVICES
        paths.append(QStringLiteral(QLDM_INSTALLED_BLUETOOTH_SERVICES));
#endif
        for (const QString &path : paths) {
            const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd < 0) continue;
            struct stat info {};
            if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > 65536) { close(fd); continue; }
            QFile file;
            if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) { close(fd); continue; }
            const QByteArray bytes = file.read(65537);
            if (bytes.size() > 65536 || file.error() != QFileDevice::NoError) continue;
            QHash<QString, QString> entries;
            const QRegularExpression entry(QStringLiteral("^([0-9a-fA-F]{4})\\t([^\\t]{1,256})$"));
            bool valid = true;
            for (const QString &line : QString::fromUtf8(bytes).split('\n')) {
                if (line.trimmed().isEmpty() || line.trimmed().startsWith('#')) continue;
                const auto match = entry.match(line.trimmed());
                if (!match.hasMatch() || entries.contains(match.captured(1).toLower())) { valid = false; break; }
                entries.insert(match.captured(1).toLower(), match.captured(2));
            }
            if (valid && !entries.isEmpty()) return entries;
        }
        return QHash<QString, QString>{};
    }();
    const QString normalized = uuid.toLower();
    return normalized.size() == 36 && normalized.startsWith("0000")
        && normalized.mid(8) == "-0000-1000-8000-00805f9b34fb" ? names.value(normalized.mid(4, 4)) : QString();
}
Attribute scalar(const QVariantMap &properties, const QString &key)
{
    if (!properties.contains(key)) return {};
    const QVariant value = properties.value(key);
    switch (value.metaType().id()) {
    case QMetaType::Bool: return available(value.toBool() ? "1" : "0");
    case QMetaType::QString:
        return value.toString().size() <= 4096 ? available(value.toString()) : failure(EOVERFLOW);
    case QMetaType::UChar: case QMetaType::UShort: case QMetaType::UInt:
        return available(QString::number(value.toUInt()));
    case QMetaType::Short: case QMetaType::Int: return available(QString::number(value.toInt()));
    case QMetaType::QStringList:
        if (value.toStringList().size() <= 128 && value.toStringList().join('\n').size() <= 16384)
            return available(value.toStringList().join('\n'));
        return failure(EOVERFLOW);
    default: return failure(EPROTO);
    }
}
void managementInfo(quint16 index, BluetoothProperties &result)
{
    // Linux Bluetooth socket ABI, no libbluetooth headers/build dependency.
    // Only MGMT_OP_READ_INFO (0x0004) is sent. No HCI commands or state changes.
    struct HciAddress { sa_family_t family; quint16 device; quint16 channel; };
    static_assert(sizeof(HciAddress) == 6, "Linux sockaddr_hci ABI");
    const int fd = socket(AF_BLUETOOTH, SOCK_RAW | SOCK_NONBLOCK | SOCK_CLOEXEC, 1 /* BTPROTO_HCI */);
    if (fd < 0) { result.managementState = failure(errno); return; }
    struct Close { int fd; ~Close() { close(fd); } } closer {fd};
    const HciAddress local {AF_BLUETOOTH, 0xffff, 3 /* HCI_CHANNEL_CONTROL */};
    if (bind(fd, reinterpret_cast<const sockaddr *>(&local), sizeof(local)) < 0) {
        result.managementState = failure(errno); return;
    }
    char request[6] = {};
    qToLittleEndian<quint16>(4, request);
    qToLittleEndian<quint16>(index, request + 2);
    if (send(fd, request, sizeof(request), MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof(request))) {
        result.managementState = failure(errno ? errno : EIO); return;
    }
    QElapsedTimer timer;
    timer.start();
    for (int packets = 0; packets < 128 && timer.elapsed() < 750; ++packets) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            result.managementState = failure(ECANCELED); return;
        }
        pollfd ready {fd, POLLIN, 0};
        const int polled = poll(&ready, 1, 50);
        if (polled < 0) { if (errno == EINTR) continue; result.managementState = failure(errno); return; }
        if (!polled) continue;
        char packet[1024];
        const ssize_t count = recv(fd, packet, sizeof(packet), MSG_TRUNC);
        if (count < 0) { if (errno == EINTR || errno == EAGAIN) continue; result.managementState = failure(errno); return; }
        if (count < 9 || count > static_cast<ssize_t>(sizeof(packet))) continue;
        const quint16 event = qFromLittleEndian<quint16>(packet);
        if (qFromLittleEndian<quint16>(packet + 2) != index
            || qFromLittleEndian<quint16>(packet + 4) != count - 6
            || (event != 1 && event != 2) || qFromLittleEndian<quint16>(packet + 6) != 4) continue;
        const quint8 status = static_cast<quint8>(packet[8]);
        if (status) {
            result.managementState = failure(status == 0x14 ? EACCES : status == 0x11 ? ENODEV
                : status == 0x0c || status == 1 ? EOPNOTSUPP : EIO);
            return;
        }
        if (event == 1) {
            result.managementState = decodeBluetoothControllerInfo(QByteArray(packet + 9, count - 9), result)
                ? available("1") : failure(EPROTO);
            return;
        }
    }
    result.managementState = failure(ETIMEDOUT);
}
#ifdef QLDM_HAVE_BLUEZ
QDBusMessage busCall(const QString &service, const QString &path, const QString &interface,
                     const QString &method, const QList<QVariant> &arguments, Attribute &state)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setAutoStartService(false);
    message.setArguments(arguments);
    QDBusPendingCallWatcher watcher(QDBusConnection::systemBus().asyncCall(message, 1000));
    QEventLoop loop;
    QTimer timeout, cancellation;
    timeout.setSingleShot(true);
    QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
        if (QThread::currentThread()->isInterruptionRequested()) loop.quit();
    });
    timeout.start(1000);
    cancellation.start(50);
    if (!watcher.isFinished()) loop.exec();
    if (!watcher.isFinished()) { state = failure(ETIMEDOUT); return {}; }
    const QDBusMessage reply = watcher.reply();
    if (reply.type() != QDBusMessage::ReplyMessage) {
        const QDBusError error(reply);
        state = failure(error.type() == QDBusError::AccessDenied ? EACCES
            : error.type() == QDBusError::NoReply || error.type() == QDBusError::Timeout
                || error.type() == QDBusError::TimedOut ? ETIMEDOUT
            : error.type() == QDBusError::ServiceUnknown || error.name() == "org.freedesktop.DBus.Error.NameHasNoOwner"
                ? EOPNOTSUPP : EIO);
        return {};
    }
    state = available("1");
    return reply;
}
void bluezInfo(BluetoothProperties &result)
{
    static const bool registered = [] {
        qDBusRegisterMetaType<BluezInterfaces>();
        qDBusRegisterMetaType<BluezObjects>();
        return true;
    }();
    Q_UNUSED(registered)
    // Pin calls to one bluetoothd instance; never activate a stopped daemon.
    const auto owner = busCall("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                              "GetNameOwner", {QStringLiteral("org.bluez")}, result.bluezState);
    if (owner.arguments().size() != 1 || result.bluezState.state != ReadState::Available) return;
    const auto reply = busCall(owner.arguments().front().toString(), "/", "org.freedesktop.DBus.ObjectManager",
                               "GetManagedObjects", {}, result.bluezState);
    if (reply.arguments().size() != 1 || result.bluezState.state != ReadState::Available) return;
    if (reply.signature() != "a{oa{sa{sv}}}") { result.bluezState = failure(EPROTO); return; }
    const BluezObjects objects = qdbus_cast<BluezObjects>(reply.arguments().front());
    if (objects.size() > 4096) { result.bluezState = failure(EOVERFLOW); return; }
    const auto adapter = objects.value(QDBusObjectPath(result.bluezPath)).value("org.bluez.Adapter1");
    if (adapter.isEmpty() || adapter.size() > 128) { result.bluezState = failure(ENODATA); return; }
    for (const QString &key : {QStringLiteral("Address"), QStringLiteral("AddressType"), QStringLiteral("Name"),
             QStringLiteral("Alias"), QStringLiteral("Powered"), QStringLiteral("Discoverable"),
             QStringLiteral("Pairable"), QStringLiteral("Discovering"), QStringLiteral("Class"),
             QStringLiteral("Roles"), QStringLiteral("UUIDs"), QStringLiteral("Modalias"),
             QStringLiteral("Manufacturer"), QStringLiteral("Version"), QStringLiteral("ExperimentalFeatures")}) {
        const Attribute value = scalar(adapter, key);
        if (adapter.contains(key)) put(result, key, value, result.bluezPath + " : org.bluez.Adapter1." + key);
    }
    qsizetype retainedCharacters = 0;
    for (auto object = objects.cbegin(); object != objects.cend(); ++object) {
        const QVariantMap properties = object.value().value("org.bluez.Device1");
        if (qdbus_cast<QDBusObjectPath>(properties.value("Adapter")).path() != result.bluezPath) continue;
        if (result.devices.size() >= 256) { result.devicesTruncated = true; break; }
        const auto remote = bluetoothKnownDevice(object.key().path(), properties);
        for (const Attribute &value : remote.values) retainedCharacters += value.value.size();
        if (retainedCharacters > 1024 * 1024) { result.devicesTruncated = true; break; }
        result.devices.append(remote);
    }
}
#endif
}

QString bluetoothServiceName(const QString &uuid)
{
    return serviceName(uuid);
}

bool bluetoothControllerName(const QString &name)
{
    static const QRegularExpression controller(QStringLiteral("^hci[0-9]+$"));
    return controller.match(name).hasMatch();
}
bool decodeBluetoothControllerInfo(const QByteArray &payload, BluetoothProperties &result)
{
    if (payload.size() != 280) return false;
    const QString source = result.controllerPath + " : Bluetooth management Read Controller Information (0x0004)";
    put(result, "ManagementAddress", available(address(payload.left(6))), source);
    put(result, "ManagementVersion", available(QString::number(static_cast<quint8>(payload[6]))), source);
    put(result, "ManagementManufacturer", available(QString::number(qFromLittleEndian<quint16>(payload.constData() + 7))), source);
    const quint32 deviceClass = static_cast<quint8>(payload[17])
        | (quint32(static_cast<quint8>(payload[18])) << 8) | (quint32(static_cast<quint8>(payload[19])) << 16);
    put(result, "ManagementClass", available(QString::number(deviceClass)), source);
    const QByteArray localName = payload.mid(20, 249);
    const int terminator = localName.indexOf('\0');
    put(result, "ManagementName", available(QString::fromUtf8(terminator < 0 ? localName : localName.left(terminator))), source);
    const quint32 supported = qFromLittleEndian<quint32>(payload.constData() + 9);
    const quint32 current = qFromLittleEndian<quint32>(payload.constData() + 13);
    for (int bit = 0; bit <= 25; ++bit) {
        put(result, "supported/" + QString::number(bit), available(supported & (quint32(1) << bit) ? "1" : "0"), source);
        put(result, "enabled/" + QString::number(bit), available(current & (quint32(1) << bit) ? "1" : "0"), source);
    }
    put(result, "SupportedMask", available(QStringLiteral("0x%1").arg(supported, 8, 16, QLatin1Char('0'))), source);
    put(result, "CurrentMask", available(QStringLiteral("0x%1").arg(current, 8, 16, QLatin1Char('0'))), source);
    return true;
}
BluetoothDeviceProperties bluetoothKnownDevice(const QString &path, const QVariantMap &properties)
{
    BluetoothDeviceProperties result;
    result.path = path;
    if (properties.size() > 128) return result;
    for (const QString &key : {QStringLiteral("Name"), QStringLiteral("Alias"), QStringLiteral("Address"),
             QStringLiteral("AddressType"), QStringLiteral("Connected"), QStringLiteral("Paired"),
             QStringLiteral("Bonded"), QStringLiteral("Trusted"), QStringLiteral("Blocked"),
             QStringLiteral("WakeAllowed"), QStringLiteral("Class"), QStringLiteral("Appearance"),
             QStringLiteral("UUIDs"), QStringLiteral("RSSI"), QStringLiteral("TxPower"), QStringLiteral("ServicesResolved")})
        result.values.insert(key, scalar(properties, key));
    // Advertising payloads are metadata only. Never read remote GATT characteristics.
#ifdef QLDM_HAVE_BLUEZ
    const auto data = qdbus_cast<QMap<quint16, QDBusVariant>>(properties.value("ManufacturerData"));
    QStringList rows;
    int retained = 0;
    for (auto it = data.cbegin(); it != data.cend() && retained < 16; ++it, ++retained) {
        const QByteArray bytes = qdbus_cast<QByteArray>(it.value().variant());
        rows.append(company(it.key()) + ": " + QString::fromLatin1(bytes.left(256).toHex(' '))
            + (bytes.size() > 256 ? " …" : ""));
    }
    if (data.size() > 16) rows.append(tr("Additional manufacturer entries omitted"));
    if (properties.contains("ManufacturerData")) result.values.insert("ManufacturerData", available(rows.join('\n')));
#endif
    return result;
}
void BluetoothProperties::invalidate(const Attribute &error)
{
    for (auto it = values.begin(); it != values.end(); ++it) it.value() = error;
    devices.clear();
    devicesTruncated = false;
    bluezState = managementState = error;
}
BluetoothProperties collectBluetoothProperties(const Device &device)
{
    BluetoothProperties result;
    if (device.subsystem != "bluetooth" || !bluetoothControllerName(device.sysname)) return result;
    result.applicable = true;
    result.controllerPath = device.path;
    result.bluezPath = "/org/bluez/" + device.sysname;
    const QString classPath = "/sys/class/bluetooth/" + device.sysname;
    if (QFileInfo(classPath).canonicalFilePath() != device.path) { result.invalidate(failure(ENODEV)); return result; }
    // Warm optional naming data in the worker, keeping file/library I/O out of UI formatters.
    serviceName(QString());
    company(0xffff);
    put(result, "Interface", available(device.sysname), classPath);
    put(result, "KernelAddress", readText(device.path + "/address"), device.path + "/address");
    QString ancestor = device.path;
    for (int depth = 0; depth < 16 && ancestor.startsWith("/sys/devices/"); ++depth) {
        const QString subsystem = QFileInfo(ancestor + "/subsystem").symLinkTarget().section('/', -1);
        if (subsystem == "usb" || subsystem == "pci" || subsystem == "serial" || subsystem == "serdev"
            || subsystem == "platform") {
            put(result, "Transport", available(subsystem), ancestor + "/subsystem");
            break;
        }
        ancestor = QFileInfo(ancestor).path();
    }
    const auto kills = QDir(device.path).entryList({"rfkill*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    if (kills.size() == 1 && readText(device.path + '/' + kills.front() + "/type").value == "bluetooth") {
        for (const QString &key : {QStringLiteral("soft"), QStringLiteral("hard")}) {
            const QString path = device.path + '/' + kills.front() + '/' + key;
            Attribute value = readText(path);
            if (value.state == ReadState::Available && value.value != "0" && value.value != "1") value = failure(EPROTO);
            put(result, key == "soft" ? "SoftBlocked" : "HardBlocked", value, path);
        }
    }
#ifdef QLDM_HAVE_BLUEZ
    if (!QThread::currentThread()->isInterruptionRequested()) bluezInfo(result);
#else
    result.bluezState = failure(EOPNOTSUPP);
#endif
    bool valid = false;
    const uint number = device.sysname.mid(3).toUInt(&valid);
    if (valid && number < 0xffff && !QThread::currentThread()->isInterruptionRequested()) managementInfo(number, result);
    if (result.values.value("KernelAddress").state != ReadState::Available
        && result.values.value("ManagementAddress").state == ReadState::Available
        && result.values.value("ManagementAddress").value != "00:00:00:00:00:00") {
        result.values.insert("KernelAddress", result.values.value("ManagementAddress"));
        result.sources.insert("KernelAddress", result.sources.value("ManagementAddress"));
    }
    const auto kernelAddress = result.values.value("KernelAddress"), bluezAddress = result.values.value("Address");
    if (result.values.value("AddressType").value == "public" && kernelAddress.state == ReadState::Available
        && bluezAddress.state == ReadState::Available
        && kernelAddress.value.compare(bluezAddress.value, Qt::CaseInsensitive) != 0)
        result.invalidate(failure(EAGAIN));
    if (QFileInfo(classPath).canonicalFilePath() != device.path) result.invalidate(failure(ENODEV));
    std::sort(result.devices.begin(), result.devices.end(), [](const auto &a, const auto &b) {
        const auto label = [](const BluetoothDeviceProperties &device) {
            QString text = device.values.value("Alias").value;
            if (text.isEmpty()) text = device.values.value("Name").value;
            return text.isEmpty() ? device.values.value("Address").value : text;
        };
        const QString left = label(a), right = label(b);
        const int order = naturalCompare(left, right);
        return order == 0 ? a.path < b.path : order < 0;
    });
    return result;
}

QString bluetoothSettingName(int bit)
{
    static const char *const names[] = {
        QT_TRANSLATE_NOOP("Bluetooth", "Powered"), QT_TRANSLATE_NOOP("Bluetooth", "Connectable"),
        QT_TRANSLATE_NOOP("Bluetooth", "Fast connectable"), QT_TRANSLATE_NOOP("Bluetooth", "Discoverable"),
        QT_TRANSLATE_NOOP("Bluetooth", "Bondable"), QT_TRANSLATE_NOOP("Bluetooth", "Legacy link security"),
        QT_TRANSLATE_NOOP("Bluetooth", "Secure Simple Pairing"), QT_TRANSLATE_NOOP("Bluetooth", "Bluetooth Classic (BR/EDR)"),
        QT_TRANSLATE_NOOP("Bluetooth", "High Speed"), QT_TRANSLATE_NOOP("Bluetooth", "Bluetooth Low Energy (LE)"),
        QT_TRANSLATE_NOOP("Bluetooth", "Advertising"), QT_TRANSLATE_NOOP("Bluetooth", "Secure Connections"),
        QT_TRANSLATE_NOOP("Bluetooth", "Debug keys"), QT_TRANSLATE_NOOP("Bluetooth", "Privacy"),
        QT_TRANSLATE_NOOP("Bluetooth", "Controller configuration"), QT_TRANSLATE_NOOP("Bluetooth", "Static address"),
        QT_TRANSLATE_NOOP("Bluetooth", "PHY configuration"), QT_TRANSLATE_NOOP("Bluetooth", "Wideband speech"),
        QT_TRANSLATE_NOOP("Bluetooth", "Connected isochronous stream — central"),
        QT_TRANSLATE_NOOP("Bluetooth", "Connected isochronous stream — peripheral"),
        QT_TRANSLATE_NOOP("Bluetooth", "Isochronous broadcaster"), QT_TRANSLATE_NOOP("Bluetooth", "Synchronized receiver"),
        QT_TRANSLATE_NOOP("Bluetooth", "Link-layer privacy"), QT_TRANSLATE_NOOP("Bluetooth", "PAST sender"),
        QT_TRANSLATE_NOOP("Bluetooth", "PAST receiver"), QT_TRANSLATE_NOOP("Bluetooth", "Shorter connection interval")};
    return bit >= 0 && bit < 26 ? tr(names[bit]) : QString::number(bit);
}
QString bluetoothPropertyText(const QString &key, const Attribute &value)
{
    if (value.state != ReadState::Available) return propertyReadValue(value);
    const QString &text = value.value;
    if (key == "UUIDs") {
        QStringList output;
        for (const QString &uuid : text.split('\n', Qt::SkipEmptyParts)) {
            const QString name = bluetoothServiceName(uuid);
            output.append(name.isEmpty() ? uuid : name + " — " + uuid);
        }
        return output.isEmpty() ? tr("None reported") : output.join('\n');
    }
    if (key == "Version" || key == "ManagementVersion") {
        bool valid = false;
        const uint code = text.toUInt(&valid);
        static const char *const versions[] = {"1.0b", "1.1", "1.2", "2.0 + EDR", "2.1 + EDR", "3.0 + HS",
            "4.0", "4.1", "4.2", "5.0", "5.1", "5.2", "5.3", "5.4", "6.0", "6.1", "6.2"};
        return valid && code < 17 ? QString::fromLatin1(versions[code]) : tr("Unknown Core version code %1").arg(text);
    }
    if (key == "Manufacturer" || key == "ManagementManufacturer") return company(text.toUShort());
    if (key == "RSSI" || key == "TxPower") return tr("%1 dBm").arg(text);
    if (key == "AddressType") return text == "public" ? tr("Public") : text == "random" ? tr("Random") : text;
    if (key == "Class" || key == "ManagementClass") {
        bool valid = false;
        const uint code = text.toUInt(&valid);
        const QString hex = QStringLiteral("0x%1").arg(code, 6, 16, QLatin1Char('0'));
        if (!valid) return text;
        QString kind;
        switch ((code >> 8) & 31) {
        case 1: kind = tr("Computer"); break; case 2: kind = tr("Phone"); break;
        case 3: kind = tr("Network access point"); break; case 4: kind = tr("Audio/video"); break;
        case 5: kind = tr("Peripheral"); break; case 6: kind = tr("Imaging"); break;
        case 7: kind = tr("Wearable"); break; case 8: kind = tr("Toy"); break;
        case 9: kind = tr("Health"); break; default: return hex;
        }
        return kind + " (" + hex + ')';
    }
    if (key == "Appearance") return QStringLiteral("0x%1").arg(text.toUInt(), 4, 16, QLatin1Char('0'));
    if (key == "Roles") {
        QStringList roles;
        for (const QString &role : text.split('\n', Qt::SkipEmptyParts))
            roles.append(role == "central-peripheral" ? tr("Central and peripheral concurrently")
                : role == "central" ? tr("Central") : role == "peripheral" ? tr("Peripheral") : role);
        return roles.isEmpty() ? tr("None reported") : roles.join('\n');
    }
    if (key == "Powered" || key == "Discoverable" || key == "Pairable" || key == "Discovering"
        || key == "Connected" || key == "Paired" || key == "Bonded" || key == "Trusted" || key == "Blocked"
        || key == "WakeAllowed" || key == "ServicesResolved" || key == "SoftBlocked" || key == "HardBlocked"
        || key.startsWith("supported/") || key.startsWith("enabled/"))
        return text == "1" ? tr("Yes") : text == "0" ? tr("No") : propertyReadValue(failure(EPROTO));
    return text.isEmpty() ? tr("None reported") : text;
}
