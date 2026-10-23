#include "ethernetproperties.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>
#include <QThread>
#include <QVector>
#include <cerrno>
#include <cstring>
#include <linux/ethtool.h>
#include <linux/ethtool_netlink.h>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <net/if.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {
constexpr int replyLimit = 65536;
constexpr int genlHeaderSize = static_cast<int>(GENL_HDRLEN);
constexpr unsigned int bitLimit = 4096;
constexpr int budgetMs = 2000; // Shared by family lookup and all replies for an interface.

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

struct NetlinkAttribute {
    int type;
    QByteArray data;
};
using NetlinkAttributes = QVector<NetlinkAttribute>;

bool parseAttributes(const QByteArray &bytes, NetlinkAttributes &items)
{
    for (int offset = 0; offset < bytes.size();) {
        if (bytes.size() - offset < NLA_HDRLEN)
            return false;
        nlattr header{};
        std::memcpy(&header, bytes.constData() + offset, sizeof(header));
        const int length = header.nla_len;
        if (length < NLA_HDRLEN || length > bytes.size() - offset ||
            NLA_ALIGN(length) > bytes.size() - offset)
            return false;
        items.append({header.nla_type & NLA_TYPE_MASK, bytes.mid(offset + NLA_HDRLEN, length - NLA_HDRLEN)});
        offset += NLA_ALIGN(length);
    }
    return true;
}

const QByteArray *findUniqueAttribute(const NetlinkAttributes &items, int type, bool &valid)
{
    const QByteArray *found = nullptr;
    for (const NetlinkAttribute &entry : items)
        if (entry.type == type) {
            if (found) {
                valid = false;
                return nullptr;
            }
            found = &entry.data;
        }
    return found;
}

template <typename T> bool readNumber(const QByteArray *data, T &value)
{
    if (!data || data->size() != static_cast<qsizetype>(sizeof(T)))
        return false;
    std::memcpy(&value, data->constData(), sizeof(T));
    return true;
}

void appendAttribute(QByteArray &bytes, int type, const QByteArray &value)
{
    const int offset = static_cast<int>(bytes.size());
    const int length = NLA_HDRLEN + static_cast<int>(value.size());
    bytes.resize(offset + NLA_ALIGN(length));
    std::memset(bytes.data() + offset, 0, static_cast<size_t>(NLA_ALIGN(length)));
    const nlattr header{static_cast<__u16>(length), static_cast<__u16>(type)};
    std::memcpy(bytes.data() + offset, &header, sizeof(header));
    std::memcpy(bytes.data() + offset + NLA_HDRLEN, value.constData(), static_cast<size_t>(value.size()));
}

QByteArray deviceHeader(unsigned int index)
{
    QByteArray header;
    appendAttribute(header, ETHTOOL_A_HEADER_DEV_INDEX,
                    QByteArray(reinterpret_cast<const char *>(&index), sizeof(index)));
    return header;
}

// A private, nonblocking request socket; no dumps or notification subscriptions.
class EthtoolQuery {
public:
    EthtoolQuery() : fd_(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_GENERIC))
    {
        timer_.start();
        if (fd_ < 0)
            error_ = errno;
    }
    ~EthtoolQuery()
    {
        if (fd_ >= 0)
            close(fd_);
    }
    EthtoolQuery(const EthtoolQuery &) = delete;
    EthtoolQuery &operator=(const EthtoolQuery &) = delete;
    int error() const
    {
        return error_;
    }
    bool request(__u16 family, __u8 command, __u8 replyCommand, const QByteArray &requestAttributes,
                 NetlinkAttributes &result)
    {
        if (fd_ < 0)
            return false;
        error_ = 0;
        if (timer_.elapsed() >= budgetMs) {
            error_ = ETIMEDOUT;
            return false;
        }
        if (QThread::currentThread()->isInterruptionRequested()) {
            error_ = ECANCELED;
            return false;
        }
        if (!sendRequest(family, command, requestAttributes))
            return false;
        return receiveReply(family, replyCommand, result);
    }

private:
    enum class ReplyStatus { Ignored, Received, Failed };

    bool sendRequest(__u16 family, __u8 command, const QByteArray &requestAttributes)
    {
        QByteArray message(NLMSG_HDRLEN + genlHeaderSize, '\0');
        message.append(requestAttributes);
        nlmsghdr header{};
        header.nlmsg_len = static_cast<__u32>(message.size());
        header.nlmsg_type = family;
        header.nlmsg_flags = NLM_F_REQUEST;
        header.nlmsg_seq = ++sequence_;
        genlmsghdr generic{};
        generic.cmd = command;
        generic.version = family == GENL_ID_CTRL ? 1 : ETHTOOL_GENL_VERSION;
        std::memcpy(message.data(), &header, sizeof(header));
        std::memcpy(message.data() + NLMSG_HDRLEN, &generic, sizeof(generic));
        sockaddr_nl kernel{};
        kernel.nl_family = AF_NETLINK;
        const ssize_t sent = sendto(fd_, message.constData(), static_cast<size_t>(message.size()), 0,
                                    reinterpret_cast<const sockaddr *>(&kernel), sizeof(kernel));
        if (sent != message.size()) {
            error_ = sent < 0 ? errno : EIO;
            return false;
        }
        return true;
    }

    // Ignore unrelated sequences and zero-error acknowledgements, but reject malformed replies.
    ReplyStatus parseReply(const QByteArray &buffer, ssize_t count, __u16 family, __u8 replyCommand,
                           NetlinkAttributes &result)
    {
        for (int offset = 0; offset < count;) {
            if (count - offset < NLMSG_HDRLEN) {
                error_ = EPROTO;
                return ReplyStatus::Failed;
            }
            nlmsghdr response{};
            std::memcpy(&response, buffer.constData() + offset, sizeof(response));
            if (response.nlmsg_len < NLMSG_HDRLEN ||
                static_cast<ssize_t>(response.nlmsg_len) > count - offset ||
                static_cast<ssize_t>(NLMSG_ALIGN(response.nlmsg_len)) > count - offset) {
                error_ = EPROTO;
                return ReplyStatus::Failed;
            }
            const char *data = buffer.constData() + offset + NLMSG_HDRLEN;
            const int length = static_cast<int>(response.nlmsg_len) - NLMSG_HDRLEN;
            offset += static_cast<int>(NLMSG_ALIGN(response.nlmsg_len));
            if (response.nlmsg_seq != sequence_)
                continue;
            if (response.nlmsg_type == NLMSG_ERROR) {
                int code = 0;
                if (length < static_cast<int>(sizeof(nlmsgerr))) {
                    error_ = EPROTO;
                    return ReplyStatus::Failed;
                }
                std::memcpy(&code, data, sizeof(code));
                if (code > 0 || code < -4095) {
                    error_ = EPROTO;
                    return ReplyStatus::Failed;
                }
                if (!code)
                    continue;
                error_ = -code;
                return ReplyStatus::Failed;
            }
            if (response.nlmsg_type != family || response.nlmsg_flags & (NLM_F_MULTI | NLM_F_DUMP_INTR) ||
                length < genlHeaderSize) {
                error_ = EPROTO;
                return ReplyStatus::Failed;
            }
            genlmsghdr reply{};
            std::memcpy(&reply, data, sizeof(reply));
            if (reply.cmd != replyCommand ||
                !parseAttributes(QByteArray(data + genlHeaderSize, length - genlHeaderSize), result)) {
                error_ = EPROTO;
                return ReplyStatus::Failed;
            }
            return ReplyStatus::Received;
        }
        return ReplyStatus::Ignored;
    }

    bool receiveReply(__u16 family, __u8 replyCommand, NetlinkAttributes &result)
    {
        QByteArray buffer(replyLimit, '\0');
        while (timer_.elapsed() < budgetMs) {
            if (QThread::currentThread()->isInterruptionRequested()) {
                error_ = ECANCELED;
                return false;
            }
            pollfd ready{fd_, POLLIN, 0};
            const int remaining = budgetMs - static_cast<int>(timer_.elapsed());
            const int status = poll(&ready, 1, qMin(50, qMax(0, remaining)));
            if (status < 0) {
                if (errno == EINTR)
                    continue;
                error_ = errno;
                return false;
            }
            if (!status)
                continue;
            if (ready.revents & (POLLERR | POLLHUP | POLLNVAL)) {
                error_ = EIO;
                return false;
            }
            sockaddr_nl sender{};
            socklen_t size = sizeof(sender);
            const ssize_t count = recvfrom(fd_, buffer.data(), replyLimit, MSG_TRUNC,
                                           reinterpret_cast<sockaddr *>(&sender), &size);
            if (count < 0) {
                if (errno == EAGAIN || errno == EINTR)
                    continue;
                error_ = errno;
                return false;
            }
            if (count > replyLimit) {
                error_ = EOVERFLOW;
                return false;
            }
            if (size != sizeof(sender) || sender.nl_family != AF_NETLINK || sender.nl_pid != 0)
                continue;
            const ReplyStatus replyStatus = parseReply(buffer, count, family, replyCommand, result);
            if (replyStatus == ReplyStatus::Received)
                return true;
            if (replyStatus == ReplyStatus::Failed)
                return false;
        }
        error_ = ETIMEDOUT;
        return false;
    }

    int fd_;
    int error_ = 0;
    __u32 sequence_ = 0;
    QElapsedTimer timer_;
};

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

Attribute queryCarrier(EthtoolQuery &query, __u16 id, const QByteArray &stateRequest, unsigned int index)
{
    NetlinkAttributes state;
    if (query.request(id, ETHTOOL_MSG_LINKSTATE_GET, ETHTOOL_MSG_LINKSTATE_GET_REPLY, stateRequest, state)) {
        return matchesDevice(state, ETHTOOL_A_LINKSTATE_HEADER, index)
                   ? readIntegerAttribute(state, ETHTOOL_A_LINKSTATE_LINK, true)
                   : failure(EPROTO);
    } else
        return failure(query.error());
}

void collectLinkSettings(EthtoolQuery &query, __u16 id, const QByteArray &modesRequest, unsigned int index,
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
    EthtoolQuery query;
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
