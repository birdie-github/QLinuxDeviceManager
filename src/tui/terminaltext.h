#pragma once
#include <final/final.h>
#include <QString>
#include <QStringList>
#include <cstddef>
finalcut::FString display(QString value);

// Wrap one physical line using the full available display columns.
QStringList wrapTerminalLine(QString line, std::size_t columns);
