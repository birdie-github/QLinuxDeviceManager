#pragma once

#include "device.h"

// Owned metadata only. Relationships are explicit edges, never capacity totals.
struct StorageField {
    enum class Format { Text, Bytes, Boolean, Removable, Epoch, Kelvin, AtaHealth, NvmeHealth };
    QString id;
    const char *label = nullptr; // Static translatable caption, not device input.
    Attribute value;
    QString source;
    Format format = Format::Text;
};
struct StorageEntity {
    QString id; // Canonical sysfs path or UDisks object path, snapshot-local.
    QVector<StorageField> fields;
};
struct StorageLink {
    QString from;
    QString to;
    const char *label = nullptr;
    QString source;
};
struct StorageProperties {
    bool applicable = false;
    QVector<StorageEntity> entities;
    QVector<StorageLink> links;
    QVector<StorageField> notes;
};

// Worker only. cachedService=false is used by deep search to avoid repeated
// service snapshots; it still searches native metadata and storage relations.
StorageProperties collectStorageProperties(const Device &device, bool cachedService);
