#include "cpumetadata.h"
#include "cputopology.h"

#include <QFile>
#include <QThread>
#include <QStringList>
#include <cerrno>

QHash<int, QString> parseCpuModels(const QString &cpuinfo)
{
    QHash<int, QString> models;
    QHash<QString, QString> record;
    const auto commit = [&] {
        bool valid = false;
        const int cpu = record.value("processor").toInt(&valid);
        if (!valid || cpu < 0) return;
        for (const char *key : {"model name", "cpu model", "uarch"}) {
            const QString value = record.value(QLatin1String(key));
            if (!value.isEmpty()) { models.insert(cpu, value); break; }
        }
    };
    for (const QString &line : cpuinfo.split('\n')) {
        if (line.trimmed().isEmpty()) { commit(); record.clear(); continue; }
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        // Only per-processor keys. Never assign a board's global Hardware string to a CPU.
        if (key == "processor" || key == "model name" || key == "cpu model" || key == "uarch")
            record.insert(key, line.mid(colon + 1).trimmed());
    }
    commit();
    return models;
}

QHash<QString, Attribute> parseCpuIdentity(const QString &cpuinfo)
{
    QHash<QString, Attribute> result;
    const auto stop = [&](int error) {
        for (const auto &field : cpuIdentityFields)
            result.insert(QLatin1String(field.key), {ReadState::Error, {}, error});
        return result;
    };
    if (cpuinfo.size() > 4 * 1024 * 1024) return stop(EOVERFLOW);
    QHash<QString, QMap<QString, QSet<int>>> groups;
    QHash<QString, QString> record;
    QSet<int> seen;
    int error = 0;
    const auto commit = [&] {
        if (!record.contains("processor")) return;
        bool valid = false;
        const int cpu = record.value("processor").toInt(&valid);
        if (!valid || cpu < 0 || seen.contains(cpu)) { error = EINVAL; return; }
        if (seen.size() >= 4096) { error = EOVERFLOW; return; }
        seen.insert(cpu);
        for (const auto &field : cpuIdentityFields)
            groups[QLatin1String(field.key)][record.value(QLatin1String(field.procName))].insert(cpu);
    };
    for (const QString &line : cpuinfo.split('\n')) {
        if (QThread::currentThread()->isInterruptionRequested()) return stop(ECANCELED);
        if (line.trimmed().isEmpty()) {
            commit();
            if (error) return stop(error);
            record.clear();
            continue;
        }
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        if (key == "processor") record.insert(key, line.mid(colon + 1).trimmed());
        for (const auto &field : cpuIdentityFields) {
            if (key != QLatin1String(field.procName)) continue;
            const QString value = line.mid(colon + 1).trimmed();
            bool valid = false;
            const bool hex = value.startsWith("0x", Qt::CaseInsensitive);
            const auto number = (hex ? value.mid(2) : value).toUInt(&valid, hex ? 16 : 10);
            if (valid) record.insert(key, hex ? QStringLiteral("0x%1").arg(number, 0, 16) : QString::number(number));
            break;
        }
    }
    commit();
    if (error) return stop(error);
    for (auto field = groups.cbegin(); field != groups.cend(); ++field) {
        const auto &values = field.value();
        if (values.size() == 1 && values.firstKey().isEmpty()) continue;
        QStringList lines;
        for (auto it = values.cbegin(); it != values.cend(); ++it)
            lines.append(cpuListText(it.value()) + '\t' + it.key());
        result.insert(field.key(), {ReadState::Available, lines.join('\n'), 0});
    }
    return result;
}

Attribute readCpuInfo()
{
    Attribute result;
    QFile file(QStringLiteral("/proc/cpuinfo"));
    if (!file.open(QIODevice::ReadOnly)) {
        result.state = file.error() == QFileDevice::PermissionsError
            ? ReadState::PermissionDenied : ReadState::Unavailable;
        return result;
    }
    constexpr qint64 limit = 4 * 1024 * 1024;
    QByteArray contents;
    while (contents.size() <= limit) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            result.state = ReadState::Error;
            result.error = ECANCELED;
            return result;
        }
        const QByteArray chunk = file.read(qMin<qint64>(65536, limit + 1 - contents.size()));
        if (file.error() != QFileDevice::NoError) {
            result.state = ReadState::Error;
            return result;
        }
        if (chunk.isEmpty()) {
            result.state = ReadState::Available;
            result.value = QString::fromUtf8(contents);
            return result;
        }
        contents.append(chunk);
    }
    result.state = ReadState::Error;
    result.error = EOVERFLOW;
    return result; // A truncated file must not become partially authoritative CPU metadata.
}
