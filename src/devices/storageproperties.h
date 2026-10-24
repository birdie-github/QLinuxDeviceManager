#pragma once

#include "device.h"

// Owned metadata only. Relationships are explicit edges, never capacity totals.
struct StorageField {
    enum class Format { Text, Bytes, Boolean, Removable, Epoch, Kelvin, Percent, Hours, Seconds, Minutes, Count, AtaHealth, NvmeHealth, HealthQueryStatus };
    QString id;
    const char *label = nullptr; // Static translatable caption, not device input.
    Attribute value;
    QString source;
    Format format = Format::Text;
};
struct AtaSmartAttribute {
    quint8 id = 0;
    QString name; // UDisks2's drive-specific identifier, not guessed from the ID.
    quint16 flags = 0;
    qint32 current = -1, worst = -1, threshold = -1;
    qint64 interpreted = 0;
    QString rawCounter = {}; // Validated smartctl raw counter; no assumed byte unit.
    QString rawSource = {};
    qint32 unit = 0; // UDisks2 unit; zero means no interpreted value.
};
struct StorageEntity {
    QString id; // Canonical sysfs path or UDisks object path, snapshot-local.
    QVector<StorageField> fields;
    QVector<AtaSmartAttribute> ataAttributes;
    QString ataCounterStatus = {};
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
