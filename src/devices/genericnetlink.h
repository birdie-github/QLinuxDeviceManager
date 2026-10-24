#pragma once
// Bounded single-reply generic-netlink transport shared by Ethernet and Wi-Fi.
#include <QByteArray>
#include <QElapsedTimer>
#include <QThread>
#include <QVector>
#include <cerrno>
#include <cstring>
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace GenericNetlink {
constexpr int replyLimit = 65536;
constexpr int genlHeaderSize = static_cast<int>(GENL_HDRLEN);
constexpr int budgetMs = 2000;
struct NetlinkAttribute {
    int type;
    QByteArray data;
};
using NetlinkAttributes = QVector<NetlinkAttribute>;

inline bool parseAttributes(const QByteArray &bytes, NetlinkAttributes &items)
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

inline const QByteArray *findUniqueAttribute(const NetlinkAttributes &items, int type, bool &valid)
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

inline void appendAttribute(QByteArray &bytes, int type, const QByteArray &value)
{
    const int offset = static_cast<int>(bytes.size());
    const int length = NLA_HDRLEN + static_cast<int>(value.size());
    bytes.resize(offset + NLA_ALIGN(length));
    std::memset(bytes.data() + offset, 0, static_cast<size_t>(NLA_ALIGN(length)));
    const nlattr header{static_cast<__u16>(length), static_cast<__u16>(type)};
    std::memcpy(bytes.data() + offset, &header, sizeof(header));
    std::memcpy(bytes.data() + offset + NLA_HDRLEN, value.constData(), static_cast<size_t>(value.size()));
}

// A private, nonblocking request socket; no dumps or notification subscriptions.
class Query {
public:
    Query() : fd_(socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC | SOCK_NONBLOCK, NETLINK_GENERIC))
    {
        timer_.start();
        if (fd_ < 0)
            error_ = errno;
    }
    ~Query()
    {
        if (fd_ >= 0)
            close(fd_);
    }
    Query(const Query &) = delete;
    Query &operator=(const Query &) = delete;
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
        generic.version = 1;
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

} // namespace GenericNetlink
