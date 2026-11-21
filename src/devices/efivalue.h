#pragma once
#include "device.h"
#include <QByteArray>

QString efiHexDump(const QByteArray &bytes);
struct EfiSnapshot {
    Attribute status;
    QByteArray bytes; // Includes the four-byte efivarfs attribute prefix.
    bool truncated = false;
    // A read error can occur even when the same readable instance is still present.
    bool instanceValidated = false;
};

// Worker-only, world-readable files, 64 KiB limit and instance revalidation.
EfiSnapshot readEfiSnapshot(const Device &device);
QString efiSnapshotText(const Attribute &status, const QByteArray &bytes, bool truncated);
