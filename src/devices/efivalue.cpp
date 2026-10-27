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

QString readEfiHexDump(const Device &device)
{
    const auto error = [](int code) {
        return propertyReadValue({code == EACCES || code == EPERM ? ReadState::PermissionDenied
            : code == ENOENT || code == ENODEV ? ReadState::Removed : ReadState::Error, {}, code});
    };
    const auto identity = [](const struct stat &info) {
        return QStringLiteral("%1:%2").arg(qulonglong(info.st_dev)).arg(qulonglong(info.st_ino));
    };
    if (device.subsystem != "efivarfs") return propertyReadValue({ReadState::NotApplicable, {}, 0});
    const auto path = device.path.toUtf8();
    struct stat before {};
    if (lstat(path.constData(), &before) != 0) return error(errno);
    if (identity(before) != device.incarnation) return error(ENODEV);
    if (!S_ISREG(before.st_mode) || !(before.st_mode & S_IROTH)) return error(EACCES);
    const int fd = open(path.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return error(errno);
    struct Close { int fd; ~Close() { close(fd); } } closeFile {fd};
    struct stat opened {};
    if (fstat(fd, &opened) != 0) return error(errno);
    if (identity(opened) != device.incarnation) return error(ENODEV);
    if (!S_ISREG(opened.st_mode) || !(opened.st_mode & S_IROTH)) return error(EACCES);
    QByteArray bytes;
    constexpr int limit = 65536;
    while (bytes.size() <= limit) {
        if (QThread::currentThread()->isInterruptionRequested()) return error(ECANCELED);
        char buffer[4096];
        const auto count = read(fd, buffer, qMin<int>(sizeof(buffer), limit + 1 - bytes.size()));
        if (count < 0) { if (errno == EINTR) continue; return error(errno); }
        if (!count) break;
        bytes.append(buffer, count);
    }
    struct stat after {};
    if (lstat(path.constData(), &after) != 0) return error(errno);
    if (identity(after) != device.incarnation) return error(ENODEV);
    if (!(after.st_mode & S_IROTH)) return error(EACCES);
    const bool truncated = bytes.size() > limit;
    bytes.truncate(limit);
    QString result = bytes.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty file") : efiHexDump(bytes);
    if (truncated) result += '\n' + QCoreApplication::translate("PropertiesDialog", "Truncated: first 65536 bytes shown.");
    return result;
}
