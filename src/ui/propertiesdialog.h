#pragma once

#include "deviceproperties.h"
#include "propertytext.h"
#include "storagepresentation.h"
#include <QDialog>
#include <QVector>
#include <QPointer>
#include <QProcess>

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
    void rebuildDetails();
    void rebuildResources();
    void rebuildStorage();
    void rebuildNetwork();
    void rebuildBluetooth();
    void rebuildDisplay();
    void rebuildCamera();
    void rebuildUsb();
    void rebuildPcie();
    void testGpuLink();
    void rebuildBattery();
    void showVolumeDetails();
    void showHealth();
    void copyResources(bool selectedOnly);
    void showDetail();
    void copyAll();
    void showEvent();
    void copyEvents(bool selectedOnly);
    DeviceProperties snapshot_;
    QVector<PropertyEntry> entries_;
    QLabel *banner_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QWidget *resourcesPage_ = nullptr;
    QTableWidget *resourcesTable_ = nullptr;
    QString resourcesText_;
    QFormLayout *general_ = nullptr;
    QFormLayout *driver_ = nullptr;
    QFormLayout *storage_ = nullptr;
    QWidget *storagePage_ = nullptr;
    QWidget *volumesPage_ = nullptr;
    QWidget *healthPage_ = nullptr;
    QTableWidget *volumesTable_ = nullptr;
    QPushButton *volumeDetailsButton_ = nullptr;
    QComboBox *healthDrive_ = nullptr;
    QVBoxLayout *healthGroups_ = nullptr;
    QLabel *storageNotice_ = nullptr;
    QLabel *volumesNotice_ = nullptr;
    QLabel *healthNotice_ = nullptr;
    StoragePresentation storagePresentation_;
    NetworkPage *networkPage_ = nullptr;
    BluetoothPage *bluetoothPage_ = nullptr;
    CameraPage *cameraPage_ = nullptr;
    DisplayPage *displayPage_ = nullptr;
    UsbPage *usbPage_ = nullptr;
    QWidget *pciePage_ = nullptr;
    QVBoxLayout *pcieGroups_ = nullptr;
    QString pcieText_;
    QPushButton *gpuTestButton_ = nullptr;
    QPointer<QProcess> gpuTest_;
    QString gpuTestText_;
    QString gpuTestTime_;
    QString gpuTestRate_;
    BatteryPage *batteryPage_ = nullptr;
    QComboBox *property_ = nullptr;
    QPlainTextEdit *value_ = nullptr;
    QLabel *source_ = nullptr;
    QCheckBox *advanced_ = nullptr;
    QPushButton *reload_ = nullptr;
    QLabel *eventsNotice_ = nullptr;
    QTableWidget *eventsTable_ = nullptr;
    QPlainTextEdit *eventDetails_ = nullptr;
    QVector<DeviceEvent> displayedEvents_;
    QString eventScope_;
    bool removed_ = false;
};
