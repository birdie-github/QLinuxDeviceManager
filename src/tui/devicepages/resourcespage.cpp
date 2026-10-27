#include "resourcespage.h"
#include "resourceformat.h"
#include "systempages/report.h"

PropertyPage resourcesPropertyPage(const DeviceResources &snapshot)
{
    SystemReport report;
    if (!snapshot.pnpState.isEmpty())
        report.fields({{"PnP device state", snapshot.pnpState == "active" ? "Active" : "Disabled"}});
    QVector<QStringList> rows {{"Resource type", "Setting", "Details", "Source"}};
    for (const ResourceRow &row : resourceRows(snapshot))
        rows.append({row.type, row.setting, row.details, row.source});
    report.table({}, rows);
    return report.take();
}
