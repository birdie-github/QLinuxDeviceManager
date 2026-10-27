#include "report.h"

PropertyPage systemOperatingSystemPage(const SystemProperties &snapshot)
{
    SystemFields fields;
    for (const auto &field : systemFields(SystemPage::OperatingSystem))
        fields.append({field.second, systemOsValue(snapshot, field.first, snapshot.values.value(field.first))});
    SystemReport report;
    report.fields(fields);
    return report.take();
}
