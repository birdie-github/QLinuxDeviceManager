#include "monitorproperties.h"
#include "displayidentity.h"
#include <QByteArray>
#include <QStringList>
#include <QThread>
#ifdef QLDM_HAVE_DISPLAY_INFO
extern "C" {
#include <libdisplay-info/info.h>
#include <libdisplay-info/edid.h>
}
#endif
#include <cerrno>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <fcntl.h>
#include <unistd.h>

namespace {
struct Report {
    Attribute state;
    QByteArray bytes;
};
Attribute failure(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENODEV ? ReadState::Removed
        : error == ENOENT || error == ENODATA ? ReadState::Unavailable : ReadState::Error, {}, error};
}
Attribute available(const QString &text) { return {ReadState::Available, text, 0}; }
Report readReport(int directory, const char *name, int limit)
{
    const int fd = openat(directory, name, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return {failure(errno), {}};
    Report result;
    char buffer[4096];
    int error = 0;
    while (result.bytes.size() <= limit) {
        if (QThread::currentThread()->isInterruptionRequested()) { error = ECANCELED; break; }
        const int wanted = qMin(static_cast<int>(sizeof(buffer)), limit + 1 - static_cast<int>(result.bytes.size()));
        const ssize_t count = read(fd, buffer, wanted);
        if (count < 0) {
            if (errno == EINTR) continue;
            error = errno;
            break;
        }
        if (count == 0) break;
        result.bytes.append(buffer, static_cast<int>(count));
    }
    close(fd);
    if (!error && result.bytes.size() > limit) error = EOVERFLOW;
    result.state = error ? failure(error) : Attribute {ReadState::Available, {}, 0};
    if (error) result.bytes.clear();
    return result;
}
bool sameReport(const Report &a, const Report &b)
{
    return a.state.state == b.state.state && a.state.error == b.state.error && a.bytes == b.bytes;
}
unsigned byte(const QByteArray &bytes, int index) { return static_cast<unsigned char>(bytes.at(index)); }
#ifdef QLDM_HAVE_DISPLAY_INFO
Attribute ownedString(char *text)
{
    if (!text) return {};
    const size_t size = strnlen(text, 4097);
    const Attribute result = size > 4096 ? failure(EOVERFLOW)
        : size ? available(QString::fromUtf8(text, static_cast<int>(size))) : Attribute {};
    std::free(text);
    return result;
}
#endif
void decodeDescriptor(MonitorProperties &result, const QByteArray &bytes, const QString &path)
{
    if (bytes.isEmpty()) { result.descriptor = {}; return; }
    // Reject truncated/corrupt framing before passing a bounded report to the parser.
    if (!DisplayIdentity::validEdid(bytes)) {
        result.descriptor = failure(EPROTO);
        return;
    }
    const QString version = QString::number(byte(bytes, 18)) + '.' + QString::number(byte(bytes, 19));
    result.values.insert("version", available(version));
    result.sources.insert("version", path + "/edid: bytes 18–19");
    if (byte(bytes, 18) != 1 || byte(bytes, 19) > 4) {
        result.descriptor = {ReadState::Unsupported, version, 0};
        return;
    }
#ifndef QLDM_HAVE_DISPLAY_INFO
    result.descriptor = {ReadState::Unsupported, {}, 0};
#else
    errno = 0;
    std::unique_ptr<di_info, decltype(&di_info_destroy)> info(
        di_info_parse_edid(bytes.constData(), static_cast<size_t>(bytes.size())), di_info_destroy);
    if (!info) { result.descriptor = failure(errno == ENOMEM ? ENOMEM : EPROTO); return; }
    const di_edid *edid = di_info_get_edid(info.get());
    if (!edid) { result.descriptor = {ReadState::Unsupported, version, 0}; return; }
    result.descriptor = {ReadState::Available, {}, 0};
    const auto put = [&](const QString &key, const Attribute &value, const QString &source) {
        result.values.insert(key, value);
        result.sources.insert(key, path + "/edid: " + source + " (libdisplay-info)");
    };
    const char *warnings = di_info_get_failure_msg(info.get());
    if (warnings) {
        const size_t length = strnlen(warnings, 16385);
        put("warnings", length > 16384 ? failure(EOVERFLOW) : available(QString::fromUtf8(warnings, static_cast<int>(length)).trimmed()),
            "parser conformance diagnostics; decoded values remain reported information");
    }
    put("manufacturer", ownedString(di_info_get_make(info.get())), "reported manufacturer, resolved by the library's PNP database");
    put("model", ownedString(di_info_get_model(info.get())), "product name, or product code if no name was supplied");
    put("serial", ownedString(di_info_get_serial(info.get())), "serial text, or numeric serial if available");
    const di_edid_vendor_product *product = di_edid_get_vendor_product(edid);
    if (product) {
        put("manufacturer_code", available(QString::fromLatin1(product->manufacturer, 3)), "PNP manufacturer code");
        put("product", available(QString("0x%1").arg(product->product, 4, 16, QChar('0'))), "reported product code");
    }
    const di_edid_screen_size *size = di_edid_get_screen_size(edid);
    put("size", size && size->width_cm > 0 && size->height_cm > 0
        ? available(QString::number(size->width_cm) + ' ' + QString::number(size->height_cm)) : Attribute {},
        "reported image dimensions in cm; unspecified/aspect-ratio encodings are not physical dimensions");
    const di_edid_video_input_digital *digital = di_edid_get_video_input_digital(edid);
    put("digital", available(digital ? "1" : "0"), "base-block video input type");
    if (digital) {
        put("depth", digital->color_bit_depth > 0 ? available(QString::number(digital->color_bit_depth)) : Attribute {},
            "declared bits per color component, not active link or framebuffer depth");
        // Use stable semantic names, not enum ordinals, in the owned snapshot.
        QString interface;
        switch (digital->interface) {
        case DI_EDID_VIDEO_INPUT_DIGITAL_UNDEFINED: break;
        case DI_EDID_VIDEO_INPUT_DIGITAL_DVI: interface = "DVI"; break;
        case DI_EDID_VIDEO_INPUT_DIGITAL_HDMI_A: interface = "HDMI-A"; break;
        case DI_EDID_VIDEO_INPUT_DIGITAL_HDMI_B: interface = "HDMI-B"; break;
        case DI_EDID_VIDEO_INPUT_DIGITAL_MDDI: interface = "MDDI"; break;
        case DI_EDID_VIDEO_INPUT_DIGITAL_DISPLAYPORT: interface = "DisplayPort"; break;
        }
        put("interface", interface.isEmpty() ? Attribute {} : available(interface), "reported digital input interface");
    }
#endif
}
Attribute modeNames(const Report &report)
{
    if (report.state.state != ReadState::Available) return report.state;
    if (report.bytes.isEmpty()) return {};
    const QList<QByteArray> lines = report.bytes.split('\n');
    if (lines.size() > 2049) return failure(EOVERFLOW);
    QStringList names;
    QSet<QString> seen;
    int count = 0;
    for (const QByteArray &line : lines) {
        if (QThread::currentThread()->isInterruptionRequested()) return failure(ECANCELED);
        if (line.isEmpty()) continue;
        if (++count > 2048) return failure(EOVERFLOW);
        if (line.size() > 128) return failure(EOVERFLOW);
        for (char c : line) if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return failure(EILSEQ);
        const QString name = QString::fromLatin1(line);
        if (!seen.contains(name)) { seen.insert(name); names.append(name); }
    }
    return names.isEmpty() ? Attribute {} : available(names.join('\n'));
}
} // namespace

MonitorProperties collectMonitorProperties(int directory, const QString &path)
{
    MonitorProperties result;
    const Report status = readReport(directory, "status", 64);
    if (status.state.state == ReadState::Available && status.bytes.trimmed() == "disconnected") return result;
    result.applicable = true;
    result.sources.insert("descriptor", path + "/edid: cached kernel descriptor; header, length and every block checksum checked. "
        "EDID 1.0–1.4 identity/input fields are decoded with optional libdisplay-info. Overrides, adapters or KVMs may supply this report.");
    result.sources.insert("modes", path + "/modes: cached kernel mode names, with duplicate names combined. "
        "These may include driver additions or restrictions; names do not distinguish refresh rates. Not a guaranteed capability list or active configuration.");
    if (status.state.state != ReadState::Available) {
        result.descriptor = status.state;
        result.sources["descriptor"] = path + "/status: connection status could not be read";
        result.values.insert("modes", status.state);
        return result;
    }
    if (status.bytes.trimmed() != "connected" && status.bytes.trimmed() != "unknown") {
        result.descriptor = failure(EINVAL);
        result.sources["descriptor"] = path + "/status: malformed connection status";
        result.values.insert("modes", result.descriptor);
        return result;
    }
    const Report edid = readReport(directory, "edid", DisplayIdentity::edidLimit);
    const Report modes = readReport(directory, "modes", 65536);
    // A monitor may be replaced without the connector directory changing identity.
    const Report afterEdid = readReport(directory, "edid", DisplayIdentity::edidLimit);
    const Report afterStatus = readReport(directory, "status", 64);
    if (afterStatus.state.state != ReadState::Available
        || (afterEdid.state.state != ReadState::Available && !sameReport(edid, afterEdid))) {
        result.descriptor = afterStatus.state.state != ReadState::Available ? afterStatus.state : afterEdid.state;
        result.values.insert("modes", result.descriptor);
        return result;
    }
    if (!sameReport(status, afterStatus) || !sameReport(edid, afterEdid)) {
        result.descriptor = failure(EAGAIN);
        result.values.insert("modes", result.descriptor);
        return result;
    }
    if (edid.state.state == ReadState::Available) decodeDescriptor(result, edid.bytes, path);
    else result.descriptor = edid.state;
    result.values.insert("modes", modeNames(modes));
    return result;
}
