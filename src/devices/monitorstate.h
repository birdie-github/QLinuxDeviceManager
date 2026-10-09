#pragma once
#include "device.h"

// Owned, bounded hints. Event payloads never become inventory records.
struct MonitorState {
    static constexpr int Limit = 4096;
    QSet<QString> changed;
    QSet<QString> removed;
    bool lost = false;
    bool dirty = false;
    void observe(const QString &path, bool removal);
    void lose();
    void beginScan();
    void excludeUnsettled(Inventory &inventory) const;
};
