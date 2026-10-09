#pragma once
#include "device.h"

struct kmod_ctx;
// Worker-only, refresh-scoped cache. No loading/unloading or subprocesses.
class ModuleNames final {
public:
    ModuleNames();
    ~ModuleNames();
    ModuleNames(const ModuleNames &) = delete;
    ModuleNames &operator=(const ModuleNames &) = delete;
    void supplement(Device &device);
private:
    struct Result {
        Attribute description;
        QString module;
    };
    Result lookup(const QString &key, bool exactName);
    kmod_ctx *context_ = nullptr;
    QHash<QString, Result> cache_;
};
