#pragma once
#include <QDir>
#include <QFile>
#include <QStringList>
#include <QThread>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>

struct DirectoryNames {
    QStringList names;
    int error = 0;
};

// Nonrecursive directory listing. Resolve links for type filtering, retain link
// names, omit hidden/dot entries, and sort only after collecting a bounded set.
// Every failure rejects the partial list. Callers decide whether ENOENT means an
// absent optional directory; an empty successful list is never an access error.
inline DirectoryNames boundedDirectoryNames(const QString &path, qsizetype limit,
                                            const QStringList &filters = {})
{
    if (limit < 0 || path.contains(QChar::Null)) return {{}, EINVAL};
    if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
    const QByteArray encoded = QFile::encodeName(path.isEmpty() ? QStringLiteral(".") : path);
    const int fd = open(encoded.constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return {{}, errno};
    DIR *raw = fdopendir(fd);
    if (!raw) { const int error = errno; close(fd); return {{}, error}; }
    const auto closeDirectory = [](DIR *directory) { closedir(directory); };
    std::unique_ptr<DIR, decltype(closeDirectory)> directory(raw, closeDirectory);
    DirectoryNames result;
    for (;;) {
        if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
        // readdir leaves errno unchanged at EOF. Clear it for each call so a
        // previous metadata operation cannot turn successful EOF into a failure.
        errno = 0;
        const dirent *entry = readdir(directory.get());
        if (!entry) {
            if (errno) return {{}, errno};
            break;
        }
        if (entry->d_name[0] == '.') continue;
        const QString name = QFile::decodeName(entry->d_name);
        if (!filters.isEmpty() && !QDir::match(filters, name)) continue;
        struct stat info {};
        if (fstatat(fd, entry->d_name, &info, AT_SYMLINK_NOFOLLOW) != 0) return {{}, errno};
        if (S_ISLNK(info.st_mode)) {
            if (fstatat(fd, entry->d_name, &info, 0) != 0) {
                // Preserve ResolveSymlinks behavior: dangling links are omitted.
                // Access and I/O failures must not masquerade as dangling links.
                if (errno == ENOENT || errno == ENOTDIR) continue;
                return {{}, errno};
            }
        }
        if (!S_ISDIR(info.st_mode)) continue;
        if (result.names.size() >= limit) return {{}, EOVERFLOW};
        result.names.append(name);
    }
    if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
    result.names.sort(Qt::CaseSensitive);
    return result;
}
