#pragma once

#include "device.h"
#include "ethernetproperties.h"
#include "wirelessproperties.h"

struct NetworkAddress {
    QString family;
    QString address;
    Attribute netmask;
};

// Owned worker-thread results for one interface in the application's network namespace.
struct NetworkProperties {
    QString name;
    QString path;
    Attribute currentAddress;
    Attribute assignmentType;
    Attribute linkType;
    Attribute mtu;
    QVector<NetworkAddress> addresses;
    Attribute addressState;
    Attribute permanentAddress;
    EthernetProperties ethernet;
    WirelessProperties wireless;
};

NetworkProperties collectNetworkProperties(const QString &name, unsigned int index, bool ethernet);
