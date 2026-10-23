#pragma once
#include <QByteArray>
#include <QRegularExpression>
#include <QString>

// Speed and width remain paired observations, never independently combined maxima.
struct GpuLinkSample {
    double speed = 0;
    int width = 0;
    bool exceeds(const GpuLinkSample &other) const {
        return speed > other.speed || (speed == other.speed && width > other.width);
    }
};
inline bool parseGpuLinkSample(const QByteArray &speed, const QByteArray &width, GpuLinkSample &result)
{
    static const QRegularExpression speedPattern(QStringLiteral("^([0-9]+(?:\\.[0-9]+)?) GT/s PCIe$"));
    static const QRegularExpression widthPattern(QStringLiteral("^[0-9]+$"));
    const auto match = speedPattern.match(QString::fromLatin1(speed));
    if (!match.hasMatch() || !widthPattern.match(QString::fromLatin1(width)).hasMatch()) return false;
    bool speedOk = false, widthOk = false;
    const double rate = match.captured(1).toDouble(&speedOk);
    const int lanes = width.toInt(&widthOk);
    // Input sanity bounds, not a declaration of supported PCIe generations.
    if (!speedOk || !widthOk || rate <= 0 || rate > 1024 || lanes <= 0 || lanes > 64) return false;
    result = {rate, lanes};
    return true;
}
