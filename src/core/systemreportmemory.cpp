#include "systemreport.h"
#include "commonproperties.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <limits>

namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(SystemDialog) };
}

QString systemMemoryScope()
{
    return Text::tr("Firmware-reported records; these do not establish removable modules, physical slots, "
        "memory channels, upgradeability or active ECC. Transfer rates are firmware declarations, not live measurements.");
}

QVector<QPair<QString, QString>> systemMemoryFields()
{
    return {{"PRESENT", Text::tr("Presence")}, {"SIZE", Text::tr("Capacity (total)")},
        {"VOLATILE_SIZE", Text::tr("Volatile capacity")}, {"NON_VOLATILE_SIZE", Text::tr("Nonvolatile capacity")},
        {"LOCATOR", Text::tr("Device locator")}, {"BANK_LOCATOR", Text::tr("Bank locator")},
        {"TYPE", Text::tr("Memory type")}, {"FORM_FACTOR", Text::tr("Form factor")},
        {"SPEED_MTS", Text::tr("Rated transfer rate")}, {"CONFIGURED_SPEED_MTS", Text::tr("Configured transfer rate")},
        {"MANUFACTURER", Text::tr("Manufacturer")}, {"PART_NUMBER", Text::tr("Part number")}};
}

QString systemRamSummaryValue(const SystemProperties &snapshot, const QString &key)
{
    const Attribute value = snapshot.values.value(key);
    if (value.state != ReadState::Available) return propertyReadValue(value);
    constexpr qulonglong gib = 1024ULL * 1024 * 1024;
    constexpr qulonglong mib = 1024ULL * 1024;
    const qulonglong bytes = value.value.toULongLong();
    const int precision = key == "physical_memory" && bytes % gib == 0 ? 0 : 2;
    QString text = Text::tr("%1 GiB").arg(QLocale().toString(bytes / static_cast<double>(gib), 'f', precision));
    const Attribute physical = snapshot.values.value("physical_memory");
    if (key == "memory" && physical.state == ReadState::Available) {
        const qulonglong installed = physical.value.toULongLong();
        if (installed >= bytes)
            text += Text::tr(" (%1 MiB is system reserved)")
                .arg(QLocale().toString((installed - bytes) / static_cast<double>(mib), 'f', 0));
    }
    return text;
}

QString systemMemoryValue(const QString &key, const Attribute &value)
{
    if (key == "PRESENT" && value.state == ReadState::Unavailable)
        return Text::tr("Unknown (no explicit presence flag)");
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "PRESENT") return value.value == "0" ? Text::tr("No device reported present") : Text::tr("Device reported present");
    if (key == "SPEED_MTS" || key == "CONFIGURED_SPEED_MTS")
        return Text::tr("%1 MT/s").arg(QLocale().toString(value.value.toULongLong()));
    if (key == "SIZE" || key == "VOLATILE_SIZE" || key == "NON_VOLATILE_SIZE") {
        const qulonglong bytes = value.value.toULongLong();
        if (bytes <= static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
            return QLocale().formattedDataSize(static_cast<qlonglong>(bytes), 2, QLocale::DataSizeIecFormat);
        return Text::tr("%1 bytes").arg(QLocale().toString(bytes));
    }
    return value.value;
}

QString systemMemoryTitle(int recordIndex)
{
    return Text::tr("Chip %1").arg(recordIndex + 1);
}

QStringList systemPropertyHeaders() { return {Text::tr("Property"), Text::tr("Value")}; }
QString systemMemoryStatus(const FirmwareMemoryInformation &memory)
{
    return memory.status.state != ReadState::Available ? propertyReadValue(memory.status)
        : memory.devices.isEmpty() ? Text::tr("No memory-device records reported") : QString();
}

QString systemMemoryHeading() { return Text::tr("Memory Devices"); }
