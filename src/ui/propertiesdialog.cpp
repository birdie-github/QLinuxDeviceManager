#include "resourceformat.h"
#include "propertiesdialog.h"
#include "bluetoothpage.h"
#include "networkpage.h"
#include "reporttable.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "gpulinktest.h"
#include "pcielinktext.h"
#include "gpulinksample.h"
#include <QJsonDocument>
#include <QDateTime>
#include <unistd.h>
#include "devicelabel.h"
#include <QApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>
#include <QCoreApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QGridLayout>
#include <QGroupBox>
#include <QSizePolicy>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QStyle>
#include <QTimer>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <QVBoxLayout>
#include <utility>

namespace {
QString eventDescription(const DeviceEvent &event)
{
    if (event.action == "add") return QCoreApplication::translate("PropertiesDialog", "Device addition observed");
    if (event.action == "remove") return QCoreApplication::translate("PropertiesDialog", "Device removal observed");
    if (event.action == "change") return QCoreApplication::translate("PropertiesDialog", "Device change reported");
    if (event.action == "bind") return QCoreApplication::translate("PropertiesDialog", "Driver binding reported");
    if (event.action == "unbind") return QCoreApplication::translate("PropertiesDialog", "Driver unbinding reported");
    if (event.action == "move") return QCoreApplication::translate("PropertiesDialog", "Device path move reported");
    if (event.action == "online") return QCoreApplication::translate("PropertiesDialog", "Device online event reported");
    if (event.action == "offline") return QCoreApplication::translate("PropertiesDialog", "Device offline event reported");
    return QCoreApplication::translate("PropertiesDialog", "udev action: %1").arg(event.action);
}
QString eventText(const DeviceEvent &event)
{
    const auto value = [](const QString &text) {
        return text.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Unavailable in event payload") : text;
    };
    // Format each field once so a literal "%9" in a device string cannot
    // become a later QString::arg placeholder.
    return QStringList {
        QCoreApplication::translate("PropertiesDialog", "Observed: %1").arg(event.observed.toString(Qt::ISODateWithMs)),
        QCoreApplication::translate("PropertiesDialog", "Elapsed since monitor startup: %1 ms").arg(event.elapsedMs),
        QCoreApplication::translate("PropertiesDialog", "Source: live udev observation"),
        QCoreApplication::translate("PropertiesDialog", "Action: %1").arg(value(event.action)),
        QCoreApplication::translate("PropertiesDialog", "Description: %1").arg(eventDescription(event)),
        QCoreApplication::translate("PropertiesDialog", "Kernel path: %1").arg(event.path),
        QCoreApplication::translate("PropertiesDialog", "Subsystem: %1").arg(value(event.subsystem)),
        QCoreApplication::translate("PropertiesDialog", "Device type: %1").arg(value(event.devtype)),
        QCoreApplication::translate("PropertiesDialog", "Driver in event payload: %1").arg(value(event.driver)),
        QCoreApplication::translate("PropertiesDialog", "udev sequence: %1").arg(event.sequence ? QString::number(event.sequence) : value(QString())),
        QCoreApplication::translate("PropertiesDialog", "Initialization stamp: %1").arg(value(event.initialized)),
        QCoreApplication::translate("PropertiesDialog", "Local instance token: %1").arg(event.instance),
        QCoreApplication::translate("PropertiesDialog", "Previous path: %1").arg(value(event.oldPath))
    }.join('\n');
}
void clearForm(QFormLayout *form)
{
    while (form->rowCount()) form->removeRow(0);
}
struct ReportPageLayout {
    QVBoxLayout *contents;
    QHBoxLayout *actions;
};
// Only report content scrolls; tab actions stay above the dialog-wide footer.
ReportPageLayout reportPageLayout(QWidget *parent)
{
    auto *layout = new QVBoxLayout(parent);
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget(scroll);
    auto *contents = new QVBoxLayout(page);
    contents->setContentsMargins(0, 0, 0, 0);
    contents->setAlignment(Qt::AlignTop);
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    auto *actions = new QHBoxLayout;
    layout->addLayout(actions);
    return {contents, actions};
}
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
void showStorageFields(QFormLayout *form, const QVector<StorageDisplayField> &fields)
{
    clearForm(form);
    for (const StorageDisplayField &field : fields) {
        auto *caption = plainLabel(field.label + ':', form->parentWidget());
        auto *value = plainLabel(field.value, form->parentWidget());
        const QString hint = propertyToolTip(field.source);
        caption->setToolTip(hint);
        value->setToolTip(hint);
        form->addRow(caption, value);
    }
}
QString selectedStorageText(const QVector<StorageDisplayRow> &rows, int index)
{
    if (index < 0 || index >= rows.size()) return {};
    const StorageDisplayRow &row = rows[index];
    return storageRowsText({row});
}
}

PropertiesDialog::PropertiesDialog(const Device &device, QWidget *parent) : QDialog(parent)
{
    snapshot_.device = device;
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("%1 — Properties").arg(deviceDisplayName(device)));
    resize(680, 560);
    auto *layout = new QVBoxLayout(this);
    banner_ = plainLabel(tr("Loading properties…"), this);
    layout->addWidget(banner_);
    tabs_ = new QTabWidget(this);
    auto *tabs = tabs_;
    layout->addWidget(tabs, 1);
    const auto addForm = [tabs](const QString &title) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *page = new QWidget(scroll);
        auto *form = new QFormLayout(page);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
        return form;
    };
    general_ = addForm(tr("General"));
    driver_ = addForm(tr("Driver"));
    const auto addSnapshotPage = [this, tabs](QWidget *&page, QVBoxLayout *&groups, QString *text,
        const QString &heading, const QString &tooltip, const QString &copyCaption,
        const PropertyPageOptions &options = PropertyPageOptions{}) {
        const auto widgets = createPropertyPage(tabs, heading, tooltip, copyCaption, options);
        page = widgets.page;
        groups = widgets.groups;
        // Read the member at click time, after any snapshot rebuild.
        connect(widgets.copyButton, &QPushButton::clicked, this, [text] {
            QApplication::clipboard()->setText(*text);
        });
    };
    networkPage_ = new NetworkPage(tabs);
    bluetoothPage_ = new BluetoothPage(tabs);
    addSnapshotPage(cameraPage_, cameraGroups_, &cameraText_,
        tr("ℹ️ Driver-reported camera capabilities snapshot"),
        tr("Queries only: no capture, format changes or measured frame rates. Reload properties to refresh. "
        "Rows preserve the format, size and frame interval relationship. Size ranges do not imply a common rate list."),
        tr("Copy camera properties"));
    addSnapshotPage(displayPage_, displayGroups_, &displayText_,
        tr("ℹ️ Monitor hardware and display configuration snapshot"),
        tr("Monitor information is reported by this connector’s cached EDID and kernel mode list; "
        "it does not guarantee capabilities. Active settings are shown separately below. Resolution and refresh rate describe the configured physical mode. "
        "Desktop color depth is not the panel or link depth. Scale excludes Qt application scaling. "
        "Wayland scale derived from logical size may include rounding; integer scale is a fallback. "
        "Reload properties to refresh. Unmatched or ambiguous connector names remain unavailable."),
        tr("Copy display properties"));
    addSnapshotPage(usbPage_, usbGroups_, &usbText_,
        tr("ℹ️ USB configuration snapshot"),
        tr("Power information is declared by the active USB configuration, not measured consumption. "
        "USB version and speed are also available as raw values on Details. Reload properties to refresh."),
        tr("Copy USB properties"));
    addSnapshotPage(batteryPage_, batteryGroups_, &batteryText_,
        tr("ℹ️ Battery snapshot for this device"),
        tr("Readings are reported by the kernel and battery firmware. Capacity retention is an estimate from "
        "compatible full/design values, not a complete health assessment. Energy and charge units remain separate. "
        "Reload properties to refresh; no background monitoring or battery settings are changed."),
        tr("Copy Battery properties"));
    addSnapshotPage(pciePage_, pcieGroups_, &pcieText_,
        tr("ℹ️ PCIe link snapshot for this device"),
        tr("Current link readings can reflect power management and topology; a lower speed is not a fault verdict. "
        "Device maxima do not establish the capacity of the upstream path. Reading these sysfs attributes may temporarily "
        "runtime-resume the device. Reload properties to refresh."),
        tr("Copy PCIe properties"));
    storagePage_ = new QWidget(tabs);
    storagePage_->hide();
    auto *storageLayout = new QVBoxLayout(storagePage_);
    storageNotice_ = plainLabel(tr("ℹ️ Storage snapshot for this device"), storagePage_);
    storageLayout->addWidget(storageNotice_);
    auto *storageScroll = new QScrollArea(storagePage_);
    storageScroll->setWidgetResizable(true);
    storageScroll->setFrameShape(QFrame::NoFrame);
    auto *storageFormPage = new QWidget(storageScroll);
    storage_ = new QFormLayout(storageFormPage);
    storage_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    storage_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    storageScroll->setWidget(storageFormPage);
    storageLayout->addWidget(storageScroll, 1);
    auto *copyStorage = new QPushButton(tr("Copy storage overview"), storagePage_);
    storageLayout->addWidget(copyStorage);
    connect(copyStorage, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(storageFieldsText(storagePresentation_.overview)
            + "\n\n" + storagePresentation_.notes);
    });

    volumesPage_ = new QWidget(tabs);
    volumesPage_->hide();
    const auto volumesReport = reportPageLayout(volumesPage_);
    auto *volumesLayout = volumesReport.contents;
    volumesNotice_ = plainLabel(tr("ℹ️ Related volumes and mounts"), volumesPage_);
    volumesLayout->addWidget(volumesNotice_);
    volumesTable_ = new ReportTable(0, 5, volumesPage_);
    volumesTable_->setHorizontalHeaderLabels({tr("Device / label"), tr("Layer"), tr("Capacity"), tr("Content"), tr("Mount points")});
    volumesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    volumesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    volumesTable_->setColumnWidth(0, 180);
    volumesTable_->setColumnWidth(1, 110);
    volumesTable_->setColumnWidth(2, 180);
    volumesLayout->addWidget(volumesTable_);
    auto *volumeButtons = volumesReport.actions;
    volumeDetailsButton_ = new QPushButton(tr("Volume details…"), volumesPage_);
    volumeDetailsButton_->setEnabled(false);
    auto *copyVolume = new QPushButton(tr("Copy selected volume"), volumesPage_);
    auto *copyVolumes = new QPushButton(tr("Copy all volumes"), volumesPage_);
    volumeButtons->addWidget(volumeDetailsButton_);
    volumeButtons->addWidget(copyVolume);
    volumeButtons->addWidget(copyVolumes);
    volumeButtons->addStretch();
    volumesLayout->addStretch();
    connect(volumesTable_, &QTableWidget::currentCellChanged, this, [this] {
        volumeDetailsButton_->setEnabled(volumesTable_->currentRow() >= 0);
    });
    connect(volumeDetailsButton_, &QPushButton::clicked, this, &PropertiesDialog::showVolumeDetails);
    connect(volumesTable_, &QTableWidget::cellDoubleClicked, this, [this] { showVolumeDetails(); });
    connect(copyVolume, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(selectedStorageText(storagePresentation_.volumes, volumesTable_->currentRow()));
    });
    connect(copyVolumes, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(storageRowsText(storagePresentation_.volumes));
    });

    healthPage_ = new QWidget(tabs);
    healthPage_->hide();
    auto *healthLayout = new QVBoxLayout(healthPage_);
    healthNotice_ = plainLabel(tr("ℹ️ Cached drive health"), healthPage_);
    healthLayout->addWidget(healthNotice_);
    auto *healthLabel = new QLabel(tr("&Drive:"), healthPage_);
    healthDrive_ = new QComboBox(healthPage_);
    healthDrive_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    healthDrive_->setMinimumContentsLength(24);
    healthLabel->setBuddy(healthDrive_);
    healthLayout->addWidget(healthLabel);
    healthLayout->addWidget(healthDrive_);
    auto *healthScroll = new QScrollArea(healthPage_);
    healthScroll->setWidgetResizable(true);
    healthScroll->setFrameShape(QFrame::NoFrame);
    auto *healthReport = new QWidget(healthScroll);
    healthGroups_ = new QVBoxLayout(healthReport);
    healthScroll->setWidget(healthReport);
    healthLayout->addWidget(healthScroll, 1);
    auto *healthButtons = new QHBoxLayout;
    auto *copyHealth = new QPushButton(tr("Copy selected drive health"), healthPage_);
    auto *copyAllHealth = new QPushButton(tr("Copy all drive health"), healthPage_);
    healthButtons->addWidget(copyHealth);
    healthButtons->addWidget(copyAllHealth);
    healthButtons->addStretch();
    healthLayout->addLayout(healthButtons);
    connect(healthDrive_, &QComboBox::currentIndexChanged, this, &PropertiesDialog::showHealth);
    connect(copyHealth, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(selectedStorageText(storagePresentation_.health, healthDrive_->currentIndex()));
    });
    connect(copyAllHealth, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(storageRowsText(storagePresentation_.health));
    });
    auto *details = new QWidget(tabs);
    auto *detailsLayout = new QVBoxLayout(details);
    auto *propertyLabel = new QLabel(tr("&Property:"), details);
    property_ = new QComboBox(details);
    property_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    property_->setMinimumContentsLength(24);
    propertyLabel->setBuddy(property_);
    detailsLayout->addWidget(propertyLabel);
    detailsLayout->addWidget(property_);
    value_ = new QPlainTextEdit(details);
    value_->setReadOnly(true);
    detailsLayout->addWidget(value_, 1);
    source_ = plainLabel(QString(), details);
    detailsLayout->addWidget(source_);
    advanced_ = new QCheckBox(tr("Show &advanced properties"), details);
    detailsLayout->addWidget(advanced_);
    auto *copyLayout = new QHBoxLayout;
    auto *copySelected = new QPushButton(tr("Copy selection"), details);
    auto *copyValue = new QPushButton(tr("Copy value"), details);
    auto *copyEverything = new QPushButton(tr("Copy all values"), details);
    copySelected->setEnabled(false);
    connect(value_, &QPlainTextEdit::copyAvailable, copySelected, &QPushButton::setEnabled);
    connect(copySelected, &QPushButton::clicked, value_, &QPlainTextEdit::copy);
    connect(copyValue, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(value_->toPlainText()); });
    connect(copyEverything, &QPushButton::clicked, this, &PropertiesDialog::copyAll);
    for (auto *button : {copySelected, copyValue, copyEverything}) copyLayout->addWidget(button);
    copyLayout->addStretch();
    detailsLayout->addLayout(copyLayout);
    tabs->addTab(details, tr("Details"));
    resourcesPage_ = new QWidget(tabs);
    resourcesPage_->hide(); // Not a tab until metadata confirms applicability.
    const auto resourcesReport = reportPageLayout(resourcesPage_);
    auto *resourcesLayout = resourcesReport.contents;
    resourcesTable_ = new ReportTable(0, 3, resourcesPage_);
    resourcesTable_->setHorizontalHeaderLabels({tr("Resource type"), tr("Setting"), tr("Details")});
    resourcesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resourcesTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    resourcesLayout->addWidget(resourcesTable_);
    auto *resourceButtons = resourcesReport.actions;
    auto *copyResourceSelection = new QPushButton(tr("Copy selection"), resourcesPage_);
    auto *copyResourceAll = new QPushButton(tr("Copy all resources"), resourcesPage_);
    copyResourceSelection->setEnabled(false);
    connect(resourcesTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this, copyResourceSelection] {
            copyResourceSelection->setEnabled(resourcesTable_->selectionModel()->hasSelection());
        });
    connect(copyResourceSelection, &QPushButton::clicked, this, [this] { copyResources(true); });
    connect(copyResourceAll, &QPushButton::clicked, this, [this] { copyResources(false); });
    resourceButtons->addWidget(copyResourceSelection);
    resourceButtons->addWidget(copyResourceAll);
    resourceButtons->addStretch();
    resourcesLayout->addStretch();
    // The tab appears only after collection finds resource records or read errors.
    auto *eventsPage = new QWidget(tabs);
    const auto eventsReport = reportPageLayout(eventsPage);
    auto *eventsLayout = eventsReport.contents;
    eventsNotice_ = plainLabel(tr("ℹ️ Live udev events since the application started"), eventsPage);
    eventsLayout->addWidget(eventsNotice_);
    eventsTable_ = new ReportTable(0, 3, eventsPage);
    eventsTable_->setHorizontalHeaderLabels({tr("Observed time"), tr("Event type"), tr("Description")});
    eventsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventsTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    eventsLayout->addWidget(eventsTable_);
    eventDetails_ = new QPlainTextEdit(eventsPage);
    eventDetails_->setReadOnly(true);
    eventsLayout->addWidget(eventDetails_, 1);
    auto *eventButtons = eventsReport.actions;
    auto *copyEventSelection = new QPushButton(tr("Copy selection"), eventsPage);
    auto *copyEventAll = new QPushButton(tr("Copy all events"), eventsPage);
    copyEventSelection->setEnabled(false);
    connect(eventsTable_->selectionModel(), &QItemSelectionModel::selectionChanged, this,
        [this, copyEventSelection] {
            copyEventSelection->setEnabled(eventsTable_->selectionModel()->hasSelection());
        });
    connect(eventsTable_, &QTableWidget::currentCellChanged, this, [this] { showEvent(); });
    connect(copyEventSelection, &QPushButton::clicked, this, [this] { copyEvents(true); });
    connect(copyEventAll, &QPushButton::clicked, this, [this] { copyEvents(false); });
    eventButtons->addWidget(copyEventSelection);
    eventButtons->addWidget(copyEventAll);
    eventButtons->addStretch();
    tabs->addTab(eventsPage, tr("Events"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    reload_ = buttons->addButton(tr("Reload properties"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reload_, &QPushButton::clicked, this, &PropertiesDialog::reloadRequested);
    connect(property_, &QComboBox::currentIndexChanged, this, &PropertiesDialog::showDetail);
    connect(advanced_, &QCheckBox::toggled, this, &PropertiesDialog::rebuildDetails);
    layout->addWidget(buttons);
    rebuild();
}
void PropertiesDialog::setBusy(bool busy)
{
    reload_->setEnabled(!busy && !removed_);
    if (busy && !removed_) {
        banner_->setText(tr("Loading properties…"));
        banner_->show();
    }
}
void PropertiesDialog::acceptResult(DeviceProperties result)
{
    Q_ASSERT(QThread::isMainThread());
    if (removed_ || result.device.path != device().path || result.device.generation != device().generation) return;
    setBusy(false);
    if (result.state == ReadState::Removed) { markRemoved(); return; }
    if (result.state != ReadState::Available) {
        banner_->show();
        banner_->setText(tr("Properties could not be refreshed: %1. Showing the previous snapshot.")
            .arg(propertyReadValue({result.state, {}, result.error})));
        return;
    }
    snapshot_ = std::move(result);
    banner_->clear();
    banner_->hide();
    rebuild();
}
void PropertiesDialog::reconcileInstance(const Device *current)
{
    if (removed_) return;
    if (!current || current->generation != device().generation || current->incarnation != device().incarnation) {
        markRemoved();
        return;
    }
    // Keep the same-instance snapshot until an explicit property reload.
    // An inventory refresh must never silently retarget this dialog.
}
void PropertiesDialog::markRemoved()
{
    removed_ = true;
    if (gpuTest_) gpuTest_->kill();
    eventScope_ += '\n' + tr("Removed/replaced device: retained events are a read-only instance snapshot.");
    eventsNotice_->setToolTip(propertyToolTip(eventScope_));
    reload_->setEnabled(false);
    banner_->show();
    banner_->setText(tr("Device removed or replaced. This dialog is a read-only snapshot; close it and reopen Properties for the current device."));
    rebuild();
}
void PropertiesDialog::rebuild()
{
    rebuildResources();
    entries_ = propertyEntries(snapshot_, removed_);
    clearForm(general_);
    clearForm(driver_);
    rebuildStorage();
    rebuildNetwork();
    rebuildBluetooth();
    rebuildDisplay();
    rebuildCamera();
    rebuildUsb();
    rebuildPcie();
    rebuildBattery();
    for (const PropertyEntry &entry : entries_) {
        if (entry.tab > 1) continue;
        QFormLayout *form = entry.tab == 0 ? general_ : driver_;
        auto *label = plainLabel(entry.value, this);
        label->setToolTip(entry.source.toHtmlEscaped());
        form->addRow(entry.label + ':', label);
    }
    rebuildDetails();
}
void PropertiesDialog::rebuildDetails()
{
    const QString previous = property_->currentData().toString();
    const QSignalBlocker blocker(property_);
    property_->clear();
    QVector<const PropertyEntry *> available, other;
    for (const PropertyEntry &entry : entries_) {
        if (!isDetailsProperty(entry, device()) || (entry.advanced && !advanced_->isChecked())) continue;
        if (detailsPropertyStatus(entry).isEmpty()) available.append(&entry);
        else other.append(&entry);
    }
    for (const PropertyEntry *entry : available) property_->addItem(entry->label, entry->id);
    if (!available.isEmpty() && !other.isEmpty()) property_->insertSeparator(property_->count());
    for (const PropertyEntry *entry : other)
        property_->addItem(tr("%1 (%2)").arg(entry->label, detailsPropertyStatus(*entry)), entry->id);
    const int index = property_->findData(previous);
    property_->setCurrentIndex(index < 0 ? 0 : index);
    showDetail();
}
void PropertiesDialog::showDetail()
{
    const QString id = property_->currentData().toString();
    for (const PropertyEntry &entry : entries_) {
        if (entry.id != id) continue;
        value_->setFont(id == "efi/hex" ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QApplication::font());
        value_->setLineWrapMode(id == "efi/hex" ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
        value_->setPlainText(entry.value);
        source_->setText(entry.source.isEmpty() ? QString() : tr("Source: %1").arg(entry.source));
        return;
    }
    value_->clear();
    source_->clear();
}
void PropertiesDialog::copyAll()
{
    QStringList values;
    for (const PropertyEntry &entry : entries_) {
        if (!isDetailsProperty(entry, device()) || (entry.advanced && !advanced_->isChecked())) continue;
        QString text = entry.label + ":\n" + entry.value;
        if (!entry.source.isEmpty()) text += '\n' + tr("Source: %1").arg(entry.source);
        values.append(text);
    }
    QApplication::clipboard()->setText(values.join("\n\n"));
}

void PropertiesDialog::rebuildResources()
{
    const DeviceResources &data = snapshot_.resources;
    const int existing = tabs_->indexOf(resourcesPage_);
    if (data.hasInformation() && existing < 0) tabs_->addTab(resourcesPage_, tr("Resources"));
    else if (!data.hasInformation() && existing >= 0) {
        tabs_->removeTab(existing);
        resourcesPage_->hide();
    }
    resourcesTable_->setRowCount(0);
    QStringList copy;
    const auto row = [this, &copy](const QString &type, const QString &setting,
                                 const QString &details, const QString &source) {
        const int index = resourcesTable_->rowCount();
        resourcesTable_->insertRow(index);
        const QStringList values {type, setting, details};
        for (int column = 0; column < values.size(); ++column) {
            auto *item = new QTableWidgetItem(values[column]);
            item->setToolTip(source.toHtmlEscaped());
            resourcesTable_->setItem(index, column, item);
        }
        resourcesTable_->item(index, 0)->setData(Qt::UserRole, source);
        copy.append(values.join('\t') + '\n' + tr("Source: %1").arg(source));
    };
    if (!data.pnpState.isEmpty())
        copy.append(tr("PnP device state: %1").arg(data.pnpState == "active" ? tr("Active") : tr("Disabled")));
    for (const ResourceRow &entry : resourceRows(data))
        row(entry.type, entry.setting, entry.details, entry.source);
    resourcesText_ = copy.join("\n\n");
}
void PropertiesDialog::copyResources(bool selectedOnly)
{
    QList<int> rows;
    if (selectedOnly) {
        for (const QModelIndex &index : resourcesTable_->selectionModel()->selectedRows()) rows.append(index.row());
        std::sort(rows.begin(), rows.end());
    } else {
        QApplication::clipboard()->setText(resourcesText_);
        return;
    }
    QStringList text;
    for (const int row : rows) {
        QStringList values;
        for (int column = 0; column < resourcesTable_->columnCount(); ++column)
            values.append(resourcesTable_->item(row, column)->text());
        text.append(values.join('\t') + '\n' + tr("Source: %1").arg(
            resourcesTable_->item(row, 0)->data(Qt::UserRole).toString()));
    }
    QApplication::clipboard()->setText(text.join("\n\n"));
}

void PropertiesDialog::rebuildBluetooth()
{
    const bool applicable = device().subsystem == "bluetooth" && bluetoothControllerName(device().sysname);
    const int existing = tabs_->indexOf(bluetoothPage_);
    if (applicable && existing < 0) tabs_->insertTab(2, bluetoothPage_, tr("Bluetooth"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        bluetoothPage_->hide();
    }
    if (applicable) bluetoothPage_->setSnapshot(snapshot_.bluetooth, device().path);
    else bluetoothPage_->clear();
}

void PropertiesDialog::rebuildNetwork()
{
    const bool applicable = device().subsystem == "net" || device().category == "network";
    const int existing = tabs_->indexOf(networkPage_);
    if (applicable && existing < 0) tabs_->insertTab(2, networkPage_, tr("Network"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        networkPage_->hide();
    }
    if (applicable) networkPage_->setSnapshot(snapshot_.network, snapshot_.networkState, device().path);
    else networkPage_->clear();
}

void PropertiesDialog::rebuildCamera()
{
    const CameraProperties &data = snapshot_.camera;
    const int existing = tabs_->indexOf(cameraPage_);
    if (data.applicable && existing < 0) tabs_->insertTab(2, cameraPage_, tr("Camera"));
    else if (!data.applicable && existing >= 0) { tabs_->removeTab(existing); cameraPage_->hide(); }
    while (QLayoutItem *item = cameraGroups_->takeAt(0)) { delete item->widget(); delete item; }
    cameraText_.clear();
    if (!data.applicable) return;
    QStringList copy;
    PropertyReport fieldsReport(cameraGroups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto errorText = [&](int error) {
        if (error == EBUSY) return tr("Device busy");
        if (error == ETIMEDOUT) return tr("Query timed out");
        if (error == EAGAIN) return tr("Previous camera query has not exited; retry later");
        if (error == EOVERFLOW) return tr("Enumeration or report limit reached");
        if (error == EPROTO) return tr("Malformed driver response");
        const ReadState state = error == EACCES || error == EPERM ? ReadState::PermissionDenied
            : error == ENODEV || error == ENOENT || error == ENXIO ? ReadState::Removed
            : error == EINVAL || error == ENOTTY || error == EOPNOTSUPP ? ReadState::Unsupported : ReadState::Error;
        return propertyReadValue({state, {}, error});
    };
    const QJsonObject &report = data.report;
    const QJsonArray modes = report.value("modes").toArray();
    auto *identity = group(tr("Capture device"));
    const QString querySource = data.node + ": VIDIOC_QUERYCAP via libv4l2 native passthrough";
    field(identity, tr("Device node"), data.node.isEmpty() ? tr("Unavailable") : data.node, device().path + " / udev devnode; verified sysfs dev and /sys/dev/char binding");
    QString status = data.state.state == ReadState::Available ? tr("Driver-reported snapshot")
        : data.state.error ? errorText(data.state.error) : propertyReadValue(data.state);
    if (data.state.state != ReadState::Available && !modes.isEmpty()) status = tr("Incomplete enumeration: %1").arg(status);
    field(identity, tr("Query status"), status, data.node + ": read-only capability enumeration; two-second worker wait budget");
    const QVector<QPair<QString, QString>> identityFields {{"card", tr("Device name")}, {"driver", tr("Driver")}, {"bus", tr("Bus information")}};
    for (const auto &entry : identityFields) {
        const QString value = report.value(entry.first).toString();
        if (report.contains(entry.first)) field(identity, entry.second, value.isEmpty() ? tr("Unavailable") : value, querySource);
    }
    auto *scope = group(tr("Collection scope"));
    field(scope, tr("Coverage"), tr("This capture node and current media pipeline. No capture or configuration changes. "
        "Reported modes do not guarantee throughput or compatibility with other simultaneous streams."), querySource);
    fieldsReport.alignCaptions();
    if (!modes.isEmpty()) {
        const QString title = tr("Format, size and frame rate combinations");
        auto *box = new QGroupBox(title, cameraGroups_->parentWidget());
        auto *layout = new QVBoxLayout(box);
        auto *table = new ReportTable(static_cast<int>(modes.size()), 4, box);
        const QStringList headers {tr("Capture queue"), tr("Pixel format"), tr("Resolution / range"), tr("Frame rates / intervals")};
        table->setHorizontalHeaderLabels(headers);
        table->setSelectionBehavior(QAbstractItemView::SelectItems);
        layout->addWidget(table); cameraGroups_->addWidget(box);
        copy.append(QString()); copy.append(title);
        const auto fractionText = [](const QJsonArray &f) { return QString::number(f.at(0).toInteger()) + '/' + QString::number(f.at(1).toInteger()); };
        const auto fps = [](const QJsonArray &f) { return static_cast<double>(f.at(1).toInteger()) / static_cast<double>(f.at(0).toInteger()); };
        for (qsizetype index = 0; index < modes.size(); ++index) {
            const QJsonObject row = modes.at(index).toObject();
            const quint32 code = static_cast<quint32>(row.value("format").toInteger());
            QString fourcc;
            for (int shift : {0, 8, 16, 24}) {
                const unsigned c = (code >> shift) & (shift == 24 ? 127 : 255);
                fourcc += c >= 32 && c <= 126 ? QString(QChar(static_cast<ushort>(c))) : QString("\\x%1").arg(c, 2, 16, QChar('0'));
            }
            QString format = fourcc + QString(" (0x%1)").arg(code, 8, 16, QChar('0'));
            if (code & 0x80000000U) format += tr(" (big endian)");
            const QString description = row.value("description").toString();
            if (!description.isEmpty()) format += " — " + description;
            const quint32 flags = static_cast<quint32>(row.value("flags").toInteger());
            if (flags & 1) format += tr(" (compressed)");
            if (flags & 2) format += tr(" (emulated, reported by driver)");
            // Keep the complete driver report for Copy, with concise names in the table.
            QString displayFormat;
            if (fourcc == "MJPG") displayFormat = "MJPEG";
            else if (fourcc == "YUYV") displayFormat = "YUYV 4:2:2";
            else if (description == fourcc || description.startsWith(fourcc + ' ')) displayFormat = description;
            else displayFormat = description.isEmpty() ? fourcc : fourcc + " — " + description;
            if (code & 0x80000000U) displayFormat += tr(" (big endian)");
            if ((flags & 1) && fourcc != "MJPG") displayFormat += tr(" (compressed)");
            if (flags & 2) displayFormat += tr(" (emulated, reported by driver)");
            const QString kind = row.value("size_kind").toString();
            QString size;
            if (row.contains("size_error")) size = errorText(row.value("size_error").toInt());
            else if (kind == "discrete") size = tr("%1 × %2").arg(row.value("width").toInteger()).arg(row.value("height").toInteger());
            else {
                size = tr("%1–%2 × %3–%4 pixels").arg(row.value("width").toInteger()).arg(row.value("max_width").toInteger())
                    .arg(row.value("height").toInteger()).arg(row.value("max_height").toInteger());
                size += kind == "continuous" ? tr(" (continuous)")
                    : tr(" (steps: %1 × %2 pixels)").arg(row.value("step_width").toInteger()).arg(row.value("step_height").toInteger());
            }
            QString rates;
            const QJsonObject interval = row.value("intervals").toObject();
            if (kind != "discrete") rates = tr("Not enumerated: intervals depend on the exact size");
            else if (interval.contains("error")) rates = errorText(interval.value("error").toInt());
            else if (interval.value("kind") == "discrete") {
                QStringList values;
                for (const QJsonValue &value : interval.value("values").toArray()) {
                    const QJsonArray f = value.toArray();
                    values.append(tr("%1 fps (%2 s)").arg(QLocale().toString(fps(f), 'g', 8), fractionText(f)));
                }
                rates = values.join('\n');
            } else {
                const QJsonArray minimum = interval.value("min").toArray(), maximum = interval.value("max").toArray();
                rates = tr("%1–%2 fps; intervals %3–%4 s").arg(QLocale().toString(fps(maximum), 'g', 8), QLocale().toString(fps(minimum), 'g', 8), fractionText(minimum), fractionText(maximum));
                rates += interval.value("kind") == "continuous" ? tr(" (continuous)")
                    : tr(" (interval step: %1 s)").arg(fractionText(interval.value("step").toArray()));
            }
            const QStringList values {row.value("queue") == "multi" ? tr("Multiple planes") : tr("Single plane"), displayFormat, size, rates};
            QStringList copyValues = values;
            copyValues[1] = format;
            const QString source = data.node + ": VIDIOC_ENUM_FMT / VIDIOC_ENUM_FRAMESIZES / VIDIOC_ENUM_FRAMEINTERVALS\n"
                + tr("Intervals queried only for this pixel format and exact discrete size. Ranges are not expanded.");
            copy.append(QString()); copy.append(tr("Mode %1").arg(index + 1));
            for (int column = 0; column < values.size(); ++column) {
                auto *item = new QTableWidgetItem(values.at(column));
                item->setToolTip(source); item->setTextAlignment(Qt::AlignLeading | Qt::AlignTop);
                table->setItem(static_cast<int>(index), column, item);
                copy.append(headers.at(column) + ": " + copyValues.at(column));
            }
        }
        table->fitContents();
    }
    cameraGroups_->addStretch();
    cameraText_ = copy.join('\n');
}

void PropertiesDialog::rebuildDisplay()
{
    const bool applicable = device().subsystem == "drm" && device().sysname.contains('-');
    const int existing = tabs_->indexOf(displayPage_);
    if (applicable && existing < 0) tabs_->insertTab(2, displayPage_, tr("Display"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        displayPage_->hide();
    }
    while (QLayoutItem *item = displayGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    displayText_.clear();
    if (!applicable) return;
    const DisplayProperties &data = snapshot_.display;
    QStringList copy;
    PropertyReport fieldsReport(displayGroups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };

    const MonitorProperties &monitor = snapshot_.monitor;
    if (monitor.applicable) {
        const auto hardwareValue = [&](const QString &key) { return propertyReadValue(monitor.values.value(key)); };
        const auto hardwareField = [&](QGridLayout *form, const QString &caption, const QString &key) {
            field(form, caption, hardwareValue(key), monitor.sources.value(key));
        };
        auto *identity = group(tr("Monitor hardware (reported)"));
        QString descriptorStatus;
        if (monitor.descriptor.state == ReadState::Available) {
            descriptorStatus = monitor.values.contains("warnings") ? tr("Decoded with conformance warnings") : tr("Decoded by libdisplay-info");
        } else if (monitor.descriptor.state == ReadState::Unsupported && monitor.descriptor.value.isEmpty()) {
            descriptorStatus = tr("EDID decoding unavailable: build with libdisplay-info support");
        } else if (monitor.descriptor.state == ReadState::Unsupported) {
            descriptorStatus = tr("Unsupported EDID version: %1").arg(monitor.descriptor.value);
        } else if (monitor.descriptor.state == ReadState::Error && monitor.descriptor.error == EPROTO) {
            descriptorStatus = tr("Malformed EDID (invalid header, length, checksum or parser rejection)");
        } else if (monitor.descriptor.state == ReadState::Error && monitor.descriptor.error == EAGAIN) {
            descriptorStatus = tr("Monitor report changed during collection; reload properties");
        } else descriptorStatus = propertyReadValue(monitor.descriptor);
        field(identity, tr("Descriptor"), descriptorStatus, monitor.sources.value("descriptor"));
        if (monitor.values.contains("version")) hardwareField(identity, tr("EDID version"), "version");
        if (monitor.descriptor.state == ReadState::Available) {
            hardwareField(identity, tr("Manufacturer"), "manufacturer");
            hardwareField(identity, tr("Manufacturer code"), "manufacturer_code");
            hardwareField(identity, tr("Model"), "model");
            hardwareField(identity, tr("Product code"), "product");
            hardwareField(identity, tr("Serial number"), "serial");
            const Attribute size = monitor.values.value("size");
            const QStringList dimensions = size.value.split(' ');
            field(identity, tr("Image dimensions"), size.state == ReadState::Available
                ? tr("%1 × %2 cm").arg(dimensions.value(0), dimensions.value(1)) : propertyReadValue(size), monitor.sources.value("size"));
            const QString ppi = monitorPixelsPerInch(monitor);
            if (!ppi.isEmpty()) field(identity, tr("PPI"), QLocale().toString(ppi.toLongLong()), tr("Pixels per inch"));
            if (monitor.values.contains("warnings")) hardwareField(identity, tr("Conformance warnings"), "warnings");
            auto *capabilities = group(tr("Monitor input (reported)"));
            const Attribute digital = monitor.values.value("digital");
            field(capabilities, tr("Signal type"), digital.state == ReadState::Available
                ? (digital.value == "1" ? tr("Digital") : tr("Analog")) : propertyReadValue(digital), monitor.sources.value("digital"));
            if (digital.state == ReadState::Available && digital.value == "1") {
                hardwareField(capabilities, tr("Input interface"), "interface");
                const Attribute depth = monitor.values.value("depth");
                field(capabilities, tr("Declared color depth"), depth.state == ReadState::Available
                    ? tr("%1 bits per color component").arg(depth.value) : propertyReadValue(depth), monitor.sources.value("depth"));
            }
        }
        auto *reportedModes = group(tr("Kernel-reported modes"));
        hardwareField(reportedModes, tr("Mode names"), "modes");
        field(reportedModes, tr("Scope"), tr("Cached mode names; refresh rates are not exposed here. "
            "The list may include driver additions or restrictions and does not guarantee monitor capabilities."), monitor.sources.value("modes"));
    }
    if (data.status.state == ReadState::Available && data.status.value == "disconnected") {
        auto *session = group(tr("Desktop session"));
        field(session, tr("Configuration"), tr("Disconnected"), data.sources.value("configuration"));
        fieldsReport.alignCaptions();
        displayGroups_->addStretch();
        displayText_ = copy.join('\n');
        return;
    }
    const auto raw = [&](const QString &key) {
        return data.status.state == ReadState::Available ? data.values.value(key) : Attribute {data.status.state, {}, data.status.error};
    };
    const auto formatted = [&](const QString &key) {
        const Attribute value = raw(key);
        if (value.state != ReadState::Available) return propertyReadValue(value);
        if (key == "resolution") return QString(value.value).replace('x', QChar(0x00d7));
        if (key == "depth") return tr("%1 bits per pixel").arg(value.value);
        if (key == "refresh") return tr("%1 Hz").arg(QLocale().toString(value.value.toDouble(), 'f', 2));
        if (key == "rotation") return value.value == "0" ? tr("0°")
            : tr("%1° counterclockwise").arg(value.value);
        if (key == "reflection") return value.value == "xy" ? tr("X and Y axes") : value.value == "x" ? tr("X axis")
            : value.value == "y" ? tr("Y axis") : value.value == "vertical" ? tr("Vertical axis") : tr("None");
        if (key == "scale") {
            if (value.value == "custom") return tr("Custom transform");
            const QStringList axes = value.value.split(' ');
            const double x = axes.value(0).toDouble(), y = axes.value(1).toDouble();
            QString text = axes.value(0) == axes.value(1) ? tr("%1×").arg(QLocale().toString(x, 'g', 6))
                : tr("X: %1×, Y: %2×").arg(QLocale().toString(x, 'g', 6), QLocale().toString(y, 'g', 6));
            const QString kind = data.values.value("scale_kind").value;
            if (kind == "derived") text += tr(" (derived)");
            if (kind == "integer") text += tr(" (integer buffer scale)");
            return text;
        }
        return value.value;
    };
    auto *session = group(tr("Desktop session"));
    field(session, tr("Backend"), data.backend.isEmpty() ? tr("Unavailable") : data.backend);
    field(session, tr("Output"), data.output.isEmpty() ? tr("Unavailable") : data.output,
        data.sources.value("output", device().path));
    field(session, tr("Configuration"), data.status.state == ReadState::Available
        ? (data.status.value == "inactive" ? tr("No active mode") : tr("Reported by desktop server"))
        : propertyReadValue(data.status), data.sources.value("configuration", tr("Read-only session query. Unavailable can mean missing runtime libraries, "
            "an output not exposed by this session, or a protocol without usable output names.")));
    auto *mode = group(tr("Active display mode"));
    field(mode, tr("Resolution"), formatted("resolution"), data.sources.value("resolution"));
    field(mode, tr("Desktop color depth"), formatted("depth"), data.sources.value("depth"));
    field(mode, tr("Refresh rate"), formatted("refresh"), data.sources.value("refresh"));
    auto *transform = group(tr("Orientation and scale"));
    field(transform, tr("Rotation"), formatted("rotation"), data.sources.value("rotation"));
    field(transform, tr("Reflection"), formatted("reflection"), data.sources.value("reflection"));
    field(transform, tr("Output scale"), formatted("scale"), data.sources.value("scale"));
    fieldsReport.alignCaptions();
    displayGroups_->addStretch();
    displayText_ = copy.join('\n');
}

void PropertiesDialog::rebuildUsb()
{
    const bool applicable = device().subsystem == "usb" && device().devtype == "usb_device";
    const int existing = tabs_->indexOf(usbPage_);
    if (applicable && existing < 0) tabs_->insertTab(2, usbPage_, tr("USB"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        usbPage_->hide();
    }
    while (QLayoutItem *item = usbGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    usbText_.clear();
    if (!applicable) return;
    const UsbProperties &data = snapshot_.usb;
    QStringList copy;
    PropertyReport fieldsReport(usbGroups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto value = [&](const QString &key) { return propertyReadValue(data.values.value(key)); };
    const auto source = [&](const QString &key) { return device().path + '/' + key; };
    auto *connection = group(tr("Connection"));
    const Attribute version = snapshot_.values.value("sysfs/version");
    field(connection, tr("USB version"), propertyReadValue(version), source("version")
        + '\n' + tr("USB specification version reported by the device descriptor; not the port's maximum capability."));
    const Attribute speed = snapshot_.values.value("sysfs/speed");
    QString speedText = propertyReadValue(speed);
    if (speed.state == ReadState::Available) {
        bool valid = false;
        const double mbps = speed.value.toDouble(&valid);
        if (speed.value.compare("unknown", Qt::CaseInsensitive) == 0)
            speedText = propertyReadValue({ReadState::Unavailable, {}, 0});
        else if (!valid || !std::isfinite(mbps) || mbps <= 0)
            speedText = propertyReadValue({ReadState::Error, {}, EINVAL});
        else speedText = mbps >= 1000
            ? tr("%1 Gbps").arg(QLocale().toString(mbps / 1000, 'g', 6))
            : tr("%1 Mbps").arg(QLocale().toString(mbps, 'g', 6));
    }
    field(connection, tr("Connection speed"), speedText, source("speed")
        + '\n' + tr("Kernel-reported link rate, not measured throughput. For root hubs this describes the USB bus."));
    auto *power = group(tr("Power declarations"));
    const Attribute attributes = data.values.value("bmAttributes");
    bool valid = false;
    const uint flags = attributes.value.toUInt(&valid, 16);
    QString supply = propertyReadValue(attributes), wake = supply;
    if (attributes.state == ReadState::Available) {
        if (!valid || flags > 255 || !(flags & 0x80)) {
            supply = wake = propertyReadValue({ReadState::Error, {}, EINVAL});
        } else {
            supply = flags & 0x40 ? tr("Self-powered") : tr("Bus-powered");
            wake = flags & 0x20 ? tr("Supported") : tr("Not supported");
        }
    }
    field(power, tr("Power source"), supply, source("bmAttributes"));
    field(power, tr("Declared maximum current"), value("bMaxPower"), source("bMaxPower"));
    field(power, tr("Remote wakeup capability"), wake, source("bmAttributes"));
    auto *configuration = group(tr("Configuration"));
    const Attribute active = data.values.value("bConfigurationValue");
    field(configuration, tr("Active configuration"), active.state == ReadState::Available && (active.value.isEmpty() || active.value == "0")
        ? tr("Unconfigured") : propertyReadValue(active), source("bConfigurationValue"));
    field(configuration, tr("Available configurations"), value("bNumConfigurations"), source("bNumConfigurations"));
    const Attribute description = data.values.value("configuration");
    field(configuration, tr("Description"), description.state == ReadState::Available && description.value.isEmpty()
        ? tr("Not provided") : propertyReadValue(description), source("configuration"));
    field(configuration, tr("Interface count"), value("bNumInterfaces"), source("bNumInterfaces"));
    // Class descriptions are deliberately limited; retain numeric codes for
    // vendor-specific and unfamiliar functions rather than guessing a name.
    const auto classText = [&](const Attribute &attribute) {
        if (attribute.state != ReadState::Available) return propertyReadValue(attribute);
        bool ok = false;
        const uint code = attribute.value.toUInt(&ok, 16);
        if (!ok || code > 255) return propertyReadValue({ReadState::Error, {}, EINVAL});
        QString name;
        switch (code) {
        case 0x01: name = tr("Audio"); break;
        case 0x02: name = tr("Communications"); break;
        case 0x03: name = tr("Human interface device"); break;
        case 0x07: name = tr("Printer"); break;
        case 0x08: name = tr("Mass storage"); break;
        case 0x09: name = tr("Hub"); break;
        case 0x0a: name = tr("CDC data"); break;
        case 0x0b: name = tr("Smart card"); break;
        case 0x0e: name = tr("Video"); break;
        case 0xe0: name = tr("Wireless controller"); break;
        case 0xef: name = tr("Miscellaneous"); break;
        case 0xfe: name = tr("Application-specific"); break;
        case 0xff: name = tr("Vendor-specific"); break;
        }
        const QString hex = QStringLiteral("0x%1").arg(code, 2, 16, QLatin1Char('0'));
        return name.isEmpty() ? hex : tr("%1 (%2)").arg(name, hex);
    };
    if (data.interfaceState.state != ReadState::Available || data.interfaces.isEmpty()) {
        auto *interfaces = group(tr("Interfaces"));
        field(interfaces, tr("Interfaces"), data.interfaceState.state == ReadState::Available
            ? tr("None observed") : propertyReadValue(data.interfaceState), device().path);
    }
    for (const UsbInterfaceProperties &interface : data.interfaces) {
        auto *form = group(tr("Interface %1").arg(interface.name));
        if (interface.values.contains("state")) {
            field(form, tr("Status"), propertyReadValue(interface.values.value("state")), interface.path);
            continue;
        }
        const auto entry = [&](const QString &key, const QString &caption) {
            field(form, caption, propertyReadValue(interface.values.value(key)), interface.path + '/' + key);
        };
        entry("bInterfaceNumber", tr("Number (hex)"));
        entry("bAlternateSetting", tr("Alternate setting"));
        const Attribute label = interface.values.value("interface");
        field(form, tr("Description"), label.state == ReadState::Available && label.value.isEmpty()
            ? tr("Not provided") : propertyReadValue(label), interface.path + "/interface");
        field(form, tr("Function class"), classText(interface.values.value("bInterfaceClass")), interface.path + "/bInterfaceClass");
        entry("bInterfaceSubClass", tr("Subclass (hex)"));
        entry("bInterfaceProtocol", tr("Protocol (hex)"));
        const Attribute driver = interface.values.value("driver");
        field(form, tr("Bound driver"), driver.state == ReadState::Available && driver.value.isEmpty()
            ? tr("Unbound") : propertyReadValue(driver), interface.path + "/driver");
    }
    fieldsReport.alignCaptions();
    usbGroups_->addStretch();
    usbText_ = copy.join('\n');
}

void PropertiesDialog::rebuildPcie()
{
    const bool applicable = device().subsystem == "pci" && snapshot_.pcieApplicable;
    const int existing = tabs_->indexOf(pciePage_);
    if (applicable && existing < 0) tabs_->insertTab(2, pciePage_, tr("PCIe"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        pciePage_->hide();
    }
    gpuTestButton_ = nullptr;
    while (QLayoutItem *item = pcieGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    pcieText_.clear();
    if (!applicable) return;
    QVector<QGridLayout *> forms;
    QVector<QWidget *> captions;
    QStringList copy;
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title, pcieGroups_->parentWidget());
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        forms.append(form);
        pcieGroups_->addWidget(box);
        if (!copy.isEmpty()) copy.append(QString());
        copy.append(title);
        return form;
    };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text, const QString &source = QString(), bool test = false) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        QWidget *captionWidget = name;
        if (test) {
            captionWidget = new QWidget(form->parentWidget());
            auto *actions = new QHBoxLayout(captionWidget);
            actions->setContentsMargins(0, 0, 0, 0);
            actions->addWidget(name);
            gpuTestButton_ = new QPushButton(gpuTestText_.isEmpty() ? tr("Test") : tr("Retest"), captionWidget);
            gpuTestButton_->setAutoDefault(false);
            gpuTestButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            gpuTestButton_->setEnabled(!removed_ && !gpuTest_ && gpuLinkTestBuilt() && geteuid() != 0);
            QString hint = tr("Apply a short graphics load to this GPU and observe its PCIe link. Power use and fan noise may increase.");
            if (!gpuLinkTestBuilt())
                hint = tr("This build has no GPU link test. Build with Vulkan headers, glslangValidator and Qt Vulkan support.");
            else if (geteuid() == 0)
                hint = tr("Run the application as an ordinary user to test the GPU link.");
            gpuTestButton_->setToolTip(hint);
            actions->addWidget(gpuTestButton_);
            connect(gpuTestButton_, &QPushButton::clicked, this, [this] {
                // Rebuild after the button's mouse/key event has returned.
                QTimer::singleShot(0, this, &PropertiesDialog::testGpuLink);
            });
        }
        captions.append(captionWidget);
        auto *value = plainLabel(text, form->parentWidget());
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setAlignment(Qt::AlignLeading | (test ? Qt::AlignVCenter : Qt::AlignTop));
        value->setMinimumWidth(120);
        const QString hint = propertyToolTip(source);
        name->setToolTip(hint);
        value->setToolTip(hint);
        const int row = form->count() / 2;
        form->addWidget(captionWidget, row, 0, Qt::AlignLeading | Qt::AlignTop);
        form->addWidget(value, row, 1);
        copy.append(caption + ": " + text);
    };
    const auto linkValue = [&](const QString &key, bool speed) {
        const Attribute value = snapshot_.values.value("sysfs/" + key);
        if (value.state == ReadState::Unsupported) return tr("Query unsupported by the kernel/device");
        if (value.state != ReadState::Available) return propertyReadValue(value);
        const QString raw = value.value;
        const auto malformed = [&] { return tr("Malformed reported value: %1").arg(raw.isEmpty() ? tr("(empty)") : raw); };
        if (raw.isEmpty()) return malformed();
        if (speed) {
            if (raw.compare("Unknown speed", Qt::CaseInsensitive) == 0
                || raw.compare("unknown", Qt::CaseInsensitive) == 0)
                return tr("Unavailable (%1)").arg(raw);
            // Parse the kernel's C-locale rate, including its usual .0 suffix.
            // Unfamiliar formats remain visible; unknown rates are not assigned
            // a generation by rounding or extrapolation.
            QString rateText = raw;
            if (rateText.endsWith(" PCIe")) rateText.chop(5);
            if (!rateText.endsWith(" GT/s")) return raw;
            rateText.chop(5);
            bool valid = false;
            const double rate = rateText.toDouble(&valid);
            if (!valid || !std::isfinite(rate) || rate <= 0) return malformed();
            int lanes = 0;
            if (key == "current_link_speed") {
                const Attribute width = snapshot_.values.value("sysfs/current_link_width");
                bool widthValid = false;
                const uint number = width.value.toUInt(&widthValid);
                if (width.state == ReadState::Available && widthValid && number > 0 && number <= 63)
                    lanes = static_cast<int>(number);
            }
            return pcieLinkText(rate, lanes);
        }
        bool valid = false;
        const qulonglong lanes = raw.toULongLong(&valid);
        if (!valid || raw.startsWith('-')) return malformed();
        // max_link_width exposes PCIE_LNK_WIDTH_UNKNOWN (0xff) verbatim.
        // It is a kernel sentinel, not a 255-lane capability. Actual PCIe
        // link width fields are six bits wide.
        if (key == "max_link_width" && lanes == 255)
            return tr("Unavailable (kernel reports unknown width: %1)").arg(raw);
        if (lanes > 63) return malformed();
        if (!lanes) return tr("Unavailable (reported width: %1)").arg(raw);
        return tr("x%1 (%2 lanes)").arg(QString::number(lanes), QLocale().toString(lanes));
    };
    const auto entry = [&](QGridLayout *form, const QString &key, const QString &caption, bool speed, const QString &scope) {
        QString source = snapshot_.sources.value("sysfs/" + key) + '\n' + scope;
        const Attribute value = snapshot_.values.value("sysfs/" + key);
        if (speed && value.state == ReadState::Available)
            source += '\n' + tr("Reported signalling rate per lane: %1").arg(value.value);
        field(form, caption, linkValue(key, speed), source);
    };
    const Attribute type = snapshot_.pcieType;
    bool validType = false;
    const uint number = type.value.toUInt(&validType);
    validType = validType && type.state == ReadState::Available;
    QString typeText = propertyReadValue(type);
    if (type.state == ReadState::Error && type.error == EPROTO)
        typeText = tr("Malformed PCI capability data");
    else if (type.state == ReadState::Error && type.error == EMSGSIZE)
        typeText = tr("Incomplete PCI configuration data");
    if (validType) {
        switch (number) {
        case 0: typeText = tr("PCIe endpoint"); break;
        case 1: typeText = tr("Legacy PCIe endpoint"); break;
        case 4: typeText = tr("Root port"); break;
        case 5: typeText = tr("Upstream switch port"); break;
        case 6: typeText = tr("Downstream switch port"); break;
        case 7: typeText = tr("PCIe-to-PCI/PCI-X bridge"); break;
        case 8: typeText = tr("PCI/PCI-X-to-PCIe bridge"); break;
        case 9: typeText = tr("Root Complex Integrated Endpoint"); break;
        case 10: typeText = tr("Root Complex Event Collector"); break;
        default: typeText = tr("Unrecognized PCIe device type: %1").arg(type.value); break;
        }
    }
    auto *connection = group(tr("Connection"));
    field(connection, tr("Connection type"), typeText, snapshot_.pcieTypeSource);
    if (snapshot_.pcieTypeRequiresRoot) {
        field(connection, tr("Access"), tr("Run the application as root or with sudo to read this device's PCIe connection type."), snapshot_.pcieTypeSource);
    }
    // Both root-complex types lack conventional PCIe link registers. This
    // identifies connection topology, not GPU integration or performance.
    const bool internal = validType && (number == 9 || number == 10);
    if (internal) {
        field(connection, tr("PCIe link"), tr("Not applicable — integrated into the root complex; no external PCIe link."), snapshot_.pcieTypeSource);
    } else {
        const QString currentScope = tr("Current link reported for this device; an on-demand reading, not measured throughput or a fault verdict.");
        auto *current = group(tr("Current link"));
        entry(current, "current_link_speed", tr("Link speed"), true, currentScope);
        if (device().category == "display") {
            QString source = tr("Highest link speed and width observed together during graphics load; not a guaranteed maximum or the device's advertised capability.");
            if (!gpuTestRate_.isEmpty()) source += '\n' + gpuTestRate_;
            if (!gpuTestTime_.isEmpty()) source += '\n' + tr("Observed: %1").arg(gpuTestTime_);
            const Attribute speed = snapshot_.values.value("sysfs/current_link_speed");
            const Attribute width = snapshot_.values.value("sysfs/current_link_width");
            GpuLinkSample sample;
            const bool readableLink = speed.state == ReadState::Available
                && width.state == ReadState::Available
                && parseGpuLinkSample(speed.value.toLatin1(), width.value.toLatin1(), sample);
            const QString observation = readableLink
                ? (gpuTestText_.isEmpty() ? tr("Not tested") : gpuTestText_)
                : tr("Unavailable — no readable PCIe link speed and width for this device.");
            field(current, tr("Maximum observed link"), observation, source, readableLink);
            if (!gpuTestTime_.isEmpty()) copy.append(tr("Observed: %1").arg(gpuTestTime_));
        }
        entry(current, "current_link_width", tr("Link width"), false, currentScope);
        const QString capabilityScope = tr("Maximum capability reported for this device, not the slot or entire upstream path. Speed and width maxima are separate declarations.");
        auto *capability = group(tr("Device capability"));
        entry(capability, "max_link_speed", tr("Maximum link speed"), true, capabilityScope);
        entry(capability, "max_link_width", tr("Maximum link width"), false, capabilityScope);
        auto *scope = group(tr("Interpretation"));
        field(scope, tr("Upstream path"), tr("Not assessed; upstream links may impose additional limits."));
        field(scope, tr("Current vs. maximum"), tr("A lower current link speed or width is not automatically a fault. Power management and topology can explain differences."));
    }
    int captionWidth = 0;
    for (const QWidget *caption : captions) captionWidth = qMax(captionWidth, caption->sizeHint().width());
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    pcieGroups_->addStretch();
    pcieText_ = copy.join('\n');
}

void PropertiesDialog::testGpuLink()
{
    if (removed_ || gpuTest_ || !gpuLinkTestBuilt() || geteuid() == 0 || !gpuTestButton_) return;
    // Recheck the snapshot when the queued button action runs.
    const Attribute speed = snapshot_.values.value("sysfs/current_link_speed");
    const Attribute width = snapshot_.values.value("sysfs/current_link_width");
    GpuLinkSample sample;
    if (speed.state != ReadState::Available || width.state != ReadState::Available
        || !parseGpuLinkSample(speed.value.toLatin1(), width.value.toLatin1(), sample)) return;
    gpuTestTime_.clear();
    gpuTestRate_.clear();
    auto *process = startGpuLinkTest(device(), this);
    if (!process) {
        gpuTestText_ = tr("Another GPU link test is already running.");
        rebuildPcie();
        return;
    }
    gpuTest_ = process;
    gpuTestText_ = tr("Testing…");
    rebuildPcie();
    auto output = std::make_shared<QByteArray>();
    auto failed = std::make_shared<bool>(false);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output, failed] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 8192) {
            *failed = true;
            output->clear();
            gpuTestText_ = tr("The GPU test returned an invalid report.");
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] {
        process->readAllStandardError(); // Do not accumulate unbounded driver diagnostics.
    });
    connect(process, &QProcess::errorOccurred, this, [this, failed](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        *failed = true;
        gpuTestText_ = tr("Could not start the GPU link test.");
        gpuTest_.clear();
        rebuildPcie();
    });
    connect(process, &QProcess::finished, this, [this, process, output, failed](int exitCode, QProcess::ExitStatus status) {
        output->append(process->readAllStandardOutput());
        if (removed_) {
            gpuTestText_ = tr("GPU link test cancelled: device removed or replaced.");
        } else if (!*failed) {
            const auto report = QJsonDocument::fromJson(*output).object();
            const QString summary = report.value("summary").toString();
            const bool valid = output->size() <= 8192 && status == QProcess::NormalExit
                && (exitCode == 0 || exitCode == 1) && !summary.isEmpty() && summary.size() <= 2048
                && report.value("ok").isBool() && report.value("ok").toBool() == (exitCode == 0);
            if (valid && exitCode == 0) {
                const double speed = report.value("speed_gt_s").toDouble(-1);
                const double width = report.value("width").toDouble(-1);
                const QString stamp = report.value("observed_at").toString();
                if (std::isfinite(speed) && speed > 0 && speed <= 1024
                    && std::isfinite(width) && width >= 1 && width <= 64 && std::floor(width) == width
                    && stamp.size() <= 64 && QDateTime::fromString(stamp, Qt::ISODate).isValid()) {
                    gpuTestText_ = pcieLinkText(speed, static_cast<int>(width));
                    gpuTestRate_ = tr("Signalling rate: %1 GT/s per lane").arg(QLocale().toString(speed, 'g', 5));
                    gpuTestTime_ = stamp;
                } else gpuTestText_ = tr("The GPU test returned an invalid observation.");
            } else gpuTestText_ = valid ? summary : tr("GPU link test failed or did not return a valid observation.");
        }
        gpuTest_.clear();
        rebuildPcie();
    });
    // Includes startup and teardown. SIGKILL ends the child without waiting on
    // Vulkan cleanup; no rendering-library call is made in this process.
    QTimer::singleShot(25000, this, [this, process = QPointer<QProcess>(process), failed] {
        if (!process || process->state() == QProcess::NotRunning) return;
        *failed = true;
        gpuTestText_ = tr("GPU link test timed out; no result was retained.");
        rebuildPcie();
        process->kill();
    });
}

void PropertiesDialog::rebuildBattery()
{
    const BatteryProperties &data = snapshot_.battery;
    const bool applicable = device().subsystem == "power_supply" && data.applicable;
    const int existing = tabs_->indexOf(batteryPage_);
    if (applicable && existing < 0) tabs_->insertTab(2, batteryPage_, tr("Battery"));
    else if (!applicable && existing >= 0) {
        tabs_->removeTab(existing);
        batteryPage_->hide();
    }
    while (QLayoutItem *item = batteryGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    batteryText_.clear();
    if (!applicable) return;
    QStringList copy;
    PropertyReport fieldsReport(batteryGroups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto source = [&](const QString &key) { return device().path + '/' + key; };
    const auto readValue = [&](const Attribute &value) {
        return value.state == ReadState::Unsupported || value.error == EOPNOTSUPP || value.error == ENOSYS
            ? tr("Query unsupported by the kernel/device") : propertyReadValue(value);
    };
    const auto textValue = [&](const QString &key) {
        const Attribute value = data.values.value(key);
        if (value.state != ReadState::Available) return readValue(value);
        if (value.value.isEmpty()) return tr("Not reported");
        if (value.value == "Unknown") return tr("Unknown (reported)");
        if (key == "status") {
            if (value.value == "Charging") return tr("Charging");
            if (value.value == "Discharging") return tr("Discharging");
            if (value.value == "Not charging") return tr("Not charging");
            if (value.value == "Full") return tr("Full");
        }
        return value.value;
    };
    // Sysfs numeric battery properties are decimal integers, in micro-units.
    // Preserve signed current/power readings, and never derive one from the other.
    const auto numberValue = [&](const QString &key, double divisor, const QString &unit, bool signedReading = false) {
        const Attribute value = data.values.value(key);
        if (value.state != ReadState::Available) return readValue(value);
        bool valid = false;
        const qlonglong number = value.value.toLongLong(&valid);
        if (!valid || (!signedReading && number < 0) || (key == "capacity" && number > 100))
            return tr("Malformed reported value: %1").arg(value.value.isEmpty() ? tr("(empty)") : value.value);
        if (!number && (key == "cycle_count" || key.endsWith("_full") || key.endsWith("_full_design")))
            return tr("Unavailable (reported value: 0)");
        const QString formatted = divisor == 1 ? QLocale().toString(number)
            : QLocale().toString(static_cast<double>(number) / divisor, 'f', 3);
        return unit.isEmpty() ? formatted : formatted + ' ' + unit;
    };
    auto *current = group(tr("Current state"));
    if (data.state.state != ReadState::Available) {
        QString state = readValue(data.state);
        if (data.state.error == EAGAIN) state = tr("Battery type or presence changed during collection; reload properties.");
        else if (data.state.error == EINVAL && data.values.contains("present"))
            state = tr("Malformed battery presence value: %1").arg(data.values.value("present").value);
        field(current, tr("Collection status"), state, source("type") + '\n' + source("present"));
        if (data.values.contains("type")) field(current, tr("Supply type"), textValue("type"), source("type"));
    } else {
        const Attribute present = data.values.value("present");
        const QString presence = present.state == ReadState::Available
            ? (present.value == "1" ? tr("Present") : tr("Absent")) : readValue(present);
        field(current, tr("Battery presence"), presence, source("present"));
        field(current, tr("Status"), textValue("status"), source("status"));
        field(current, tr("Charge percentage"), numberValue("capacity", 1, "%"), source("capacity"));
        field(current, tr("Voltage"), numberValue("voltage_now", 1000000, "V"), source("voltage_now"));
        field(current, tr("Current"), numberValue("current_now", 1000000, "A", true), source("current_now")
            + '\n' + tr("Driver-reported instantaneous current; the reported sign is preserved. Charging state is reported separately."));
        field(current, tr("Power"), numberValue("power_now", 1000000, "W", true), source("power_now")
            + '\n' + tr("Driver-reported instantaneous power; not calculated from voltage and current."));
        const auto retention = [&](const QString &prefix) {
            const Attribute full = data.values.value(prefix + "_full");
            const Attribute design = data.values.value(prefix + "_full_design");
            if (full.state == ReadState::NotApplicable || design.state == ReadState::NotApplicable)
                return readValue({ReadState::NotApplicable, {}, 0});
            if (full.state != ReadState::Available || design.state != ReadState::Available)
                return tr("Unavailable — requires compatible full and design readings");
            bool fullValid = false, designValid = false;
            const qlonglong fullValue = full.value.toLongLong(&fullValid);
            const qlonglong designValue = design.value.toLongLong(&designValid);
            if (!fullValid || !designValid || fullValue <= 0 || designValue <= 0)
                return tr("Unavailable — requires positive full and design readings");
            const double percent = 100.0 * static_cast<double>(fullValue) / static_cast<double>(designValue);
            return tr("%1 % (estimate)").arg(QLocale().toString(percent, 'f', 1));
        };
        const auto hasCapacity = [&](const QString &prefix) {
            for (const QString &suffix : {QStringLiteral("_full_design"), QStringLiteral("_full"), QStringLiteral("_now")}) {
                const Attribute value = data.values.value(prefix + suffix);
                if (value.state != ReadState::Unavailable || value.error != ENOENT) return true;
            }
            return false;
        };
        const bool energy = hasCapacity("energy"), charge = hasCapacity("charge");
        const auto capacityGroup = [&](const QString &prefix, const QString &title, double divisor, const QString &unit) {
            auto *form = group(title);
            field(form, tr("Design capacity"), numberValue(prefix + "_full_design", divisor, unit), source(prefix + "_full_design"));
            field(form, tr("Last full-charge capacity"), numberValue(prefix + "_full", divisor, unit), source(prefix + "_full")
                + '\n' + tr("Firmware-reported learned full capacity; it can depend on temperature, age and calibration."));
            field(form, tr("Remaining capacity"), numberValue(prefix + "_now", divisor, unit), source(prefix + "_now"));
            field(form, tr("Estimated capacity retention"), retention(prefix), source(prefix + "_full") + '\n'
                + source(prefix + "_full_design") + '\n' + tr("100 × full / design using matching units; not a complete battery health verdict. Values above 100% are not clamped."));
        };
        if (energy) capacityGroup("energy", tr("Energy capacity"), 1000000, "Wh");
        if (charge) capacityGroup("charge", tr("Charge capacity"), 1000, "mAh");
        if (!energy && !charge) {
            auto *form = group(tr("Capacity"));
            field(form, tr("Reported capacities"), tr("Unavailable — no energy or charge capacity attributes exposed"), device().path);
            field(form, tr("Estimated capacity retention"), tr("Unavailable — requires compatible full and design readings"), device().path);
        }
        auto *identity = group(tr("Battery information"));
        field(identity, tr("Cycle count"), numberValue("cycle_count", 1, QString()), source("cycle_count")
            + '\n' + tr("Reported full charge/discharge cycles. The kernel ABI defines zero as unavailable."));
        field(identity, tr("Chemistry"), textValue("technology"), source("technology"));
        field(identity, tr("Manufacturer"), textValue("manufacturer"), source("manufacturer"));
        field(identity, tr("Model"), textValue("model_name"), source("model_name"));
        field(identity, tr("Serial number"), textValue("serial_number"), source("serial_number"));
    }
    fieldsReport.alignCaptions();
    batteryGroups_->addStretch();
    batteryText_ = copy.join('\n');
}

void PropertiesDialog::rebuildStorage()
{
    for (const auto &page : {qMakePair(storagePage_, tr("Storage")),
                             qMakePair(volumesPage_, tr("Volumes")), qMakePair(healthPage_, tr("Health"))}) {
        const int existing = tabs_->indexOf(page.first);
        if (snapshot_.storage.applicable && existing < 0) tabs_->addTab(page.first, page.second);
        else if (!snapshot_.storage.applicable && existing >= 0) {
            tabs_->removeTab(existing);
            page.first->hide();
        }
    }
    const QString previousVolume = volumesTable_->currentRow() >= 0
        ? volumesTable_->item(volumesTable_->currentRow(), 0)->data(Qt::UserRole).toString() : QString();
    const QString previousDrive = healthDrive_->currentData().toString();
    storagePresentation_ = storagePresentation(snapshot_.storage, device().path);
    const QString scope = tr("Read-only metadata snapshot. Each entity is a separate layer; capacities are not added. "
        "Kernel and UDisks2 block records are merged only by their exact device-number mapping. "
        "Source paths remain in tooltips and copied text. Deep search includes native metadata only.");
    const auto hint = [](QLabel *label, const QString &text) {
        label->setToolTip(propertyToolTip(text));
    };
    hint(storageNotice_, scope + "\n\n" + storagePresentation_.notes);
    hint(volumesNotice_, scope + '\n' + tr("Related volumes may span several drives. Mount points cover only the application "
        "mount namespace. No filesystem is probed, mounted or unlocked.") + "\n\n" + storagePresentation_.notes);
    hint(healthNotice_, tr("Cached UDisks2 health only, separate from device operational status. "
        "Reload does not request SMART updates or self-tests. Missing evidence is unavailable; "
        "no reported warning does not guarantee health. Related drives are shown separately.")
        + "\n\n" + storagePresentation_.notes);
    showStorageFields(storage_, storagePresentation_.overview);
    {
        const QSignalBlocker blocker(volumesTable_);
        volumesTable_->setRowCount(0);
        int selected = -1;
        for (const StorageDisplayRow &entry : storagePresentation_.volumes) {
            const int row = volumesTable_->rowCount();
            volumesTable_->insertRow(row);
            const QStringList columns {entry.name, entry.kind, entry.capacity, entry.filesystem, entry.mounts};
            for (int column = 0; column < columns.size(); ++column) {
                auto *item = new QTableWidgetItem(columns[column]);
                item->setToolTip(entry.source.toHtmlEscaped());
                volumesTable_->setItem(row, column, item);
            }
            volumesTable_->item(row, 0)->setData(Qt::UserRole, entry.id);
            if (entry.id == previousVolume || (previousVolume.isEmpty() && entry.id == device().path)) selected = row;
        }
        if (selected < 0 && volumesTable_->rowCount()) selected = 0;
        volumesTable_->setCurrentCell(selected, 0);
    }
    volumeDetailsButton_->setEnabled(volumesTable_->currentRow() >= 0);
    {
        const QSignalBlocker blocker(healthDrive_);
        healthDrive_->clear();
        for (const StorageDisplayRow &entry : storagePresentation_.health) {
            healthDrive_->addItem(entry.name, entry.id);
            healthDrive_->setItemData(healthDrive_->count() - 1, entry.source.toHtmlEscaped(), Qt::ToolTipRole);
        }
        int selected = healthDrive_->findData(previousDrive);
        if (selected < 0 && !storagePresentation_.preferredHealthDrive.isEmpty())
            selected = healthDrive_->findData(storagePresentation_.preferredHealthDrive);
        healthDrive_->setCurrentIndex(selected < 0 ? 0 : selected);
    }
    showHealth();
}
void PropertiesDialog::showVolumeDetails()
{
    const int row = volumesTable_->currentRow();
    if (row < 0 || row >= storagePresentation_.volumes.size()) return;
    // Own the selected snapshot while the modal dialog runs its event loop.
    const StorageDisplayRow entry = storagePresentation_.volumes[row];
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
void PropertiesDialog::showHealth()
{
    while (QLayoutItem *item = healthGroups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const int row = healthDrive_->currentIndex();
    if (row < 0 || row >= storagePresentation_.health.size()) {
        healthGroups_->addWidget(plainLabel(tr("Cached health unavailable."), healthGroups_->parentWidget()));
        healthGroups_->addStretch();
        return;
    }
    const StorageDisplayRow &entry = storagePresentation_.health[row];
    QVector<QGridLayout *> forms;
    QVector<QLabel *> captions;
    const auto group = [&](const QString &title, const QVector<StorageDisplayField> &fields) {
        auto *box = new QGroupBox(title, healthGroups_->parentWidget());
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        forms.append(form);
        healthGroups_->addWidget(box);
        int fieldRow = 0;
        for (const StorageDisplayField &field : fields) {
            auto *caption = new QLabel(field.label, box);
            caption->setTextFormat(Qt::PlainText);
            captions.append(caption);
            auto *value = plainLabel(field.value, box);
            QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            policy.setHeightForWidth(true);
            value->setSizePolicy(policy);
            value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
            value->setMinimumWidth(120);
            const QString hint = propertyToolTip(field.tooltip.isEmpty() ? field.source : field.tooltip + '\n' + field.source);
            caption->setToolTip(hint);
            value->setToolTip(hint);
            form->addWidget(caption, fieldRow, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, fieldRow++, 1);
        }
    };
    if (!entry.fields.isEmpty()) group(tr("Health"), entry.fields);
    for (const StorageDisplayGroup &section : entry.healthGroups) group(section.title, section.fields);
    int captionWidth = 0;
    for (const QLabel *caption : captions) captionWidth = qMax(captionWidth, caption->sizeHint().width());
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    if (!entry.ataAttributes.isEmpty()) {
        auto *box = new QGroupBox(tr("ATA SMART attributes"), healthGroups_->parentWidget());
        auto *layout = new QVBoxLayout(box);
        auto *table = new ReportTable(static_cast<int>(entry.ataAttributes.size()), 7, box);
        table->setHorizontalHeaderLabels({tr("ID"), tr("Name"), tr("Current"), tr("Worst"), tr("Threshold"), tr("Interpreted value"), tr("Flags")});
        const QStringList hints {
            tr("Attribute ID; the drive-specific name is the authoritative identifier."),
            tr("UDisks2's drive-specific attribute identifier."),
            tr("Current normalized value; not necessarily a percentage."),
            tr("Worst normalized value reported by the drive."),
            tr("Drive-reported normalized threshold."),
            tr("UDisks2 interpreted value or smartctl raw counter, as labeled. See the value tooltip for its source and units."),
            tr("Raw attribute flags: bit 0 indicates pre-failure/old-age; bit 1 indicates online/offline collection.")
        };
        for (int column = 0; column < table->columnCount(); ++column) {
            table->horizontalHeaderItem(column)->setToolTip(hints[column]);
        }
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        table->setSelectionMode(QAbstractItemView::ExtendedSelection);
        for (int index = 0; index < entry.ataAttributes.size(); ++index) {
            const StorageDisplayAtaAttribute &attribute = entry.ataAttributes[index];
            const QStringList values {attribute.id, attribute.name, attribute.current, attribute.worst,
                                      attribute.threshold, attribute.interpreted, attribute.flags};
            for (int column = 0; column < values.size(); ++column) {
                auto *item = new QTableWidgetItem(values[column]);
                item->setToolTip(propertyToolTip((column == 5 && !attribute.interpretedTooltip.isEmpty()
                    ? attribute.interpretedTooltip + '\n' : QString()) + hints[column] + '\n' + attribute.source));
                if (column == 0 || (column >= 2 && column <= 4) || column == 6)
                    item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
                table->setItem(index, column, item);
                if (column == 5 && !attribute.counterNotice.isEmpty()) {
                    // Paint the prefix and value together: cell widgets would overlap
                    // the underlying item text and interfere with row sizing.
                    item->setText(QStringLiteral("ℹ️ ") + attribute.interpreted);
                    item->setData(Qt::UserRole, attribute.counterNotice);
                }
            }
        }
        connect(table, &QTableWidget::itemClicked, this, [this](QTableWidgetItem *item) {
            const QString notice = item->data(Qt::UserRole).toString();
            if (!notice.isEmpty()) QMessageBox::information(this, tr("SMART counter information"), notice);
        });
        layout->addWidget(table);
        table->fitContents();
        healthGroups_->addWidget(box);
    }
    healthGroups_->addStretch();
}

void PropertiesDialog::updateEvents(const DeviceEventsSnapshot &history, const QString &monitorNote)
{
    Q_ASSERT(QThread::isMainThread());
    // A late removal observation may arrive after a property read detected removal.
    // Accept only the old token, retaining its rows even after global eviction.
    eventScope_ = tr("Live udev events since %1. Receipt times; exact device instance only. "
        "Latest %3 events per device; %2 retained globally. Historical logs are not queried.")
        .arg(history.started.isValid() ? history.started.toString(Qt::ISODate) : tr("monitor startup"))
        .arg(DeviceEventsSnapshot::Limit).arg(DeviceEventsSnapshot::PerDeviceLimit);
    if (history.evicted) eventScope_ += '\n' + tr("%1 older events evicted from the global history.").arg(history.evicted);
    if (history.unassociated) eventScope_ += '\n' + tr("%1 events could not be associated safely with an inventory instance.").arg(history.unassociated);
    if (history.gaps) eventScope_ += '\n' + tr("%1 event-identity resets after monitor failures/losses; coverage is incomplete.").arg(history.gaps);
    if (!history.monitoring) eventScope_ += '\n' + tr("Live event monitoring is unavailable. Viewing and retained events remain available.");
    if (!monitorNote.isEmpty()) eventScope_ += '\n' + monitorNote;
    if (!device().eventInstance) eventScope_ += '\n' + tr("No safe live-event association is available for this instance.");
    if (device().subsystem == "efivarfs") eventScope_ += '\n' + tr("EFI variables are reconciled by inventory scans; libudev does not supply their value-change history.");
    QVector<DeviceEvent> next;
    for (auto it = history.records.crbegin(); it != history.records.crend(); ++it) {
        if (!device().eventInstance || it->instance != device().eventInstance || it->path != device().path) continue;
        next.append(*it);
        if (next.size() == DeviceEventsSnapshot::PerDeviceLimit) break;
    }
    if (removed_) {
        for (const DeviceEvent &old : displayedEvents_) {
            bool retained = false;
            for (const DeviceEvent &event : next)
                if (event.sequence == old.sequence && event.elapsedMs == old.elapsedMs
                    && event.observed == old.observed && event.action == old.action) { retained = true; break; }
            if (!retained && next.size() < DeviceEventsSnapshot::PerDeviceLimit) next.append(old);
        }
        eventScope_ += '\n' + tr("Removed/replaced device: retained events are a read-only instance snapshot.");
    }
    if (next.isEmpty()) eventScope_ += '\n' + tr("No retained live events for this instance. This does not establish that the device has had no errors.");
    eventsNotice_->setToolTip(propertyToolTip(eventScope_));
    bool unchanged = next.size() == displayedEvents_.size();
    for (int i = 0; unchanged && i < next.size(); ++i) {
        const DeviceEvent &a = next.at(i);
        const DeviceEvent &b = displayedEvents_.at(i);
        unchanged = a.sequence == b.sequence && a.elapsedMs == b.elapsedMs && a.observed == b.observed
            && a.action == b.action && a.path == b.path && a.instance == b.instance;
    }
    if (unchanged) return; // Unrelated events must not reset selection/scroll.
    const int oldRow = eventsTable_->currentRow();
    DeviceEvent selected;
    const bool hadSelection = oldRow >= 0 && oldRow < displayedEvents_.size();
    if (hadSelection) selected = displayedEvents_.at(oldRow);
    displayedEvents_ = std::move(next);
    const QSignalBlocker blocker(eventsTable_);
    eventsTable_->setRowCount(0);
    eventsTable_->setRowCount(displayedEvents_.size());
    int restore = -1;
    for (int row = 0; row < displayedEvents_.size(); ++row) {
        const DeviceEvent &event = displayedEvents_.at(row);
        const QStringList values {event.observed.toString(Qt::ISODateWithMs), event.action, eventDescription(event)};
        for (int column = 0; column < values.size(); ++column)
            eventsTable_->setItem(row, column, new QTableWidgetItem(values[column]));
        if (hadSelection && event.sequence == selected.sequence && event.elapsedMs == selected.elapsedMs
            && event.action == selected.action) restore = row;
    }
    if (restore < 0 && !displayedEvents_.isEmpty()) restore = 0;
    if (restore >= 0) eventsTable_->setCurrentCell(restore, 0);
    showEvent();
}
void PropertiesDialog::showEvent()
{
    const int row = eventsTable_->currentRow();
    eventDetails_->setPlainText(row >= 0 && row < displayedEvents_.size() ? eventText(displayedEvents_.at(row)) : QString());
}
void PropertiesDialog::copyEvents(bool selectedOnly)
{
    QStringList text {eventScope_};
    if (selectedOnly) {
        QList<int> rows;
        for (const QModelIndex &index : eventsTable_->selectionModel()->selectedRows()) rows.append(index.row());
        std::sort(rows.begin(), rows.end());
        for (const int row : rows) text.append(eventText(displayedEvents_.at(row)));
    } else {
        for (const DeviceEvent &event : displayedEvents_) text.append(eventText(event));
    }
    QApplication::clipboard()->setText(text.join("\n\n"));
}
