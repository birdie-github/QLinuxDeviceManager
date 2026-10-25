#include "propertypages.h"
#include <QStringList>
#include <algorithm>

CommonProperties inventoryCommonProperties(const Device &device)
{
    CommonProperties result;
    result.device = device;
    // Leave state Unavailable: the shared GUI formatter will correctly describe
    // presence in the inventory, rather than assert a verified property read.
    for (auto it = device.properties.cbegin(); it != device.properties.cend(); ++it) {
        result.values.insert("udev/" + it.key(), {ReadState::Available, it.value(), 0});
        result.sources.insert("udev/" + it.key(), device.propertySources.value(it.key()));
    }
    for (auto it = device.attributes.cbegin(); it != device.attributes.cend(); ++it) {
        // module_description can be an unbound alias candidate; never promote
        // this naming hint into observed driver metadata.
        if (it.key() == "module_description") continue;
        result.values.insert("sysfs/" + it.key(), it.value());
        result.sources.insert("sysfs/" + it.key(), device.path + '/' + it.key());
    }
    if (!device.driver.isEmpty()) {
        result.values.insert("driver/name", {ReadState::Available, device.driver, 0});
        result.sources.insert("driver/name", device.path + "/driver");
    }
    if (!device.driverModule.isEmpty()) {
        result.values.insert("module/name", {ReadState::Available, device.driverModule, 0});
        result.sources.insert("module/name", device.path + "/driver/module");
    }
    return result;
}

namespace {
void field(QString &text, const QString &label, QString value)
{
    if (value.isEmpty()) value = QStringLiteral("Unavailable");
    value.replace('\n', QStringLiteral("\n  "));
    text += label + ":\n  " + value + "\n\n";
}
}

QString commonPropertyPage(const QVector<PropertyEntry> &entries, int tab)
{
    QString text;
    for (const PropertyEntry &entry : entries)
        if (entry.tab == tab) field(text, entry.label, entry.value);
    return text;
}

QString inventoryDetailsPage(const Device &device)
{
    QString text;
    field(text, "Full kernel path", device.path);
    field(text, "Parent device path", device.parentPath);
    field(text, "Original name", device.name);
    field(text, "Name source", device.nameSource);
    field(text, "Kernel name", device.sysname);
    field(text, "Device type", device.devtype);
    text += "Udev properties\n\n";
    auto keys = device.properties.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) field(text, key, device.properties.value(key));
    text += "Captured attributes\n\n";
    keys = device.attributes.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) field(text, key, propertyReadValue(device.attributes.value(key)));
    return text;
}
