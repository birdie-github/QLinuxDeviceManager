#pragma once
#include "systemproperties.h"
#include <QStringList>

// Shared System Information wording and interpretation; no frontend widgets.
enum class SystemPage { OperatingSystem, Motherboard, Processor, Ram };
using SystemFields = QVector<QPair<QString, QString>>;
QStringList systemPageTitles();
SystemFields systemFields(SystemPage page);
QString systemFieldCaption(SystemPage page, const QString &key);
SystemFields systemMemoryFields();
QString systemMemoryScope();
QString systemMemoryTitle(int recordIndex);
QString systemOsValue(const SystemProperties &snapshot, const QString &key, const Attribute &value);
QString systemProcessorValue(const SystemProperties &snapshot, const QString &key, const Attribute &value, bool compact = false);
QString systemRamSummaryValue(const SystemProperties &snapshot, const QString &key);
QString systemMemoryValue(const QString &key, const Attribute &value);
QString systemMokText(const SystemProperties &snapshot);
struct SystemCertificateReport { QString title; SystemFields fields; };
SystemCertificateReport systemCertificateReport(const MokCertificate &certificate, int index, int count);

QString systemMotherboardValue(const Attribute &value);
QString systemMokOmitted(int count);
QStringList systemPropertyHeaders();
QString systemMemoryStatus(const FirmwareMemoryInformation &memory);
QString systemInformationTitle();
QString systemCollectingText();
QString systemMokTitle();
QString systemMemoryHeading();
