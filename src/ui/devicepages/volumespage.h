#pragma once

#include "storagepresentation.h"
#include <QCoreApplication>
#include <QWidget>

class QLabel;
class QPushButton;
class QTableWidget;

// Owns displayed rows; the modal details dialog also copies its selected row.
class VolumesPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit VolumesPage(QWidget *parent = nullptr);
    void setSnapshot(const QVector<StorageDisplayRow> &rows, const QString &devicePath,
                     const QString &scope, const QString &notes);

private:
    void showVolumeDetails();
    QVector<StorageDisplayRow> rows_;
    QTableWidget *volumesTable_ = nullptr;
    QPushButton *volumeDetailsButton_ = nullptr;
    QLabel *volumesNotice_ = nullptr;
};
