#include "propertypages.h"
#include "terminaltext.h"
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
struct Row { QString label; QString value; bool section = false; };
PropertyPage table(const QVector<Row> &rows, std::size_t columns = 0)
{
    std::size_t width = 0;
    for (const Row &row : rows)
        if (!row.section) width = std::max(width, finalcut::getColumnWidth(display(row.label)));
    PropertyPage result;
    int lineNumber = 0;
    int fieldNumber = 0;
    for (const Row &row : rows) {
        if (row.section) {
            result.text += '\n' + row.label + '\n';
            lineNumber += 2;
            fieldNumber = 0;
            continue;
        }
        const QString value = row.value.isEmpty() ? QStringLiteral("Unavailable") : row.value;
        const bool stacked = columns && width + 2 + 20 > columns;
        const std::size_t indent = stacked ? 2 : width + 2;
        const auto append = [&](const QString &line) {
            result.text += line + '\n';
            if (fieldNumber % 2) result.shadedLines.append(lineNumber);
            ++lineNumber;
        };
        if (stacked)
            for (const QString &line : wrapTerminalLine(row.label, columns)) append(line);
        bool first = true;
        for (const QString &physical : value.split('\n')) {
            const auto lines = columns ? wrapTerminalLine(physical, columns > indent ? columns - indent : 2)
                                       : QStringList{physical};
            for (const QString &line : lines) {
                const QString label = first && !stacked ? row.label : QString();
                const auto cells = finalcut::getColumnWidth(display(label));
                append(label + QString(static_cast<qsizetype>(indent - cells), QLatin1Char(' ')) + line);
                first = false;
            }
        }
        ++fieldNumber;
    }
    return result;
}
}

PropertyPage commonPropertyPage(const QVector<PropertyEntry> &entries, int tab, std::size_t columns)
{
    QVector<Row> rows;
    for (const PropertyEntry &entry : entries)
        if (entry.tab == tab) rows.append({entry.label, entry.value});
    return table(rows, columns);
}

PropertyPage inventoryDetailsPage(const Device &device)
{
    QVector<Row> rows;
    rows.append({"Full kernel path", device.path});
    rows.append({"Parent device path", device.parentPath});
    rows.append({"Original name", device.name});
    rows.append({"Name source", device.nameSource});
    rows.append({"Kernel name", device.sysname});
    rows.append({"Device type", device.devtype});
    rows.append({"Udev properties", {}, true});
    auto keys = device.properties.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) rows.append({key, device.properties.value(key)});
    keys = device.attributes.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) rows.append({key, propertyReadValue(device.attributes.value(key))});
    return table(rows);
}
