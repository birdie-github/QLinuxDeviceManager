#include "displaybackend.h"
#include "directoryscan.h"
#include <cerrno>

DisplayProperties collectDisplayProperties(const Device &device, const Attribute &connectionStatus)
{
    DisplayProperties result;
    if (device.subsystem != "drm" || !device.sysname.contains('-')) {
        result.status.state = ReadState::NotApplicable;
        return result;
    }
    // Read by the properties worker from the open device directory, rather
    // than using the potentially stale enumeration snapshot.
    if (connectionStatus.state == ReadState::Available && connectionStatus.value == "disconnected") {
        result.status = DisplayBackend::available("disconnected");
        result.sources.insert("configuration", device.path + "/status");
        return result;
    }
    const QString connector = device.sysname.mid(device.sysname.indexOf('-') + 1);
    // Session protocols name outputs, not GPU-qualified kernel connectors.
    // Do not silently attribute eDP-1/DP-1 to the wrong GPU on multi-card systems.
    const DirectoryNames names = boundedDirectoryNames("/sys/class/drm", DisplayBackend::outputLimit * 2);
    if (names.error) { result.status = DisplayBackend::error(names.error); return result; }
    int matches = 0;
    for (const QString &name : names.names) {
        const int separator = name.indexOf('-');
        if (separator >= 0 && DisplayBackend::canonicalName(name.mid(separator + 1))
            == DisplayBackend::canonicalName(connector)) ++matches;
    }
    if (matches != 1) { result.status = DisplayBackend::error(matches ? ENOTUNIQ : ENODEV); return result; }
    const QByteArray session = qgetenv("XDG_SESSION_TYPE");
    const bool wayland = session == "wayland" || (session != "x11" && !qgetenv("WAYLAND_DISPLAY").isEmpty());
    if (wayland) result = DisplayBackend::wayland(connector);
    else if (!qgetenv("DISPLAY").isEmpty()) result = DisplayBackend::x11(connector, device.path);
    // Never fall back to XWayland's emulated outputs after a Wayland failure.
    return result;
}
