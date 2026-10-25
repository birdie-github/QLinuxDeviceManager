#include "propertiesdialog.h"
#include "storagepresentation.h"
#include "propertytext.h"
#include "propertyreport.h"
#include "devicelabel.h"
#include "devicepages/all.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QPair>
#include <QPushButton>
#include <QTabWidget>
#include <QThread>
#include <QVBoxLayout>
#include <utility>

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
    generalPage_ = new PropertyFieldsPage(PropertyFieldsPage::Section::General, tabs);
    tabs->addTab(generalPage_, tr("General"));
    driverPage_ = new PropertyFieldsPage(PropertyFieldsPage::Section::Driver, tabs);
    tabs->addTab(driverPage_, tr("Driver"));
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
    detailsPage_ = new DetailsPage(tabs);
    tabs->addTab(detailsPage_, tr("Details"));
    resourcesPage_ = new ResourcesPage(tabs);
    eventsPage_ = new EventsPage(tabs);
    tabs->addTab(eventsPage_, tr("Events"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    reload_ = buttons->addButton(tr("Reload properties"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reload_, &QPushButton::clicked, this, &PropertiesDialog::reloadRequested);
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
    const auto entries = propertyEntries(snapshot_, removed_);
    rebuildStorage();
    rebuildNetwork();
    rebuildBluetooth();
    rebuildDisplay();
    rebuildCamera();
    rebuildUsb();
    rebuildPcie();
    rebuildBattery();
    generalPage_->setSnapshot(entries);
    driverPage_->setSnapshot(entries);
    detailsPage_->setSnapshot(entries, device());
}
// Existing tabs keep their position and selection. A negative index appends.
void PropertiesDialog::setPageVisible(QWidget *page, bool visible, const QString &title, int index)
{
    const int existing = tabs_->indexOf(page);
    if (visible && existing < 0) {
        if (index < 0) tabs_->addTab(page, title);
        else tabs_->insertTab(index, page, title);
    } else if (!visible && existing >= 0) {
        tabs_->removeTab(existing);
        page->hide();
    }
}

void PropertiesDialog::rebuildResources()
{
    const DeviceResources &data = snapshot_.resources;
    setPageVisible(resourcesPage_, data.hasInformation(), tr("Resources"));
    resourcesPage_->setSnapshot(data);
}

void PropertiesDialog::rebuildBluetooth()
{
    const bool applicable = device().subsystem == "bluetooth" && bluetoothControllerName(device().sysname);
    setPageVisible(bluetoothPage_, applicable, tr("Bluetooth"), 2);
    if (applicable) bluetoothPage_->setSnapshot(snapshot_.bluetooth, device().path);
    else bluetoothPage_->clear();
}

void PropertiesDialog::rebuildNetwork()
{
    const bool applicable = device().subsystem == "net" || device().category == "network";
    setPageVisible(networkPage_, applicable, tr("Network"), 2);
    if (applicable) networkPage_->setSnapshot(snapshot_.network, snapshot_.networkState, device().path);
    else networkPage_->clear();
}

void PropertiesDialog::rebuildCamera()
{
    const CameraProperties &data = snapshot_.camera;
    setPageVisible(cameraPage_, data.applicable, tr("Camera"), 2);
    if (data.applicable) cameraPage_->setSnapshot(data, device().path);
    else cameraPage_->clear();
}

void PropertiesDialog::rebuildDisplay()
{
    const bool applicable = device().subsystem == "drm" && device().sysname.contains('-');
    setPageVisible(displayPage_, applicable, tr("Display"), 2);
    if (applicable) displayPage_->setSnapshot(snapshot_.display, snapshot_.monitor, device().path);
    else displayPage_->clear();
}

void PropertiesDialog::rebuildUsb()
{
    const bool applicable = device().subsystem == "usb" && device().devtype == "usb_device";
    setPageVisible(usbPage_, applicable, tr("USB"), 2);
    if (applicable) usbPage_->setSnapshot(snapshot_.usb, snapshot_.values.value("sysfs/version"),
                                        snapshot_.values.value("sysfs/speed"), device().path);
    else usbPage_->clear();
}

void PropertiesDialog::rebuildPcie()
{
    const bool applicable = device().subsystem == "pci" && snapshot_.pcieApplicable;
    setPageVisible(pciePage_, applicable, tr("PCIe"), 2);
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
    setPageVisible(batteryPage_, applicable, tr("Battery"), 2);
    if (applicable) batteryPage_->setSnapshot(data, device().path);
    else batteryPage_->clear();
}

void PropertiesDialog::rebuildStorage()
{
    for (const auto &page : {qMakePair(static_cast<QWidget *>(storagePage_), tr("Storage")),
                             qMakePair(static_cast<QWidget *>(volumesPage_), tr("Volumes")), qMakePair(static_cast<QWidget *>(healthPage_), tr("Health"))}) {
        setPageVisible(page.first, snapshot_.storage.applicable, page.second);
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
