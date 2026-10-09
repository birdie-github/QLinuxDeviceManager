#pragma once
#include "deviceproperties.h"

// Pure text presentation shared by Properties and deep search; no widgets.
struct PropertyEntry {
    QString id;
    QString label;
    QString value;
    QString source;
    bool advanced = false;
    int tab = 2;
};
QString propertyReadValue(const Attribute &attribute);
QVector<PropertyEntry> propertyEntries(const DeviceProperties &snapshot, bool removed = false,
                                      const QString &resourcesText = {});
