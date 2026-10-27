#pragma once
#include "device.h"
#include <functional>

// Synchronous snapshot only: no monitor, event loop or frontend ownership.
Inventory collectInventory(const std::function<bool()> &cancelled = {});
// Projection edges bridge only real directory ancestry; inventory stays unchanged.
QHash<QString, QString> connectionParents(const QVector<Device> &devices);
