#include "commonproperties.h"
#include "devicelabel.h"
#include <QCoreApplication>
#include <QStringList>
#include <cerrno>

QString propertyReadValue(const Attribute &a)
{
    switch (a.state) {
    case ReadState::Available:
        return a.value.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty value") : a.value;
    case ReadState::Unavailable: return QCoreApplication::translate("PropertiesDialog", "Unavailable");
    case ReadState::PermissionDenied: return QCoreApplication::translate("PropertiesDialog", "Permission denied");
    case ReadState::Removed: return QCoreApplication::translate("PropertiesDialog", "Device removed");
    case ReadState::Unsupported: return QCoreApplication::translate("PropertiesDialog", "Unsupported");
    case ReadState::NotApplicable: return QCoreApplication::translate("PropertiesDialog", "Not applicable");
    case ReadState::Error:
        switch (a.error) {
        case ETIMEDOUT: return QCoreApplication::translate("PropertiesDialog", "Query timed out (errno %1)").arg(a.error);
        case EOVERFLOW: return QCoreApplication::translate("PropertiesDialog", "Data exceeds supported limits (errno %1)").arg(a.error);
        case EAGAIN: return QCoreApplication::translate("PropertiesDialog", "Temporarily unavailable; retry the query (errno %1)").arg(a.error);
        case EINVAL:
        case EPROTO: return QCoreApplication::translate("PropertiesDialog", "Invalid data or query (errno %1)").arg(a.error);
        case ECANCELED: return QCoreApplication::translate("PropertiesDialog", "Query cancelled (errno %1)").arg(a.error);
        default: return QCoreApplication::translate("PropertiesDialog", "Read error (errno %1)").arg(a.error);
        }
    }
    return {};
}
QVector<PropertyEntry> commonPropertyEntries(const CommonProperties &snapshot, bool removed,
                                           PropertyPresentation presentation)
{
    const auto wording = [presentation](CommonPropertyWording::Text text) {
        return CommonPropertyWording::translated(text, presentation);
    };
    QVector<PropertyEntry> entries;
    const Device &d = snapshot.device;
    const bool efi = d.subsystem == "efivarfs";
    const Attribute notApplicable {ReadState::NotApplicable, {}, 0};
    const auto add = [&](const QString &id, const QString &label, const QString &value,
                           const QString &source = QString(), bool advanced = false, int tab = 2,
                           ReadState state = ReadState::Available, int error = 0) {
        entries.append({id, label, value, source, advanced, tab, state, error});
    };
    const auto raw = [&](const QString &id) { return snapshot.values.value(id); };
    const auto addRaw = [&](const QString &id, const QString &label, int tab, const Attribute &fallback = Attribute()) {
        const Attribute a = snapshot.values.value(id, fallback);
        add(id, label, propertyReadValue(a), snapshot.sources.value(id), false, tab, a.state, a.error);
    };
    add("name", QCoreApplication::translate("PropertiesDialog", "Device name"), deviceDisplayName(d), d.nameSource, false, 0);
    add("category", QCoreApplication::translate("PropertiesDialog", "Category"), categoryLabel(d.category), QCoreApplication::translate("PropertiesDialog", "Application grouping"), false, 0);
    Attribute manufacturer;
    QString manufacturerSource;
    if (efi) manufacturer = notApplicable;
    else {
        for (const QString &id : {QStringLiteral("sysfs/manufacturer"), QStringLiteral("sysfs/vendor_name"),
                                 QStringLiteral("acpi/vendor"),
                                 QStringLiteral("udev/ID_VENDOR_FROM_DATABASE"), QStringLiteral("udev/ID_VENDOR")}) {
            const Attribute candidate = raw(id);
            if (candidate.state == ReadState::Available && !candidate.value.isEmpty()) {
                manufacturer = candidate; manufacturerSource = snapshot.sources.value(id); break;
            }
            if (candidate.state == ReadState::PermissionDenied || candidate.state == ReadState::Error) {
                manufacturer = candidate; manufacturerSource = snapshot.sources.value(id); break;
            }
        }
        if (manufacturer.state == ReadState::Unavailable) {
            for (const QString &key : {QStringLiteral("ID_VENDOR_FROM_DATABASE"), QStringLiteral("ID_VENDOR")}) {
                if (d.properties.value(key).isEmpty()) continue;
                manufacturer = {ReadState::Available, d.properties.value(key), 0};
                manufacturerSource = d.propertySources.value(key, QStringLiteral("udev: %1 (%2)").arg(key, d.path));
                break;
            }
        }
    }
    add("manufacturer", QCoreApplication::translate("PropertiesDialog", "Manufacturer"), propertyReadValue(manufacturer), manufacturerSource, false, 0, manufacturer.state, manufacturer.error);
    Attribute bus = raw("udev/ID_BUS");
    if (bus.state != ReadState::Available) bus = raw("driver/bus");
    if (efi) bus = notApplicable;
    add("bus", QCoreApplication::translate("PropertiesDialog", "Bus / subsystem"), efi ? propertyReadValue(bus)
        : bus.state == ReadState::Available ? bus.value + " / " + d.subsystem : d.subsystem,
        QCoreApplication::translate("PropertiesDialog", "Device udev metadata and direct driver bus"), false, 0, efi ? ReadState::NotApplicable : ReadState::Available);
    add("location", QCoreApplication::translate("PropertiesDialog", "Location"), efi ? d.parentPath : d.path, QCoreApplication::translate("PropertiesDialog", "Kernel path"), false, 0);
    QString status;
    if (removed) status = QCoreApplication::translate("PropertiesDialog", "Removed or replaced; showing a snapshot.");
    else if (snapshot.state != ReadState::Available) status = wording(CommonPropertyWording::unverifiedPresence);
    else {
        status = wording(CommonPropertyWording::verifiedPresence);
        if (!efi) {
            const Attribute binding = raw("driver/name");
            status += '\n' + (binding.state == ReadState::Available
                ? binding.value.isEmpty() ? wording(CommonPropertyWording::unboundStatus) : QCoreApplication::translate("PropertiesDialog", "Driver bound: %1").arg(binding.value)
                : QCoreApplication::translate("PropertiesDialog", "Driver binding: %1").arg(propertyReadValue(binding)));
            const Attribute authorization = raw("sysfs/authorized");
            if (authorization.state == ReadState::Available && authorization.value == "0") status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB device/interface is unauthorized.");
            else if (authorization.state == ReadState::Available && authorization.value == "1") status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB device/interface is authorized.");
            else if (authorization.state == ReadState::PermissionDenied || authorization.state == ReadState::Error)
                status += '\n' + QCoreApplication::translate("PropertiesDialog", "USB authorization: %1").arg(propertyReadValue(authorization));
        }
    }
    add("status", QCoreApplication::translate("PropertiesDialog", "Device status"), status, QCoreApplication::translate("PropertiesDialog", "Observed presence, binding and USB authorization only"), false, 0);
    Attribute binding = snapshot.values.value("driver/name", d.driver.isEmpty() ? Attribute()
        : Attribute{ReadState::Available, d.driver, 0});
    if (efi) binding = notApplicable;
    const QString driverName = !efi && binding.state == ReadState::Available && binding.value.isEmpty()
        ? wording(CommonPropertyWording::unboundDriver) : propertyReadValue(binding);
    add("driver/name", wording(CommonPropertyWording::driver), driverName, efi ? QString() : d.path + "/driver", false, 0, binding.state, binding.error);
    QStringList identifiers;
    const Attribute alias = snapshot.values.value("udev/MODALIAS",
        d.properties.contains("MODALIAS")
            ? Attribute{ReadState::Available, d.properties.value("MODALIAS"), 0} : Attribute());
    if (alias.state == ReadState::Available && !alias.value.isEmpty()) {
        // Use the collected alias, never a device-name prefix or a related device's ID.
        if ((d.subsystem == "acpi" || d.subsystem == "platform" || d.subsystem == "i2c")
            && alias.value.startsWith("acpi:")) {
            const QStringList ids = alias.value.mid(5).split(':', Qt::SkipEmptyParts);
            if (!ids.isEmpty()) identifiers.append(
                QCoreApplication::translate("PropertiesDialog", "ACPI identifiers: %1").arg(ids.join(", ")));
        }
        identifiers.append(QCoreApplication::translate("PropertiesDialog", "Modalias: %1").arg(alias.value));
    }
    for (const QString &key : {QStringLiteral("PCI_ID"), QStringLiteral("PCI_SUBSYS_ID"), QStringLiteral("PRODUCT"),
                             QStringLiteral("ID_VENDOR_ID"), QStringLiteral("ID_MODEL_ID"), QStringLiteral("ID_SERIAL_SHORT")}) {
        const Attribute a = snapshot.values.value("udev/" + key,
            d.properties.contains(key) ? Attribute{ReadState::Available, d.properties.value(key), 0} : Attribute());
        if (a.state == ReadState::Available && !a.value.isEmpty()) identifiers.append(key + ": " + a.value);
    }
    for (const QString &key : {QStringLiteral("vendor"), QStringLiteral("device"), QStringLiteral("idVendor"),
                             QStringLiteral("idProduct"), QStringLiteral("serial"), QStringLiteral("hid"), QStringLiteral("uid")}) {
        const Attribute a = raw("sysfs/" + key);
        if (a.state != ReadState::Unavailable && !(a.state == ReadState::Available && a.value.isEmpty()))
            identifiers.append(key + ": " + propertyReadValue(a));
    }
    add("identifiers", QCoreApplication::translate("PropertiesDialog", "Hardware identifiers"), efi ? propertyReadValue(notApplicable)
        : identifiers.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Unavailable") : identifiers.join('\n'), QCoreApplication::translate("PropertiesDialog", "Selected udev and direct sysfs identifiers"), false, 0,
        efi ? ReadState::NotApplicable : identifiers.isEmpty() ? ReadState::Unavailable : ReadState::Available);
    add("driver/binding", wording(CommonPropertyWording::driver), driverName, efi ? QString() : d.path + "/driver", false, 1, binding.state, binding.error);
    const Attribute moduleFallback = efi || (binding.state == ReadState::Available && binding.value.isEmpty())
        ? notApplicable : Attribute();
    addRaw("driver/bus", QCoreApplication::translate("PropertiesDialog", "Driver bus"), 1, moduleFallback);
    addRaw("module/name", wording(CommonPropertyWording::module), 1, moduleFallback);
    const Attribute type = snapshot.values.value("module/type", moduleFallback);
    add("module/type", wording(CommonPropertyWording::moduleType), type.state == ReadState::Available ? wording(CommonPropertyWording::modular)
        : type.state == ReadState::NotApplicable || type.state == ReadState::PermissionDenied || type.state == ReadState::Error
            ? propertyReadValue(type) : wording(CommonPropertyWording::undetermined),
        snapshot.sources.value("module/type"), false, 1,
        type.state == ReadState::NotApplicable || type.state == ReadState::PermissionDenied || type.state == ReadState::Error
            ? type.state : ReadState::Available, type.error);
    addRaw("module/runtime_version", wording(CommonPropertyWording::runtimeVersion), 1, moduleFallback);
    const QStringList fields {"filename", "version", "description", "author", "license", "firmware"};
    const QStringList labels {wording(CommonPropertyWording::moduleFile), wording(CommonPropertyWording::installedVersion), wording(CommonPropertyWording::description),
        wording(CommonPropertyWording::author), wording(CommonPropertyWording::license), wording(CommonPropertyWording::firmwareNames)};
    for (int i = 0; i < fields.size(); ++i) addRaw("module/" + fields[i], labels[i], 1, moduleFallback);
    addRaw("sysfs/firmware_rev", wording(CommonPropertyWording::firmwareRevision), 1,
        d.subsystem == "nvme" ? Attribute() : notApplicable);
    if (CommonPropertyWording::source(CommonPropertyWording::metadataScope, presentation))
        add("module/note", QCoreApplication::translate("PropertiesDialog", "Metadata scope"), efi ? propertyReadValue(notApplicable)
            : wording(CommonPropertyWording::metadataScope), {}, false, 1,
            efi ? ReadState::NotApplicable : ReadState::Available);
    return entries;
}
