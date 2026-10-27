#pragma once

#include "device.h"
#include "usbproperties.h"
#include "deviceresources.h"
#include "storageproperties.h"
#include "networkproperties.h"
#include "displayproperties.h"
#include "monitorproperties.h"
#include "cameraproperties.h"
#include "bluetoothproperties.h"
#include "audioproperties.h"
#include <QByteArray>
#include <QThread>

struct BatteryProperties {
    bool applicable = false;
    QHash<QString, Attribute> values;
    Attribute state;
};

// Owned raw results. Display labels, translations and hex formatting belong to UI.
struct DeviceProperties {
    Device device;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
    DeviceResources resources;
    StorageProperties storage;
    QVector<NetworkProperties> network;
    Attribute networkState;
    DisplayProperties display;
    MonitorProperties monitor;
    CameraProperties camera;
    UsbProperties usb;
    BatteryProperties battery;
    BluetoothProperties bluetooth;
    AudioProperties audio;
    bool pcieApplicable = false; // Evidence from this PCI device's link attributes only.
    Attribute pcieType; // Numeric PCI Express device/port type; no GPU-ID inference.
    QString pcieTypeSource;
    bool pcieTypeRequiresRoot = false;
    QByteArray efiBytes;
    bool efiTruncated = false;
    ReadState state = ReadState::Unavailable;
    int error = 0;
    quint64 request = 0;
};

// Worker-thread collector shared by Properties and deep search. No GUI objects.
DeviceProperties collectDeviceProperties(const Device &device, bool cachedStorageService = true);

// One window-owned worker, shared by successive dialogs. Requests serialize.
class PropertiesReader final : public QThread {
    Q_OBJECT
public:
    explicit PropertiesReader(QObject *parent = nullptr) : QThread(parent) {}
    void setRequest(const Device &device, quint64 request); // Only while stopped.
    DeviceProperties takeResult(); // Only after finished().
protected:
    void run() override;
private:
    Device device_;
    quint64 request_ = 0;
    DeviceProperties result_;
};
