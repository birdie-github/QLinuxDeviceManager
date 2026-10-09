#include "deviceevents.h"
#include "device.h"
#include <QSet>
#include <utility>

DeviceEventHistory::DeviceEventHistory(const QDateTime &started)
{
    snapshot_.started = started;
}
DeviceEventHistory::Identity *DeviceEventHistory::identity(const QString &path)
{
    auto found = identities_.find(path);
    if (found != identities_.end()) return &found.value();
    if (identities_.size() >= IdentityLimit) return nullptr;
    return &identities_.insert(path, {}).value();
}
void DeviceEventHistory::retire(const QString &path)
{
    for (auto it = identities_.begin(); it != identities_.end(); ++it)
        if (it.key() == path || it.key().startsWith(path + '/')) it->retired = true;
}
void DeviceEventHistory::observe(DeviceEvent event)
{
    if (!event.path.startsWith("/sys/devices/") || event.path.size() > 4096) return;
    // Bound all caller-owned strings too, including fixture/future collectors.
    for (QString *value : {&event.path, &event.action, &event.subsystem, &event.devtype,
                           &event.driver, &event.initialized, &event.incarnation, &event.oldPath})
        value->truncate(4096);
    event.instance = 0; // Only this tracker may assign an association.
    Identity *current = identity(event.path);
    const bool stale = current && event.sequence && current->sequence && event.sequence <= current->sequence;
    if (stale) current = nullptr; // Reordered/duplicate payloads cannot change identity state.
    if (current) {
        const bool stampChanged = !current->initialized.isEmpty() && !event.initialized.isEmpty()
            && current->initialized != event.initialized;
        const bool inodeChanged = !current->incarnation.isEmpty() && !event.incarnation.isEmpty()
            && current->incarnation != event.incarnation;
        if (event.action == "add") {
            if (!current->token || current->retired || stampChanged || inodeChanged)
                *current = {++nextToken_, event.incarnation, event.initialized, false, event.sequence};
            event.instance = current->token;
        } else if (current->token && !stampChanged && !inodeChanged && (!current->retired || event.action == "remove")) {
            // Removal may have no readable inode, but can use a known stream
            // identity. A late event with conflicting evidence stays unassociated.
            event.instance = current->token;
        } else if (!current->token && event.action != "remove" && !event.incarnation.isEmpty()) {
            *current = {++nextToken_, event.incarnation, event.initialized, false, event.sequence};
            event.instance = current->token;
        }
        if (event.sequence > current->sequence) current->sequence = event.sequence;
        if (event.action == "remove" && !stampChanged && !inodeChanged) retire(event.path);
    }
    if (!stale && !event.oldPath.isEmpty() && event.oldPath != event.path) retire(event.oldPath);
    if (!event.instance) ++snapshot_.unassociated;
    if (snapshot_.records.size() == DeviceEventsSnapshot::Limit) {
        snapshot_.records.removeFirst();
        ++snapshot_.evicted;
    }
    snapshot_.records.append(std::move(event));
}
void DeviceEventHistory::lose()
{
    identities_.clear();
    ++snapshot_.gaps;
}
void DeviceEventHistory::reconcile(QVector<Device> &devices)
{
    QSet<QString> present;
    for (Device &device : devices) {
        present.insert(device.path);
        Identity *current = identity(device.path);
        device.eventInstance = 0;
        if (!current) continue;
        const QString initialized = device.properties.value("USEC_INITIALIZED");
        const bool same = current->token && !current->retired
            && ((!current->incarnation.isEmpty() && current->incarnation == device.incarnation)
                || (current->incarnation.isEmpty() && !current->initialized.isEmpty()
                    && current->initialized == initialized));
        if (!same) *current = {++nextToken_, device.incarnation, initialized, false, 0};
        else {
            current->incarnation = device.incarnation;
            current->initialized = initialized;
        }
        device.eventInstance = current->token;
    }
    // Missing/withheld records are not allowed to retain live associations.
    // Their already-observed events remain in the bounded ring for old dialogs.
    for (auto it = identities_.begin(); it != identities_.end();) {
        if (!present.contains(it.key())) it = identities_.erase(it);
        else ++it;
    }
}
DeviceEventsSnapshot DeviceEventHistory::snapshot(bool monitoring) const
{
    DeviceEventsSnapshot result = snapshot_;
    result.monitoring = monitoring;
    return result;
}
