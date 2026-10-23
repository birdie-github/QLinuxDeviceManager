#pragma once
#include "device.h"
#include <QByteArray>

namespace DisplayIdentity {
constexpr int edidLimit = 128 * 256; // Base block plus the maximum extension count.
bool validEdid(const QByteArray &bytes);
struct Match { Attribute status; int output = -1; };
// Fallback only: the complete EDID must identify exactly one connected monitor
// on each side. Missing identities or duplicate descriptors cannot prove a match.
Match matchEdid(const QByteArray &selected, const QVector<QByteArray> &kernel,
                const QVector<QByteArray> &session);
}
