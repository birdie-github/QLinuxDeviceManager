#include "displaybackend.h"
#include "displayidentity.h"
#include "directoryscan.h"
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QThread>
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <poll.h>
#include <xcb/xcb.h>
#include <xcb/xcbext.h>
#include <xcb/randr.h>

namespace DisplayBackend {
namespace {
template<class T> using Reply = std::unique_ptr<T, decltype(&std::free)>;
// XCB keeps protocol/I/O errors local to this connection, unlike Xlib's global
// error handlers. These declarations introduce no direct library references.
#define XCB_FUNCTIONS(X) \
    X(connect, xcb_connect) X(disconnect, xcb_disconnect) \
    X(hasError, xcb_connection_has_error) X(setup, xcb_get_setup) \
    X(roots, xcb_setup_roots_iterator) X(nextScreen, xcb_screen_next) \
    X(fd, xcb_get_file_descriptor) X(flush, xcb_flush) X(pollReply, xcb_poll_for_reply) \
    X(internAtom, xcb_intern_atom)
#define RANDR_FUNCTIONS(X) \
    X(version, xcb_randr_query_version) \
    X(resources, xcb_randr_get_screen_resources_current) \
    X(outputs, xcb_randr_get_screen_resources_current_outputs) \
    X(modes, xcb_randr_get_screen_resources_current_modes) \
    X(outputInfo, xcb_randr_get_output_info) X(outputName, xcb_randr_get_output_info_name) \
    X(crtcInfo, xcb_randr_get_crtc_info) X(transform, xcb_randr_get_crtc_transform) \
    X(outputProperty, xcb_randr_get_output_property) X(propertyData, xcb_randr_get_output_property_data)
struct KernelEdids {
    Attribute status;
    QMap<QString, QByteArray> connected;
};
Attribute readFile(const QString &path, int limit, QByteArray &bytes)
{
    if (QThread::currentThread()->isInterruptionRequested()) return error(ECANCELED);
    QFile file(path);
    errno = 0;
    if (!file.open(QIODevice::ReadOnly)) {
        const int code = errno ? errno : EIO;
        return {code == EACCES || code == EPERM ? ReadState::PermissionDenied
            : code == ENOENT ? ReadState::Unavailable : ReadState::Error, {}, code};
    }
    bytes = file.read(limit + 1);
    if (file.error() != QFileDevice::NoError) return error(EIO);
    if (bytes.size() > limit) return error(EOVERFLOW);
    return available(QString());
}
KernelEdids kernelEdids()
{
    KernelEdids result;
    const QString base = QStringLiteral("/sys/class/drm");
    const DirectoryNames names = boundedDirectoryNames(base, outputLimit * 2);
    if (names.error) { result.status = error(names.error); return result; }
    for (const QString &name : names.names) {
        if (!name.startsWith("card") || !name.contains('-')) continue;
        const QString path = QFileInfo(base + '/' + name).canonicalFilePath();
        if (!path.startsWith("/sys/devices/")) { result.status = error(EAGAIN); return result; }
        QByteArray status, edid;
        result.status = readFile(path + "/status", 64, status);
        if (result.status.state != ReadState::Available) return result;
        status = status.trimmed();
        if (status == "disconnected") continue;
        if (status != "connected" && status != "unknown") { result.status = error(EINVAL); return result; }
        if (result.connected.size() >= outputLimit) { result.status = error(EOVERFLOW); return result; }
        result.status = readFile(path + "/edid", DisplayIdentity::edidLimit, edid);
        if (result.status.state != ReadState::Available) return result;
        if (!DisplayIdentity::validEdid(edid)) { result.status = {}; return result; }
        result.connected.insert(path, edid);
    }
    const DirectoryNames after = boundedDirectoryNames(base, outputLimit * 2);
    result.status = after.error ? error(after.error)
        : after.names != names.names ? error(EAGAIN) : available(QString());
    return result;
}
struct X11 {
    QLibrary core {QStringLiteral("libxcb.so.1")};
    QLibrary randr {QStringLiteral("libxcb-randr.so.0")};
    xcb_connection_t *connection = nullptr;
    QElapsedTimer timer;
    int errorNumber = 0;
#define DECLARE(member, symbol) decltype(&symbol) member = nullptr;
    XCB_FUNCTIONS(DECLARE)
    RANDR_FUNCTIONS(DECLARE)
#undef DECLARE
    ~X11()
    {
        if (connection) disconnect(connection);
        randr.unload();
        core.unload();
    }
    bool load()
    {
        if (!core.load() || !randr.load()) return false;
        bool valid = true;
#define CORE_LOAD(member, symbol) valid = resolve(core, #symbol, member) && valid;
        XCB_FUNCTIONS(CORE_LOAD)
#undef CORE_LOAD
#define RANDR_LOAD(member, symbol) valid = resolve(randr, #symbol, member) && valid;
        RANDR_FUNCTIONS(RANDR_LOAD)
#undef RANDR_LOAD
        return valid;
    }
    template<class T> Reply<T> reply(unsigned int sequence)
    {
        void *raw = nullptr;
        while (!QThread::currentThread()->isInterruptionRequested()) {
            xcb_generic_error_t *protocolError = nullptr;
            if (pollReply(connection, sequence, &raw, &protocolError)) {
                if (protocolError || !raw) {
                    std::free(protocolError);
                    std::free(raw);
                    errorNumber = EPROTO;
                    return {nullptr, &std::free};
                }
                return {static_cast<T *>(raw), &std::free};
            }
            if (hasError(connection)) { errorNumber = EIO; break; }
            const int remaining = queryTimeoutMs - static_cast<int>(timer.elapsed());
            if (remaining <= 0) { errorNumber = ETIMEDOUT; break; }
            if (flush(connection) <= 0 && hasError(connection)) { errorNumber = EIO; break; }
            pollfd socket {fd(connection), POLLIN, 0};
            const int status = poll(&socket, 1, qMin(remaining, 100));
            if (status < 0 && errno != EINTR) { errorNumber = errno; break; }
            if (status > 0 && (socket.revents & (POLLERR | POLLHUP | POLLNVAL))) { errorNumber = EIO; break; }
        }
        if (!errorNumber) errorNumber = ECANCELED;
        return {nullptr, &std::free};
    }
    QByteArray edid(xcb_randr_output_t output, xcb_atom_t atom)
    {
        const auto property = reply<xcb_randr_get_output_property_reply_t>(
            outputProperty(connection, output, atom, XCB_ATOM_ANY, 0, DisplayIdentity::edidLimit / 4, 0, 0).sequence);
        if (!property) return {};
        if (property->type == XCB_ATOM_NONE) return {};
        if (property->format != 8 || property->type != XCB_ATOM_INTEGER) { errorNumber = EPROTO; return {}; }
        if (property->bytes_after || property->num_items > DisplayIdentity::edidLimit) { errorNumber = EOVERFLOW; return {}; }
        if (property->length * 4ULL < property->num_items) { errorNumber = EPROTO; return {}; }
        const QByteArray bytes(reinterpret_cast<const char *>(propertyData(property.get())), static_cast<int>(property->num_items));
        if (!bytes.isEmpty() && !DisplayIdentity::validEdid(bytes)) { errorNumber = EPROTO; return {}; }
        return bytes;
    }
};
#undef XCB_FUNCTIONS
#undef RANDR_FUNCTIONS
}

DisplayProperties x11(const QString &connector, const QString &kernelPath)
{
    DisplayProperties result;
    result.backend = "X11 / RandR";
    X11 api;
    if (!api.load()) return result;
    int screenNumber = 0;
    api.connection = api.connect(nullptr, &screenNumber);
    if (!api.connection || api.hasError(api.connection)) { result.status = error(EIO); return result; }
    api.timer.start();
    const auto version = api.reply<xcb_randr_query_version_reply_t>(api.version(api.connection, 1, 3).sequence);
    if (!version) { result.status = error(api.errorNumber); return result; }
    if (version->major_version < 1 || (version->major_version == 1 && version->minor_version < 3)) return result;
    const xcb_setup_t *setup = api.setup(api.connection);
    if (!setup) { result.status = error(EIO); return result; }
    auto screen = api.roots(setup);
    for (int i = 0; i < screenNumber && screen.rem; ++i) api.nextScreen(&screen);
    if (!screen.rem || !screen.data) { result.status = error(EIO); return result; }
    const auto resources = api.reply<xcb_randr_get_screen_resources_current_reply_t>(
        api.resources(api.connection, screen.data->root).sequence);
    if (!resources) { result.status = error(api.errorNumber); return result; }
    if (resources->num_outputs > outputLimit || resources->num_modes > textLimit) { result.status = error(EOVERFLOW); return result; }
    xcb_randr_crtc_t selected = XCB_NONE;
    int matches = 0;
    bool disconnected = false;
    xcb_randr_output_t namedOutput = XCB_NONE;
    struct Output { xcb_randr_output_t id; xcb_randr_crtc_t crtc; QString name; };
    QVector<Output> connected;
    const auto *outputs = api.outputs(resources.get());
    for (int i = 0; i < resources->num_outputs; ++i) {
        const auto info = api.reply<xcb_randr_get_output_info_reply_t>(
            api.outputInfo(api.connection, outputs[i], resources->config_timestamp).sequence);
        if (!info) { result.status = error(api.errorNumber); return result; }
        if (info->status != XCB_RANDR_SET_CONFIG_SUCCESS) { result.status = error(EAGAIN); return result; }
        if (info->name_len > textLimit) { result.status = error(EOVERFLOW); return result; }
        const QString name = QString::fromUtf8(reinterpret_cast<const char *>(api.outputName(info.get())), info->name_len);
        if (info->connection != XCB_RANDR_CONNECTION_DISCONNECTED) connected.append({outputs[i], info->crtc, name});
        if (canonicalName(name) != canonicalName(connector)) continue;
        ++matches;
        result.output = name;
        namedOutput = outputs[i];
        selected = info->crtc;
        disconnected = info->connection == XCB_RANDR_CONNECTION_DISCONNECTED;
    }
    KernelEdids kernel;
    xcb_atom_t edidAtom = XCB_ATOM_NONE;
    xcb_randr_output_t matchedOutput = XCB_NONE;
    QByteArray matchedEdid;
    if (matches > 1) { result.status = error(ENOTUNIQ); return result; }
    bool fallback = matches == 0 || disconnected;
    QByteArray namedKernelEdid;
    Attribute namedKernelState;
    if (matches == 1) {
        // Connector numbering can overlap accidentally across kernel and X11
        // drivers. A unique name must not override conflicting monitor identity.
        namedKernelState = readFile(kernelPath + "/edid", DisplayIdentity::edidLimit, namedKernelEdid);
        if (namedKernelState.state == ReadState::Error) { result.status = namedKernelState; return result; }
        if (namedKernelState.state == ReadState::Available && DisplayIdentity::validEdid(namedKernelEdid)) {
            const auto atom = api.reply<xcb_intern_atom_reply_t>(api.internAtom(api.connection, 1, 4, "EDID").sequence);
            if (!atom) { result.status = error(api.errorNumber); return result; }
            edidAtom = atom->atom;
            if (edidAtom) {
                const QByteArray namedEdid = api.edid(namedOutput, edidAtom);
                if (api.errorNumber) { result.status = error(api.errorNumber); return result; }
                if (!namedEdid.isEmpty()) {
                    if (namedEdid != namedKernelEdid) fallback = true;
                    else {
                        matchedOutput = namedOutput;
                        matchedEdid = namedEdid;
                    }
                }
            }
        }
    }
    if (fallback) {
        // Clear the rejected name candidate even if fallback cannot identify an output.
        result.output.clear();
        selected = XCB_NONE;
        matchedOutput = XCB_NONE;
        matchedEdid.clear();
        // Drivers such as NVIDIA use independent output numbering. EDID fallback
        // is permitted only with a unique, complete descriptor on both sides.
        result.sources.insert("configuration", "No consistent RandR connector-name match; fallback requires one unique, complete EDID on both kernel and RandR sides");
        kernel = kernelEdids();
        if (kernel.status.state != ReadState::Available) { result.status = kernel.status; return result; }
        const QByteArray selectedEdid = kernel.connected.value(kernelPath);
        if (selectedEdid.isEmpty()) return result;
        const auto atom = api.reply<xcb_intern_atom_reply_t>(api.internAtom(api.connection, 1, 4, "EDID").sequence);
        if (!atom) { result.status = error(api.errorNumber); return result; }
        edidAtom = atom->atom;
        if (!edidAtom) return result;
        QVector<QByteArray> sessionEdids;
        for (const Output &output : connected) {
            sessionEdids.append(api.edid(output.id, edidAtom));
            if (api.errorNumber) { result.status = error(api.errorNumber); return result; }
        }
        const DisplayIdentity::Match match = DisplayIdentity::matchEdid(selectedEdid, kernel.connected.values(), sessionEdids);
        if (match.status.state != ReadState::Available) { result.status = match.status; return result; }
        const Output &output = connected[match.output];
        result.output = output.name;
        selected = output.crtc;
        matchedOutput = output.id;
        matchedEdid = sessionEdids[match.output];
        result.sources.insert("output", kernelPath + "/edid and RandR EDID property: unique complete descriptor match");
        result.sources.insert("configuration", "RandR output associated by a unique complete EDID match; no connector-number offset is assumed");
    } else result.sources.insert("output", matchedOutput != XCB_NONE
        ? "Unique kernel/RandR connector-name match, verified against complete EDID on both sides"
        : "Unique kernel/RandR connector-name match; EDID verification unavailable");
    const auto validate = [&] {
        // Reject a result spanning a modeset/hotplug rather than showing mixed data.
        const auto after = api.reply<xcb_randr_get_screen_resources_current_reply_t>(
            api.resources(api.connection, screen.data->root).sequence);
        bool stable = after && after->config_timestamp == resources->config_timestamp && after->timestamp == resources->timestamp;
        if (stable && matches == 1 && !fallback) {
            QByteArray finalKernelEdid;
            const Attribute finalState = readFile(kernelPath + "/edid", DisplayIdentity::edidLimit, finalKernelEdid);
            stable = finalState.state == namedKernelState.state && finalState.error == namedKernelState.error
                && finalKernelEdid == namedKernelEdid;
        }
        if (stable && matchedOutput != XCB_NONE) {
            const QByteArray finalEdid = api.edid(matchedOutput, edidAtom);
            stable = !api.errorNumber && finalEdid == matchedEdid;
            if (stable && fallback) {
                const KernelEdids finalKernel = kernelEdids();
                stable = finalKernel.status.state == ReadState::Available && finalKernel.connected == kernel.connected;
            }
        }
        if (!stable) {
            result.values.clear();
            result.sources.clear();
            result.status = error(after ? EAGAIN : api.errorNumber);
        }
    };
    result.status = available(selected ? "configured" : "inactive");
    const auto put = [&](const QString &key, const QString &value, const QString &source) {
        result.values.insert(key, available(value));
        result.sources.insert(key, source);
    };
    put("depth", QString::number(screen.data->root_depth), "X11 screen root depth; not panel/link bits per channel");
    if (!selected) {
        for (const QString key : {"resolution", "refresh", "rotation", "reflection", "scale"})
            result.values.insert(key, {ReadState::NotApplicable, {}, 0});
        validate();
        return result;
    }
    const auto crtc = api.reply<xcb_randr_get_crtc_info_reply_t>(
        api.crtcInfo(api.connection, selected, resources->config_timestamp).sequence);
    if (!crtc || crtc->status != XCB_RANDR_SET_CONFIG_SUCCESS || !crtc->mode) {
        result.status = error(crtc ? EAGAIN : api.errorNumber);
        return result;
    }
    const xcb_randr_mode_info_t *modes = api.modes(resources.get());
    bool foundMode = false;
    for (int i = 0; i < resources->num_modes; ++i) {
        const auto &mode = modes[i];
        if (mode.id != crtc->mode) continue;
        foundMode = true;
        put("resolution", QStringLiteral("%1x%2").arg(mode.width).arg(mode.height), "RandR current mode, before rotation/scaling");
        if (mode.htotal && mode.vtotal && mode.dot_clock) {
            double refresh = static_cast<double>(mode.dot_clock) / mode.htotal / mode.vtotal;
            if (mode.mode_flags & XCB_RANDR_MODE_FLAG_INTERLACE) refresh *= 2;
            if (mode.mode_flags & XCB_RANDR_MODE_FLAG_DOUBLE_SCAN) refresh /= 2;
            put("refresh", number(refresh), "RandR current mode pixel clock/totals; nominal rate, not measured/VRR rate");
        }
        break;
    }
    if (!foundMode) { result.status = error(EAGAIN); return result; }
    const unsigned int rotation = crtc->rotation & 15;
    if (rotation == XCB_RANDR_ROTATION_ROTATE_0 || rotation == XCB_RANDR_ROTATION_ROTATE_90
        || rotation == XCB_RANDR_ROTATION_ROTATE_180 || rotation == XCB_RANDR_ROTATION_ROTATE_270)
        put("rotation", QString::number(rotation == 1 ? 0 : rotation == 2 ? 90 : rotation == 4 ? 180 : 270),
            "RandR CRTC rotation, counterclockwise");
    put("reflection", (crtc->rotation & XCB_RANDR_ROTATION_REFLECT_X)
        ? ((crtc->rotation & XCB_RANDR_ROTATION_REFLECT_Y) ? "xy" : "x")
        : ((crtc->rotation & XCB_RANDR_ROTATION_REFLECT_Y) ? "y" : "none"), "RandR CRTC reflection flags");
    const auto transform = api.reply<xcb_randr_get_crtc_transform_reply_t>(api.transform(api.connection, selected).sequence);
    if (!transform) result.values.insert("scale", error(api.errorNumber));
    else if (!transform->has_transforms) {
        result.sources.insert("scale", "RandR CRTC transforms are not supported by this output");
    } else {
        const auto &matrix = transform->current_transform;
        const bool diagonal = matrix.matrix12 == 0 && matrix.matrix21 == 0 && matrix.matrix31 == 0
            && matrix.matrix32 == 0 && matrix.matrix33 == 65536 && matrix.matrix11 > 0 && matrix.matrix22 > 0;
        if (diagonal) put("scale", number(matrix.matrix11 / 65536.0) + ' ' + number(matrix.matrix22 / 65536.0),
            "RandR transform: framebuffer pixels per output pixel (X, Y); excludes desktop DPI and Qt scaling");
        else put("scale", "custom", "RandR uses a non-axis-aligned transform; no single scale factor describes it");
    }
    validate();
    return result;
}
}
