#pragma once
#include "displayproperties.h"
#include <QLibrary>

namespace DisplayBackend {
constexpr int outputLimit = 256;
constexpr int textLimit = 4096;
constexpr int queryTimeoutMs = 2000;
inline Attribute available(const QString &value) { return {ReadState::Available, value, 0}; }
inline Attribute error(int number) { return {ReadState::Error, {}, number}; }
inline QString number(double value) { return QString::number(value, 'g', 8); }
inline QString canonicalName(QString name)
{
    // RandR's modesetting driver uses HDMI-N for the kernel's HDMI-A-N.
    if (name.startsWith("HDMI-A-")) name.replace(0, 7, "HDMI-");
    return name;
}
template<class Function> bool resolve(QLibrary &library, const char *name, Function &function)
{
    function = reinterpret_cast<Function>(library.resolve(name));
    return function != nullptr;
}
DisplayProperties x11(const QString &connector, const QString &kernelPath);
DisplayProperties wayland(const QString &connector);
}
