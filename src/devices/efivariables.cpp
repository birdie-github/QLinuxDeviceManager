#include "efivariables.h"
#include <QRegularExpression>
#include <QStringList>
#include <QThread>
#include <algorithm>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

bool splitEfiVariableFilename(const QString &filename, QString &name, QString &guid)
{
    name.clear();
    guid.clear();
    static const QRegularExpression pattern(QStringLiteral(
        "^(.+)-([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})$"));
    const auto match = pattern.match(filename);
    if (!match.hasMatch()) return false;
    name = match.captured(1);
    guid = match.captured(2).toLower();
    return true;
}
QString efiVariableLabel(const QString &name)
{
    // Preserve standard mixed-case spellings before applying case boundaries.
    // Unknown capital runs remain intact; this does not decode vendor meanings.
    static const QStringList preserved {QStringLiteral("WiMAX"), QStringLiteral("WiFi"),
        QStringLiteral("NVMe"), QStringLiteral("PCIe"), QStringLiteral("IPv4"), QStringLiteral("IPv6")};
    const auto preservedAt = [&name](int offset) {
        for (const QString &word : preserved) {
            const int end = offset + static_cast<int>(word.size());
            if (name.mid(offset, word.size()) == word && (end == name.size() || !name[end].isLower()))
                return word;
        }
        return QString();
    };
    QStringList words;
    int offset = 0;
    while (offset < name.size()) {
        if (name[offset].isSpace() || name[offset] == '_' || name[offset] == '-') { ++offset; continue; }
        const QString preservedWord = preservedAt(offset);
        if (!preservedWord.isEmpty()) {
            words.append(preservedWord);
            offset += static_cast<int>(preservedWord.size());
            continue;
        }
        const int start = offset++;
        while (offset < name.size()) {
            const QChar previous = name[offset - 1], current = name[offset];
            if (current.isSpace() || current == '_' || current == '-' || !preservedAt(offset).isEmpty()) break;
            const bool nextLower = offset + 1 < name.size() && name[offset + 1].isLower();
            if ((previous.isLower() && current.isUpper())
                || (previous.isUpper() && current.isUpper() && nextLower)
                || (previous.isDigit() && current.isUpper() && nextLower)) break;
            ++offset;
        }
        const QString word = name.mid(start, offset - start);
        // This known compound joins an identifier and endorsement-key acronym.
        // Preserve other runs, including EDID, UUID and ordinary words like IDLE.
        if (word == "IDEK") {
            words.append("ID");
            words.append(word.mid(2));
        } else words.append(word);
    }
    QString label = words.join(' ');
    static const QRegularExpression boot(QStringLiteral("^(Boot|Driver|SysPrep)([0-9a-fA-F]{4})$"));
    const auto match = boot.match(name);
    if (match.hasMatch()) label = (match.captured(1) == "SysPrep" ? QStringLiteral("Sys Prep") : match.captured(1))
        + ' ' + match.captured(2);
    return label.simplified();
}
void appendEfiVariables(Inventory &inventory)
{
    const QString directory = QStringLiteral("/sys/firmware/efi/efivars");
    const QByteArray encoded = directory.toUtf8();
    const int fd = open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return; // EFI/efivarfs may be absent or unavailable to this account.
    DIR *raw = fdopendir(fd);
    if (!raw) { close(fd); return; }
    std::unique_ptr<DIR, decltype(&closedir)> entries(raw, &closedir);
    QVector<Device> variables;
    int examined = 0;
    while (!QThread::currentThread()->isInterruptionRequested()) {
        const dirent *entry = readdir(entries.get());
        if (!entry) break;
        const QByteArray filename(entry->d_name);
        if (filename == "." || filename == "..") continue;
        if (++examined > 4096) { ++inventory.skipped; break; }
        struct stat info {};
        if (fstatat(dirfd(entries.get()), entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) {
            ++inventory.skipped;
            continue;
        }
        // Root execution must not broaden the public inventory. Never follow links.
        if (!S_ISREG(info.st_mode) || !(info.st_mode & S_IROTH)) continue;
        QString name, guid;
        if (!splitEfiVariableFilename(QString::fromUtf8(filename), name, guid)) continue;
        Device d;
        d.path = directory + '/' + QString::fromUtf8(filename);
        d.parentPath = directory;
        d.sysname = QString::fromUtf8(filename);
        d.subsystem = "efivarfs";
        d.category = "efi";
        d.name = name;
        d.nameSource = "UEFI variable filename; contents not read";
        d.properties.insert("EFI_VENDOR_GUID", guid);
        d.incarnation = QStringLiteral("%1:%2").arg(static_cast<qulonglong>(info.st_dev))
            .arg(static_cast<qulonglong>(info.st_ino));
        variables.append(std::move(d));
    }
    std::sort(variables.begin(), variables.end(), [](const Device &a, const Device &b) { return a.path < b.path; });
    for (auto &d : variables) inventory.devices.append(std::move(d));
}
