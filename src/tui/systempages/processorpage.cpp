#include "report.h"

PropertyPage systemProcessorPage(const SystemProperties &snapshot)
{
    SystemReport report;
    SystemFields fields;
    for (const auto &field : systemFields(SystemPage::Processor)) {
        const QString value = systemProcessorValue(snapshot, field.first, snapshot.values.value(field.first), true);
        if (field.first == "core_groups" || field.first == "cache_topology") {
            if (!fields.isEmpty()) { report.fields(fields); fields.clear(); }
            QVector<QStringList> rows;
            for (const auto &line : value.split('\n')) rows.append(line.split('\t'));
            report.table(field.second, rows);
        } else fields.append({field.second, value});
    }
    if (!fields.isEmpty()) report.fields(fields);
    return report.take();
}
