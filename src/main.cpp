#include "mainwindow.h"
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
    MainWindow window;
    window.show();
    return app.exec();
}
