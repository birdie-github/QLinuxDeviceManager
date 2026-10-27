#include "typeprojection.h"
#include "inventory.h"
#include "devicelabel.h"
#include <QCoreApplication>
#include <algorithm>

TypeProjection typeProjection(const QVector<Device> &devices, bool showAll)
{
    const auto ancestry = connectionParents(devices);
    QHash<QString, const Device *> records;
    for (const Device &device : devices) records.insert(device.path, &device);
    TypeProjection result;
    for (const Device &device : devices) {
        if (showAll || !device.hidden) result.included.insert(device.path);
        const QString parent = ancestry.value(device.path);
        const auto found = records.constFind(parent);
        if (found == records.cend() || parent == device.path) continue;
        const Device &ancestor = **found;
        if (device.subsystem == "acpi") {
            // ACPI namespace ancestry only: no ID/name/physical-device guesses.
            if (ancestor.subsystem == "acpi") result.parents.insert(device.path, parent);
        } else if (showAll && (device.subsystem == "net" || device.subsystem == "sound"
                || device.subsystem == "drm" || device.subsystem == "bluetooth"
                || !device.representedByPath.isEmpty()
                || (device.subsystem == "usb" && (device.devtype == "usb_interface"
                    || device.sysname.startsWith("usb"))))) {
            result.parents.insert(device.path, parent);
        }
    }
    // Malformed fixture/kernel metadata must not produce recursive projections.
    auto paths = result.parents.keys();
    std::sort(paths.begin(), paths.end());
    for (const QString &start : paths) {
        QSet<QString> visited;
        QString path = start;
        while (result.parents.contains(path)) {
            visited.insert(path);
            const QString parent = result.parents.value(path);
            if (visited.contains(parent)) { result.parents.remove(path); break; }
            path = parent;
        }
    }
    // Context ancestors may be normally hidden. Keep each record exactly once.
    const auto seeds = result.included.values();
    for (const QString &start : seeds) {
        QString path = start;
        while (result.parents.contains(path)) {
            path = result.parents.value(path);
            result.included.insert(path);
        }
    }
    return result;
}

QString typeDeviceLabel(const Device &device, bool nested, bool includeIdentifiers)
{
    if (!nested) return deviceDisplayName(device, includeIdentifiers);
    const char *role = nullptr;
    const QString &subsystem = device.subsystem;
    const QString &name = device.sysname;
    if (subsystem == "net") role = QT_TRANSLATE_NOOP("DeviceLabels", "Network interface");
    else if (subsystem == "sound") {
        if (name.startsWith("card")) role = QT_TRANSLATE_NOOP("DeviceLabels", "ALSA sound card");
        else if (name.startsWith("controlC")) role = QT_TRANSLATE_NOOP("DeviceLabels", "ALSA control node");
        else if (name.startsWith("pcmC")) role = QT_TRANSLATE_NOOP("DeviceLabels", "ALSA PCM node");
        else if (name.startsWith("hwC")) role = QT_TRANSLATE_NOOP("DeviceLabels", "ALSA hardware-dependent node");
    } else if (subsystem == "drm") {
        if (name.startsWith("renderD")) role = QT_TRANSLATE_NOOP("DeviceLabels", "DRM render node");
        else if (name.startsWith("card") && !name.contains('-')) role = QT_TRANSLATE_NOOP("DeviceLabels", "DRM primary node");
        else if (name.startsWith("card") && name.contains('-')) role = QT_TRANSLATE_NOOP("DeviceLabels", "Display output");
    } else if (subsystem == "bluetooth") {
        role = name.contains(':') ? QT_TRANSLATE_NOOP("DeviceLabels", "Bluetooth child device")
                                  : QT_TRANSLATE_NOOP("DeviceLabels", "Bluetooth controller");
    } else if (subsystem == "usb" && device.devtype == "usb_interface")
        role = QT_TRANSLATE_NOOP("DeviceLabels", "USB interface");
    // Role labels always retain the endpoint identifier, even in the normal view.
    return role ? QCoreApplication::translate("DeviceLabels", role) + QStringLiteral(" [%1]").arg(name)
                : deviceDisplayName(device, includeIdentifiers);
}
