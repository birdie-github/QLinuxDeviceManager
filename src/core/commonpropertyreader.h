#pragma once
#include "commonproperties.h"
#include <functional>

// Runs synchronously on a worker. Pass a fresh snapshot with device set.
// The optional callback borrows the validated
// sysfs descriptor for device-specific reads before final identity and binding checks.
using CollectDeviceDetails = std::function<void(int fd, const QString &devnode)>;
void collectCommonDeviceProperties(CommonProperties &result, const CollectDeviceDetails &details = {});
