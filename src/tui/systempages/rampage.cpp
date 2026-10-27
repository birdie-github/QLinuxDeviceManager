#include "report.h"

PropertyPage systemRamSummaryPage(const SystemProperties &snapshot)
{
    SystemReport report;
    SystemFields summary;
    for (const auto &field : systemFields(SystemPage::Ram))
        summary.append({field.second, systemRamSummaryValue(snapshot, field.first)});
    report.fields(summary);
    return report.take();
}

PropertyPage systemRamPage(const SystemProperties &snapshot, int chip)
{
    SystemReport report;
    const auto &memory = snapshot.memoryDevices;
    if (memory.status.state != ReadState::Available || memory.devices.isEmpty()) {
        report.fields({{systemMemoryHeading(), systemMemoryStatus(memory)}});
    } else if (chip >= 0 && chip < memory.devices.size()) {
        QVector<QStringList> rows {systemPropertyHeaders()};
        for (const auto &field : systemMemoryFields())
            rows.append({field.second, systemMemoryValue(field.first, memory.devices.at(chip).values.value(field.first))});
        report.table({}, rows);
    }
    return report.take();
}
