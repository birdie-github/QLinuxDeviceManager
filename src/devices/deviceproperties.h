#pragma once

#include "device.h"
#include "deviceresources.h"
#include <QByteArray>
#include <QThread>

// Owned raw results. Display labels, translations and hex formatting belong to UI.
struct DeviceProperties {
    Device device;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
    DeviceResources resources;
    QByteArray efiBytes;
    bool efiTruncated = false;
    ReadState state = ReadState::Unavailable;
    int error = 0;
    quint64 request = 0;
};

// Worker-thread collector shared by Properties and deep search. No GUI objects.
DeviceProperties collectDeviceProperties(const Device &device);

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
