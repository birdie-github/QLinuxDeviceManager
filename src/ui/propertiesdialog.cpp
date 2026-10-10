#include "resourceformat.h"
#include "propertiesdialog.h"
#include "devicelabel.h"
#include <QApplication>
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
#include <QItemSelectionModel>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <algorithm>
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
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setFocusPolicy(Qt::StrongFocus);
    return label;
}
QFormLayout *storageDetailsForm(QVBoxLayout *layout, QWidget *parent)
{
    auto *scroll = new QScrollArea(parent);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::StyledPanel);
    auto *page = new QWidget(scroll);
    auto *form = new QFormLayout(page);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    form->setFormAlignment(Qt::AlignTop);
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    return form;
}
void showStorageFields(QFormLayout *form, const QVector<StorageDisplayField> &fields)
{
    clearForm(form);
    for (const StorageDisplayField &field : fields) {
        auto *caption = plainLabel(field.label + ':', form->parentWidget());
        auto *value = plainLabel(field.value, form->parentWidget());
        const QString hint = "<qt>" + field.source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
        caption->setToolTip(hint);
        value->setToolTip(hint);
        form->addRow(caption, value);
    }
}
QString selectedStorageText(const QVector<StorageDisplayRow> &rows, int index)
{
    if (index < 0 || index >= rows.size()) return {};
    const StorageDisplayRow &row = rows[index];
    return row.name + '\n' + storageFieldsText(row.fields);
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
    auto *volumesLayout = new QVBoxLayout(volumesPage_);
    volumesNotice_ = plainLabel(tr("ℹ️ Related volumes and mounts"), volumesPage_);
    volumesLayout->addWidget(volumesNotice_);
    volumesTable_ = new QTableWidget(0, 5, volumesPage_);
    volumesTable_->setAlternatingRowColors(true);
    volumesTable_->setHorizontalHeaderLabels({tr("Device / label"), tr("Layer"), tr("Capacity"), tr("Content"), tr("Mount points")});
    volumesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    volumesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    volumesTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    volumesTable_->verticalHeader()->hide();
    volumesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    volumesTable_->horizontalHeader()->setStretchLastSection(true);
    volumesTable_->setColumnWidth(0, 180);
    volumesTable_->setColumnWidth(1, 110);
    volumesTable_->setColumnWidth(2, 180);
    volumesLayout->addWidget(volumesTable_, 2);
    volumeDetails_ = storageDetailsForm(volumesLayout, volumesPage_);
    auto *volumeButtons = new QHBoxLayout;
    auto *copyVolume = new QPushButton(tr("Copy selected volume"), volumesPage_);
    auto *copyVolumes = new QPushButton(tr("Copy all volumes"), volumesPage_);
    volumeButtons->addWidget(copyVolume);
    volumeButtons->addWidget(copyVolumes);
    volumeButtons->addStretch();
    volumesLayout->addLayout(volumeButtons);
    connect(volumesTable_, &QTableWidget::currentCellChanged, this, [this] { showVolume(); });
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
    healthDetails_ = storageDetailsForm(healthLayout, healthPage_);
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
    auto *resourcesLayout = new QVBoxLayout(resourcesPage_);
    resourcesTable_ = new QTableWidget(0, 3, resourcesPage_);
    resourcesTable_->setHorizontalHeaderLabels({tr("Resource type"), tr("Setting"), tr("Details")});
    resourcesTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    resourcesTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    resourcesTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    resourcesTable_->verticalHeader()->hide();
    resourcesTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    resourcesTable_->horizontalHeader()->setStretchLastSection(true);
    resourcesLayout->addWidget(resourcesTable_);
    auto *resourceButtons = new QHBoxLayout;
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
    resourcesLayout->addLayout(resourceButtons);
    // The tab appears only after collection finds resource records or read errors.
    auto *eventsPage = new QWidget(tabs);
    auto *eventsLayout = new QVBoxLayout(eventsPage);
    eventsNotice_ = plainLabel(tr("ℹ️ Live udev events since the application started"), eventsPage);
    eventsLayout->addWidget(eventsNotice_);
    eventsTable_ = new QTableWidget(0, 3, eventsPage);
    eventsTable_->setHorizontalHeaderLabels({tr("Observed time"), tr("Event type"), tr("Description")});
    eventsTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    eventsTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    eventsTable_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    eventsTable_->verticalHeader()->hide();
    eventsTable_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    eventsTable_->horizontalHeader()->setStretchLastSection(true);
    eventsLayout->addWidget(eventsTable_, 2);
    eventDetails_ = new QPlainTextEdit(eventsPage);
    eventDetails_->setReadOnly(true);
    eventsLayout->addWidget(eventDetails_, 1);
    auto *eventButtons = new QHBoxLayout;
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
    eventsLayout->addLayout(eventButtons);
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
    eventScope_ += '\n' + tr("Removed/replaced device: retained events are a read-only instance snapshot.");
    eventsNotice_->setToolTip("<qt>" + eventScope_.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    reload_->setEnabled(false);
    banner_->show();
    banner_->setText(tr("Device removed or replaced. This dialog is a read-only snapshot; close it and reopen Properties for the current device."));
    rebuild();
}
void PropertiesDialog::rebuild()
{
    rebuildResources();
    entries_ = propertyEntries(snapshot_, removed_, resourcesText_);
    clearForm(general_);
    clearForm(driver_);
    rebuildStorage();
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
    for (const PropertyEntry &entry : entries_)
        if (isDetailsProperty(entry, device()) && (!entry.advanced || advanced_->isChecked()))
            property_->addItem(entry.label, entry.id);
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
        label->setToolTip("<qt>" + text.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
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
    showVolume();
    {
        const QSignalBlocker blocker(healthDrive_);
        healthDrive_->clear();
        for (const StorageDisplayRow &entry : storagePresentation_.health) {
            healthDrive_->addItem(entry.name, entry.id);
            healthDrive_->setItemData(healthDrive_->count() - 1, entry.source.toHtmlEscaped(), Qt::ToolTipRole);
        }
        const int selected = healthDrive_->findData(previousDrive);
        healthDrive_->setCurrentIndex(selected < 0 ? 0 : selected);
    }
    showHealth();
}
void PropertiesDialog::showVolume()
{
    const int row = volumesTable_->currentRow();
    if (row < 0 || row >= storagePresentation_.volumes.size()) {
        clearForm(volumeDetails_);
        volumeDetails_->addRow(plainLabel(tr("No related volume metadata available."), volumeDetails_->parentWidget()));
        return;
    }
    const StorageDisplayRow &entry = storagePresentation_.volumes[row];
    showStorageFields(volumeDetails_, entry.fields);
}
void PropertiesDialog::showHealth()
{
    const int row = healthDrive_->currentIndex();
    if (row < 0 || row >= storagePresentation_.health.size()) {
        clearForm(healthDetails_);
        healthDetails_->addRow(plainLabel(tr("Cached health unavailable."), healthDetails_->parentWidget()));
        return;
    }
    const StorageDisplayRow &entry = storagePresentation_.health[row];
    showStorageFields(healthDetails_, entry.fields);
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
    eventsNotice_->setToolTip("<qt>" + eventScope_.toHtmlEscaped().replace('\n', "<br>") + "</qt>");
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
