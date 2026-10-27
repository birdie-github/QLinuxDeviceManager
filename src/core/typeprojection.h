#pragma once
#include "device.h"

// A read-only type-view projection. Root devices retain their own category;
// nested endpoints inherit their root's placement, not its hardware identity.
struct TypeProjection {
    QHash<QString, QString> parents;
    QSet<QString> included;
};
TypeProjection typeProjection(const QVector<Device> &devices, bool showAll);
QString typeDeviceLabel(const Device &device, bool nested, bool includeIdentifiers);
