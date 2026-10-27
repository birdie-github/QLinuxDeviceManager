#pragma once
#include "propertypages.h"
#include "usbproperties.h"
PropertyPage usbPropertyPage(const UsbProperties &snapshot, const QString &path, std::size_t columns = 0);
