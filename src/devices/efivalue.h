#pragma once
#include "device.h"
#include <QByteArray>

QString efiHexDump(const QByteArray &bytes);
// Worker-only, world-readable files, 64 KiB limit and instance revalidation.
QString readEfiHexDump(const Device &device);
