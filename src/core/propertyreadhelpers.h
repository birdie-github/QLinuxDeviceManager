#pragma once
#include "commonproperties.h"
#include <sys/stat.h>
#include <unistd.h>

namespace PropertyRead {
struct Descriptor {
    int fd = -1;
    explicit Descriptor(int value) : fd(value) {}
    ~Descriptor() { if (fd >= 0) close(fd); }
    Descriptor(const Descriptor &) = delete;
    Descriptor &operator=(const Descriptor &) = delete;
};
Attribute failure(int error);
Attribute available(const QString &value);
Attribute readText(int directory, const char *name);
void put(CommonProperties &result, const QString &id, const Attribute &value, const QString &source);
}
