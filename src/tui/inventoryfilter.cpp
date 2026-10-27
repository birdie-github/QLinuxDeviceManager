#include "inventoryfilter.h"
#include "devicelabel.h"

// Snapshot-only matching: do not trigger deep collectors or filesystem reads.
bool matches(const Device &d, const QString &query)
{
    const auto contains = [&query](const QString &text) {
        return text.contains(query, Qt::CaseInsensitive);
    };
    for (const QString &text : {deviceDisplayName(d, true), d.name, d.sysname,
            d.path, d.parentPath, d.driver, d.driverModule, d.subsystem,
            d.category, categoryLabel(d.category)})
        if (contains(text)) return true;
    for (auto it = d.properties.cbegin(); it != d.properties.cend(); ++it)
        if (contains(it.value())) return true;
    for (auto it = d.attributes.cbegin(); it != d.attributes.cend(); ++it)
        if (it->state == ReadState::Available && contains(it->value)) return true;
    return false;
}
