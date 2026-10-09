#pragma once
#include "deviceresources.h"

struct ResourceRow {
    QString key;
    QString type;
    QString setting;
    QString details;
    QString source;
    QString group; // Untranslated resource type ID, or "issues".
};
// Shared Qt Core-only presentation for the Resources tab and tree projections.
QVector<ResourceRow> resourceRows(const DeviceResources &resources);
bool sameDeviceResources(const std::shared_ptr<const DeviceResources> &a,
                         const std::shared_ptr<const DeviceResources> &b);
