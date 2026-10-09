#pragma once

#include "device.h"

struct DeviceResource {
    enum class Type { Memory, Io, Irq, Dma, Bus };
    enum class Allocation { Assigned, Unassigned, Disabled, Unavailable };
    Type type = Type::Memory;
    Allocation allocation = Allocation::Assigned;
    quint64 start = 0;
    quint64 end = 0;
    quint64 flags = 0;
    int index = -1; // PCI resource slot, not necessarily a BAR.
    bool pci = false;
    QString mode; // MSI/MSI-X or reported irq; raw kernel evidence only.
    QString source;
};
struct ResourceIssue {
    QString source;
    Attribute error;
};
struct DeviceResources {
    QVector<DeviceResource> items;
    QVector<ResourceIssue> issues;
    QString pnpState;
    bool hasInformation() const { return !items.isEmpty() || !issues.isEmpty(); }
};

// Strict parsers produce owned raw records. No translation or presentation here.
DeviceResources parsePciResources(const QString &text, const QString &source);
DeviceResources parsePnpResources(const QString &text, const QString &source);
// Read only selected metadata relative to the caller's pinned device descriptor.
DeviceResources collectDeviceResources(int deviceFd, const Device &device);
