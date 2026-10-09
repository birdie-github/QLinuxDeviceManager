#include "systemproperties.h"
#include "cpumetadata.h"
#include <QDir>
#include <QMap>
#include <QStringList>
#include <QSysInfo>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <cmath>
#include <limits>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <utility>
#if defined(__i386__) || defined(__x86_64__)
#include <cpuid.h>
#endif

namespace {
Attribute available(const QString &value) { return {ReadState::Available, value, 0}; }
Attribute failed(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENOENT ? ReadState::Unavailable : ReadState::Error, {}, error};
}
Attribute readText(const QString &path, int limit = 4096)
{
    if (QThread::currentThread()->isInterruptionRequested()) return failed(ECANCELED);
    const int fd = open(path.toUtf8().constData(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return failed(errno);
    QByteArray bytes;
    int error = 0;
    while (bytes.size() <= limit) {
        char buffer[4096];
        const ssize_t n = read(fd, buffer, qMin<int>(sizeof(buffer), limit + 1 - bytes.size()));
        if (n < 0) { if (errno == EINTR) continue; error = errno; break; }
        if (!n) break;
        bytes.append(buffer, static_cast<int>(n));
        if (QThread::currentThread()->isInterruptionRequested()) { error = ECANCELED; break; }
    }
    close(fd);
    if (error) return failed(error);
    if (bytes.size() > limit) return failed(EOVERFLOW);
    const QString text = QString::fromUtf8(bytes).trimmed();
    return text.isEmpty() ? Attribute() : available(text);
}
bool number(const Attribute &value, qlonglong &result)
{
    bool ok = false;
    result = value.value.toLongLong(&ok);
    return value.state == ReadState::Available && ok && result >= 0;
}
void cpuTopology(SystemProperties &result)
{
    const QString base = "/sys/devices/system/cpu";
    const Attribute present = readText(base + "/present");
    const Attribute online = readText(base + "/online");
    QSet<int> cpus, active;
    const bool havePresent = present.state == ReadState::Available && parseCpuList(present.value, cpus);
    const bool haveOnline = online.state == ReadState::Available && parseCpuList(online.value, active);
    result.values["logical"] = havePresent ? available(QString::number(cpus.size()))
        : present.state == ReadState::Available ? failed(EINVAL) : present;
    result.values["online"] = haveOnline ? available(QString::number(active.size()))
        : online.state == ReadState::Available ? failed(EINVAL) : online;
    QSet<QString> packages, cores;
    Attribute topologyError;
    bool topologyComplete = havePresent;
    if (havePresent) {
        for (int cpu : cpus) {
            const QString path = base + QStringLiteral("/cpu%1/topology/").arg(cpu);
            const Attribute package = readText(path + "physical_package_id");
            const Attribute core = readText(path + "core_id");
            const Attribute die = readText(path + "die_id");
            qlonglong packageId, coreId, dieId;
            if (!number(package, packageId) || !number(core, coreId)) {
                topologyError = package.state != ReadState::Available ? package
                    : core.state != ReadState::Available ? core : failed(ENODATA);
                topologyComplete = false;
                break; // No apparently complete count from partial topology.
            }
            if (die.state == ReadState::PermissionDenied || die.state == ReadState::Error) {
                topologyError = die;
                topologyComplete = false;
                break;
            }
            packages.insert(QString::number(packageId));
            cores.insert(QStringLiteral("%1:%2:%3").arg(packageId)
                .arg(number(die, dieId) ? QString::number(dieId) : QStringLiteral("unknown"))
                .arg(coreId));
        }
    } else topologyError = result.values.value("logical");
    result.values["sockets"] = topologyComplete ? available(QString::number(packages.size())) : topologyError;
    result.values["cores"] = topologyComplete ? available(QString::number(cores.size())) : topologyError;
    for (const char *key : {"logical", "online", "sockets", "cores"})
        result.sources[QLatin1String(key)] = base + "/present, online and cpu*/topology";

    if (!haveOnline) { result.values["caches"] = result.values.value("online"); return; }
    // Deduplicate by level/type and canonical sharing set, never by CPU number.
    QHash<QString, qulonglong> seen;
    QMap<QString, QPair<qulonglong, int>> totals;
    int entries = 0;
    Attribute cacheError;
    bool complete = true;
    for (int cpu : active) {
        const QString path = base + QStringLiteral("/cpu%1/cache").arg(cpu);
        struct stat st {};
        if (stat(path.toUtf8().constData(), &st) != 0) { cacheError = failed(errno); complete = false; break; }
        const int cacheFd = open(path.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (cacheFd < 0) { cacheError = failed(errno); complete = false; break; }
        close(cacheFd);
        const QStringList indexes = QDir(path).entryList({"index*"}, QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
        if (indexes.isEmpty()) { complete = false; break; }
        for (const QString &index : indexes) {
            if (++entries > 16384) { cacheError = failed(EOVERFLOW); complete = false; break; }
            const QString prefix = path + '/' + index + '/';
            const Attribute level = readText(prefix + "level"), type = readText(prefix + "type"),
                size = readText(prefix + "size"), sharing = readText(prefix + "shared_cpu_list");
            for (const Attribute &value : {level, type, size, sharing})
                if (value.state != ReadState::Available) { cacheError = value; complete = false; break; }
            if (!complete) break;
            qlonglong levelNumber;
            QSet<int> siblings;
            QString sizeText = size.value;
            qulonglong scale = 1;
            if (sizeText.endsWith('K')) { scale = 1024; sizeText.chop(1); }
            else if (sizeText.endsWith('M')) { scale = 1024 * 1024; sizeText.chop(1); }
            bool ok = false;
            const qulonglong amount = sizeText.toULongLong(&ok);
            if (!number(level, levelNumber) || !levelNumber || !ok || !amount
                || amount > std::numeric_limits<qulonglong>::max() / scale
                || !parseCpuList(sharing.value, siblings) || !siblings.contains(cpu)
                || (type.value != "Data" && type.value != "Instruction" && type.value != "Unified")) {
                cacheError = failed(EINVAL); complete = false; break;
            }
            QList<int> sorted = siblings.values();
            std::sort(sorted.begin(), sorted.end());
            QStringList ids;
            for (int id : sorted) ids.append(QString::number(id));
            const QString kind = QStringLiteral("L%1 %2").arg(levelNumber).arg(type.value);
            const QString key = kind + ':' + ids.join(',');
            const qulonglong bytes = amount * scale;
            if (seen.contains(key)) {
                if (seen.value(key) != bytes) { cacheError = failed(EAGAIN); complete = false; break; }
            } else {
                seen.insert(key, bytes);
                auto &total = totals[kind];
                if (bytes > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max())
                    || total.first > static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()) - bytes) {
                    cacheError = failed(EOVERFLOW); complete = false; break;
                }
                total.first += bytes;
                ++total.second;
            }
        }
        if (!complete) break;
    }
    QStringList lines;
    for (auto it = totals.cbegin(); it != totals.cend(); ++it)
        lines.append(QStringLiteral("%1\t%2\t%3").arg(it.key()).arg(it.value().first).arg(it.value().second));
    result.values["caches"] = complete && !lines.isEmpty() ? available(lines.join('\n')) : cacheError;
    result.sources["caches"] = base + "/cpu*/cache/index*/{level,type,size,shared_cpu_list}; online CPUs only";
    // Detect observed hotplug during collection rather than publish mixed counts.
    if (readText(base + "/present").value != present.value || readText(base + "/online").value != online.value)
        for (const char *key : {"logical", "online", "sockets", "cores", "caches"})
            result.values[QLatin1String(key)] = failed(EAGAIN);
}
}

Attribute parseOsName(const QString &text)
{
    Attribute result = available("Linux"); // Specified default, only after a successful file read.
    for (const QString &line : text.split('\n')) {
        if (!line.startsWith("PRETTY_NAME=")) continue;
        QString value = line.mid(12).trimmed();
        QString decoded;
        const QChar quote = value.isEmpty() ? QChar() : value.front();
        const bool quoted = quote == '\'' || quote == '"';
        if (quoted) {
            if (value.size() < 2 || value.back() != quote) return failed(EINVAL);
            value = value.mid(1, value.size() - 2);
        }
        for (int i = 0; i < value.size(); ++i) {
            const QChar c = value.at(i);
            if (quoted && c == quote) return failed(EINVAL);
            if (c == '\\' && quote != '\'') {
                if (++i >= value.size()) return failed(EINVAL);
                const QChar next = value.at(i);
                if (quote == '"' && next != '"' && next != '\\' && next != '$' && next != '`') decoded += '\\';
                decoded += next;
            } else decoded += c;
        }
        result = decoded.isEmpty() ? Attribute() : available(decoded);
    }
    return result;
}
bool parseCpuList(const QString &text, QSet<int> &cpus)
{
    QSet<int> parsed;
    for (const QString &range : text.split(',')) {
        const QStringList limits = range.trimmed().split('-');
        bool firstOk = false, lastOk = false;
        const int first = limits.value(0).toInt(&firstOk);
        const int last = limits.size() == 1 ? first : limits.value(1).toInt(&lastOk);
        if (!firstOk || (limits.size() != 1 && !lastOk) || limits.size() > 2
            || first < 0 || last < first || last > 1048575 || last - first >= 4096) return false;
        for (int cpu = first; cpu <= last; ++cpu) parsed.insert(cpu);
        if (parsed.size() > 4096) return false;
    }
    if (parsed.isEmpty()) return false;
    cpus = std::move(parsed);
    return true;
}
SystemProperties collectSystemProperties()
{
    SystemProperties result;
    const auto put = [&](const QString &key, const Attribute &value, const QString &source) {
        result.values.insert(key, value); result.sources.insert(key, source);
    };
    const QString hostname = QSysInfo::machineHostName();
    put("hostname", hostname.isEmpty() ? Attribute() : available(hostname), "gethostname");
    QString osPath = "/etc/os-release";
    Attribute os = readText(osPath, 65536);
    if (os.error == ENOENT) { osPath = "/usr/lib/os-release"; os = readText(osPath, 65536); }
    put("os", os.state == ReadState::Available ? parseOsName(os.value) : os, osPath + ": PRETTY_NAME");
    struct utsname kernel {};
    if (uname(&kernel) == 0) {
        put("architecture", available(QString::fromLocal8Bit(kernel.machine)), "uname.machine (kernel architecture)");
        put("kernel", available(QString::fromLocal8Bit(kernel.sysname) + ' ' + QString::fromLocal8Bit(kernel.release)), "uname.sysname, release");
        put("build", available(QString::fromLocal8Bit(kernel.version)), "uname.version");
    } else {
        const Attribute error = failed(errno);
        for (const char *key : {"architecture", "kernel", "build"}) put(QLatin1String(key), error, "uname");
    }
    const Attribute cpu = readCpuInfo();
    QStringList models = parseCpuModels(cpu.value).values();
    models.removeDuplicates(); models.sort(Qt::CaseInsensitive);
    put("cpu", cpu.state == ReadState::Available ? models.isEmpty() ? Attribute() : available(models.join('\n')) : cpu, "/proc/cpuinfo: per-processor model fields");
    cpuTopology(result);
    const Attribute memory = readText("/proc/meminfo", 65536);
    Attribute total = memory.state == ReadState::Available ? Attribute() : memory;
    for (const QString &line : memory.value.split('\n')) {
        if (!line.startsWith("MemTotal:")) continue;
        const QStringList parts = line.mid(9).simplified().split(' ');
        bool ok = false;
        const qulonglong kib = parts.value(0).toULongLong(&ok);
        total = ok && kib && parts.size() == 2 && parts.at(1) == "kB"
            && kib <= std::numeric_limits<qulonglong>::max() / 1024
            ? available(QString::number(kib * 1024)) : failed(EINVAL);
        break;
    }
    put("memory", total, "/proc/meminfo: MemTotal; usable physical RAM, not installed DIMM capacity");
    const Attribute uptime = readText("/proc/uptime");
    bool valid = false;
    const double seconds = uptime.value.section(' ', 0, 0).toDouble(&valid);
    put("uptime", uptime.state != ReadState::Available ? uptime
        : valid && std::isfinite(seconds) && seconds >= 0 && seconds < 1e12
            ? available(QString::number(static_cast<qulonglong>(seconds))) : failed(EINVAL), "/proc/uptime");
    for (const auto &field : {qMakePair("manufacturer", "sys_vendor"), qMakePair("model", "product_name"),
                             qMakePair("board_vendor", "board_vendor"), qMakePair("board_model", "board_name"),
                             qMakePair("firmware", "bios_version"), qMakePair("firmware_date", "bios_date")})
        put(QLatin1String(field.first), readText("/sys/class/dmi/id/" + QString::fromLatin1(field.second)),
            "/sys/class/dmi/id/" + QString::fromLatin1(field.second));
    struct stat efi {};
    const int status = stat("/sys/firmware/efi", &efi);
    put("boot", status == 0 ? S_ISDIR(efi.st_mode) ? available("uefi") : failed(ENOTDIR) : errno == ENOENT ? available("not-exposed") : failed(errno), "/sys/firmware/efi directory presence");
    Attribute hypervisor = readText("/sys/hypervisor/type");
    QString hypervisorSource = "/sys/hypervisor/type";
#if defined(__i386__) || defined(__x86_64__)
    unsigned int a, b, c, d;
    if (hypervisor.state != ReadState::Available && __get_cpuid(1, &a, &b, &c, &d) && (c & (1u << 31))) {
        __cpuid(0x40000000, a, b, c, d);
        if (a >= 0x40000000 && a < 0x40010000) {
            char vendor[13] {};
            memcpy(vendor, &b, 4); memcpy(vendor + 4, &c, 4); memcpy(vendor + 8, &d, 4);
            const QString name = QString::fromLatin1(vendor, 12).remove(QChar('\0')).trimmed();
            hypervisor = name.isEmpty() ? Attribute() : available(name);
            hypervisorSource = "CPUID hypervisor-present bit and vendor leaf 0x40000000";
        }
    }
#endif
    put("virtualization", hypervisor, hypervisorSource);
    return result;
}
SystemProperties SystemPropertiesReader::takeResult()
{
    Q_ASSERT(!isRunning());
    return std::move(result_);
}
