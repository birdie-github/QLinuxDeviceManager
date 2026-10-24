#include "ethernetproperties.h"
#include "genericnetlink.h"
#include <QByteArray>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <cerrno>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <net/if.h>

namespace {
constexpr unsigned int bitLimit = 4096;

Attribute available(const QString &value)
{
    return {ReadState::Available, value, 0};
}

Attribute failure(int error)
{
    ReadState state = ReadState::Error;
    if (error == EACCES || error == EPERM)
        state = ReadState::PermissionDenied;
    else if (error == ENODEV || error == ENXIO)
        state = ReadState::Removed;
    else if (error == EOPNOTSUPP || error == ENOSYS || error == ENOENT || error == EPROTONOSUPPORT ||
             error == EAFNOSUPPORT)
        state = ReadState::Unsupported;
    else if (error == ENODATA)
        state = ReadState::Unavailable;
    return {state, {}, error};
}

using namespace GenericNetlink;

QByteArray deviceHeader(unsigned int index)
{
    QByteArray header;
    appendAttribute(header, ETHTOOL_A_HEADER_DEV_INDEX,
                    QByteArray(reinterpret_cast<const char *>(&index), sizeof(index)));
    return header;
}

bool matchesDevice(const NetlinkAttributes &items, int headerType, unsigned int index)
{
    bool valid = true;
    const QByteArray *header = findUniqueAttribute(items, headerType, valid);
    NetlinkAttributes fields;
    if (!valid || !header || !parseAttributes(*header, fields))
        return false;
    __u32 found = 0;
    const QByteArray *value = findUniqueAttribute(fields, ETHTOOL_A_HEADER_DEV_INDEX, valid);
    return valid && readNumber(value, found) && found == index;
}

Attribute readIntegerAttribute(const NetlinkAttributes &items, int type, bool byte)
{
    bool valid = true;
    const QByteArray *value = findUniqueAttribute(items, type, valid);
    if (!valid)
        return failure(EPROTO);
    if (!value)
        return failure(ENODATA);
    __u8 small = 0;
    __u32 large = 0;
    if (byte ? !readNumber(value, small) : !readNumber(value, large))
        return failure(EPROTO);
    return available(QString::number(byte ? small : large));
}

Attribute readLinkModes(const NetlinkAttributes &items, int type, bool supported)
{
    bool valid = true;
    const QByteArray *data = findUniqueAttribute(items, type, valid);
    if (!valid)
        return failure(EPROTO);
    if (!data)
        return failure(ENODATA);
    NetlinkAttributes fields;
    if (!parseAttributes(*data, fields))
        return failure(EPROTO);
    const QByteArray *nomask = findUniqueAttribute(fields, ETHTOOL_A_BITSET_NOMASK, valid);
    const QByteArray *sizeData = findUniqueAttribute(fields, ETHTOOL_A_BITSET_SIZE, valid);
    const QByteArray *bits = findUniqueAttribute(fields, ETHTOOL_A_BITSET_BITS, valid);
    if (!valid || !bits || (nomask && !nomask->isEmpty()) || (supported && nomask))
        return failure(EPROTO);
    __u32 size = bitLimit;
    if (sizeData && !readNumber(sizeData, size))
        return failure(EPROTO);
    if (size > bitLimit)
        return failure(EOVERFLOW);
    NetlinkAttributes entries;
    if (!parseAttributes(*bits, entries))
        return failure(EPROTO);
    if (entries.size() > static_cast<qsizetype>(bitLimit))
        return failure(EOVERFLOW);
    QSet<__u32> seen;
    QStringList names;
    for (const NetlinkAttribute &entry : entries) {
        if (QThread::currentThread()->isInterruptionRequested())
            return failure(ECANCELED);
        if (entry.type != ETHTOOL_A_BITSET_BITS_BIT)
            return failure(EPROTO);
        NetlinkAttributes bit;
        if (!parseAttributes(entry.data, bit))
            return failure(EPROTO);
        __u32 index = 0;
        const QByteArray *indexData = findUniqueAttribute(bit, ETHTOOL_A_BITSET_BIT_INDEX, valid);
        const QByteArray *name = findUniqueAttribute(bit, ETHTOOL_A_BITSET_BIT_NAME, valid);
        const QByteArray *set = findUniqueAttribute(bit, ETHTOOL_A_BITSET_BIT_VALUE, valid);
        if (!valid || !readNumber(indexData, index) || index >= size || seen.contains(index) ||
            (set && !set->isEmpty()))
            return failure(EPROTO);
        seen.insert(index);
        QString label;
        if (name) {
            if (name->isEmpty() || name->size() > 128 || !name->endsWith('\0') ||
                name->indexOf('\0') != name->size() - 1)
                return failure(EPROTO);
            label = QString::fromUtf8(name->constData(), static_cast<int>(name->size()) - 1);
        }
        if (label.isEmpty())
            label = QStringLiteral("bit %1").arg(index);
        // OURS mask = supported bits (every listed entry); PEER NOMASK =
        // advertised bits (every entry). In value/mask form only VALUE bits apply.
        if (supported || nomask || set)
            names.append(label);
    }
    return names.isEmpty() ? failure(ENODATA) : available(names.join('\n'));
}

Attribute queryCarrier(Query &query, __u16 id, const QByteArray &stateRequest, unsigned int index)
{
    NetlinkAttributes state;
    if (query.request(id, ETHTOOL_MSG_LINKSTATE_GET, ETHTOOL_MSG_LINKSTATE_GET_REPLY, stateRequest, state)) {
        return matchesDevice(state, ETHTOOL_A_LINKSTATE_HEADER, index)
                   ? readIntegerAttribute(state, ETHTOOL_A_LINKSTATE_LINK, true)
                   : failure(EPROTO);
    } else
        return failure(query.error());
}

void collectLinkSettings(Query &query, __u16 id, const QByteArray &modesRequest, unsigned int index,
                         EthernetProperties &result)
{
    NetlinkAttributes settings;
    if (query.request(id, ETHTOOL_MSG_LINKMODES_GET, ETHTOOL_MSG_LINKMODES_GET_REPLY, modesRequest,
                      settings)) {
        if (!matchesDevice(settings, ETHTOOL_A_LINKMODES_HEADER, index)) {
            result.speed = result.duplex = result.autoneg = result.supportedModes = result.partnerModes =
                failure(EPROTO);
        } else {
            result.speed = readIntegerAttribute(settings, ETHTOOL_A_LINKMODES_SPEED, false);
            result.duplex = readIntegerAttribute(settings, ETHTOOL_A_LINKMODES_DUPLEX, true);
            result.autoneg = readIntegerAttribute(settings, ETHTOOL_A_LINKMODES_AUTONEG, true);
            result.supportedModes = readLinkModes(settings, ETHTOOL_A_LINKMODES_OURS, true);
            result.partnerModes = readLinkModes(settings, ETHTOOL_A_LINKMODES_PEER, false);
        }
    } else
        result.speed = result.duplex = result.autoneg = result.supportedModes = result.partnerModes =
            failure(query.error());
}

// Carrier and autonegotiation determine which negotiated values are meaningful.
void applyLinkAvailability(EthernetProperties &result)
{
    if (result.carrier.state == ReadState::Available && result.carrier.value == "0") {
        result.speed = result.duplex = result.partnerModes = {ReadState::NotApplicable, {}, 0};
    } else if (result.carrier.state != ReadState::Available) {
        result.speed = result.duplex = result.partnerModes = result.carrier;
    }
    if (result.autoneg.state == ReadState::Available && result.autoneg.value == "0")
        result.partnerModes = {ReadState::NotApplicable, {}, 0};
}
} // namespace

EthernetProperties collectEthernetProperties(const QString &name, unsigned int index)
{
    EthernetProperties result;
    result.applicable = true;
    result.scope = available(QString());
    result.source = QStringLiteral("ethtool generic netlink: LINKSTATE_GET / LINKMODES_GET; ifindex %1 (%2)")
                        .arg(index)
                        .arg(name);
    const QByteArray encoded = name.toUtf8();
    if (encoded.isEmpty() || encoded.size() >= IFNAMSIZ || encoded.contains('\0') || !index) {
        result.invalidate(failure(EINVAL));
        return result;
    }
    if (if_nametoindex(encoded.constData()) != index) {
        result.invalidate(failure(ENODEV));
        return result;
    }
    Query query;
    QByteArray lookup;
    appendAttribute(lookup, CTRL_ATTR_FAMILY_NAME, QByteArray(ETHTOOL_GENL_NAME, sizeof(ETHTOOL_GENL_NAME)));
    NetlinkAttributes family;
    if (!query.request(GENL_ID_CTRL, CTRL_CMD_GETFAMILY, CTRL_CMD_NEWFAMILY, lookup, family)) {
        result.invalidate(failure(query.error()));
        return result;
    }
    bool valid = true;
    __u16 id = 0;
    const QByteArray *idData = findUniqueAttribute(family, CTRL_ATTR_FAMILY_ID, valid);
    if (!valid || !readNumber(idData, id) || id < GENL_MIN_ID) {
        result.invalidate(failure(EPROTO));
        return result;
    }
    QByteArray stateRequest, modesRequest;
    appendAttribute(stateRequest, ETHTOOL_A_LINKSTATE_HEADER | NLA_F_NESTED, deviceHeader(index));
    appendAttribute(modesRequest, ETHTOOL_A_LINKMODES_HEADER | NLA_F_NESTED, deviceHeader(index));
    result.carrier = queryCarrier(query, id, stateRequest, index);
    // Settings and carrier support are independent; a failed carrier query
    // must not hide available capabilities or autonegotiation configuration.
    collectLinkSettings(query, id, modesRequest, index, result);
    if (result.carrier.state == ReadState::Available) {
        const Attribute afterCarrier = queryCarrier(query, id, stateRequest, index);
        if (afterCarrier.state != ReadState::Available)
            result.invalidate(afterCarrier);
        else if (afterCarrier.value != result.carrier.value)
            result.invalidate(failure(EAGAIN));
    }
    applyLinkAvailability(result);
    if (if_nametoindex(encoded.constData()) != index)
        result.invalidate(failure(ENODEV));
    return result;
}
