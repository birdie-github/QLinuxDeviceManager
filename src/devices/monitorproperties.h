#pragma once

#include "device.h"
#include <QVector>

struct MonitorTiming {
    enum class Origin { BaseDetailed, CtaDetailed, CtaVideo, CtaVideo420Only };
    Origin origin;
    int width = 0;
    int height = 0;
    double refreshHz = 0; // Field rate for interlaced video.
    bool interlaced = false;
    bool preferred = false;
    bool native = false;
    int vic = 0;
};

// Cached hardware reports from this DRM connector, separate from session settings.
struct MonitorProperties {
    bool applicable = false;
    Attribute descriptor;
    QVector<MonitorTiming> timings;
    bool timingsTruncated = false;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
};

// Explicit Properties snapshots only. The caller owns the selected connector fd.
MonitorProperties collectMonitorProperties(int directory, const QString &path);
// Estimated PPI from the mode with most pixels and reported physical dimensions.
// Empty if either input is unavailable or contains no usable dimensions.
QString monitorPixelsPerInch(const MonitorProperties &monitor);
