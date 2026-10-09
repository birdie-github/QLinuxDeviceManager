#include "deviceresources.h"
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <algorithm>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <unistd.h>

namespace {
// Stable userspace resource flag values: Linux include/linux/ioport.h.
constexpr quint64 typeMask = 0x00001f00;
constexpr quint64 io = 0x00000100;
constexpr quint64 memory = 0x00000200;
constexpr quint64 window = 0x00200000;
constexpr quint64 disabled = 0x10000000;
constexpr quint64 unset = 0x20000000;
constexpr int inputLimit = 65536;
constexpr int rowLimit = 256;
constexpr int vectorLimit = 4096;

Attribute failure(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENODEV ? ReadState::Removed
        : error == ENOENT ? ReadState::Unavailable : ReadState::Error, {}, error};
}
void issue(DeviceResources &result, const QString &source, int error)
{
    result.issues.append({source, failure(error)});
}
Attribute readMetadata(int parent, const char *name, int limit = inputLimit)
{
    const int fd = openat(parent, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return failure(errno);
    QByteArray bytes;
    char buffer[4096];
    int error = 0;
    while (bytes.size() <= limit) {
        if (QThread::currentThread()->isInterruptionRequested()) { error = ECANCELED; break; }
        const int wanted = qMin(static_cast<int>(sizeof(buffer)), limit + 1 - static_cast<int>(bytes.size()));
        const ssize_t count = read(fd, buffer, wanted);
        if (count < 0) {
            if (errno == EINTR) continue;
            error = errno;
            break;
        }
        if (!count) break;
        bytes.append(buffer, static_cast<int>(count));
    }
    close(fd);
    if (error) return failure(error);
    if (bytes.size() > limit) return failure(EOVERFLOW);
    return {ReadState::Available, QString::fromUtf8(bytes), 0};
}
void recordReadError(DeviceResources &result, const QString &source, const Attribute &value)
{
    if (value.state != ReadState::Available && value.state != ReadState::Unavailable)
        result.issues.append({source, value});
}
bool hexNumber(const QString &text, quint64 &value)
{
    static const QRegularExpression hex(QStringLiteral("^(?:0[xX])?[0-9a-fA-F]{1,16}$"));
    if (!hex.match(text).hasMatch()) return false;
    bool ok = false;
    value = text.toULongLong(&ok, 16);
    return ok;
}
bool decimalNumber(const QString &text, quint64 &value)
{
    static const QRegularExpression decimal(QStringLiteral("^[0-9]{1,10}$"));
    if (!decimal.match(text).hasMatch()) return false;
    bool ok = false;
    value = text.toULongLong(&ok, 10);
    return ok && value <= 0xffffffffULL;
}
void append(DeviceResources &target, const DeviceResources &source)
{
    target.items += source.items;
    target.issues += source.issues;
    target.pnpState = source.pnpState;
}
}

DeviceResources parsePciResources(const QString &text, const QString &source)
{
    DeviceResources result;
    if (text.size() > inputLimit) { issue(result, source, EOVERFLOW); return result; }
    const QStringList lines = text.split('\n');
    int index = 0;
    static const QRegularExpression whitespace(QStringLiteral("\\s+"));
    for (int line = 0; line < lines.size(); ++line) {
        const QString record = lines[line].trimmed();
        if (record.isEmpty() && line == lines.size() - 1) continue;
        if (index >= rowLimit) { issue(result, source, EOVERFLOW); break; }
        const QStringList fields = record.split(whitespace, Qt::SkipEmptyParts);
        quint64 start = 0, end = 0, flags = 0;
        if (fields.size() != 3 || !hexNumber(fields[0], start)
            || !hexNumber(fields[1], end) || !hexNumber(fields[2], flags)) {
            // Do not shift slot numbers or invent ranges after malformed input.
            result.items.clear();
            issue(result, source, EINVAL);
            return result;
        }
        const int slot = index++;
        if (start == 0 && end == 0 && flags == 0) continue; // Empty resource slot.
        const quint64 type = flags & typeMask;
        if (type != io && type != memory) continue;
        DeviceResource r;
        r.pci = true;
        r.index = slot;
        r.type = type == io ? DeviceResource::Type::Io : DeviceResource::Type::Memory;
        r.start = start;
        r.end = end;
        r.flags = flags;
        r.source = source;
        if (flags & disabled) r.allocation = DeviceResource::Allocation::Disabled;
        else if (flags & unset) r.allocation = DeviceResource::Allocation::Unassigned;
        else if (start == 0 && end == 0) r.allocation = DeviceResource::Allocation::Unavailable;
        else if (end < start) { result.items.clear(); issue(result, source, EINVAL); return result; }
        result.items.append(r);
    }
    return result;
}

DeviceResources parsePnpResources(const QString &text, const QString &source)
{
    DeviceResources result;
    if (text.size() > inputLimit) { issue(result, source, EOVERFLOW); return result; }
    const QStringList lines = text.trimmed().split('\n');
    if (lines.isEmpty() || (lines[0] != "state = active" && lines[0] != "state = disabled")) {
        issue(result, source, EINVAL); return result;
    }
    result.pnpState = lines[0].mid(8);
    static const QRegularExpression record(QStringLiteral("^(io|mem|irq|dma|bus) (.+)$"));
    static const QRegularExpression range(QStringLiteral("^(0[xX][0-9a-fA-F]+|0)-(0[xX][0-9a-fA-F]+|0)( window)?$"));
    for (int line = 1; line < lines.size(); ++line) {
        if (line > rowLimit) { issue(result, source, EOVERFLOW); break; }
        const auto match = record.match(lines[line].trimmed());
        if (!match.hasMatch()) { result.items.clear(); issue(result, source, EINVAL); return result; }
        const QString type = match.captured(1), value = match.captured(2);
        DeviceResource r;
        r.type = type == "io" ? DeviceResource::Type::Io : type == "mem" ? DeviceResource::Type::Memory
            : type == "irq" ? DeviceResource::Type::Irq : type == "dma" ? DeviceResource::Type::Dma : DeviceResource::Type::Bus;
        r.source = source;
        if (value == "disabled") r.allocation = DeviceResource::Allocation::Disabled;
        else if (r.type == DeviceResource::Type::Irq || r.type == DeviceResource::Type::Dma) {
            if (value == "-1") r.allocation = DeviceResource::Allocation::Unassigned;
            else if (!decimalNumber(value, r.start)) { result.items.clear(); issue(result, source, EINVAL); return result; }
            r.end = r.start;
        } else {
            const auto addresses = range.match(value);
            if (!addresses.hasMatch() || !hexNumber(addresses.captured(1), r.start)
                || !hexNumber(addresses.captured(2), r.end) || r.end < r.start) {
                result.items.clear(); issue(result, source, EINVAL); return result;
            }
            if (!addresses.captured(3).isEmpty()) r.flags |= window;
            if (r.start == 0 && r.end == 0) r.allocation = DeviceResource::Allocation::Unavailable;
        }
        if (result.pnpState == "disabled") r.allocation = DeviceResource::Allocation::Disabled;
        result.items.append(r);
    }
    return result;
}

DeviceResources collectDeviceResources(int deviceFd, const Device &device)
{
    DeviceResources result;
    if (device.subsystem == "pnp") {
        const Attribute data = readMetadata(deviceFd, "resources");
        const QString source = device.path + "/resources";
        if (data.state == ReadState::Available) append(result, parsePnpResources(data.value, source));
        else recordReadError(result, source, data);
        return result;
    }
    if (device.subsystem != "pci") return result; // No guessed parent resources.
    const Attribute ranges = readMetadata(deviceFd, "resource");
    if (ranges.state == ReadState::Available) append(result, parsePciResources(ranges.value, device.path + "/resource"));
    else recordReadError(result, device.path + "/resource", ranges);

    const int fd = openat(deviceFd, "msi_irqs", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    bool msiObserved = false;
    if (fd >= 0) {
        DIR *raw = fdopendir(fd);
        if (!raw) { const int error = errno; close(fd); issue(result, device.path + "/msi_irqs", error); }
        else {
            const auto closeDirectory = [](DIR *directory) { closedir(directory); };
            std::unique_ptr<DIR, decltype(closeDirectory)> entries(raw, closeDirectory);
            QVector<DeviceResource> vectors;
            int examined = 0;
            while (!QThread::currentThread()->isInterruptionRequested()) {
                errno = 0;
                const dirent *entry = readdir(entries.get());
                if (!entry) {
                    if (errno) issue(result, device.path + "/msi_irqs", errno);
                    break;
                }
                const QString filename = QString::fromUtf8(entry->d_name);
                if (filename == "." || filename == "..") continue;
                if (++examined > vectorLimit) { issue(result, device.path + "/msi_irqs", EOVERFLOW); break; }
                quint64 number = 0;
                if (!decimalNumber(filename, number) || number == 0) {
                    issue(result, device.path + "/msi_irqs", EINVAL); continue;
                }
                msiObserved = true;
                const Attribute mode = readMetadata(dirfd(entries.get()), entry->d_name, 32);
                const QString source = device.path + "/msi_irqs/" + filename;
                if (mode.state != ReadState::Available) {
                    // Even a vanished vector is an explicit incomplete observation.
                    result.issues.append({source, mode}); continue;
                }
                const QString value = mode.value.trimmed();
                if (value != "msi" && value != "msix") { issue(result, source, EINVAL); continue; }
                DeviceResource r;
                r.type = DeviceResource::Type::Irq;
                r.start = r.end = number;
                r.mode = value;
                r.source = source;
                vectors.append(r);
            }
            std::sort(vectors.begin(), vectors.end(), [](const DeviceResource &a, const DeviceResource &b) { return a.start < b.start; });
            result.items += vectors;
        }
    } else if (errno != ENOENT) issue(result, device.path + "/msi_irqs", errno);

    // irq may be the first MSI vector or the legacy INTx number during MSI-X.
    // Prefer the complete msi_irqs allocation; never invent an active INTx IRQ.
    if (!msiObserved && !QThread::currentThread()->isInterruptionRequested()) {
        const Attribute irq = readMetadata(deviceFd, "irq", 32);
        if (irq.state != ReadState::Available) recordReadError(result, device.path + "/irq", irq);
        else {
            quint64 number = 0;
            if (!decimalNumber(irq.value.trimmed(), number)) issue(result, device.path + "/irq", EINVAL);
            else if (number != 0) {
                DeviceResource r;
                r.type = DeviceResource::Type::Irq;
                r.start = r.end = number;
                r.mode = "reported";
                r.source = device.path + "/irq";
                result.items.append(r);
            }
        }
    }
    return result;
}
