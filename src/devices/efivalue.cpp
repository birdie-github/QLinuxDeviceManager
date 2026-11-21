#include "efivalue.h"
#include "commonproperties.h"
#include <QCoreApplication>
#include <QThread>
#include <QStringList>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

QString efiHexDump(const QByteArray &bytes)
{
    QStringList lines;
    for (int offset = 0; offset < bytes.size(); offset += 16) {
        QStringList hex;
        QString printable;
        for (int i = offset; i < qMin(offset + 16, static_cast<int>(bytes.size())); ++i) {
            const unsigned char byte = static_cast<unsigned char>(bytes.at(i));
            hex.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
            printable += byte >= 32 && byte <= 126 ? QLatin1Char(static_cast<char>(byte)) : QLatin1Char('.');
        }
        lines.append(QStringLiteral("%1  %2  %3").arg(offset, 8, 16, QLatin1Char('0'))
            .arg(hex.join(' ').leftJustified(47), printable));
    }
    return lines.join('\n');
}

EfiSnapshot readEfiSnapshot(const Device &device)
{
    const auto failure = [](int code) -> EfiSnapshot {
        const auto state = code == EACCES || code == EPERM ? ReadState::PermissionDenied
            : code == ENOENT || code == ENODEV ? ReadState::Removed : ReadState::Error;
        return {{state, {}, code}, {}, false, false};
    };
    const auto identity = [](const struct stat &info) {
        return QStringLiteral("%1:%2").arg(qulonglong(info.st_dev)).arg(qulonglong(info.st_ino));
    };
    if (device.subsystem != "efivarfs") return {{ReadState::NotApplicable, {}, 0}, {}, false, false};
    const auto path = device.path.toUtf8();
    struct stat before {};
    if (lstat(path.constData(), &before) != 0) return failure(errno);
    if (identity(before) != device.incarnation) return failure(ENODEV);
    if (!S_ISREG(before.st_mode) || !(before.st_mode & S_IROTH)) return failure(EACCES);
    const int fd = open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) return failure(errno);
    struct Close { int fd; ~Close() { close(fd); } } closeFile {fd};
    struct stat opened {};
    if (fstat(fd, &opened) != 0) return failure(errno);
    if (identity(opened) != device.incarnation) return failure(ENODEV);
    if (!S_ISREG(opened.st_mode) || !(opened.st_mode & S_IROTH)) return failure(EACCES);

    EfiSnapshot result;
    constexpr int limit = 65536;
    while (result.bytes.size() <= limit) {
        if (QThread::currentThread()->isInterruptionRequested()) return failure(ECANCELED);
        char buffer[4096];
        const auto count = read(fd, buffer, qMin<int>(sizeof(buffer), limit + 1 - result.bytes.size()));
        if (count < 0) {
            if (errno == EINTR) continue;
            result = failure(errno);
            break;
        }
        if (!count) break;
        result.bytes.append(buffer, count);
    }

    struct stat after {};
    if (lstat(path.constData(), &after) != 0) return failure(errno);
    if (identity(after) != device.incarnation) return failure(ENODEV);
    if (!S_ISREG(after.st_mode) || !(after.st_mode & S_IROTH)) return failure(EACCES);
    if (QThread::currentThread()->isInterruptionRequested()) return failure(ECANCELED);
    result.instanceValidated = true;
    if (result.status.error) return result;
    result.status = {ReadState::Available, {}, 0};
    result.truncated = result.bytes.size() > limit;
    result.bytes.truncate(limit);
    return result;
}

QString efiSnapshotText(const Attribute &status, const QByteArray &bytes, bool truncated)
{
    if (status.state != ReadState::Available) return propertyReadValue(status);
    QString text = bytes.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty file") : efiHexDump(bytes);
    if (truncated) text += '\n' + QCoreApplication::translate("PropertiesDialog", "Truncated: first 65536 bytes shown.");
    return text;
}
