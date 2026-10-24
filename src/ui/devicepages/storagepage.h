#pragma once

#include "storagepresentation.h"
#include <QCoreApplication>
#include <QWidget>

class QFormLayout;
class QLabel;

// Owns rendered overview fields and copy text; no borrowed snapshot state.
class StoragePage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit StoragePage(QWidget *parent = nullptr);
    void setSnapshot(const QVector<StorageDisplayField> &overview,
                     const QString &scope, const QString &notes);

private:
    QFormLayout *storage_ = nullptr;
    QLabel *storageNotice_ = nullptr;
    QString copyText_;
};
