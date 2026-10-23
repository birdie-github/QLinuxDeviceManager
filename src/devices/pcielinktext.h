#pragma once
#include <QCoreApplication>
#include <QLocale>
#include <QString>
#include <cmath>

// Signalling rate -> generation. Unknown/future rates remain visible rather
// than being rounded or mistaken for measured application throughput.
inline QString pcieLinkText(double rate, int lanes = 0)
{
    if (!std::isfinite(rate) || rate <= 0) return QCoreApplication::translate("PcieLinkText", "Unavailable");
    constexpr double rates[] {2.5, 5, 8, 16, 32, 64, 128};
    int generation = 0;
    int candidate = 0;
    for (double known : rates) {
        ++candidate;
        if (rate == known) { generation = candidate; break; }
    }
    QString result = generation ? QCoreApplication::translate("PcieLinkText", "PCIe Gen %1").arg(generation)
        : QCoreApplication::translate("PcieLinkText", "%1 GT/s per lane (generation unknown)").arg(QLocale().toString(rate, 'g', 5));
    if (lanes > 0) result += QCoreApplication::translate("PcieLinkText", " ×%1 (%2 lanes)").arg(lanes).arg(QLocale().toString(lanes));
    return result;
}
