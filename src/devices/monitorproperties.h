#pragma once

#include "device.h"

// Cached hardware reports from this DRM connector, separate from session settings.
struct MonitorProperties {
    bool applicable = false;
    Attribute descriptor;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
};

// Explicit Properties snapshots only. The caller owns the selected connector fd.
MonitorProperties collectMonitorProperties(int directory, const QString &path);
// Estimated PPI from the mode with most pixels and reported physical dimensions.
// Empty if either input is unavailable or contains no usable dimensions.
QString monitorPixelsPerInch(const MonitorProperties &monitor);
