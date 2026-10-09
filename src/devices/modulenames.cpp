#include "modulenames.h"
#include <QFileInfo>
#include <QThread>
#include <cerrno>
#include <cstring>
#include <memory>
#ifdef QLDM_HAVE_KMOD
#include <libkmod.h>
#endif

ModuleNames::ModuleNames()
{
#ifdef QLDM_HAVE_KMOD
    // Consult the running kernel's indexes, without modprobe.d overrides/commands.
    const char *const noConfiguration[] = {nullptr};
    context_ = kmod_new(nullptr, noConfiguration);
#endif
}
ModuleNames::~ModuleNames()
{
#ifdef QLDM_HAVE_KMOD
    if (context_) kmod_unref(context_);
#endif
}
ModuleNames::Result ModuleNames::lookup(const QString &key, bool exactName)
{
    const QString cacheKey = (exactName ? QStringLiteral("module:") : QStringLiteral("alias:")) + key;
    const auto cached = cache_.constFind(cacheKey);
    if (cached != cache_.cend()) return cached.value();
    Result result;
#ifdef QLDM_HAVE_KMOD
    if (!context_ || cache_.size() >= 512 || QThread::currentThread()->isInterruptionRequested()) return result;
    const QByteArray encoded = key.toUtf8();
    kmod_module *raw = nullptr;
    if (exactName) {
        const int status = kmod_module_new_from_name_lookup(context_, encoded.constData(), &raw);
        if (status < 0) { result.description.state = ReadState::Error; result.description.error = -status; }
    } else {
        kmod_list *matches = nullptr;
        const int status = kmod_module_new_from_lookup(context_, encoded.constData(), &matches);
        // Never select an arbitrary first module from ambiguous aliases.
        if (status == 0 && matches && !kmod_list_next(matches, matches)) raw = kmod_module_get_module(matches);
        else if (status < 0) { result.description.state = ReadState::Error; result.description.error = -status; }
        kmod_module_unref_list(matches);
    }
    std::unique_ptr<kmod_module, decltype(&kmod_module_unref)> module(raw, &kmod_module_unref);
    if (module) {
        const char *name = kmod_module_get_name(module.get());
        if (name) result.module = QString::fromUtf8(name);
        kmod_list *info = nullptr;
        const int status = kmod_module_get_info(module.get(), &info);
        if (status < 0) {
            result.description.error = -status;
            result.description.state = status == -EACCES || status == -EPERM
                ? ReadState::PermissionDenied : status == -ENOENT ? ReadState::Unavailable : ReadState::Error;
        } else {
            kmod_list *entry = nullptr;
            kmod_list_foreach(entry, info) {
                const char *field = kmod_module_info_get_key(entry);
                const char *value = kmod_module_info_get_value(entry);
                if (!field || std::strcmp(field, "description") != 0 || !value) continue;
                const size_t length = strnlen(value, 4097);
                if (length > 4096) { result.description.state = ReadState::Error; result.description.error = EOVERFLOW; break; }
                const QString description = QString::fromUtf8(value, static_cast<int>(length)).trimmed();
                if (description.isEmpty()) continue;
                result.description.state = ReadState::Available;
                result.description.value = description;
                break;
            }
        }
        kmod_module_info_free_list(info);
    }
#else
    Q_UNUSED(key)
    Q_UNUSED(exactName)
#endif
    if (cache_.size() < 512) cache_.insert(cacheKey, result); // Cache misses too.
    return result;
}
void ModuleNames::supplement(Device &d)
{
#ifndef QLDM_HAVE_KMOD
    Q_UNUSED(d)
    return;
#else
    if (d.subsystem == "cpu" || (d.nameSource != "kernel name" && d.nameSource != "direct kernel driver and kernel name")) return;
    const QFileInfo link(d.path + "/driver/module");
    QString module;
    if (link.isSymLink()) module = QFileInfo(link.symLinkTarget()).fileName();
    const bool candidate = module.isEmpty();
    const QString key = candidate ? d.properties.value("MODALIAS") : module;
    if (key.isEmpty()) return; // No module link does not prove a built-in driver.
    const Result result = lookup(key, !candidate);
    d.attributes.insert("module_description", result.description);
    if (result.description.state != ReadState::Available) return;
    d.name = result.description.value;
    d.nameCandidate = candidate;
    d.nameSource = QStringLiteral("installed module metadata: %1; %2: %3")
        .arg(result.module, candidate ? QStringLiteral("unique modalias candidate") : QStringLiteral("direct driver/module link"), key);
#endif
}
