#pragma once
#include "device.h"
#include "commonpropertywording.h"

// Pure text presentation shared by Properties and deep search; no widgets.
struct PropertyEntry {
    QString id;
    QString label;
    QString value;
    QString source;
    bool advanced = false;
    int tab = 2;
    ReadState state = ReadState::Available;
    int error = 0;
};

// Only common captured fields: independent of hardware-specific collectors.
struct CommonProperties {
    Device device;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
    ReadState state = ReadState::Unavailable;
    int error = 0;
};
QString propertyReadValue(const Attribute &attribute);
// Only for sources known to use administrative access.
QString administrativeReadValue(const Attribute &attribute);
QString administrativeReadHint(const Attribute &attribute);
QVector<PropertyEntry> commonPropertyEntries(const CommonProperties &snapshot, bool removed = false,
                                           PropertyPresentation presentation = PropertyPresentation::Gui);
