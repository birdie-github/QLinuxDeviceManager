#pragma once
#include "device.h"

QHash<int, QString> parseCpuModels(const QString &cpuinfo);
struct CpuIdentityField {
    const char *key;
    const char *procName;
};
inline constexpr CpuIdentityField cpuIdentityFields[] = {
    {"cpu_family", "cpu family"}, {"cpu_model_id", "model"},
    {"cpu_stepping", "stepping"}, {"cpu_microcode", "microcode"}
};
QHash<QString, Attribute> parseCpuIdentity(const QString &cpuinfo);
Attribute readCpuInfo();
