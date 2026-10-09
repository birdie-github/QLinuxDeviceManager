#pragma once
#include "storageproperties.h"

// Owned, widget-free presentation of one bounded collector snapshot.
struct StorageDisplayField { QString label, value, source; };
struct StorageDisplayRow {
    QString id, name, kind, capacity, filesystem, mounts;
    QString source;
    QVector<StorageDisplayField> fields;
};
struct StoragePresentation {
    QVector<StorageDisplayField> overview;
    QVector<StorageDisplayRow> volumes;
    QVector<StorageDisplayRow> health;
    QString notes;
};
StoragePresentation storagePresentation(const StorageProperties &snapshot, const QString &selectedPath);
QString storageFieldsText(const QVector<StorageDisplayField> &fields);
QString storageRowsText(const QVector<StorageDisplayRow> &rows);
