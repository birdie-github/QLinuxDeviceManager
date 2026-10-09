#include "device.h"
#include "functionnames.h"
#include "devicelabel.h"

#include <QCoreApplication>
#include <cstring>

const QVector<Category> &categories()
{
    static const QVector<Category> list = {
        {"audio", QT_TRANSLATE_NOOP("Categories", "Audio devices"), "audio-card"},
        {"battery", QT_TRANSLATE_NOOP("Categories", "Batteries and power supplies"), "battery"},
        {"bluetooth", QT_TRANSLATE_NOOP("Categories", "Bluetooth"), "bluetooth"},
        {"camera", QT_TRANSLATE_NOOP("Categories", "Cameras and video devices"), "camera-web"},
        {"disk", QT_TRANSLATE_NOOP("Categories", "Disk drives"), "drive-harddisk"},
        {"display", QT_TRANSLATE_NOOP("Categories", "Display adapters"), "video-display"},
        {"hid", QT_TRANSLATE_NOOP("Categories", "Human interface devices"), "input-gaming"},
        {"keyboard", QT_TRANSLATE_NOOP("Categories", "Keyboards"), "input-keyboard"},
        {"mouse", QT_TRANSLATE_NOOP("Categories", "Mice and pointing devices"), "input-mouse"},
        {"monitor", QT_TRANSLATE_NOOP("Categories", "Display outputs"), "video-display"},
        {"network", QT_TRANSLATE_NOOP("Categories", "Network adapters"), "network-wired"},
        {"cpu", QT_TRANSLATE_NOOP("Categories", "Processors"), "cpu"},
        {"storage", QT_TRANSLATE_NOOP("Categories", "Storage controllers"), "drive-harddisk"},
        {"system", QT_TRANSLATE_NOOP("Categories", "System devices"), "computer"},
        {"usb", QT_TRANSLATE_NOOP("Categories", "USB controllers and devices"), "drive-removable-media-usb"},
        {"other", QT_TRANSLATE_NOOP("Categories", "Other devices"), "preferences-system"}
    };
    return list;
}
QString categoryLabel(const QString &id)
{
    for (const auto &c : categories())
        if (id == QLatin1String(c.id))
            return QCoreApplication::translate("Categories", c.label);
    return QCoreApplication::translate("Categories", "Other devices");
}
QString categoryIcon(const QString &id)
{
    for (const auto &c : categories())
        if (id == QLatin1String(c.id))
            return QString::fromLatin1(c.icon);
    return QStringLiteral("preferences-system");
}
void classify(Device &d)
{
    const QString &s = d.subsystem;
    const QString &n = d.sysname;
    const auto flag = [&d](const char *key) { return d.properties.value(QLatin1String(key)) == "1"; };
    d.category = "other";
    d.hidden = d.path.startsWith("/sys/devices/virtual/");
    if (s == "pci") {
        bool ok = false;
        const uint code = d.properties.value("PCI_CLASS").toUInt(&ok, 16);
        const uint base = code >> 16;
        const uint sub = (code >> 8) & 0xff;
        if (ok && base == 1) d.category = "storage";
        else if (ok && base == 2) d.category = "network";
        else if (ok && base == 3) d.category = "display";
        else if (ok && base == 4) d.category = "audio";
        else if (ok && base == 0x0c && sub == 3) d.category = "usb";
        else d.category = "system";
    } else if (s == "usb") {
        d.category = "usb";
        d.hidden = d.hidden || d.devtype != "usb_device";
    } else if (s == "block") {
        d.category = "disk";
        d.hidden = d.hidden || d.devtype != "disk";
    } else if (s == "input") {
        if (isHdmiAudioJack(d)) { d.category = "audio"; d.hidden = true; }
        else if (flag("ID_INPUT_KEYBOARD")) d.category = "keyboard";
        else if (flag("ID_INPUT_MOUSE") || flag("ID_INPUT_TOUCHPAD") || flag("ID_INPUT_POINTINGSTICK")) d.category = "mouse";
        else d.category = "hid";
        // Keep inputN functions; event/js/mouse endpoints are redundant in the type view.
        d.hidden = d.hidden || !n.startsWith("input");
    } else if (s == "hid" || s == "hidraw") {
        d.category = "hid";
        d.hidden = true;
    } else if (s == "sound") {
        d.category = "audio";
        d.hidden = d.hidden || !n.startsWith("card");
    } else if (s == "drm") {
        d.category = n.contains('-') ? "monitor" : "display";
        // A connector is an output, not evidence of an attached monitor.
        d.hidden = true;
    } else if (s == "net") d.category = "network";
    else if (s == "bluetooth") {
        d.category = "bluetooth";
        d.hidden = d.hidden || !n.startsWith("hci") || n.contains(':');
    } else if (s == "video4linux") d.category = "camera";
    else if (s == "power_supply") d.category = "battery";
    else if (s == "cpu") d.category = "cpu";
    else if (s == "nvme" || s == "scsi_host" || s == "ata_port") {
        d.category = "storage";
        d.hidden = d.hidden || s != "nvme";
    } else if (s == "platform" || s == "acpi" || s == "pnp") {
        d.category = "system";
        d.hidden = d.hidden || (s == "acpi" && d.driver.isEmpty());
    } else {
        // Unrecognized directly bound physical functions remain useful fallbacks.
        d.hidden = d.hidden || d.driver.isEmpty();
    }
}
void nameDevice(Device &d)
{
    d.nameTranslated = false;
    d.nameCandidate = false;
    const auto attributeName = [&d](const QString &key) {
        const auto it = d.attributes.constFind(key);
        if (it == d.attributes.cend() || it->state != ReadState::Available || it->value.isEmpty()) return false;
        d.name = it->value;
        d.nameSource = QStringLiteral("sysfs: %1/%2").arg(d.path, key);
        return true;
    };
    if (d.subsystem == "cpu") {
        const auto model = d.attributes.value("cpu_model");
        if (model.state == ReadState::Available && !model.value.isEmpty()) {
            d.name = model.value;
            d.nameSource = QStringLiteral("/proc/cpuinfo: processor %1").arg(d.sysname.mid(3));
            return;
        }
    }
    // Function names take priority over generic USB receiver/camera model names.
    if ((d.subsystem == "input" || d.subsystem == "video4linux") && attributeName("name")) return;
    if (d.subsystem == "input") {
        QString name = d.properties.value("NAME");
        if (name.startsWith('"') && name.endsWith('"') && name.size() >= 2) name = name.mid(1, name.size() - 2);
        if (!name.isEmpty()) { d.name = name; d.nameSource = "udev: NAME"; return; }
    }
    if (d.subsystem == "video4linux") {
        const QString name = d.properties.value("ID_V4L_PRODUCT");
        if (!name.isEmpty()) { d.name = name; d.nameSource = "udev: ID_V4L_PRODUCT"; return; }
    }
    if (d.subsystem == "nvme" && attributeName("model")) return;
    if (d.subsystem == "hdaudio") {
        const auto chip = d.attributes.value("chip_name");
        const auto vendor = d.attributes.value("vendor_name");
        if (chip.state == ReadState::Available && !chip.value.isEmpty()) {
            d.name = chip.value;
            if (vendor.state == ReadState::Available && !vendor.value.isEmpty()) d.name.prepend(vendor.value + ' ');
            d.nameSource = QStringLiteral("sysfs: %1/chip_name and available vendor_name").arg(d.path);
            return;
        }
    }
    // Replace generic database root-hub text only for verified Linux root hubs.
    if (d.subsystem == "usb") {
        const FunctionName rootHub = knownFunctionName(d);
        if (!rootHub.label.isEmpty()) {
            d.name = rootHub.label;
            d.nameSource = rootHub.source;
            d.nameTranslated = true;
            return;
        }
    }
    for (const char *key : {"ID_MODEL_FROM_DATABASE", "ID_MODEL", "NAME"}) {
        if (std::strcmp(key, "ID_MODEL") == 0 && d.subsystem == "usb"
            && d.devtype == "usb_device" && attributeName("product")) return;
        const QString value = d.properties.value(QLatin1String(key));
        if (!value.isEmpty()) {
            d.name = value;
            d.nameSource = d.propertySources.value(QLatin1String(key), QStringLiteral("udev: %1").arg(QLatin1String(key)));
            return;
        }
    }
    if (d.subsystem == "sound" && attributeName("id")) return;
    if (d.subsystem == "power_supply" && attributeName("model_name")) return;
    const FunctionName function = knownFunctionName(d);
    if (!function.label.isEmpty()) {
        d.name = function.label;
        d.nameSource = function.source;
        d.nameTranslated = true;
        return;
    }
    d.name = d.sysname.isEmpty() ? d.path : d.sysname;
    d.nameSource = "kernel name";
    // Describe unresolved bound functions using real binding evidence, without
    // claiming that a module/driver name is the hardware's marketing model.
    if (!d.driver.isEmpty() && d.subsystem != "cpu") {
        d.name = QStringLiteral("%1 — %2").arg(d.driver, d.name);
        d.nameSource = "direct kernel driver and kernel name";
    }
}
void reconcile(QVector<Device> &next, const QVector<Device> &previous, quint64 &generation)
{
    QHash<QString, const Device *> old;
    for (const auto &d : previous) old.insert(d.path, &d);
    for (auto &d : next) {
        const Device *before = old.value(d.path, nullptr);
        d.generation = before && before->incarnation == d.incarnation
            ? before->generation : ++generation;
    }
}
