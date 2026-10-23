#pragma once
#include "device.h"
#include <QString>
class QObject;
class QProcess;

// No renderer or driver is loaded in the device manager process.
bool gpuLinkTestBuilt();
QProcess *startGpuLinkTest(const Device &device, QObject *owner);
int runGpuLinkTest(const QString &path, const QString &incarnation);
