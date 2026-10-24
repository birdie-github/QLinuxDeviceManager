#include "wirelessproperties.h"
#include "genericnetlink.h"
#include "propertytext.h"
#include <QCoreApplication>
#include <QLocale>
#include <QFileInfo>
#include <QStringList>
#include <QMap>
#include <QVariant>
#include <linux/nl80211.h>
#include <net/if.h>
#ifdef QLDM_HAVE_DBUS
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QEventLoop>
#include <QTimer>
#endif

namespace {
using namespace GenericNetlink;
Attribute available(const QString &value) { return {ReadState::Available, value, 0}; }
Attribute failure(int error)
{
    return {error == EPERM || error == EACCES ? ReadState::PermissionDenied
        : error == ENODEV || error == ENXIO ? ReadState::Removed
        : error == EOPNOTSUPP || error == ENOENT ? ReadState::Unsupported
        : error == ENODATA ? ReadState::Unavailable : ReadState::Error, {}, error};
}
QString mac(const QByteArray &bytes)
{
    if (bytes.size() != 6 || (static_cast<unsigned char>(bytes[0]) & 1)) return {};
    bool nonzero = false;
    for (char byte : bytes) nonzero = nonzero || byte != 0;
    if (!nonzero) return {};
    QStringList parts;
    for (unsigned char byte : bytes) parts.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
    return parts.join(':');
}
QString ssid(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > 32) return {};
    const QString text = QString::fromUtf8(bytes);
    bool printable = text.toUtf8() == bytes;
    for (QChar c : text) printable = printable && c.isPrint();
    return printable ? text : "0x" + QString::fromLatin1(bytes.toHex());
}
const QByteArray *unique(const NetlinkAttributes &items, int type)
{
    bool valid = true;
    const auto *value = findUniqueAttribute(items, type, valid);
    return valid ? value : nullptr;
}
template <typename T> Attribute number(const NetlinkAttributes &items, int type)
{
    bool valid = true;
    const auto *bytes = findUniqueAttribute(items, type, valid);
    T value{};
    if (!valid || (bytes && !readNumber(bytes, value))) return failure(EPROTO);
    return bytes ? available(QString::number(value)) : Attribute {};
}
void put(WirelessProperties &result, const char *key, const Attribute &value, const QString &source)
{
    result.values.insert(QLatin1String(key), value);
    result.sources.insert(QLatin1String(key), source);
}
bool matching(const NetlinkAttributes &items, unsigned int index)
{
    __u32 value = 0;
    return readNumber(unique(items, NL80211_ATTR_IFINDEX), value) && value == index;
}
void rate(WirelessProperties &result, const NetlinkAttributes &station, int type, const char *rateKey,
          const char *streamsKey, const QString &source)
{
    NetlinkAttributes values;
    const auto *bytes = unique(station, type);
    if (!bytes) return;
    if (!parseAttributes(*bytes, values)) {
        put(result, rateKey, failure(EPROTO), source);
        return;
    }
    Attribute bitrate = number<__u32>(values, NL80211_RATE_INFO_BITRATE32);
    if (!unique(values, NL80211_RATE_INFO_BITRATE32)) bitrate = number<__u16>(values, NL80211_RATE_INFO_BITRATE);
    put(result, rateKey, bitrate, source + "; 100 kbit/s units, last reported PHY rate, not throughput");
    Attribute streams;
#ifdef NL80211_EHT_MIN_CAPABILITY_LEN
    streams = number<__u8>(values, NL80211_RATE_INFO_EHT_NSS);
#endif
    if (streams.state != ReadState::Available) streams = number<__u8>(values, NL80211_RATE_INFO_HE_NSS);
    if (streams.state != ReadState::Available) streams = number<__u8>(values, NL80211_RATE_INFO_VHT_NSS);
    // HT MCS 0..31 encodes 1..4 streams; MCS 32 is a special duplicate mode.
    if (streams.state != ReadState::Available) {
        __u8 mcs = 0;
        if (readNumber(unique(values, NL80211_RATE_INFO_MCS), mcs) && mcs < 32)
            streams = available(QString::number(mcs / 8 + 1));
    }
    put(result, streamsKey, streams, source + "; spatial streams used by this reported rate, not antenna count");
}
#ifdef QLDM_HAVE_DBUS
// All calls are read-only, on the system bus. Never retrieve connection secrets,
// request a scan or activate services. GetSettings excludes secrets.
class BusQuery {
public:
    BusQuery() { timer_.start(); }
    QDBusMessage call(const QString &service, const QString &path, const QString &interface,
                      const QString &method, const QList<QVariant> &arguments)
    {
        error = {};
        const int remaining = 2000 - static_cast<int>(timer_.elapsed());
        if (remaining <= 0 || QThread::currentThread()->isInterruptionRequested()) {
            error = failure(remaining <= 0 ? ETIMEDOUT : ECANCELED);
            return {};
        }
        QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
        message.setAutoStartService(false);
        message.setArguments(arguments);
        QDBusPendingCallWatcher watcher(QDBusConnection::systemBus().asyncCall(message, qMin(remaining, 500)));
        QEventLoop loop;
        QTimer timeout, cancellation;
        timeout.setSingleShot(true);
        QObject::connect(&watcher, &QDBusPendingCallWatcher::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
        QObject::connect(&cancellation, &QTimer::timeout, &loop, [&] {
            if (QThread::currentThread()->isInterruptionRequested()) loop.quit();
        });
        timeout.start(qMin(remaining, 500));
        cancellation.start(50);
        if (!watcher.isFinished()) loop.exec();
        if (!watcher.isFinished()) { error = failure(ETIMEDOUT); return {}; }
        const QDBusMessage reply = watcher.reply();
        if (reply.type() != QDBusMessage::ReplyMessage) {
            const QDBusError problem(reply);
            error = failure(problem.type() == QDBusError::AccessDenied ? EACCES
                : problem.type() == QDBusError::ServiceUnknown ? EOPNOTSUPP : EIO);
            return {};
        }
        return reply;
    }
    QVariantMap properties(const QString &service, const QString &path, const QString &interface)
    {
        const auto reply = call(service, path, "org.freedesktop.DBus.Properties", "GetAll", {interface});
        if (reply.arguments().size() != 1) return {};
        const auto values = qdbus_cast<QVariantMap>(reply.arguments().constFirst());
        if (values.size() > 256) { error = failure(EOVERFLOW); return {}; }
        return values;
    }
    Attribute error;
private:
    QElapsedTimer timer_;
};
#endif
}

void WirelessProperties::invalidate(const Attribute &error)
{
    for (auto it = values.begin(); it != values.end(); ++it) it.value() = error;
}

WirelessProperties collectWirelessProperties(const QString &name, unsigned int index, const QString &path)
{
    WirelessProperties result;
    result.applicable = true;
    const QString source = QStringLiteral("nl80211: ifindex %1 (%2)").arg(index).arg(name);
    const QStringList keys {"mode", "ssid", "bssid", "frequency", "width", "signal", "txrate", "rxrate",
                           "txstreams", "rxstreams", "txpower", "security", "nmrate", "quality", "securityconfig", "connection"};
    for (const QString &key : keys) {
        result.values.insert(key, {});
        result.sources.insert(key, source);
    }
    if (!index || if_nametoindex(name.toUtf8().constData()) != index) {
        result.invalidate(failure(ENODEV));
        return result;
    }
    Query query;
    NetlinkAttributes family, info;
    QByteArray familyName("nl80211"); familyName.append('\0');
    QByteArray request;
    appendAttribute(request, CTRL_ATTR_FAMILY_NAME, familyName);
    __u16 id = 0;
    bool native = query.request(GENL_ID_CTRL, CTRL_CMD_GETFAMILY, CTRL_CMD_NEWFAMILY, request, family)
        && readNumber(unique(family, CTRL_ATTR_FAMILY_ID), id) && id >= GENL_MIN_ID;
    QByteArray interfaceRequest;
    appendAttribute(interfaceRequest, NL80211_ATTR_IFINDEX, QByteArray(reinterpret_cast<const char *>(&index), sizeof(index)));
    native = native && query.request(id, NL80211_CMD_GET_INTERFACE, NL80211_CMD_NEW_INTERFACE, interfaceRequest, info)
        && matching(info, index);
    __u32 mode = 0;
    QByteArray ownMac;
    if (native) {
        put(result, "mode", number<__u32>(info, NL80211_ATTR_IFTYPE), source);
        readNumber(unique(info, NL80211_ATTR_IFTYPE), mode);
        put(result, "frequency", number<__u32>(info, NL80211_ATTR_WIPHY_FREQ), source);
        put(result, "width", number<__u32>(info, NL80211_ATTR_CHANNEL_WIDTH), source);
        put(result, "txpower", number<qint32>(info, NL80211_ATTR_WIPHY_TX_POWER_LEVEL), source);
        if (const auto *bytes = unique(info, NL80211_ATTR_SSID)) {
            const QString value = ssid(*bytes);
            if (!value.isEmpty()) put(result, "ssid", available(value), source);
        }
        if (const auto *bytes = unique(info, NL80211_ATTR_MAC))
            if (!mac(*bytes).isEmpty()) ownMac = *bytes;
        if (mode == NL80211_IFTYPE_AP && !ownMac.isEmpty()) put(result, "bssid", available(mac(ownMac)), source);
    } else result.invalidate(failure(query.error() ? query.error() : EPROTO));
    QByteArray peer;
#ifdef QLDM_HAVE_DBUS
    BusQuery bus;
    const QString nm = QStringLiteral("org.freedesktop.NetworkManager");
    const QString wireless = nm + ".Device.Wireless";
    const auto deviceReply = bus.call(nm, "/org/freedesktop/NetworkManager", nm, "GetDeviceByIpIface", {name});
    QString devicePath, apPath;
    QVariantMap wifi, device;
    if (deviceReply.arguments().size() == 1) devicePath = deviceReply.arguments().constFirst().value<QDBusObjectPath>().path();
    if (!devicePath.isEmpty()) {
        device = bus.properties(nm, devicePath, nm + ".Device");
        if (device.value("Interface").toString() != name
            || QFileInfo(device.value("Udi").toString()).canonicalFilePath() != QFileInfo(path).canonicalFilePath()) devicePath.clear();
    }
    if (!devicePath.isEmpty()) {
        wifi = bus.properties(nm, devicePath, wireless);
        if (device.contains("State")) put(result, "connection", available(QString::number(device.value("State").toUInt())), nm + " : " + devicePath + ".State");
        bool daemonMac = false;
        if (ownMac.isEmpty()) {
            const QString address = device.value("HwAddress").toString().toLower();
            const QByteArray bytes = QByteArray::fromHex(address.toLatin1().replace(":", ""));
            if (!address.isEmpty() && mac(bytes) == address) { ownMac = bytes; daemonMac = true; }
        }
        if (!native) {
            switch (wifi.value("Mode").toUInt()) {
            case 1: mode = NL80211_IFTYPE_ADHOC; break;
            case 2: mode = NL80211_IFTYPE_STATION; break;
            case 3: mode = NL80211_IFTYPE_AP; break;
            case 4: mode = NL80211_IFTYPE_MESH_POINT; break;
            default: mode = NL80211_IFTYPE_UNSPECIFIED; break;
            }
            if (mode) put(result, "mode", available(QString::number(mode)), nm + " : " + devicePath + ".Mode");
        }
        if (mode == NL80211_IFTYPE_AP && daemonMac)
            put(result, "bssid", available(mac(ownMac)), nm + " : " + devicePath + ".HwAddress (local hotspot BSSID)");
        apPath = wifi.value("ActiveAccessPoint").value<QDBusObjectPath>().path();
        if (device.value("State").toUInt() == 100 && !apPath.isEmpty() && apPath != "/") {
            const QVariantMap ap = bus.properties(nm, apPath, nm + ".AccessPoint");
            const QString apSource = nm + " : " + apPath + " (active AP, cached metadata)";
            const QString ssidText = ssid(ap.value("Ssid").toByteArray());
            if (!ssidText.isEmpty()) put(result, "ssid", available(ssidText), apSource);
            const QString address = ap.value("HwAddress").toString().toLower();
            const QByteArray addressBytes = QByteArray::fromHex(address.toLatin1().replace(":", ""));
            if (!mac(addressBytes).isEmpty() && mac(addressBytes) == address) {
                peer = addressBytes;
                put(result, "bssid", available(address), apSource);
            }
            if (ap.contains("Strength") && ap.value("Strength").toUInt() <= 100)
                put(result, "quality", available(QString::number(ap.value("Strength").toUInt())), apSource + "; signal quality percentage");
            if (result.values.value("frequency").state != ReadState::Available && ap.value("Frequency").toUInt())
                put(result, "frequency", available(QString::number(ap.value("Frequency").toUInt())), apSource);
        }
        if (mode == NL80211_IFTYPE_AP && device.value("State").toUInt() == 100) {
            const QString activePath = device.value("ActiveConnection").value<QDBusObjectPath>().path();
            if (!activePath.isEmpty() && activePath != "/") {
                const QVariantMap active = bus.properties(nm, activePath, nm + ".Connection.Active");
                const QString profilePath = active.value("Connection").value<QDBusObjectPath>().path();
                if (!profilePath.isEmpty() && profilePath != "/") {
                    const auto reply = bus.call(nm, profilePath, nm + ".Settings.Connection", "GetSettings", {});
                    if (reply.arguments().size() == 1) {
                        const auto settings = qdbus_cast<QMap<QString, QVariantMap>>(reply.arguments().constFirst());
                        const QString key = settings.value("802-11-wireless-security").value("key-mgmt").toString();
                        if (!key.isEmpty() && key.size() <= 128)
                            put(result, "securityconfig", available(key), nm + " : " + profilePath + " (configured key management, not per-client negotiation)");
                    }
                }
            }
        }
        const uint bitrate = wifi.value("Bitrate").toUInt();
        if (device.value("State").toUInt() == 100 && mode == NL80211_IFTYPE_STATION && bitrate)
            put(result, "nmrate", available(QString::number(bitrate)), nm + " : " + devicePath + ".Bitrate; kbit/s, not throughput");
    }
    // CurrentAuthMode is negotiated authentication. AP WPA/RSN flags only
    // describe advertised choices and must never be used as the selected mode.
    const QString supplicant = QStringLiteral("fi.w1.wpa_supplicant1");
    const auto supplicantReply = bus.call(supplicant, "/fi/w1/wpa_supplicant1", supplicant, "GetInterface", {name});
    if (supplicantReply.arguments().size() == 1 && mode == NL80211_IFTYPE_STATION && peer.size() == 6) {
        const QString supplicantPath = supplicantReply.arguments().constFirst().value<QDBusObjectPath>().path();
        const QVariantMap state = bus.properties(supplicant, supplicantPath, supplicant + ".Interface");
        const QString bssPath = state.value("CurrentBSS").value<QDBusObjectPath>().path();
        if (state.value("Ifname").toString() == name && state.value("State").toString() == "completed" && bssPath != "/") {
            const QVariantMap bss = bus.properties(supplicant, bssPath, supplicant + ".BSS");
            const QString auth = state.value("CurrentAuthMode").toString();
            if (bss.value("BSSID").toByteArray() == peer && !auth.isEmpty() && auth.size() <= 128)
                put(result, "security", available(auth), supplicant + " : " + supplicantPath + ".CurrentAuthMode");
        }
    } else if (bus.error.state != ReadState::Available) put(result, "security", bus.error, supplicant);
#else
    put(result, "security", {ReadState::Unsupported, {}, 0}, "QtDBus support not built");
#endif
    Query linkQuery; // Separate bounded kernel budget after optional daemon queries.
    if (native && mode == NL80211_IFTYPE_STATION && peer.size() == 6) {
        QByteArray stationRequest = interfaceRequest;
        appendAttribute(stationRequest, NL80211_ATTR_MAC, peer);
        NetlinkAttributes stationReply, station;
        if (linkQuery.request(id, NL80211_CMD_GET_STATION, NL80211_CMD_NEW_STATION, stationRequest, stationReply)) {
            const auto *stationInfo = unique(stationReply, NL80211_ATTR_STA_INFO);
            if (matching(stationReply, index) && unique(stationReply, NL80211_ATTR_MAC)
                && *unique(stationReply, NL80211_ATTR_MAC) == peer && stationInfo && parseAttributes(*stationInfo, station)) {
                put(result, "signal", number<qint8>(station, NL80211_STA_INFO_SIGNAL), source);
                rate(result, station, NL80211_STA_INFO_TX_BITRATE, "txrate", "txstreams", source);
                rate(result, station, NL80211_STA_INFO_RX_BITRATE, "rxrate", "rxstreams", source);
            }
        } else for (const char *key : {"signal", "txrate", "rxrate", "txstreams", "rxstreams"})
            put(result, key, failure(linkQuery.error()), source);
    }
    // Reject a changed interface/channel/SSID, and daemon metadata that roamed
    // while its properties were being collected. Rates remain a point-in-time report.
    bool changed = if_nametoindex(name.toUtf8().constData()) != index;
    if (native) {
        NetlinkAttributes after;
        if (!linkQuery.request(id, NL80211_CMD_GET_INTERFACE, NL80211_CMD_NEW_INTERFACE, interfaceRequest, after) || !matching(after, index)) changed = true;
        else for (int type : {NL80211_ATTR_IFTYPE, NL80211_ATTR_MAC, NL80211_ATTR_SSID, NL80211_ATTR_WIPHY_FREQ}) {
            const auto *beforeValue = unique(info, type), *afterValue = unique(after, type);
            if ((beforeValue == nullptr) != (afterValue == nullptr) || (beforeValue && *beforeValue != *afterValue)) changed = true;
        }
    }
#ifdef QLDM_HAVE_DBUS
    if (!devicePath.isEmpty()) {
        const QVariantMap after = bus.properties(nm, devicePath, wireless);
        if (after.isEmpty()) {
            for (auto it = result.values.begin(); it != result.values.end(); ++it)
                if (result.sources.value(it.key()).startsWith(nm) || result.sources.value(it.key()).startsWith(supplicant))
                    it.value() = bus.error.state == ReadState::Available ? failure(ENODATA) : bus.error;
        } else if (after.value("ActiveAccessPoint") != wifi.value("ActiveAccessPoint")
                   || after.value("Mode") != wifi.value("Mode")) changed = true;
        const QVariantMap afterDevice = bus.properties(nm, devicePath, nm + ".Device");
        if (afterDevice.isEmpty()) {
            for (auto it = result.values.begin(); it != result.values.end(); ++it)
                if (result.sources.value(it.key()).startsWith(nm) || result.sources.value(it.key()).startsWith(supplicant))
                    it.value() = bus.error.state == ReadState::Available ? failure(ENODATA) : bus.error;
        } else if (afterDevice.value("State") != device.value("State")
            || afterDevice.value("ActiveConnection") != device.value("ActiveConnection")) changed = true;
        if (device.contains("State") && device.value("State").toUInt() != 100) {
            for (const char *key : {"ssid", "bssid", "signal", "txrate", "rxrate", "txstreams", "rxstreams", "security"})
                put(result, key, {ReadState::NotApplicable, {}, 0}, nm + " : interface has no activated connection");
        }
    }
#endif
    if (mode == NL80211_IFTYPE_AP) {
        if (native && ownMac.size() == 6) put(result, "bssid", available(mac(ownMac)), source);
        for (const char *key : {"signal", "txrate", "rxrate", "txstreams", "rxstreams", "nmrate"})
            put(result, key, {ReadState::NotApplicable, {}, 0}, source + "; hotspot rates are per client");
    }
    if (changed) result.invalidate(failure(EAGAIN));
    return result;
}

QString wirelessPropertyText(const QString &key, const Attribute &value)
{
    const auto tr = [](const char *text) { return QCoreApplication::translate("Wireless", text); };
    if (value.state != ReadState::Available) {
        if (value.state == ReadState::Error && value.error == EAGAIN) return tr("Connection changed; reload properties");
        return propertyReadValue(value);
    }
    const uint number = value.value.toUInt();
    if (key == "connection") {
        if (number == 100) return tr("Active");
        if (number == 30) return tr("Disconnected");
        if (number == 20) return tr("Unavailable");
        if (number == 10) return tr("Unmanaged");
        if (number >= 40 && number <= 90) return tr("Connecting / configuring");
        if (number == 110) return tr("Disconnecting");
        if (number == 120) return tr("Connection failed");
        return tr("Unknown (%1)").arg(number);
    }
    if (key == "mode") {
        switch (number) {
        case NL80211_IFTYPE_STATION: return tr("Client");
        case NL80211_IFTYPE_AP: return tr("Hotspot / access point");
        case NL80211_IFTYPE_ADHOC: return tr("Ad hoc");
        case NL80211_IFTYPE_MONITOR: return tr("Monitor");
        case NL80211_IFTYPE_MESH_POINT: return tr("Mesh");
        case NL80211_IFTYPE_P2P_CLIENT: return tr("Wi-Fi Direct client");
        case NL80211_IFTYPE_P2P_GO: return tr("Wi-Fi Direct group owner");
        default: return tr("Other mode (%1)").arg(number);
        }
    }
    if (key == "security") {
        if (value.value == "SAE" || value.value == "FT-SAE") return tr("WPA3-Personal (%1)").arg(value.value);
        if (value.value == "WPA2-PSK") return tr("WPA2-Personal (PSK)");
        if (value.value == "WPA-PSK") return tr("WPA-Personal (PSK)");
        if (value.value == "WPA2-EAP") return tr("WPA2-Enterprise (EAP)");
        if (value.value == "NONE") return tr("No WPA authentication (open or legacy security)");
    }
    if (key == "securityconfig") {
        if (value.value == "sae") return tr("WPA3-Personal (SAE)");
        if (value.value == "wpa-psk") return tr("WPA/WPA2-Personal (PSK)");
        if (value.value == "wpa-eap") return tr("WPA/WPA2-Enterprise (EAP)");
        if (value.value == "owe") return tr("Enhanced Open (OWE)");
    }
    if (key == "quality") return tr("%1%").arg(value.value);
    if (key == "txrate" || key == "rxrate" || key == "nmrate")
        return tr("%1 Mb/s").arg(QLocale().toString(value.value.toDouble() / (key == "nmrate" ? 1000 : 10), 'f', 1));
    if (key == "signal") return tr("%1 dBm").arg(value.value);
    if (key == "txpower") return tr("%1 dBm").arg(QLocale().toString(value.value.toDouble() / 100, 'f', 2));
    if (key == "width") {
        switch (number) {
        case NL80211_CHAN_WIDTH_20_NOHT: return tr("20 MHz (non-HT)");
        case NL80211_CHAN_WIDTH_20: return QStringLiteral("20 MHz");
        case NL80211_CHAN_WIDTH_40: return QStringLiteral("40 MHz");
        case NL80211_CHAN_WIDTH_80: return QStringLiteral("80 MHz");
        case NL80211_CHAN_WIDTH_80P80: return QStringLiteral("80 + 80 MHz");
        case NL80211_CHAN_WIDTH_160: return QStringLiteral("160 MHz");
#ifdef NL80211_EHT_MIN_CAPABILITY_LEN
        case NL80211_CHAN_WIDTH_320: return QStringLiteral("320 MHz");
#endif
        case NL80211_CHAN_WIDTH_5: return QStringLiteral("5 MHz");
        case NL80211_CHAN_WIDTH_10: return QStringLiteral("10 MHz");
        case NL80211_CHAN_WIDTH_1: return QStringLiteral("1 MHz");
        case NL80211_CHAN_WIDTH_2: return QStringLiteral("2 MHz");
        case NL80211_CHAN_WIDTH_4: return QStringLiteral("4 MHz");
        case NL80211_CHAN_WIDTH_8: return QStringLiteral("8 MHz");
        case NL80211_CHAN_WIDTH_16: return QStringLiteral("16 MHz");
        default: return tr("Unrecognized channel width (%1)").arg(number);
        }
    }
    if (key == "frequency") {
        uint channel = 0;
        QString band;
        if (number == 2484) { channel = 14; band = QStringLiteral("2.4 GHz"); }
        else if (number >= 2412 && number <= 2472 && (number - 2407) % 5 == 0) {
            channel = (number - 2407) / 5; band = QStringLiteral("2.4 GHz");
        } else if (number >= 5005 && number < 5900 && number % 5 == 0) {
            channel = (number - 5000) / 5; band = QStringLiteral("5 GHz");
        } else if (number == 5935) { channel = 2; band = QStringLiteral("6 GHz"); }
        else if (number >= 5955 && number <= 7115 && number % 5 == 0) {
            channel = (number - 5950) / 5; band = QStringLiteral("6 GHz");
        }
        return channel ? tr("%1 MHz — %2, channel %3").arg(QLocale().toString(number), band, QString::number(channel))
            : tr("%1 MHz").arg(QLocale().toString(number));
    }
    return value.value;
}
