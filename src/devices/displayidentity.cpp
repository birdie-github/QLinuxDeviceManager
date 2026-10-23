#include "displayidentity.h"
#include <cerrno>

namespace DisplayIdentity {
bool validEdid(const QByteArray &bytes)
{
    if (bytes.size() < 128 || bytes.size() > edidLimit
        || bytes.left(8) != QByteArray::fromHex("00ffffffffffff00")
        || bytes.size() != 128 * (1 + static_cast<unsigned char>(bytes[126]))) return false;
    for (qsizetype block = 0; block < bytes.size(); block += 128) {
        unsigned sum = 0;
        for (qsizetype i = block; i < block + 128; ++i) sum += static_cast<unsigned char>(bytes[i]);
        if (sum & 255) return false;
    }
    return true;
}
Match matchEdid(const QByteArray &selected, const QVector<QByteArray> &kernel,
                const QVector<QByteArray> &session)
{
    if (!validEdid(selected)) return {};
    int kernelMatches = 0, sessionMatches = 0, index = -1;
    for (const QByteArray &edid : kernel) {
        if (!validEdid(edid)) return {};
        if (edid == selected) ++kernelMatches;
    }
    for (qsizetype i = 0; i < session.size(); ++i) {
        if (!validEdid(session[i])) return {};
        if (session[i] == selected) { ++sessionMatches; index = static_cast<int>(i); }
    }
    if (kernelMatches > 1 || sessionMatches > 1)
        return {{ReadState::Error, {}, ENOTUNIQ}, -1};
    if (kernelMatches != 1 || sessionMatches != 1) return {};
    return {{ReadState::Available, QStringLiteral("edid"), 0}, index};
}
}
