#pragma once
#include "propertypages.h"
#include "systemreport.h"

PropertyPage systemOperatingSystemPage(const SystemProperties &snapshot);
PropertyPage systemMotherboardPage(const SystemProperties &snapshot);
PropertyPage systemProcessorPage(const SystemProperties &snapshot);
PropertyPage systemRamPage(const SystemProperties &snapshot, int chip);
