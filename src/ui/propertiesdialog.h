#pragma once

#include "deviceproperties.h"
#include "propertytext.h"
#include <QDialog>
#include <QVector>

class DetailsPage;
class ResourcesPage;
class EventsPage;
class StoragePage;
class VolumesPage;
class HealthPage;
class PciePage;
class BatteryPage;
class UsbPage;
class DisplayPage;
class CameraPage;
class BluetoothPage;
class NetworkPage;
class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;
class QVBoxLayout;

class PropertiesDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(const Device &device, QWidget *parent = nullptr);
    const Device &device() const { return snapshot_.device; }
    bool isRemoved() const { return removed_; }
    void acceptResult(DeviceProperties result);
    void reconcileInstance(const Device *current); // Borrowed only for this call.
    void setBusy(bool busy);
    void markRemoved();
    void updateEvents(const DeviceEventsSnapshot &history, const QString &monitorNote);
signals:
    void reloadRequested();
private:
    void rebuild();
    void rebuildResources();
    void rebuildStorage();
    void rebuildNetwork();
    void rebuildBluetooth();
    void rebuildDisplay();
    void rebuildCamera();
    void rebuildUsb();
    void rebuildPcie();
    void rebuildBattery();
    DeviceProperties snapshot_;
    QVector<PropertyEntry> entries_;
    QLabel *banner_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    ResourcesPage *resourcesPage_ = nullptr;
    QFormLayout *general_ = nullptr;
    QFormLayout *driver_ = nullptr;
    StoragePage *storagePage_ = nullptr;
    VolumesPage *volumesPage_ = nullptr;
    HealthPage *healthPage_ = nullptr;
    NetworkPage *networkPage_ = nullptr;
    BluetoothPage *bluetoothPage_ = nullptr;
    CameraPage *cameraPage_ = nullptr;
    DisplayPage *displayPage_ = nullptr;
    UsbPage *usbPage_ = nullptr;
    PciePage *pciePage_ = nullptr;
    BatteryPage *batteryPage_ = nullptr;
    QPushButton *reload_ = nullptr;
    EventsPage *eventsPage_ = nullptr;
    DetailsPage *detailsPage_ = nullptr;
    bool removed_ = false;
};
