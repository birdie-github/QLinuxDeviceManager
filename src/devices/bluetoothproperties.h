#pragma once
#include "device.h"
#include <QByteArray>
#include <QVariantMap>

struct BluetoothDeviceProperties {
    QString path;
    QHash<QString, Attribute> values;
};
struct BluetoothProperties {
    bool applicable = false;
    QString controllerPath, bluezPath;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
    QVector<BluetoothDeviceProperties> devices;
    Attribute bluezState, managementState;
    bool devicesTruncated = false;
    void invalidate(const Attribute &error);
};

BluetoothProperties collectBluetoothProperties(const Device &device);
// Pure decoders shared by the collector and fixture checks.
bool decodeBluetoothControllerInfo(const QByteArray &payload, BluetoothProperties &result);
bool bluetoothControllerName(const QString &name);
BluetoothDeviceProperties bluetoothKnownDevice(const QString &path, const QVariantMap &properties);
QString bluetoothPropertyText(const QString &key, const Attribute &value);
QString bluetoothSettingName(int bit);
