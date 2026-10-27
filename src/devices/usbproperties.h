#pragma once
#include "device.h"
#include "commonproperties.h"

// USB-specific values stay separate from generic Details metadata.
struct UsbInterfaceProperties {
    QString name;
    QString path;
    QHash<QString, Attribute> values;
};
struct UsbProperties {
    QHash<QString, Attribute> values;
    QVector<UsbInterfaceProperties> interfaces;
    Attribute interfaceState;
};
// Bounded reads from an already-open, instance-checked device directory.
UsbProperties collectUsbProperties(int fd, const Device &device, int openError = 0);
struct UsbSection {
    QString title;
    QVector<PropertyEntry> fields;
};
QVector<UsbSection> usbPropertySections(const UsbProperties &data, const Attribute &version,
                                      const Attribute &speed, const QString &devicePath);
