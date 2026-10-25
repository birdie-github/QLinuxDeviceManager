#include "audioproperties.h"
#include <cerrno>
#ifdef QLDM_HAVE_PULSE
#include <QElapsedTimer>
#include <QFileInfo>
#include <QThread>
#include <pulse/pulseaudio.h>
#include <cstring>
#include <functional>

namespace {
// Limits cover the entire snapshot, not each individual wait.
constexpr int queryBudgetMs = 2000;
constexpr int objectLimit = 512;
constexpr int cardLimit = 16;
constexpr int listLimit = 128;
constexpr size_t textLimit = 4096;

class Query final {
public:
    explicit Query(const Device &device) : devicePath_(QFileInfo(device.path).canonicalFilePath()) {}
    ~Query()
    {
        if (context_) {
            if (PA_CONTEXT_IS_GOOD(pa_context_get_state(context_))) pa_context_disconnect(context_);
            pa_context_unref(context_);
        }
        if (loop_) pa_mainloop_free(loop_);
    }
    AudioProperties run()
    {
        result_.applicable = true;
        timer_.start();
        loop_ = pa_mainloop_new();
        if (loop_) context_ = pa_context_new(pa_mainloop_get_api(loop_), "QLinuxDeviceManager");
        if (!context_) error_ = ENOMEM;
        else if (!devicePath_.startsWith("/sys/devices/")) error_ = ENOENT;
        else if (pa_context_connect(context_, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0) pulseFailure();
        else if (waitUntil([this] { return pa_context_get_state(context_) == PA_CONTEXT_READY; })) {
            // Remote servers cannot establish identity with this machine's sysfs.
            if (pa_context_is_local(context_) != 1) error_ = EOPNOTSUPP;
            else if (operation(pa_context_get_server_info(context_, serverInfo, this))
                && operation(pa_context_get_card_info_list(context_, cardInfo, this))
                && !result_.cards.isEmpty()
                && operation(pa_context_get_sink_info_list(context_, sinkInfo, this)))
                operation(pa_context_get_source_info_list(context_, sourceInfo, this));
        }
        if (error_) {
            result_.cards.clear(); // Never present incomplete rows as a complete report.
            result_.status = {error_ == EACCES ? ReadState::PermissionDenied : ReadState::Error, {}, error_};
        } else result_.status = {ReadState::Available, {}, 0};
        return result_;
    }
private:
    QString string(const char *value)
    {
        if (!value) return {};
        const size_t size = strnlen(value, textLimit + 1);
        if (size > textLimit) { error_ = EOVERFLOW; return {}; }
        return QString::fromUtf8(value, static_cast<qsizetype>(size));
    }
    QString property(pa_proplist *properties, const char *key)
    {
        return properties ? string(pa_proplist_gets(properties, key)) : QString();
    }
    void pulseFailure()
    {
        const int code = pa_context_errno(context_);
        result_.diagnostic = string(pa_strerror(code));
        error_ = code == PA_ERR_ACCESS ? EACCES : code == PA_ERR_CONNECTIONREFUSED ? ECONNREFUSED : EIO;
    }
    bool waitUntil(const std::function<bool()> &done)
    {
        while (!error_ && !done()) {
            if (QThread::currentThread()->isInterruptionRequested()) { error_ = ECANCELED; break; }
            if (timer_.elapsed() >= queryBudgetMs) { error_ = ETIMEDOUT; break; }
            const auto state = pa_context_get_state(context_);
            if (!PA_CONTEXT_IS_GOOD(state)) { pulseFailure(); break; }
            const int events = pa_mainloop_iterate(loop_, 0, nullptr);
            if (events < 0) { error_ = EIO; break; }
            if (!events) QThread::msleep(2);
        }
        return !error_;
    }
    bool operation(pa_operation *op)
    {
        if (!op) { pulseFailure(); return false; }
        const bool ok = waitUntil([op] { return pa_operation_get_state(op) != PA_OPERATION_RUNNING; });
        if (!ok) pa_operation_cancel(op);
        else if (!complete_) error_ = EPROTO;
        pa_operation_unref(op);
        complete_ = false;
        objects_ = 0;
        return !error_;
    }
    bool entry(int eol)
    {
        if (eol) {
            complete_ = true;
            if (eol < 0) pulseFailure();
            return false;
        }
        if (++objects_ > objectLimit) error_ = EOVERFLOW;
        return !error_;
    }
    static void serverInfo(pa_context *, const pa_server_info *info, void *data)
    {
        auto &q = *static_cast<Query *>(data);
        q.complete_ = true;
        if (!info) { q.pulseFailure(); return; }
        q.result_.server = q.string(info->server_name);
        q.result_.version = q.string(info->server_version);
        q.defaultSink_ = q.string(info->default_sink_name);
        q.defaultSource_ = q.string(info->default_source_name);
    }
    static void cardInfo(pa_context *, const pa_card_info *info, int eol, void *data)
    {
        auto &q = *static_cast<Query *>(data);
        if (!q.entry(eol) || !info) return;
        QString path = q.property(info->proplist, "sysfs.path");
        if (path.startsWith("/devices/")) path.prepend("/sys");
        if (!path.startsWith("/sys/devices/")) return;
        path = QFileInfo(path).canonicalFilePath();
        if (path.isEmpty() || !(path == q.devicePath_ || path.startsWith(q.devicePath_ + '/')
            || q.devicePath_.startsWith(path + '/'))) return;
        if (q.result_.cards.size() >= cardLimit || info->n_profiles > listLimit || info->n_ports > listLimit) {
            q.error_ = EOVERFLOW; return;
        }
        AudioCard card;
        card.index = info->index;
        card.name = q.string(info->name);
        card.description = q.property(info->proplist, "device.description");
        card.path = path;
        card.driver = q.property(info->proplist, "alsa.driver_name");
        card.mixer = q.property(info->proplist, "alsa.mixer_name");
        if (info->active_profile2) card.activeProfile = q.string(info->active_profile2->name);
        for (uint32_t i = 0; info->profiles2 && i < info->n_profiles; ++i) {
            const auto *profile = info->profiles2[i];
            if (!profile) break;
            AudioProfile row;
            row.name = q.string(profile->name);
            row.description = q.string(profile->description);
            row.playback = profile->n_sinks;
            row.recording = profile->n_sources;
            row.available = profile->available != 0;
            row.active = row.name == card.activeProfile;
            card.profiles.append(row);
        }
        for (uint32_t i = 0; info->ports && i < info->n_ports; ++i) {
            const auto *port = info->ports[i];
            if (!port) break;
            card.ports.append({q.string(port->name), q.string(port->description),
                static_cast<int>(port->direction), static_cast<int>(port->available)});
        }
        q.result_.cards.append(card);
    }
    template<class Info> void endpoint(const Info &info, bool input)
    {
        for (auto &card : result_.cards) {
            if (card.index != info.card) continue;
            AudioEndpoint row;
            row.name = string(info.name);
            row.description = string(info.description);
            row.input = input;
            row.isDefault = row.name == (input ? defaultSource_ : defaultSink_);
            row.muted = info.mute != 0;
            row.state = static_cast<int>(info.state);
            row.format = string(pa_sample_format_to_string(info.sample_spec.format));
            row.rate = info.sample_spec.rate;
            row.channels = info.sample_spec.channels;
            char map[PA_CHANNEL_MAP_SNPRINT_MAX];
            row.channelMap = string(pa_channel_map_snprint(map, sizeof(map), &info.channel_map));
            if (info.active_port) row.port = string(info.active_port->name);
            bool valid = false;
            const int bits = property(info.proplist, "alsa.resolution_bits").toInt(&valid);
            if (valid && bits > 0 && bits <= 64) row.resolutionBits = bits;
            card.endpoints.append(row);
            break;
        }
    }
    static void sinkInfo(pa_context *, const pa_sink_info *info, int eol, void *data)
    {
        auto &q = *static_cast<Query *>(data);
        if (q.entry(eol) && info) q.endpoint(*info, false);
    }
    static void sourceInfo(pa_context *, const pa_source_info *info, int eol, void *data)
    {
        auto &q = *static_cast<Query *>(data);
        if (q.entry(eol) && info && info->monitor_of_sink == PA_INVALID_INDEX) q.endpoint(*info, true);
    }
    QString devicePath_, defaultSink_, defaultSource_;
    AudioProperties result_;
    QElapsedTimer timer_;
    pa_mainloop *loop_ = nullptr;
    pa_context *context_ = nullptr;
    int error_ = 0, objects_ = 0;
    bool complete_ = false;
};
}
#endif

AudioProperties collectAudioProperties(const Device &device)
{
    if (device.category != "audio" && device.subsystem != "sound") return {};
#ifdef QLDM_HAVE_PULSE
    return Query(device).run();
#else
    AudioProperties result;
    result.applicable = true;
    result.status = {ReadState::Unsupported, {}, 0};
    return result;
#endif
}
