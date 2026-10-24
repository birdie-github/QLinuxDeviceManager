#include "volumespage.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QApplication>
#include <QClipboard>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace {
QFormLayout *storageDetailsForm(QVBoxLayout *layout, QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFormAlignment(Qt::AlignTop);
    layout->addWidget(page);
    return form;
}
QString selectedStorageText(const QVector<StorageDisplayRow> &rows, int index)
{
    if (index < 0 || index >= rows.size()) return {};
    const StorageDisplayRow &row = rows[index];
    return storageRowsText({row});
}
}

VolumesPage::VolumesPage(QWidget *parent) : QWidget(parent)
{
    hide();
    const auto volumesReport = reportPageLayout(this);
    auto *volumesLayout = volumesReport.contents;
    volumesNotice_ = plainLabel(tr("ℹ️ Related volumes and mounts"), this);
    volumesLayout->addWidget(volumesNotice_);
    volumesTable_ = new ReportTable(0, 5, this);
    volumesTable_->setHorizontalHeaderLabels({tr("Device / label"), tr("Layer"), tr("Capacity"), tr("Content"), tr("Mount points")});
    volumesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    volumesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    volumesTable_->setColumnWidth(0, 180);
    volumesTable_->setColumnWidth(1, 110);
    volumesTable_->setColumnWidth(2, 180);
    volumesLayout->addWidget(volumesTable_);
    auto *volumeButtons = volumesReport.actions;
    volumeDetailsButton_ = new QPushButton(tr("Volume details…"), this);
    volumeDetailsButton_->setEnabled(false);
    auto *copyVolume = new QPushButton(tr("Copy selected volume"), this);
    auto *copyVolumes = new QPushButton(tr("Copy all volumes"), this);
    volumeButtons->addWidget(volumeDetailsButton_);
    volumeButtons->addWidget(copyVolume);
    volumeButtons->addWidget(copyVolumes);
    volumeButtons->addStretch();
    volumesLayout->addStretch();
    connect(volumesTable_, &QTableWidget::currentCellChanged, this, [this] {
        volumeDetailsButton_->setEnabled(volumesTable_->currentRow() >= 0);
    });
    connect(volumeDetailsButton_, &QPushButton::clicked, this, &VolumesPage::showVolumeDetails);
    connect(volumesTable_, &QTableWidget::cellDoubleClicked, this, [this] { showVolumeDetails(); });
    connect(copyVolume, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(selectedStorageText(rows_, volumesTable_->currentRow()));
    });
    connect(copyVolumes, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(storageRowsText(rows_));
    });

}

void VolumesPage::setSnapshot(const QVector<StorageDisplayRow> &rows, const QString &devicePath,
                              const QString &scope, const QString &notes)
{
    const QString previousVolume = volumesTable_->currentRow() >= 0
        ? volumesTable_->item(volumesTable_->currentRow(), 0)->data(Qt::UserRole).toString() : QString();
    rows_ = rows;
    volumesNotice_->setToolTip(propertyToolTip(scope + '\n' + tr("Related volumes may span several drives. Mount points cover only the application "
        "mount namespace. No filesystem is probed, mounted or unlocked.") + "\n\n" + notes));
    {
        const QSignalBlocker blocker(volumesTable_);
        volumesTable_->setRowCount(0);
        int selected = -1;
        for (const StorageDisplayRow &entry : rows_) {
            const int row = volumesTable_->rowCount();
            volumesTable_->insertRow(row);
            const QStringList columns {entry.name, entry.kind, entry.capacity, entry.filesystem, entry.mounts};
            for (int column = 0; column < columns.size(); ++column) {
                auto *item = new QTableWidgetItem(columns[column]);
                item->setToolTip(entry.source.toHtmlEscaped());
                volumesTable_->setItem(row, column, item);
            }
            volumesTable_->item(row, 0)->setData(Qt::UserRole, entry.id);
            if (entry.id == previousVolume || (previousVolume.isEmpty() && entry.id == devicePath)) selected = row;
        }
        if (selected < 0 && volumesTable_->rowCount()) selected = 0;
        volumesTable_->setCurrentCell(selected, 0);
    }
    volumeDetailsButton_->setEnabled(volumesTable_->currentRow() >= 0);
}
void VolumesPage::showVolumeDetails()
{
    const int row = volumesTable_->currentRow();
    if (row < 0 || row >= rows_.size()) return;
    // Own the selected snapshot while the modal dialog runs its event loop.
    const StorageDisplayRow entry = rows_[row];
    QDialog details(this);
    details.setWindowTitle(tr("Volume details"));
    details.resize(720, 520);
    auto *layout = new QVBoxLayout(&details);
    layout->addWidget(plainLabel(entry.name, &details));
    auto *scroll = new QScrollArea(&details);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *contents = new QVBoxLayout(page);
    showStorageFields(storageDetailsForm(contents, page), entry.fields);
    contents->addStretch();
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, &details);
    auto *copy = buttons->addButton(tr("Copy volume details"), QDialogButtonBox::ActionRole);
    connect(copy, &QPushButton::clicked, &details, [entry] {
        QApplication::clipboard()->setText(storageRowsText({entry}));
    });
    connect(buttons, &QDialogButtonBox::rejected, &details, &QDialog::reject);
    layout->addWidget(buttons);
    details.exec();
}
