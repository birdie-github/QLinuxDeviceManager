#include "icontheme.h"
#include "device.h"
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QPixmap>
#include <QPointer>
#include <array>

#ifdef QLDM_HAVE_ICON_PATHS
#include "iconpaths.h"
#endif
#ifndef QLDM_ICON_THEME_DIR
#define QLDM_ICON_THEME_DIR ""
#endif

namespace {
constexpr std::array<int, 9> sizes = {16, 20, 24, 32, 40, 48, 64, 96, 128};
const QStringList &roles()
{
    static const QStringList names = {
        "computer", "audio", "battery", "bluetooth", "camera", "disk", "graphics-card",
        "human-interface", "keyboard", "mouse", "monitor", "network", "processor",
        "storage-controller", "system-device", "usb", "uefi", "other-device",
        "system-information", "refresh", "properties", "ethernet", "network-card",
        "ssd", "usb-memory-stick", "nvme"
    };
    return names;
}
QString rootPath()
{
#ifdef QLDM_RELATIVE_ICON_THEME_DIR
    const QString relative = QDir(QCoreApplication::applicationDirPath())
        .filePath(QString::fromUtf8(QLDM_RELATIVE_ICON_THEME_DIR));
    if (QFileInfo(relative).isDir()) return relative;
#endif
    return QString::fromUtf8(QLDM_ICON_THEME_DIR);
}
QString imagePath(const QDir &directory, int size, const QString &role)
{
    return directory.filePath(QStringLiteral("%1x%1/%2.png").arg(size).arg(role));
}
}

IconTheme &IconTheme::instance()
{
    // Release cached GUI resources with QApplication, including fixture applications.
    static QPointer<IconTheme> instance;
    if (!instance) instance = new IconTheme(QCoreApplication::instance());
    return *instance.data();
}
QStringList IconTheme::availableThemes() const
{
    QStringList names;
    if (rootPath().isEmpty()) return names;
    const QDir root(rootPath());
    for (const QString &name : root.entryList(QDir::Dirs | QDir::Readable | QDir::NoDotAndDotDot, QDir::Name)) {
        const QDir directory(root.filePath(name));
        bool found = false;
        for (int size : sizes) {
            for (const QString &role : roles()) {
                if (QFileInfo(imagePath(directory, size, role)).isFile()) { found = true; break; }
            }
            if (found) break;
        }
        if (found) names.append(name);
    }
    return names;
}
void IconTheme::setTheme(const QString &name)
{
    icons_.clear();
    // Match discovered directory names exactly; never interpret settings as a path.
    theme_ = availableThemes().contains(name) ? name : QString();
    if (theme_.isEmpty()) return;
    const QDir directory(QDir(rootPath()).filePath(theme_));
    for (const QString &role : roles()) {
        QIcon result;
        for (int size : sizes) {
            const QString path = imagePath(directory, size, role);
            const QFileInfo file(path);
            if (!file.isFile() || file.size() > 1024 * 1024) continue;
            QImageReader reader(path, "png");
            // Bound decoded dimensions; reject mislabeled, corrupt and oversized assets.
            if (reader.size() != QSize(size, size)) continue;
            const QImage image = reader.read();
            if (!image.isNull()) result.addPixmap(QPixmap::fromImage(image));
        }
        if (!result.isNull()) icons_.insert(role, result);
    }
}
QIcon IconTheme::icon(const QString &role, const QString &systemName,
                      QStyle::StandardPixmap fallback, const QString &category) const
{
    auto found = icons_.constFind(role);
    if (found == icons_.cend() && !category.isEmpty()) found = icons_.constFind(category);
    if (found != icons_.cend()) return found.value();
    return QIcon::fromTheme(systemName, QApplication::style()->standardIcon(fallback));
}
QString IconTheme::categoryRole(const QString &category)
{
    if (category == "display") return QStringLiteral("graphics-card");
    if (category == "hid") return QStringLiteral("human-interface");
    if (category == "cpu") return QStringLiteral("processor");
    if (category == "storage") return QStringLiteral("storage-controller");
    if (category == "system") return QStringLiteral("system-device");
    if (category == "efi") return QStringLiteral("uefi");
    if (category == "other" || category.isEmpty()) return QStringLiteral("other-device");
    return category;
}
QString IconTheme::deviceRole(const Device &device)
{
    if (device.category == "network") {
        bool valid = false;
        const uint pciClass = device.properties.value("PCI_CLASS").toUInt(&valid, 16);
        if (device.subsystem == "pci" && valid && (pciClass >> 8) == 0x0200)
            return QStringLiteral("ethernet");
        return QStringLiteral("network-card");
    }
    if (device.subsystem == "block" && device.devtype == "disk") {
        const QString bus = device.properties.value("ID_BUS");
        if (bus == "usb" && device.properties.value("ID_DRIVE_THUMB") == "1") return QStringLiteral("usb-memory-stick");
        if (bus == "nvme") return QStringLiteral("nvme");
        const auto rotation = device.attributes.constFind("queue/rotational");
        if (bus == "ata" && rotation != device.attributes.cend()
            && rotation->state == ReadState::Available && rotation->value == "0")
            return QStringLiteral("ssd");
    }
    return categoryRole(device.category);
}
