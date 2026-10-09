#include "directoryscan.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <cstdio>
#include <unistd.h>

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
