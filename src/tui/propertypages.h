#pragma once
#include "commonproperties.h"

// Adapt the inventory without claiming that on-demand properties were read.
CommonProperties inventoryCommonProperties(const Device &device);
QString commonPropertyPage(const QVector<PropertyEntry> &entries, int tab);
QString inventoryDetailsPage(const Device &device);
