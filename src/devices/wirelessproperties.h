#pragma once
#include "device.h"

// Owned worker snapshot. Values retain kernel/API units; UI supplies labels/units.
struct WirelessProperties {
    bool applicable = false;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
    void invalidate(const Attribute &error);
};
// Pure display formatter shared by the Network UI; no queries or widgets.
QString wirelessPropertyText(const QString &key, const Attribute &value);
WirelessProperties collectWirelessProperties(const QString &name, unsigned int index, const QString &path);
