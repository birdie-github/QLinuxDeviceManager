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
    ReadState state = ReadState::Available;
    int error = 0;
};
// Empty suffix means this entry contains an available value.
QString detailsPropertyStatus(const PropertyEntry &entry);
QString storageFieldValue(const StorageField &field);
// UI boundary: storage entries remain searchable, but never enter Details or its copy output.
bool isDetailsProperty(const PropertyEntry &entry, const Device &device);
QString propertyReadValue(const Attribute &attribute);
QVector<PropertyEntry> propertyEntries(const DeviceProperties &snapshot, bool removed = false);
