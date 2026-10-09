#pragma once
#include "device.h"

bool splitEfiVariableFilename(const QString &filename, QString &name, QString &guid);
QString efiVariableLabel(const QString &name);
// Worker-only, fixed efivarfs directory. Metadata enumeration; no value reads.
void appendEfiVariables(Inventory &inventory);
