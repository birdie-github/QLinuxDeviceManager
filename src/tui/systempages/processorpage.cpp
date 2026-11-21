#include "report.h"

PropertyPage systemProcessorPage(const SystemProperties &snapshot)
{
    SystemReport report;
    SystemFields fields;
    for (const auto &field : systemFields(SystemPage::Processor)) {
        if (field.first == "instruction_sets") {
            fields.append({field.second, QStringLiteral("Run `cat /proc/cpuinfo` to find out")});
            continue;
        }
        if (field.first == "core_groups" || field.first == "cache_topology") {
            if (!fields.isEmpty()) { report.fields(fields); fields.clear(); }
            const auto table = systemProcessorTable(snapshot, field.first, true);
            if (table.rows.isEmpty()) {
                report.table(field.second, {{table.status}});
            } else {
                auto rows = table.rows;
                rows.prepend(table.headers);
                report.table(field.second, rows);
            }
        } else {
            fields.append({field.second, systemProcessorValue(snapshot, field.first, snapshot.values.value(field.first))});
        }
    }
    if (!fields.isEmpty()) report.fields(fields);
    return report.take();
}
