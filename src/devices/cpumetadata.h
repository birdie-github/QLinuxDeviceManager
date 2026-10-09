#pragma once
#include "device.h"

QHash<int, QString> parseCpuModels(const QString &cpuinfo);
Attribute readCpuInfo();
