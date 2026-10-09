#include "devicepresentation.h"
#include "devicelabel.h"
#include <QSet>

namespace {
bool pciClass(const Device &d, uint base, int subclass = -1)
{
    if (d.subsystem != "pci") return false;
    bool valid = false;
    const uint code = d.properties.value("PCI_CLASS").toUInt(&valid, 16);
    return valid && (code >> 16) == base
        && (subclass < 0 || ((code >> 8) & 0xff) == static_cast<uint>(subclass));
}
// Follow recorded udev parents to the nearest requested subsystem. PCI callers
// stop at the first function, so an outer controller cannot represent its child.
int ancestor(const Device &d, const QVector<Device> &devices, const QHash<QString, int> &lookup,
             const char *subsystem)
{
    QString path = d.parentPath;
    QSet<QString> visited;
    while (!path.isEmpty() && !visited.contains(path)) {
        visited.insert(path);
        const auto entry = lookup.constFind(path);
        if (entry == lookup.cend()) return -1;
        const Device &parent = devices.at(entry.value());
        if (parent.subsystem == QLatin1String(subsystem)) return entry.value();
        path = parent.parentPath;
    }
    return -1;
}
}

void refinePresentation(QVector<Device> &devices)
{
    QHash<QString, int> lookup;
    for (int i = 0; i < devices.size(); ++i) lookup.insert(devices.at(i).path, i);
    QHash<int, QVector<int>> functions;
    for (int i = 0; i < devices.size(); ++i) {
        const Device &d = devices.at(i);
        if (isHdmiAudioJack(d)) {
            const int card = ancestor(d, devices, lookup, "sound");
            if (card >= 0) devices[i].representedByPath = devices.at(card).path;
        }
        if (d.hidden || (d.subsystem != "net" && d.subsystem != "nvme" && d.subsystem != "sound")) continue;
        const int parent = ancestor(d, devices, lookup, "pci");
        if (parent < 0 || devices.at(parent).hidden) continue;
        if (d.subsystem == "net" && pciClass(devices.at(parent), 2)) {
            functions[parent].append(i);
        } else if (d.subsystem == "sound" && d.sysname.startsWith("card") && pciClass(devices.at(parent), 4)) {
            functions[parent].append(i);
        } else if (d.subsystem == "nvme" && pciClass(devices.at(parent), 1, 8)) {
            // PCI function represents the controller; keep namespace disks separately.
            devices[i].hidden = true;
            devices[i].representedByPath = devices.at(parent).path;
        }
    }
    for (auto it = functions.cbegin(); it != functions.cend(); ++it) {
        if (it.value().size() == 1) {
            const int child = it.value().front();
            devices[child].hidden = true;
            devices[child].representedByPath = devices.at(it.key()).path;
        } else {
            // Multiple cards/ports need their independently useful function rows.
            // Suppress the extra bus row, never collapse distinct cards/interfaces.
            devices[it.key()].hidden = true;
        }
    }
    // A single ALSA card may itself be grouped under its PCI audio function.
    for (auto &d : devices) {
        if (!isHdmiAudioJack(d)) continue;
        const auto card = lookup.constFind(d.representedByPath);
        if (card != lookup.cend() && !devices.at(card.value()).representedByPath.isEmpty())
            d.representedByPath = devices.at(card.value()).representedByPath;
    }
}
