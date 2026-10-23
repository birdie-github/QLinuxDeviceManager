#include "mainwindow.h"
#include "gpulinktest.h"
#include "cameraproperties.h"
#include <cstring>
#include <cstdlib>
#include <climits>
#include <cerrno>
#include "projectmetadata.h"
#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QLibraryInfo>
#include <QLocale>
#include <QStandardPaths>
#include <QTranslator>
#include <unistd.h>

int main(int argc, char **argv)
{
    // Private hardware-query child: no GUI, settings, session access or capture.
    if (argc == 5 && std::strcmp(argv[1], "--camera-query") == 0) {
        char *end = nullptr;
        errno = 0;
        const long directory = std::strtol(argv[4], &end, 10);
        if (errno || end == argv[4] || !end || *end || directory < 0 || directory > INT_MAX) return 1;
        return runCameraQueryHelper(static_cast<int>(directory), QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    }
    // Never write root settings into the sudo caller's XDG directories.
    // This does not modify display/session authorization.
    if (geteuid() == 0) {
        qputenv("XDG_CONFIG_HOME", "/root/.config");
        qputenv("XDG_DATA_HOME", "/root/.local/share");
        qputenv("XDG_CACHE_HOME", "/root/.cache");
    }
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QLDM_NAME);
    QCoreApplication::setApplicationName(QLDM_NAME);
    QCoreApplication::setApplicationVersion(QLDM_VERSION);
    app.setDesktopFileName(QStringLiteral("io.github.birdie-github.QLinuxDeviceManager"));
    QIcon applicationIcon;
    for (const int size : {16, 32, 48, 64, 128, 256, 512})
        applicationIcon.addFile(QStringLiteral(":/icons/%1x%1/QLinuxDeviceManager.png").arg(size));
    app.setWindowIcon(applicationIcon);
    QTranslator qtTranslator;
    if (qtTranslator.load(QLocale(), "qtbase", "_", QLibraryInfo::path(QLibraryInfo::TranslationsPath)))
        app.installTranslator(&qtTranslator);
    QTranslator translator;
    QStringList locations = QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);
    locations.prepend(QDir(QCoreApplication::applicationDirPath()).absoluteFilePath("../share"));
    for (const auto &location : locations) {
        if (translator.load(QLocale(), "qlinuxdevicemanager", "_",
                            location + "/QLinuxDeviceManager/translations")) {
            app.installTranslator(&translator);
            break;
        }
    }
    if (argc == 4 && std::strcmp(argv[1], "--gpu-link-test") == 0)
        return runGpuLinkTest(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]));
    MainWindow window;
    window.show();
    return app.exec();
}
