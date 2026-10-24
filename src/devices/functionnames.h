#pragma once
#include "device.h"

struct FunctionName {
    QString label; // Untranslated source text; formatting belongs to the UI.
    QString source;
};
FunctionName knownFunctionName(const Device &device);
// Call before starting inventory workers. Publishes a complete immutable snapshot;
// a failed load leaves the previous snapshot intact. No file reads during lookup.
bool loadAcpiFunctionNames(const QString &path, QString &error);
