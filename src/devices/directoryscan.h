#pragma once
#include <QDirListing>
#include <QStringList>
#include <QThread>
#include <cerrno>

struct DirectoryNames {
    QStringList names;
    int error = 0;
};

// Nonrecursive sysfs directory listing. Resolve links for type filtering, retain
// link names, omit hidden/dot entries, and sort only after collecting a bounded set.
// Like QDir::entryList, an inaccessible/missing directory yields an empty list;
// callers that need an access error must validate the directory separately.
inline DirectoryNames boundedDirectoryNames(const QString &path, qsizetype limit,
                                            const QStringList &filters = {})
{
    if (limit < 0) return {{}, EINVAL};
    if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
    DirectoryNames result;
    using Flag = QDirListing::IteratorFlag;
    for (const auto &entry : QDirListing(path, filters, Flag::DirsOnly | Flag::ResolveSymlinks)) {
        if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
        if (result.names.size() >= limit) return {{}, EOVERFLOW};
        result.names.append(entry.fileName());
    }
    if (QThread::currentThread()->isInterruptionRequested()) return {{}, ECANCELED};
    result.names.sort(Qt::CaseSensitive);
    return result;
}
