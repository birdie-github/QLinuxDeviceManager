#pragma once
#include "storageproperties.h"

// Owned, widget-free presentation of one bounded collector snapshot.
struct StorageDisplayField { QString label, value, source, tooltip; };
struct StorageDisplayGroup { QString title; QVector<StorageDisplayField> fields; };
struct StorageDisplayAtaAttribute {
    QString id, name, current, worst, threshold, interpreted, flags, source, interpretedTooltip;
};
struct StorageDisplayRow {
    QString id, name, kind, capacity, filesystem, mounts;
    QString source;
    QVector<StorageDisplayField> fields;
    QVector<StorageDisplayGroup> healthGroups;
    QVector<StorageDisplayAtaAttribute> ataAttributes;
};
struct StoragePresentation {
    QVector<StorageDisplayField> overview;
    QVector<StorageDisplayRow> volumes;
    QVector<StorageDisplayRow> health;
    QString preferredHealthDrive; // Unique explicit association with the selected device, if any.
    QString notes;
};
StoragePresentation storagePresentation(const StorageProperties &snapshot, const QString &selectedPath);
QString storageFieldsText(const QVector<StorageDisplayField> &fields);
QString storageRowsText(const QVector<StorageDisplayRow> &rows);
