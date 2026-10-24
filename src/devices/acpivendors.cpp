#include "acpivendors.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QStringList>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

AcpiVendorRegistry AcpiVendorRegistry::parse(const QString &text, const QString &source)
{
    AcpiVendorRegistry result;
    result.source_ = source;
    const QRegularExpression match(QStringLiteral("^acpi:([A-Z0-9]{4})\\*:$"));
    QStringList prefixes;
    QString vendor;
    bool unsupported = false, properties = false;
    const auto commit = [&] {
        if (!unsupported && !vendor.isEmpty())
            for (const QString &prefix : prefixes) result.vendors_.insert(prefix, vendor);
        prefixes.clear();
        vendor.clear();
        unsupported = properties = false;
    };
    for (const QString &line : text.split('\n')) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('#')) continue;
        if (trimmed.isEmpty()) { commit(); continue; }
        if (line.size() > 4096) { unsupported = true; continue; }
        if (line[0].isSpace()) {
            properties = true;
            if (trimmed.startsWith("ID_VENDOR_FROM_DATABASE=")) {
                vendor = trimmed.mid(sizeof("ID_VENDOR_FROM_DATABASE=") - 1).trimmed();
                if (vendor.size() > 512) unsupported = true;
            }
        } else {
            const auto record = match.match(trimmed);
            if (properties || !record.hasMatch()) unsupported = true;
            else prefixes.append(record.captured(1));
        }
    }
    commit();
    return result;
}

AcpiVendor AcpiVendorRegistry::lookup(const QString &subsystem, const QString &modalias) const
{
    if (subsystem != "acpi" && subsystem != "platform" && subsystem != "i2c") return {};
    if (!modalias.startsWith("acpi:")) return {};
    // Only the primary HID defines the vendor. Compatible IDs may name other vendors.
    const QString id = modalias.mid(5).section(':', 0, 0).toUpper();
    const QRegularExpression acpiId(QStringLiteral("^[A-Z0-9]{4}[0-9A-F]{4}$"));
    if (!acpiId.match(id).hasMatch()) return {};
    const QString prefix = id.left(4);
    const auto found = vendors_.constFind(prefix);
    if (found == vendors_.cend()) return {};
    return {found.value(), QStringLiteral("%1 : acpi:%2*: (four-character vendor ID from %3)")
        .arg(source_, prefix, id)};
}

namespace {
AcpiVendorRegistry loadRegistry()
{
    // Same-name administrator files replace vendor files, including /dev/null masks.
    for (const QString &directory : {QStringLiteral("/etc/udev/hwdb.d"),
             QStringLiteral("/run/udev/hwdb.d"), QStringLiteral("/usr/local/lib/udev/hwdb.d"),
             QStringLiteral("/usr/lib/udev/hwdb.d"), QStringLiteral("/lib/udev/hwdb.d")}) {
        const QString path = directory + "/20-acpi-vendor.hwdb";
        const QFileInfo entry(path);
        if (!entry.exists() && !entry.isSymLink()) continue;
        constexpr qint64 limit = 1024 * 1024;
        const int fd = open(QFile::encodeName(path).constData(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) return {};
        struct stat info {};
        if (fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > limit) {
            close(fd);
            return {};
        }
        QFile file;
        if (!file.open(fd, QIODevice::ReadOnly, QFileDevice::AutoCloseHandle)) {
            close(fd);
            return {};
        }
        const QByteArray bytes = file.read(limit + 1);
        if (file.error() != QFileDevice::NoError || bytes.size() > limit) return {};
        QStringDecoder decoder(QStringDecoder::Utf8);
        const QString text = decoder(bytes);
        if (decoder.hasError()) return {};
        return AcpiVendorRegistry::parse(text, path);
    }
    return {};
}
}

AcpiVendor acpiVendor(const QString &subsystem, const QString &modalias)
{
    if (!modalias.startsWith("acpi:")) return {};
    static const AcpiVendorRegistry registry = loadRegistry();
    return registry.lookup(subsystem, modalias);
}
