#include "systemreport.h"
#include "commonproperties.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <limits>

namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(SystemDialog) };
}

QString systemProcessorValue(const SystemProperties &snapshot, const QString &key, const Attribute &value, bool compact)
{
    if (key == "logical") {
        const Attribute online = snapshot.values.value("online");
        if (value.state == ReadState::Available && online.state == ReadState::Available && value.value == online.value)
            return value.value;
        return Text::tr("%1 present; %2 online").arg(propertyReadValue(value), propertyReadValue(online));
    }
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "smt") {
        if (value.value == "on") return Text::tr("Enabled (kernel policy)");
        if (value.value == "off") return Text::tr("Disabled");
        if (value.value == "forceoff") return Text::tr("Disabled by system policy");
        if (value.value == "notsupported") return Text::tr("Not supported");
        if (value.value == "notimplemented") return Text::tr("Status reporting not implemented by kernel");
        return Text::tr("Unknown (%1)").arg(value.value);
    }
    // Tab-separated cells are shared by the native views and Copy All.
    const auto tableText = [](const QVector<QStringList> &rows) {
        QStringList lines;
        for (const auto &row : rows) lines.append(row.join('\t'));
        return lines.join('\n');
    };
    if (key == "core_groups") {
        QVector<QStringList> rows {{Text::tr("Core group"), Text::tr("Cores / threads"), Text::tr("Maximum frequency"), Text::tr("Logical CPUs")}};
        const Attribute sockets = snapshot.values.value("sockets");
        const bool showPackage = sockets.state != ReadState::Available || sockets.value.toUInt() != 1;
        for (const CpuCoreGroup &group : snapshot.cpuGroups) {
            if (group.members.isEmpty()) continue;
            const auto &first = group.members.constFirst();
            const QString kind = first.kind == 1 ? Text::tr("Performance cores")
                : first.kind == 2 ? Text::tr("Efficiency cores") : Text::tr("Cores (type unspecified)");
            QSet<int> cpus;
            QMap<QString, int> threads;
            qulonglong minimum = std::numeric_limits<qulonglong>::max(), maximum = 0;
            Attribute frequencyError;
            bool frequencyComplete = true;
            for (const auto &member : group.members) {
                cpus.insert(member.cpu);
                ++threads[member.core];
                if (member.maximumKhz.state != ReadState::Available) {
                    if (frequencyComplete) frequencyError = member.maximumKhz;
                    frequencyComplete = false;
                } else {
                    const qulonglong khz = member.maximumKhz.value.toULongLong();
                    minimum = qMin(minimum, khz);
                    maximum = qMax(maximum, khz);
                }
            }
            const auto ghz = [](qulonglong khz) { return QLocale().toString(khz / 1000000.0, 'g', 6); };
            const QString frequency = !frequencyComplete ? propertyReadValue(frequencyError)
                : minimum == maximum ? Text::tr("%1 GHz").arg(ghz(maximum))
                : Text::tr("%1–%2 GHz").arg(ghz(minimum), ghz(maximum));
            const QString name = showPackage ? Text::tr("%1 (package %2)").arg(kind, first.package) : kind;
            rows.append({name, Text::tr("%1 / %2").arg(threads.size()).arg(cpus.size()), frequency, cpuListText(cpus)});
        }
        return rows.size() == 1 ? Text::tr("Unavailable") : tableText(rows);
    }
    if (key == "cache_topology") {
        // Keep each sharing domain explicit; aggregate equal-sized cache instances.
        QMap<QString, QVector<CpuCacheRecord>> kinds;
        for (const auto &cache : snapshot.cpuCaches)
            kinds[QStringLiteral("%1/%2/%3").arg(cache.level).arg(cache.type).arg(cache.bytes)].append(cache);
        QMap<int, QString> cpuCores;
        QMap<QString, QSet<int>> coreCpus;
        QSet<int> onlineCpus;
        for (const auto &group : snapshot.cpuGroups) {
            for (const auto &member : group.members) {
                cpuCores.insert(member.cpu, member.core);
                coreCpus[member.core].insert(member.cpu);
                onlineCpus.insert(member.cpu);
            }
        }
        QVector<QStringList> rows {{Text::tr("Cache"), Text::tr("Configuration"), Text::tr("Sharing (logical CPUs)")}};
        for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
            auto instances = it.value();
            std::sort(instances.begin(), instances.end(), [](const auto &a, const auto &b) {
                return *std::min_element(a.cpus.cbegin(), a.cpus.cend())
                    < *std::min_element(b.cpus.cbegin(), b.cpus.cend());
            });
            const auto &cache = instances.constFirst();
            const QString type = cache.type == "Data" ? Text::tr("data")
                : cache.type == "Instruction" ? Text::tr("instruction") : Text::tr("unified");
            QStringList sharing;
            QSet<int> privateCpus;
            bool allPrivate = true;
            for (const auto &instance : instances) {
                sharing.append(cpuListText(instance.cpus));
                allPrivate = allPrivate && instance.cpus.size() == 1;
                privateCpus.unite(instance.cpus);
            }
            QString domains = allPrivate ? Text::tr("Private to CPUs %1").arg(cpuListText(privateCpus))
                : instances.size() == 1 ? Text::tr("Shared by CPUs %1").arg(sharing.constFirst())
                : Text::tr("CPU groups %1").arg(sharing.join("; "));
            if (compact && !onlineCpus.isEmpty()) {
                // Require an exact match to a known core's online threads. Unknown
                // or offline sharing IDs keep the explicit kernel sharing sets.
                bool perCore = true;
                QSet<QString> seenCores;
                for (const auto &instance : instances) {
                    const int cpu = *instance.cpus.cbegin();
                    const QString core = cpuCores.value(cpu);
                    if (!cpuCores.contains(cpu) || instance.cpus != coreCpus.value(core)
                        || seenCores.contains(core)) {
                        perCore = false;
                        break;
                    }
                    seenCores.insert(core);
                }
                if (perCore)
                    domains = privateCpus == onlineCpus ? Text::tr("Private per core")
                        : Text::tr("Private per core · CPUs %1").arg(cpuListText(privateCpus));
                else if (instances.size() == 1 && cache.cpus == onlineCpus)
                    domains = Text::tr("Shared by all online cores");
            }
            const QString size = QLocale().formattedDataSize(static_cast<qlonglong>(cache.bytes), 0, QLocale::DataSizeIecFormat);
            rows.append({Text::tr("L%1 %2").arg(cache.level).arg(type),
                         instances.size() == 1 ? size : Text::tr("%1 × %2").arg(instances.size()).arg(size), domains});
        }
        return rows.size() == 1 ? Text::tr("Unavailable") : tableText(rows);
    }
    if (key == "instruction_sets") {
        const QStringList groups = value.value.split('\n');
        QStringList lines;
        for (const QString &group : groups) {
            const QStringList parts = group.split('\t');
            lines.append(groups.size() == 1 ? parts.value(1)
                : Text::tr("Logical CPUs %1: %2").arg(parts.value(0), parts.value(1)));
        }
        return lines.join("\n\n");
    }
    if (key == "caches") {
        QStringList lines;
        for (const QString &raw : value.value.split('\n')) {
            const QStringList parts = raw.split('\t');
            const QString kind = parts.value(0);
            QString type = kind.section(' ', 1);
            if (type == "Data") type = Text::tr("data");
            else if (type == "Instruction") type = Text::tr("instruction");
            else if (type == "Unified") type = Text::tr("unified");
            lines.append(Text::tr("%1 %2: %3 (%4 unique instances)")
                .arg(kind.section(' ', 0, 0), type,
                     QLocale().formattedDataSize(parts.value(1).toLongLong()), parts.value(2)));
        }
        return lines.join('\n');
    }
    return value.value;
}
