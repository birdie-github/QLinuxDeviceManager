#pragma once
#include "device.h"
#include <QMap>
#include <QSet>
#include <QStringList>
#include <algorithm>

struct CpuTopologyRecord {
    int cpu = 0;
    QString package;
    QString core; // Package/die/core identity, not a logical CPU number.
    int kind = 0; // 0 unknown/general, 1 Intel Core PMU, 2 Intel Atom PMU.
    Attribute maximumKhz;
};
struct CpuCacheRecord {
    int level = 0;
    QString type;
    qulonglong bytes = 0;
    QSet<int> cpus; // Full kernel sharing set, including any offline siblings.
};
struct CpuCoreGroup {
    QVector<CpuTopologyRecord> members;
};

inline QString cpuListText(const QSet<int> &cpus)
{
    QList<int> sorted = cpus.values();
    std::sort(sorted.begin(), sorted.end());
    QStringList ranges;
    for (qsizetype i = 0; i < sorted.size();) {
        const int first = sorted[i];
        int last = first;
        while (++i < sorted.size() && sorted[i] == last + 1) last = sorted[i];
        ranges.append(first == last ? QString::number(first)
                                   : QStringLiteral("%1-%2").arg(first).arg(last));
    }
    return ranges.join(',');
}

// Cache geometry and sharing distinguish groups; clock differences do not split
// otherwise identical cores. No LP-E classification is inferred from this key.
inline QVector<CpuCoreGroup> groupCpuTopology(QVector<CpuTopologyRecord> records,
                                               const QVector<CpuCacheRecord> &caches)
{
    std::sort(records.begin(), records.end(), [](const auto &a, const auto &b) { return a.cpu < b.cpu; });
    QMap<int, QStringList> profiles;
    for (const auto &cache : caches) {
        const QString shape = QStringLiteral("%1:%2:%3:%4")
            .arg(cache.level).arg(cache.type).arg(cache.bytes).arg(cache.cpus.size());
        for (int cpu : cache.cpus) profiles[cpu].append(shape);
    }
    QMap<QString, qsizetype> indexes;
    QVector<CpuCoreGroup> groups;
    for (const auto &record : records) {
        QStringList profile = profiles.value(record.cpu);
        profile.sort();
        const QString key = record.package + '/' + QString::number(record.kind) + '/' + profile.join(';');
        if (!indexes.contains(key)) {
            indexes.insert(key, groups.size());
            groups.append(CpuCoreGroup{});
        }
        groups[indexes.value(key)].members.append(record);
    }
    return groups;
}
