#pragma once
#include "device.h"

// Cross-record presentation rules; never delete inventory or alter ancestry/binding.
void refinePresentation(QVector<Device> &devices);
