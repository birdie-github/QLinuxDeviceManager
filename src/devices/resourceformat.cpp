#include "resourceformat.h"
#include "propertytext.h"
#include <QCoreApplication>
#include <QStringList>

namespace {
QString tr(const char *text) { return QCoreApplication::translate("PropertiesDialog", text); }
QString resourceKey(const DeviceResource &r)
{
    return QStringLiteral("%1:%2:%3:%4:%5:%6")
        .arg(r.source).arg(static_cast<int>(r.type)).arg(r.index).arg(r.start).arg(r.end).arg(r.mode);
}
}
QVector<ResourceRow> resourceRows(const DeviceResources &data)
{
    QVector<ResourceRow> result;
    for (const DeviceResource &resource : data.items) {
        QString type;
        switch (resource.type) {
        case DeviceResource::Type::Memory: type = tr("Memory range"); break;
        case DeviceResource::Type::Io: type = tr("I/O range"); break;
        case DeviceResource::Type::Irq: type = tr("IRQ"); break;
        case DeviceResource::Type::Dma: type = tr("DMA channel"); break;
        case DeviceResource::Type::Bus: type = tr("Bus range"); break;
        }
        QString setting;
        switch (resource.allocation) {
        case DeviceResource::Allocation::Assigned:
            if (resource.type == DeviceResource::Type::Irq || resource.type == DeviceResource::Type::Dma)
                setting = QString::number(resource.start);
            else {
                const int width = resource.type == DeviceResource::Type::Memory ? 16 : 4;
                setting = QStringLiteral("%1 – %2").arg(resource.start, width, 16, QLatin1Char('0'))
                    .arg(resource.end, width, 16, QLatin1Char('0')).toUpper();
            }
            break;
        case DeviceResource::Allocation::Unassigned: setting = tr("Unassigned"); break;
        case DeviceResource::Allocation::Disabled: setting = tr("Disabled"); break;
        case DeviceResource::Allocation::Unavailable: setting = tr("Address unavailable (zeroed or masked)"); break;
        }
        QStringList details;
        if (resource.pci) {
            if (resource.index < 6) details.append(tr("BAR %1").arg(resource.index));
            else if (resource.index == 6) details.append(tr("Expansion ROM"));
            else details.append(tr("PCI resource %1").arg(resource.index));
        }
        // Decode only stable flags; never interpret overlap or BUSY as a conflict.
        if (resource.flags & 0x00002000) details.append(tr("Prefetchable"));
        if (resource.flags & 0x00004000) details.append(tr("Read-only"));
        if (resource.flags & 0x00100000) details.append(tr("64-bit memory"));
        if (resource.flags & 0x00200000) details.append(tr("Bridge window"));
        if (resource.pci)
            details.append(tr("Flags: %1").arg(QStringLiteral("0x%1").arg(resource.flags, 0, 16)));
        if (resource.mode == "msi") details.append(QStringLiteral("MSI"));
        else if (resource.mode == "msix") details.append(QStringLiteral("MSI-X"));
        else if (resource.mode == "reported") details.append(tr("Reported irq attribute; active mode not established"));
        if (data.pnpState == "disabled") details.append(tr("PnP device disabled"));
        result.append({resourceKey(resource), type, setting, details.join("; "), resource.source,
                       QString::number(static_cast<int>(resource.type))});
    }
    for (const ResourceIssue &problem : data.issues)
        result.append({QStringLiteral("issue:") + problem.source, tr("Resource metadata"),
                       propertyReadValue(problem.error), tr("Read or parse failed"), problem.source, QStringLiteral("issues")});
    // Preserve multiple identical reported rows without colliding projection keys.
    QHash<QString, int> duplicates;
    for (ResourceRow &row : result) {
        const QString key = row.key;
        row.key += ':' + QString::number(duplicates[key]++);
    }
    return result;
}
bool sameDeviceResources(const std::shared_ptr<const DeviceResources> &a,
                         const std::shared_ptr<const DeviceResources> &b)
{
    if (a == b) return true;
    static const DeviceResources empty;
    const DeviceResources &left = a ? *a : empty;
    const DeviceResources &right = b ? *b : empty;
    if (left.pnpState != right.pnpState || left.items.size() != right.items.size()
        || left.issues.size() != right.issues.size()) return false;
    for (qsizetype i = 0; i < left.items.size(); ++i) {
        const auto &x = left.items.at(i), &y = right.items.at(i);
        if (x.type != y.type || x.allocation != y.allocation || x.start != y.start || x.end != y.end
            || x.flags != y.flags || x.index != y.index || x.pci != y.pci || x.mode != y.mode
            || x.source != y.source) return false;
    }
    for (qsizetype i = 0; i < left.issues.size(); ++i) {
        const auto &x = left.issues.at(i), &y = right.issues.at(i);
        if (x.source != y.source || x.error.state != y.error.state || x.error.value != y.error.value
            || x.error.error != y.error.error) return false;
    }
    return true;
}
