#include "directoryscan.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    QTemporaryDir fixture;
    check(fixture.isValid(), "Temporary fixture directory is available");
    if (!fixture.isValid()) return 1;
    const QDir root(fixture.path());
    check(root.mkdir("zeta") && root.mkdir("alpha") && root.mkdir(".hidden"), "Fixture directories created");
    QFile file(root.filePath("regular"));
    check(file.open(QIODevice::WriteOnly), "Fixture regular file created");
    file.close();
    const auto link = [&](const char *target, const char *name) {
        return symlink(target, root.filePath(QLatin1String(name)).toUtf8().constData()) == 0;
    };
    check(link("alpha", "linked") && link("regular", "file-link") && link("missing", "broken"),
          "Fixture directory, file and dangling links created");
    const DirectoryNames complete = boundedDirectoryNames(root.path(), 3);
    check(!complete.error && complete.names == QStringList({"alpha", "linked", "zeta"}),
          "Listing preserves directory symlinks and sorted names; omits files, broken links, hidden and dot entries");
    const DirectoryNames overflow = boundedDirectoryNames(root.path(), 2);
    check(overflow.error == EOVERFLOW && overflow.names.isEmpty(), "Overflow rejects the partial listing");
    const DirectoryNames filtered = boundedDirectoryNames(root.path(), 1, {"a*"});
    check(!filtered.error && filtered.names == QStringList({"alpha"}), "Name filtering counts only matching directories");
    check(boundedDirectoryNames(root.path(), 0).error == EOVERFLOW, "Zero bound rejects a nonempty listing");
    check(boundedDirectoryNames(root.path(), -1).error == EINVAL, "Negative bound is invalid");
    check(root.mkdir("empty"), "Empty fixture directory created");
    const DirectoryNames empty = boundedDirectoryNames(root.filePath("empty"), 0);
    check(!empty.error && empty.names.isEmpty(), "Empty listing fits a zero bound");
    const DirectoryNames missing = boundedDirectoryNames(root.filePath("missing"), 3);
    check(missing.error == ENOENT && missing.names.isEmpty(), "A missing directory is not a successful empty listing");
    const DirectoryNames notDirectory = boundedDirectoryNames(root.filePath("regular"), 3);
    check(notDirectory.error == ENOTDIR && notDirectory.names.isEmpty(), "A regular file cannot be enumerated as a directory");
    const DirectoryNames inaccessibleChild = boundedDirectoryNames(root.filePath("regular/child"), 3);
    check(inaccessibleChild.error == ENOTDIR && inaccessibleChild.names.isEmpty(), "Invalid ancestor errors are preserved");
    const DirectoryNames caseFiltered = boundedDirectoryNames(root.path(), 1, {"A*"});
    check(!caseFiltered.error && caseFiltered.names == QStringList({"alpha"}), "Qt's case-insensitive wildcard filtering is preserved");
    errno = EIO;
    const DirectoryNames afterError = boundedDirectoryNames(root.filePath("empty"), 0);
    check(!afterError.error && afterError.names.isEmpty(), "Successful EOF does not inherit an earlier errno");
    check(root.mkdir("denied"), "Permission fixture directory created");
    const QByteArray deniedPath = root.filePath("denied").toUtf8();
    check(chmod(deniedPath.constData(), 0) == 0, "Permission fixture made unreadable");
    if (geteuid() != 0) {
        const DirectoryNames denied = boundedDirectoryNames(root.filePath("denied"), 3);
        check(denied.error == EACCES && denied.names.isEmpty(), "Access denied is not a successful empty listing");
        check(link("denied/child", "denied-link"), "Inaccessible symlink target fixture created");
        const DirectoryNames deniedTarget = boundedDirectoryNames(root.path(), 3, {"denied-link"});
        check(deniedTarget.error == EACCES && deniedTarget.names.isEmpty(),
              "An inaccessible symlink target is not silently treated as a dangling link");
    }
    check(chmod(deniedPath.constData(), 0700) == 0, "Permission fixture restored for cleanup");
    class CancelledScan final : public QThread {
    public:
        QString path;
        DirectoryNames result;
    protected:
        void run() override
        {
            requestInterruption();
            result = boundedDirectoryNames(path, 3);
        }
    } worker;
    worker.path = root.path();
    worker.start();
    check(worker.wait(5000), "Cancellation fixture worker completes");
    if (worker.isRunning()) worker.wait();
    check(worker.result.error == ECANCELED && worker.result.names.isEmpty(), "Cancellation never returns partial names");
    return failures ? 1 : 0;
}
