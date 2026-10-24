#include "systemproperties.h"
#include "secureboot.h"
#include "cpumetadata.h"
#include "directoryscan.h"
#include <libudev.h>
#include <memory>
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
bool unsignedDecimal(const QString &text, qulonglong &value)
{
    if (text.isEmpty() || text.size() > 20) return false;
    for (QChar c : text) if (c < QLatin1Char('0') || c > QLatin1Char('9')) return false;
    bool ok = false;
    value = text.toULongLong(&ok);
    return ok;
}
constexpr int maxMemoryDevices = 4096;
constexpr int maxDetailedMemoryDevices = 256;
constexpr int maxMemoryString = 256;
constexpr int maxMemoryBytes = 1024 * 1024;
const char *const memorySuffixes[] = {
    "SIZE", "PRESENT", "NON_VOLATILE_SIZE", "VOLATILE_SIZE", "LOCATOR", "BANK_LOCATOR",
    "TYPE", "FORM_FACTOR", "SPEED_MTS", "CONFIGURED_SPEED_MTS", "MANUFACTURER", "PART_NUMBER"
};
void installedMemory(SystemProperties &result)
{
    const auto failure = [&](const Attribute &status) {
        result.values["physical_memory"] = status;
        result.memoryDevices.status = status;
    };
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), &udev_unref);
    if (!context) { failure(failed(ENOMEM)); return; }
    errno = 0;
    std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
        udev_device_new_from_subsystem_sysname(context.get(), "dmi", "id"), &udev_device_unref);
    if (!device) { failure(errno ? failed(errno) : Attribute()); return; }
    QHash<QString, QString> properties;
    int copiedBytes = 0;
    const auto copy = [&](const QString &key, int limit = maxMemoryString) {
        const char *raw = udev_device_get_property_value(device.get(), key.toUtf8().constData());
        if (!raw) return true;
        const size_t length = strnlen(raw, static_cast<size_t>(limit + 1));
        if (length > static_cast<size_t>(limit) || copiedBytes > maxMemoryBytes - static_cast<int>(length)) return false;
        copiedBytes += static_cast<int>(length);
        properties.insert(key, QString::fromUtf8(raw, static_cast<int>(length)));
        return true;
    };
    const QString countKey = "MEMORY_ARRAY_NUM_DEVICES";
    if (!copy(countKey, 64)) { failure(failed(EOVERFLOW)); return; }
    qulonglong count = 0;
    if (!properties.contains(countKey)) return;
    if (!unsignedDecimal(properties.value(countKey), count)) { failure(failed(EINVAL)); return; }
    if (count > maxMemoryDevices) { failure(failed(EOVERFLOW)); return; }
    // Capacity and metadata share one owned udev database snapshot. Optional
    // metadata bounds must not invalidate the existing installed-RAM calculation.
    for (qulonglong index = 0; index < count; ++index) {
        if (QThread::currentThread()->isInterruptionRequested()) { failure(failed(ECANCELED)); return; }
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(index);
        for (const char *suffix : {"SIZE", "PRESENT", "NON_VOLATILE_SIZE"})
            if (!copy(prefix + QLatin1String(suffix), 64)) { failure(failed(EOVERFLOW)); return; }
    }
    result.values["physical_memory"] = parseInstalledMemory(properties);
    if (count > maxDetailedMemoryDevices) { result.memoryDevices.status = failed(EOVERFLOW); return; }
    for (qulonglong index = 0; index < count; ++index) {
        if (QThread::currentThread()->isInterruptionRequested()) { result.memoryDevices.status = failed(ECANCELED); return; }
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(index);
        for (const char *suffix : memorySuffixes) {
            const QString key = prefix + QLatin1String(suffix);
            if (!properties.contains(key) && !copy(key)) { result.memoryDevices.status = failed(EOVERFLOW); return; }
        }
    }
    result.memoryDevices = parseMemoryDevices(properties);
}
void gpuResizableBar(SystemProperties &result)
{
    const QString base = "/sys/bus/pci/devices";
    const int fd = open(base.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) { result.gpuRebarStatus = failed(errno); return; }
    close(fd);
    const DirectoryNames devices = boundedDirectoryNames(base, 4096);
    if (devices.error) { result.gpuRebarStatus = failed(devices.error); return; }
    for (const QString &name : devices.names) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            result.gpuRebar.clear(); result.gpuRebarStatus = failed(ECANCELED); return;
        }
        const QString path = base + '/' + name;
        struct stat before {};
        if (stat(path.toUtf8().constData(), &before) != 0) {
            result.gpuRebar.clear(); result.gpuRebarStatus = failed(errno); return;
        }
        const Attribute cls = readText(path + "/class", 32);
        bool ok = false;
        const uint code = cls.value.toUInt(&ok, 16);
        if (cls.state != ReadState::Available || !ok || code > 0xffffff) {
            result.gpuRebar.clear(); result.gpuRebarStatus = cls.state == ReadState::Available ? failed(EINVAL) : cls; return;
        }
        if (code >> 16 != 0x03) continue; // PCI display controllers only; no GPU-ID database.
        if (result.gpuRebar.size() >= 64) {
            result.gpuRebar.clear(); result.gpuRebarStatus = failed(EOVERFLOW); return;
        }
        GpuResizableBar gpu;
        gpu.pciAddress = name;
        Attribute resources;
        bool haveCapability = false;
        for (int bar = 0; bar < 6; ++bar) {
            const QString bitmapPath = path + QStringLiteral("/resource%1_resize").arg(bar);
            struct stat attribute {};
            if (stat(bitmapPath.toUtf8().constData(), &attribute) != 0) {
                if (errno == ENOENT) continue;
                gpu.status = failed(errno);
                break;
            }
            const Attribute bitmap = readText(bitmapPath, 32);
            haveCapability = true; // Existing sysfs attribute is positive evidence, even if unreadable.
            gpu.supportedSizes[bar] = bitmap.state == ReadState::Available ? parseResizableBarSizes(bitmap.value)
                : bitmap.state == ReadState::Unavailable ? failed(ENODATA) : bitmap;
            if (resources.state == ReadState::Unavailable && resources.error == 0)
                resources = readText(path + "/resource", 65536);
            gpu.currentSizes[bar] = resources.state == ReadState::Available
                ? parseResizableBarAllocation(resources.value, bar) : resources;
        }
        if (gpu.status.state == ReadState::Unavailable && gpu.status.error == 0)
            gpu.status = haveCapability ? available("reported") : Attribute();
        // Catch removal/replacement or allocation changes observed during collection.
        struct stat after {};
        const bool same = stat(path.toUtf8().constData(), &after) == 0
            && before.st_dev == after.st_dev && before.st_ino == after.st_ino;
        bool stable = same;
        if (stable && haveCapability) {
            const Attribute finalResources = readText(path + "/resource", 65536);
            stable = resources.state == finalResources.state && resources.error == finalResources.error
                && resources.value == finalResources.value;
        }
        if (stable && haveCapability) {
            for (auto it = gpu.supportedSizes.cbegin(); it != gpu.supportedSizes.cend(); ++it) {
                Attribute bitmap = readText(path + QStringLiteral("/resource%1_resize").arg(it.key()), 32);
                if (bitmap.state == ReadState::Available) bitmap = parseResizableBarSizes(bitmap.value);
                else if (bitmap.state == ReadState::Unavailable && bitmap.error == 0) bitmap = failed(ENODATA);
                if (bitmap.state != it.value().state || bitmap.error != it.value().error || bitmap.value != it.value().value)
                    stable = false;
            }
        }
        if (!stable) {
            gpu.status = failed(EAGAIN);
            gpu.supportedSizes.clear(); gpu.currentSizes.clear();
        }
        result.gpuRebar.append(std::move(gpu));
    }
    const DirectoryNames after = boundedDirectoryNames(base, 4096);
    if (after.error || after.names != devices.names) {
        result.gpuRebar.clear(); result.gpuRebarStatus = failed(after.error ? after.error : EAGAIN); return;
    }
    result.gpuRebarStatus = available(QString::number(result.gpuRebar.size()));
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
    result.values["smt"] = readText(base + "/smt/control");
    result.sources["smt"] = base + "/smt/control";
    const QString pmuBase = "/sys/bus/event_source/devices/";
    const Attribute corePmu = readText(pmuBase + "cpu_core/cpus");
    const Attribute atomPmu = readText(pmuBase + "cpu_atom/cpus");
    QSet<int> performance, efficiency;
    const bool haveKinds = corePmu.state == ReadState::Available && atomPmu.state == ReadState::Available
        && parseCpuList(corePmu.value, performance) && parseCpuList(atomPmu.value, efficiency)
        && !performance.intersects(efficiency);
    QVector<CpuTopologyRecord> records;
    result.sources["core_groups"] = base + "/cpu*/topology, cpu*/cpufreq/cpuinfo_max_freq; "
        + pmuBase + "{cpu_core,cpu_atom}/cpus; online CPUs only; groups use core type and cache geometry/sharing, not clock speed";
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
            const QString coreIdentity = QStringLiteral("%1:%2:%3").arg(packageId)
                .arg(number(die, dieId) ? QString::number(dieId) : QStringLiteral("unknown"))
                .arg(coreId);
            cores.insert(coreIdentity);
            if (active.contains(cpu)) {
                Attribute maximum = readText(base + QStringLiteral("/cpu%1/cpufreq/cpuinfo_max_freq").arg(cpu));
                qulonglong khz = 0;
                if (maximum.state == ReadState::Available
                    && (!unsignedDecimal(maximum.value, khz) || khz == 0 || khz > 1000000000))
                    maximum = failed(EINVAL);
                const int kind = !haveKinds ? 0 : performance.contains(cpu) ? 1 : efficiency.contains(cpu) ? 2 : 0;
                records.append({cpu, QString::number(packageId), coreIdentity, kind, maximum});
            }
        }
    } else topologyError = result.values.value("logical");
    result.values["sockets"] = topologyComplete ? available(QString::number(packages.size())) : topologyError;
    result.values["cores"] = topologyComplete ? available(QString::number(cores.size())) : topologyError;
    for (const char *key : {"logical", "online", "sockets", "cores"})
        result.sources[QLatin1String(key)] = base + "/present, online and cpu*/topology";

    if (!haveOnline || !havePresent || !(active - cpus).isEmpty()) {
        result.values["caches"] = result.values["core_groups"] = result.values["cache_topology"] =
            !haveOnline ? result.values.value("online") : !havePresent ? result.values.value("logical") : failed(EAGAIN);
        return;
    }

    // Deduplicate by level/type and canonical sharing set, never by CPU number.
    QHash<QString, qulonglong> seen;
    QMap<QString, QPair<qulonglong, int>> totals;
    int entries = 0;
    int sharingEntries = 0;
    Attribute cacheError;
    bool complete = true;
    for (int cpu : active) {
        const QString path = base + QStringLiteral("/cpu%1/cache").arg(cpu);
        struct stat st {};
        if (stat(path.toUtf8().constData(), &st) != 0) { cacheError = failed(errno); complete = false; break; }
        const int cacheFd = open(path.toUtf8().constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (cacheFd < 0) { cacheError = failed(errno); complete = false; break; }
        close(cacheFd);
        const DirectoryNames indexes = boundedDirectoryNames(path, 16384 - entries, {"index*"});
        if (indexes.error) { cacheError = failed(indexes.error); complete = false; break; }
        if (indexes.names.isEmpty()) { complete = false; break; }
        for (const QString &index : indexes.names) {
            ++entries;
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
            if (!number(level, levelNumber) || !levelNumber || levelNumber > 255 || !ok || !amount
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
                sharingEntries += siblings.size();
                if (sharingEntries > 65536) { cacheError = failed(EOVERFLOW); complete = false; break; }
                seen.insert(key, bytes);
                result.cpuCaches.append({static_cast<int>(levelNumber), type.value, bytes, siblings});
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
    if (!complete) result.cpuCaches.clear();
    result.values["cache_topology"] = result.values.value("caches");
    result.sources["cache_topology"] = result.sources.value("caches");
    result.values["core_groups"] = topologyComplete ? available(QString()) : topologyError;
    if (topologyComplete) result.cpuGroups = groupCpuTopology(records, result.cpuCaches);
    // Detect observed hotplug during collection rather than publish mixed counts.
    if (readText(base + "/present").value != present.value || readText(base + "/online").value != online.value) {
        for (const char *key : {"logical", "online", "sockets", "cores", "caches", "core_groups", "cache_topology"})
            result.values[QLatin1String(key)] = failed(EAGAIN);
        result.cpuGroups.clear();
        result.cpuCaches.clear();
    }
}
}

Attribute parseResizableBarSizes(const QString &text)
{
    if (text.isEmpty() || text.size() > 16) return failed(EINVAL);
    for (QChar c : text)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return failed(EINVAL);
    bool ok = false;
    const qulonglong bitmap = text.toULongLong(&ok, 16);
    if (!ok || !bitmap) return failed(EINVAL);
    // bit n means 2^(n+20) bytes. Do not shift beyond a 64-bit size.
    if (bitmap >> 44) return failed(EOVERFLOW);
    return available(QString::number(bitmap, 16));
}
Attribute parseResizableBarAllocation(const QString &resources, int bar)
{
    if (bar < 0 || bar >= 6 || resources.size() > 65536) return failed(EINVAL);
    const QStringList lines = resources.split('\n');
    if (bar >= lines.size()) return failed(EINVAL);
    const QStringList fields = lines[bar].simplified().split(' ');
    if (fields.size() != 3) return failed(EINVAL);
    qulonglong values[3] {};
    for (int i = 0; i < 3; ++i) {
        QString text = fields[i];
        if (text.startsWith("0x", Qt::CaseInsensitive)) text.remove(0, 2);
        if (text.isEmpty() || text.size() > 16) return failed(EINVAL);
        for (QChar c : text)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) return failed(EINVAL);
        bool ok = false;
        values[i] = text.toULongLong(&ok, 16);
        if (!ok) return failed(EINVAL);
    }
    const qulonglong start = values[0], end = values[1], flags = values[2];
    if ((flags & 0x1f00) != 0x200) return {}; // No reported memory resource.
    if (flags & 0x30000000) return {}; // Unassigned or disabled resource, not a BIOS ReBAR verdict.
    if (start == 0 && end == 0) return {}; // Empty/masked addresses never mean a one-byte BAR.
    if (end < start) return failed(EINVAL);
    if (end - start == std::numeric_limits<qulonglong>::max()) return failed(EOVERFLOW);
    return available(QString::number(end - start + 1));
}
Attribute parseCpuInstructionSets(const QString &cpuinfo)
{
    // Reuse the bounded /proc/cpuinfo snapshot, never claim a union of features
    // is supported on every CPU. Preserve kernel tokens, including non-ISA flags.
    if (cpuinfo.size() > 4 * 1024 * 1024) return failed(EOVERFLOW);
    QMap<int, QString> processors;
    QHash<QString, QString> record;
    Attribute error;
    bool complete = true;
    const auto commit = [&] {
        if (!record.contains("processor")) return;
        bool ok = false;
        const int id = record.value("processor").toInt(&ok);
        if (!ok || id < 0 || processors.contains(id)) { error = failed(EINVAL); complete = false; return; }
        if (processors.size() >= 4096) { error = failed(EOVERFLOW); complete = false; return; }
        QString features;
        for (const char *key : {"flags", "Features", "isa"}) {
            if (!record.value(QLatin1String(key)).isEmpty()) {
                features = record.value(QLatin1String(key)).simplified();
                break;
            }
        }
        if (features.isEmpty()) { complete = false; return; }
        QStringList tokens = features.split(' ', Qt::SkipEmptyParts);
        if (tokens.size() > 4096) { error = failed(EOVERFLOW); complete = false; return; }
        tokens.removeDuplicates();
        tokens.sort(Qt::CaseSensitive);
        processors.insert(id, tokens.join(' '));
    };
    for (const QString &line : cpuinfo.split('\n')) {
        if (QThread::currentThread()->isInterruptionRequested()) return failed(ECANCELED);
        if (line.trimmed().isEmpty()) { commit(); record.clear(); continue; }
        const int colon = line.indexOf(':');
        if (colon < 0) continue;
        const QString key = line.left(colon).trimmed();
        if (key == "processor" || key == "flags" || key == "Features" || key == "isa")
            record.insert(key, line.mid(colon + 1).trimmed());
    }
    commit();
    if (!complete) return error;
    if (processors.isEmpty()) return {};
    QMap<QString, QStringList> groups;
    for (auto it = processors.cbegin(); it != processors.cend(); ++it)
        groups[it.value()].append(QString::number(it.key()));
    QStringList lines;
    for (auto it = groups.cbegin(); it != groups.cend(); ++it)
        lines.append(it.value().join(", ") + '\t' + it.key());
    return available(lines.join('\n'));
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
Attribute parseInstalledMemory(const QHash<QString, QString> &properties)
{
    const QString countKey = "MEMORY_ARRAY_NUM_DEVICES";
    if (!properties.contains(countKey)) return {};
    qulonglong count = 0, total = 0;
    if (!unsignedDecimal(properties.value(countKey), count)) return failed(EINVAL);
    if (count > 4096) return failed(EOVERFLOW);
    for (qulonglong slot = 0; slot < count; ++slot) {
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(slot);
        const QString present = properties.value(prefix + "PRESENT");
        const QString size = properties.value(prefix + "SIZE");
        if (present == "0") {
            if (!size.isEmpty() && size != "0") return failed(EINVAL);
            continue; // Explicitly empty slots do not require a capacity.
        }
        if (!present.isEmpty() && present != "1") return failed(EINVAL);
        qulonglong bytes = 0;
        if (size.isEmpty()) return {}; // Unknown slot invalidates the entire total.
        if (!unsignedDecimal(size, bytes)) return failed(EINVAL);
        if (!bytes) return {};
        const QString persistent = properties.value(prefix + "NON_VOLATILE_SIZE");
        qulonglong nonVolatile = 0;
        if (!persistent.isEmpty() && persistent != "Unknown") {
            if (!unsignedDecimal(persistent, nonVolatile)) return failed(EINVAL);
            // SIZE may include persistent capacity; do not call that installed RAM.
            if (nonVolatile) return {ReadState::Unsupported, {}, 0};
        }
        if (total > std::numeric_limits<qulonglong>::max() - bytes) return failed(EOVERFLOW);
        total += bytes;
    }
    return total ? available(QString::number(total)) : Attribute();
}
FirmwareMemoryInformation parseMemoryDevices(const QHash<QString, QString> &properties)
{
    FirmwareMemoryInformation result;
    const QString countKey = "MEMORY_ARRAY_NUM_DEVICES";
    if (!properties.contains(countKey)) return result;
    qulonglong count = 0;
    if (!unsignedDecimal(properties.value(countKey), count)) { result.status = failed(EINVAL); return result; }
    if (count > maxDetailedMemoryDevices) { result.status = failed(EOVERFLOW); return result; }
    int bytes = 0;
    const auto numeric = [](const QString &key) {
        return key == "SIZE" || key == "VOLATILE_SIZE" || key == "NON_VOLATILE_SIZE"
            || key == "SPEED_MTS" || key == "CONFIGURED_SPEED_MTS";
    };
    const QStringList placeholders = {"unknown", "not specified", "not provided", "none",
        "to be filled by o.e.m.", "default string"};
    for (qulonglong index = 0; index < count; ++index) {
        if (QThread::currentThread()->isInterruptionRequested()) {
            result.devices.clear(); result.status = failed(ECANCELED); return result;
        }
        FirmwareMemoryDevice record;
        record.index = static_cast<int>(index);
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(index);
        for (const char *suffix : memorySuffixes) {
            const QString key = QLatin1String(suffix);
            const QString raw = properties.value(prefix + key);
            const qsizetype byteSize = raw.toUtf8().size();
            if (byteSize > maxMemoryString || bytes > maxMemoryBytes - byteSize) {
                result.devices.clear(); result.status = failed(EOVERFLOW); return result;
            }
            bytes += static_cast<int>(byteSize);
            const QString text = raw.trimmed();
            Attribute value;
            if (!text.isEmpty() && !placeholders.contains(text.toLower())) {
                value = available(text);
                qulonglong amount = 0;
                if (key == "PRESENT") {
                    if (text != "0" && text != "1") value = failed(EINVAL);
                } else if (numeric(key)) {
                    if (!unsignedDecimal(text, amount)) value = failed(EINVAL);
                    else if (!amount && (key == "SPEED_MTS" || key == "CONFIGURED_SPEED_MTS")) value = {};
                }
            }
            record.values.insert(key, value);
        }
        const Attribute present = record.values.value("PRESENT");
        if (present.state == ReadState::Available && present.value == "0") {
            for (const char *suffix : {"SIZE", "VOLATILE_SIZE", "NON_VOLATILE_SIZE"}) {
                Attribute &capacity = record.values[QLatin1String(suffix)];
                if (capacity.state == ReadState::Error
                    || (capacity.state == ReadState::Available && capacity.value.toULongLong() != 0)) {
                    capacity = failed(EINVAL);
                    record.values["PRESENT"] = failed(EINVAL);
                } else capacity = {ReadState::NotApplicable, {}, 0};
            }
        } else {
            // A zero total capacity does not establish an empty device.
            Attribute &capacity = record.values["SIZE"];
            if (capacity.state == ReadState::Available && capacity.value.toULongLong() == 0) capacity = {};
        }
        result.devices.append(std::move(record));
    }
    result.status = available(QString::number(count));
    return result;
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
    put("instruction_sets", cpu.state == ReadState::Available ? parseCpuInstructionSets(cpu.value) : cpu,
        "/proc/cpuinfo: per-processor flags, Features or isa; kernel-exposed instruction sets and CPU features");
    gpuResizableBar(result);
    result.sources["gpu_rebar"] = "/sys/bus/pci/devices: display-controller class, resourceN_resize supported-size bitmaps, resource allocation metadata; BIOS setting is not exposed";
    cpuTopology(result);
    installedMemory(result);
    result.sources["physical_memory"] = "libudev: dmi/id MEMORY_ARRAY_NUM_DEVICES and MEMORY_DEVICE_n_SIZE/PRESENT; firmware-reported installed RAM";
    result.sources["memory_devices"] = "libudev: dmi/id cached MEMORY_ARRAY_NUM_DEVICES and MEMORY_DEVICE_n_* properties";
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
    const SecureBootInformation secureBoot = collectSecureBootInformation();
    put("secure_boot", secureBoot.state, secureBoot.stateSource);
    result.mok = secureBoot.mok;
    result.sources["mok"] = secureBoot.mokSource;
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
