#pragma once
#include "device.h"

struct FunctionName {
    QString label; // Untranslated source text; formatting belongs to the UI.
    QString source;
};
FunctionName knownFunctionName(const Device &device);
