#pragma once
#include <final/final.h>
#include <QString>
#include <QStringList>
#include <cstddef>
finalcut::FString display(QString value);

// Wrap one physical line by display columns; continuation markers consume space.
QStringList wrapTerminalLine(QString line, std::size_t columns);
