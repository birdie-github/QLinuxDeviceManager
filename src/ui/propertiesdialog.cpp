#include "propertiesdialog.h"
#include "bluetoothpage.h"
#include "networkpage.h"
#include "camerapage.h"
#include "displaypage.h"
#include "usbpage.h"
#include "batterypage.h"
#include "pciepage.h"
#include "healthpage.h"
#include "volumespage.h"
#include "storagepage.h"
#include "eventspage.h"
#include "resourcespage.h"
#include "storagepresentation.h"
#include "propertypage.h"
#include "reporttable.h"
#include "propertyreport.h"
#include <QDateTime>
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
#include <QGridLayout>
#include <QGroupBox>
#include <QSizePolicy>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLocale>
#include <QPlainTextEdit>
#include <QStyle>
#include <QPushButton>
#include <QMessageBox>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QTableWidget>
#include <algorithm>
#include <QVBoxLayout>
#include <utility>

namespace {
void clearForm(QFormLayout *form)
{
    while (form->rowCount()) form->removeRow(0);
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
    networkPage_ = new NetworkPage(tabs);
    bluetoothPage_ = new BluetoothPage(tabs);
    cameraPage_ = new CameraPage(tabs);
    displayPage_ = new DisplayPage(tabs);
    usbPage_ = new UsbPage(tabs);
    batteryPage_ = new BatteryPage(tabs);
    pciePage_ = new PciePage(tabs);
    storagePage_ = new StoragePage(tabs);

    volumesPage_ = new VolumesPage(tabs);

    healthPage_ = new HealthPage(tabs);
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
    resourcesPage_ = new ResourcesPage(tabs);
    eventsPage_ = new EventsPage(tabs);
    tabs->addTab(eventsPage_, tr("Events"));
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
    pciePage_->markRemoved();
    eventsPage_->markRemoved();
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
    resourcesPage_->setSnapshot(data);
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
    if (data.applicable) cameraPage_->setSnapshot(data, device().path);
    else cameraPage_->clear();
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
    if (applicable) displayPage_->setSnapshot(snapshot_.display, snapshot_.monitor, device().path);
    else displayPage_->clear();
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
    if (applicable) usbPage_->setSnapshot(snapshot_.usb, snapshot_.values.value("sysfs/version"),
                                        snapshot_.values.value("sysfs/speed"), device().path);
    else usbPage_->clear();
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
    PciePageSnapshot page;
    page.device = device();
    page.pcieApplicable = applicable;
    page.pcieType = snapshot_.pcieType;
    page.pcieTypeSource = snapshot_.pcieTypeSource;
    page.pcieTypeRequiresRoot = snapshot_.pcieTypeRequiresRoot;
    for (const QString &key : {QStringLiteral("sysfs/current_link_speed"), QStringLiteral("sysfs/current_link_width"),
                              QStringLiteral("sysfs/max_link_speed"), QStringLiteral("sysfs/max_link_width")}) {
        page.values.insert(key, snapshot_.values.value(key));
        page.sources.insert(key, snapshot_.sources.value(key));
    }
    pciePage_->setSnapshot(page);
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
    if (applicable) batteryPage_->setSnapshot(data, device().path);
    else batteryPage_->clear();
}

void PropertiesDialog::rebuildStorage()
{
    for (const auto &page : {qMakePair(static_cast<QWidget *>(storagePage_), tr("Storage")),
                             qMakePair(static_cast<QWidget *>(volumesPage_), tr("Volumes")), qMakePair(static_cast<QWidget *>(healthPage_), tr("Health"))}) {
        const int existing = tabs_->indexOf(page.first);
        if (snapshot_.storage.applicable && existing < 0) tabs_->addTab(page.first, page.second);
        else if (!snapshot_.storage.applicable && existing >= 0) {
            tabs_->removeTab(existing);
            page.first->hide();
        }
    }
    const StoragePresentation presentation = storagePresentation(snapshot_.storage, device().path);
    const QString scope = tr("Read-only metadata snapshot. Each entity is a separate layer; capacities are not added. "
        "Kernel and UDisks2 block records are merged only by their exact device-number mapping. "
        "Source paths remain in tooltips and copied text. Deep search includes native metadata only.");
    storagePage_->setSnapshot(presentation.overview, scope, presentation.notes);
    volumesPage_->setSnapshot(presentation.volumes, device().path, scope, presentation.notes);
    healthPage_->setSnapshot(presentation.health, presentation.preferredHealthDrive,
                             presentation.notes);
}

void PropertiesDialog::updateEvents(const DeviceEventsSnapshot &history, const QString &monitorNote)
{
    eventsPage_->updateEvents(device(), history, monitorNote);
}
