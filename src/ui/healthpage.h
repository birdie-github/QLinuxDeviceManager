#pragma once

#include "storagepresentation.h"
#include <QCoreApplication>
#include <QWidget>

class QComboBox;
class QLabel;
class QVBoxLayout;

// Owns the displayed drive rows so selection and Copy remain independent of
// the caller's snapshot lifetime. No health queries are performed here.
class HealthPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit HealthPage(QWidget *parent = nullptr);
    void setSnapshot(const QVector<StorageDisplayRow> &rows,
                     const QString &preferredDrive, const QString &notes);

private:
    void showHealth();
    QVector<StorageDisplayRow> rows_;
    QComboBox *healthDrive_ = nullptr;
    QVBoxLayout *healthGroups_ = nullptr;
    QLabel *healthNotice_ = nullptr;
};
