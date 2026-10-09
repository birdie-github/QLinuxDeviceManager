#pragma once
#include "device.h"

// Presentation only: retain raw inventory names and perform no metadata reads.
QString inputDeviceName(const Device &device);
bool isHdmiAudioJack(const Device &device);
QString deviceDisplayName(const Device &device, bool includeIdentifiers = false);
