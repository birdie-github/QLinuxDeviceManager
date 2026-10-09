#include "devicesearch.h"
#include "devicelabel.h"
#include "propertytext.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QStringList>
#include <utility>

namespace {
QString numbers(quint64 value)
{
    return QStringLiteral("%1 0x%2 %3").arg(value).arg(value, 0, 16).arg(value, 16, 16, QLatin1Char('0'));
}
// Only documented numeric fields are interpreted. Serial numbers/names stay literal.
QString numericAliases(const QString &key, const QString &value, const QString &subsystem)
{
    static const QSet<QString> hexKeys {"idVendor", "idProduct", "bcdDevice", "vendor_id", "subsystem_id", "revision_id",
        "ID_VENDOR_ID", "ID_MODEL_ID", "PCI_CLASS", "flags"};
    static const QSet<QString> pciHex {"vendor", "device", "subsystem_vendor", "subsystem_device", "class", "revision"};
    static const QSet<QString> decimalKeys {"busnum", "devnum", "ifindex", "index", "authorized", "present"};
    int base = hexKeys.contains(key) || (subsystem == "pci" && pciHex.contains(key)) ? 16
        : decimalKeys.contains(key) ? 10 : 0;
    QStringList components;
    if (key == "PCI_ID" || key == "PCI_SUBSYS_ID") { base = 16; components = value.split(':'); }
    else if (key == "PRODUCT") { base = 16; components = value.split('/'); }
    else components.append(value);
    if (!base) return {};
    QStringList result;
    for (const QString &part : components) {
        bool ok = false;
        const quint64 number = part.trimmed().toULongLong(&ok, base);
        if (ok) result.append(numbers(number));
    }
    return result.join(' ');
}
}
SearchDocument deviceSearchDocument(const SearchRecord &record, const DeviceProperties &p)
{
    SearchDocument doc;
    const Device &d = record.device;
    const auto add = [&](const QString &label, const QString &value) {
        if (!value.isEmpty()) doc.append({label, value});
    };
    const auto attribute = [&](const QString &key, const Attribute &a, const QString &source) {
        add(key, a.state == ReadState::Available ? a.value : propertyReadValue(a));
        if (a.error) add(key, QString::number(a.error));
        if (a.state == ReadState::Available)
            add(key, numericAliases(key.section('/', -1), a.value, d.subsystem));
        add(key, source);
    };
    add(QCoreApplication::translate("PropertiesDialog", "Device name"), record.label);
    add(QCoreApplication::translate("PropertiesDialog", "Device name"), deviceDisplayName(d));
    add(QCoreApplication::translate("PropertiesDialog", "Original name"), d.name);
    add(QCoreApplication::translate("PropertiesDialog", "Name source"), d.nameSource);
    add(QCoreApplication::translate("PropertiesDialog", "Category"), categoryLabel(d.category));
    add(QCoreApplication::translate("PropertiesDialog", "Category"), d.category);
    add(QCoreApplication::translate("PropertiesDialog", "Full kernel path"), d.path);
    add(QCoreApplication::translate("PropertiesDialog", "Parent device path"), d.parentPath);
    add(QCoreApplication::translate("PropertiesDialog", "Kernel name"), d.sysname);
    add(QCoreApplication::translate("PropertiesDialog", "Bus / subsystem"), d.subsystem);
    add("DEVTYPE", d.devtype);
    add(QCoreApplication::translate("PropertiesDialog", "Bound kernel driver"), d.driver);
    add(QCoreApplication::translate("PropertiesDialog", "Represented by"), d.representedByPath);
    add(QCoreApplication::translate("PropertiesDialog", "Grouped records"), record.groupedPaths);
    for (auto it = d.properties.cbegin(); it != d.properties.cend(); ++it)
        attribute(it.key(), {ReadState::Available, it.value(), 0}, d.propertySources.value(it.key()));
    for (auto it = d.attributes.cbegin(); it != d.attributes.cend(); ++it) attribute(it.key(), it.value(), {});
    // An invalidated read must not contribute partially collected properties.
    if (p.state != ReadState::Available) {
        add(QCoreApplication::translate("PropertiesDialog", "Device status"), propertyReadValue({p.state, {}, p.error}));
        return doc;
    }
    for (auto it = p.values.cbegin(); it != p.values.cend(); ++it) attribute(it.key(), it.value(), p.sources.value(it.key()));
    for (const PropertyEntry &entry : propertyEntries(p)) {
        add(entry.label, entry.value);
        add(entry.label, entry.source);
    }
    if (p.values.value("efi/hex").state == ReadState::Available)
    add(QCoreApplication::translate("PropertiesDialog", "UEFI variable — hex dump"), QString::fromLatin1(p.efiBytes.toHex()));
    add(QCoreApplication::translate("PropertiesDialog", "PnP device state"), p.resources.pnpState);
    if (p.resources.pnpState == "disabled") add(QCoreApplication::translate("PropertiesDialog", "PnP device state"), QCoreApplication::translate("PropertiesDialog", "PnP device disabled"));
    if (p.efiTruncated) add(QCoreApplication::translate("PropertiesDialog", "UEFI variable — hex dump"), QCoreApplication::translate("PropertiesDialog", "Truncated: first 65536 bytes shown."));
    for (const DeviceResource &r : p.resources.items) {
        QString label;
        switch (r.type) {
        case DeviceResource::Type::Memory: label = QCoreApplication::translate("PropertiesDialog", "Memory range"); break;
        case DeviceResource::Type::Io: label = QCoreApplication::translate("PropertiesDialog", "I/O range"); break;
        case DeviceResource::Type::Irq: label = QCoreApplication::translate("PropertiesDialog", "IRQ"); break;
        case DeviceResource::Type::Dma: label = QCoreApplication::translate("PropertiesDialog", "DMA channel"); break;
        case DeviceResource::Type::Bus: label = QCoreApplication::translate("PropertiesDialog", "Bus range"); break;
        }
        add(label, r.source);
        add(label, numbers(r.start) + ' ' + numbers(r.end) + ' ' + numbers(r.flags));
        add(label, QStringLiteral("%1 – %2").arg(r.start, r.type == DeviceResource::Type::Memory ? 16 : 4, 16, QLatin1Char('0'))
            .arg(r.end, r.type == DeviceResource::Type::Memory ? 16 : 4, 16, QLatin1Char('0')));
        if (r.pci) add(label, r.index < 6 ? QCoreApplication::translate("PropertiesDialog", "BAR %1").arg(r.index)
            : r.index == 6 ? QCoreApplication::translate("PropertiesDialog", "Expansion ROM") : QCoreApplication::translate("PropertiesDialog", "PCI resource %1").arg(r.index));
        switch (r.allocation) {
        case DeviceResource::Allocation::Assigned: break;
        case DeviceResource::Allocation::Unassigned: add(label, QCoreApplication::translate("PropertiesDialog", "Unassigned")); break;
        case DeviceResource::Allocation::Disabled: add(label, QCoreApplication::translate("PropertiesDialog", "Disabled")); break;
        case DeviceResource::Allocation::Unavailable: add(label, QCoreApplication::translate("PropertiesDialog", "Address unavailable (zeroed or masked)")); break;
        }
        if (r.flags & 0x2000) add(label, QCoreApplication::translate("PropertiesDialog", "Prefetchable"));
        if (r.flags & 0x4000) add(label, QCoreApplication::translate("PropertiesDialog", "Read-only"));
        if (r.flags & 0x100000) add(label, QCoreApplication::translate("PropertiesDialog", "64-bit memory"));
        if (r.flags & 0x200000) add(label, QCoreApplication::translate("PropertiesDialog", "Bridge window"));
        add(label, r.mode == "msix" ? QStringLiteral("MSI-X") : r.mode);
        if (r.mode == "reported") add(label, QCoreApplication::translate("PropertiesDialog", "Reported irq attribute; active mode not established"));
    }
    for (const ResourceIssue &issue : p.resources.issues) attribute(QCoreApplication::translate("PropertiesDialog", "Resource metadata"), issue.error, issue.source);
    return doc;
}
QString searchDocumentMatch(const SearchDocument &document, const QString &query)
{
    if (query.isEmpty()) return {};
    for (const SearchField &field : document)
        if (field.value.contains(query, Qt::CaseInsensitive) || field.label.contains(query, Qt::CaseInsensitive)) return field.label;
    return {};
}
void DeepSearchWorker::setRequest(QVector<SearchRecord> records, QString query, quint64 revision, quint64 request)
{
    Q_ASSERT(!isRunning());
    records_ = std::move(records);
    query_ = std::move(query);
    revision_ = revision;
    request_ = request;
}
void DeepSearchWorker::run()
{
    if (revision_ != cachedRevision_) {
        cache_.clear(); cacheBytes_ = 0; cachedRevision_ = revision_;
    }
    SearchBatch batch;
    batch.request = request_;
    batch.total = static_cast<int>(records_.size());
    QElapsedTimer timer;
    timer.start();
    for (const SearchRecord &record : records_) {
        if (isInterruptionRequested()) return;
        const auto cached = cache_.constFind(record.device.path);
        SearchDocument document;
        bool unavailable = false;
        if (cached != cache_.cend() && cached->generation == record.device.generation) {
            document = cached->document;
            unavailable = cached->unavailable;
        } else {
            const DeviceProperties properties = collectDeviceProperties(record.device, false);
            if (isInterruptionRequested()) return;
            unavailable = properties.state != ReadState::Available || !properties.resources.issues.isEmpty();
            for (const Attribute &value : properties.values)
                if (value.state == ReadState::PermissionDenied || value.state == ReadState::Error) unavailable = true;
            document = deviceSearchDocument(record, properties);
            qsizetype bytes = 0;
            for (const SearchField &field : document) bytes += 2 * (field.label.size() + field.value.size()) + sizeof(SearchField);
            // Bound cached payload to 64 MiB. Uncached records still get fully searched.
            if (cacheBytes_ + bytes <= 64 * 1024 * 1024) {
                cache_.insert(record.device.path, {record.device.generation, document, unavailable});
                cacheBytes_ += bytes;
            }
        }
        const QString match = searchDocumentMatch(document, query_);
        if (!match.isEmpty()) batch.matches.append({record.device.path, record.device.generation, match});
        ++batch.done;
        if (unavailable) ++batch.unavailable;
        if (timer.elapsed() >= 100 || batch.matches.size() >= 128) {
            emit batchReady(batch);
            batch.matches.clear(); timer.restart();
        }
    }
    emit batchReady(batch);
}
