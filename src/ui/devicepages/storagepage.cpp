#include "storagepage.h"
#include "propertyreport.h"
#include "propertypage.h"
#include <QApplication>
#include <QClipboard>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

StoragePage::StoragePage(QWidget *parent) : QWidget(parent)
{
    hide();
    auto *storageLayout = new QVBoxLayout(this);
    storageNotice_ = plainLabel(tr("ℹ️ Storage snapshot for this device"), this);
    storageLayout->addWidget(storageNotice_);
    auto *storageScroll = new QScrollArea(this);
    storageScroll->setWidgetResizable(true);
    storageScroll->setFrameShape(QFrame::NoFrame);
    auto *storageFormPage = new QWidget(storageScroll);
    storage_ = new QFormLayout(storageFormPage);
    storage_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    storage_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    setReportScrollContent(storageScroll, storageFormPage);
    storageLayout->addWidget(storageScroll, 1);
    auto *copyStorage = new QPushButton(tr("Copy storage overview"), this);
    storageLayout->addWidget(copyStorage);
    connect(copyStorage, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });

}

void StoragePage::setSnapshot(const QVector<StorageDisplayField> &overview,
                              const QString &scope, const QString &notes)
{
    storageNotice_->setToolTip(propertyToolTip(scope + "\n\n" + notes));
    showStorageFields(storage_, overview);
    copyText_ = storageFieldsText(overview) + "\n\n" + notes;
}
