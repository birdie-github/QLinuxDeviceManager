#include "cpumetadata.h"

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
