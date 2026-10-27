#pragma once
#include "deviceproperties.h"
#include "commonproperties.h"

// Empty suffix means this entry contains an available value.
QString detailsPropertyStatus(const PropertyEntry &entry);
QString storageFieldValue(const StorageField &field);
QString storageDuration(quint64 seconds);
// UI boundary: storage entries remain searchable, but never enter Details or its copy output.
bool isDetailsProperty(const PropertyEntry &entry, const Device &device);
QVector<PropertyEntry> propertyEntries(const DeviceProperties &snapshot, bool removed = false);
