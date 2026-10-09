#include "monitorstate.h"
#include <algorithm>

namespace {
bool related(const QString &a, const QString &b)
{
    return a == b || a.startsWith(b + '/') || b.startsWith(a + '/');
}
}
void MonitorState::observe(const QString &path, bool removal)
{
    if (!path.startsWith("/sys/devices/")) return;
    dirty = true;
    if (changed.size() >= Limit || removed.size() >= Limit) { lose(); return; }
    changed.insert(path);
    if (removal) removed.insert(path);
}
void MonitorState::lose()
{
    dirty = true;
    lost = true;
    changed.clear();
    removed.clear();
}
void MonitorState::beginScan()
{
    changed.clear();
    dirty = false;
    // Removal/identity-loss evidence survives all scans until GUI publication.
}
void MonitorState::excludeUnsettled(Inventory &inventory) const
{
    if (!dirty) return;
    if (lost) {
        inventory.skipped += static_cast<int>(inventory.devices.size());
        inventory.devices.clear();
        return;
    }
    QSet<QString> excluded = changed;
    // Include real ancestry and presentation dependencies. An ancestor's metadata
    // may supply a child's name, grouping or driver relationship, and vice versa.
    bool extended;
    do {
        extended = false;
        for (const Device &d : inventory.devices) {
            if (excluded.contains(d.path)) {
                if (!d.representedByPath.isEmpty() && !excluded.contains(d.representedByPath)) {
                    excluded.insert(d.representedByPath);
                    extended = true;
                }
                continue;
            }
            bool affected = !d.representedByPath.isEmpty() && excluded.contains(d.representedByPath);
            if (!affected) {
                for (const QString &path : changed)
                    if (related(d.path, path)) { affected = true; break; }
            }
            if (affected) { excluded.insert(d.path); extended = true; }
        }
    } while (extended);
    const auto end = std::remove_if(inventory.devices.begin(), inventory.devices.end(),
        [&](const Device &d) { return excluded.contains(d.path); });
    inventory.skipped += static_cast<int>(std::distance(end, inventory.devices.end()));
    inventory.devices.erase(end, inventory.devices.end());
}
