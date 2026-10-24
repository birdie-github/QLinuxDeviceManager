#include "batterypage.h"
#include "deviceproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"

#include <QApplication>
#include <QClipboard>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>
#include <cerrno>

BatteryPage::BatteryPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ Battery snapshot for this device"),
        tr("Readings are reported by the kernel and battery firmware. Capacity retention is an estimate from "
        "compatible full/design values, not a complete health assessment. Energy and charge units remain separate. "
        "Reload properties to refresh; no background monitoring or battery settings are changed."),
        tr("Copy Battery properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void BatteryPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void BatteryPage::setSnapshot(const BatteryProperties &data, const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    const auto source = [&](const QString &key) { return devicePath + '/' + key; };
    const auto readValue = [&](const Attribute &value) {
        return value.state == ReadState::Unsupported || value.error == EOPNOTSUPP || value.error == ENOSYS
            ? tr("Query unsupported by the kernel/device") : propertyReadValue(value);
    };
    const auto textValue = [&](const QString &key) {
        const Attribute value = data.values.value(key);
        if (value.state != ReadState::Available) return readValue(value);
        if (value.value.isEmpty()) return tr("Not reported");
        if (value.value == "Unknown") return tr("Unknown (reported)");
        if (key == "status") {
            if (value.value == "Charging") return tr("Charging");
            if (value.value == "Discharging") return tr("Discharging");
            if (value.value == "Not charging") return tr("Not charging");
            if (value.value == "Full") return tr("Full");
        }
        return value.value;
    };
    // Sysfs numeric battery properties are decimal integers, in micro-units.
    // Preserve signed current/power readings, and never derive one from the other.
    const auto numberValue = [&](const QString &key, double divisor, const QString &unit, bool signedReading = false) {
        const Attribute value = data.values.value(key);
        if (value.state != ReadState::Available) return readValue(value);
        bool valid = false;
        const qlonglong number = value.value.toLongLong(&valid);
        if (!valid || (!signedReading && number < 0) || (key == "capacity" && number > 100))
            return tr("Malformed reported value: %1").arg(value.value.isEmpty() ? tr("(empty)") : value.value);
        if (!number && (key == "cycle_count" || key.endsWith("_full") || key.endsWith("_full_design")))
            return tr("Unavailable (reported value: 0)");
        const QString formatted = divisor == 1 ? QLocale().toString(number)
            : QLocale().toString(static_cast<double>(number) / divisor, 'f', 3);
        return unit.isEmpty() ? formatted : formatted + ' ' + unit;
    };
    auto *current = group(tr("Current state"));
    if (data.state.state != ReadState::Available) {
        QString state = readValue(data.state);
        if (data.state.error == EAGAIN) state = tr("Battery type or presence changed during collection; reload properties.");
        else if (data.state.error == EINVAL && data.values.contains("present"))
            state = tr("Malformed battery presence value: %1").arg(data.values.value("present").value);
        field(current, tr("Collection status"), state, source("type") + '\n' + source("present"));
        if (data.values.contains("type")) field(current, tr("Supply type"), textValue("type"), source("type"));
    } else {
        const Attribute present = data.values.value("present");
        const QString presence = present.state == ReadState::Available
            ? (present.value == "1" ? tr("Present") : tr("Absent")) : readValue(present);
        field(current, tr("Battery presence"), presence, source("present"));
        field(current, tr("Status"), textValue("status"), source("status"));
        field(current, tr("Charge percentage"), numberValue("capacity", 1, "%"), source("capacity"));
        field(current, tr("Voltage"), numberValue("voltage_now", 1000000, "V"), source("voltage_now"));
        field(current, tr("Current"), numberValue("current_now", 1000000, "A", true), source("current_now")
            + '\n' + tr("Driver-reported instantaneous current; the reported sign is preserved. Charging state is reported separately."));
        field(current, tr("Power"), numberValue("power_now", 1000000, "W", true), source("power_now")
            + '\n' + tr("Driver-reported instantaneous power; not calculated from voltage and current."));
        const auto retention = [&](const QString &prefix) {
            const Attribute full = data.values.value(prefix + "_full");
            const Attribute design = data.values.value(prefix + "_full_design");
            if (full.state == ReadState::NotApplicable || design.state == ReadState::NotApplicable)
                return readValue({ReadState::NotApplicable, {}, 0});
            if (full.state != ReadState::Available || design.state != ReadState::Available)
                return tr("Unavailable — requires compatible full and design readings");
            bool fullValid = false, designValid = false;
            const qlonglong fullValue = full.value.toLongLong(&fullValid);
            const qlonglong designValue = design.value.toLongLong(&designValid);
            if (!fullValid || !designValid || fullValue <= 0 || designValue <= 0)
                return tr("Unavailable — requires positive full and design readings");
            const double percent = 100.0 * static_cast<double>(fullValue) / static_cast<double>(designValue);
            return tr("%1 % (estimate)").arg(QLocale().toString(percent, 'f', 1));
        };
        const auto hasCapacity = [&](const QString &prefix) {
            for (const QString &suffix : {QStringLiteral("_full_design"), QStringLiteral("_full"), QStringLiteral("_now")}) {
                const Attribute value = data.values.value(prefix + suffix);
                if (value.state != ReadState::Unavailable || value.error != ENOENT) return true;
            }
            return false;
        };
        const bool energy = hasCapacity("energy"), charge = hasCapacity("charge");
        const auto capacityGroup = [&](const QString &prefix, const QString &title, double divisor, const QString &unit) {
            auto *form = group(title);
            field(form, tr("Design capacity"), numberValue(prefix + "_full_design", divisor, unit), source(prefix + "_full_design"));
            field(form, tr("Last full-charge capacity"), numberValue(prefix + "_full", divisor, unit), source(prefix + "_full")
                + '\n' + tr("Firmware-reported learned full capacity; it can depend on temperature, age and calibration."));
            field(form, tr("Remaining capacity"), numberValue(prefix + "_now", divisor, unit), source(prefix + "_now"));
            field(form, tr("Estimated capacity retention"), retention(prefix), source(prefix + "_full") + '\n'
                + source(prefix + "_full_design") + '\n' + tr("100 × full / design using matching units; not a complete battery health verdict. Values above 100% are not clamped."));
        };
        if (energy) capacityGroup("energy", tr("Energy capacity"), 1000000, "Wh");
        if (charge) capacityGroup("charge", tr("Charge capacity"), 1000, "mAh");
        if (!energy && !charge) {
            auto *form = group(tr("Capacity"));
            field(form, tr("Reported capacities"), tr("Unavailable — no energy or charge capacity attributes exposed"), devicePath);
            field(form, tr("Estimated capacity retention"), tr("Unavailable — requires compatible full and design readings"), devicePath);
        }
        auto *identity = group(tr("Battery information"));
        field(identity, tr("Cycle count"), numberValue("cycle_count", 1, QString()), source("cycle_count")
            + '\n' + tr("Reported full charge/discharge cycles. The kernel ABI defines zero as unavailable."));
        field(identity, tr("Chemistry"), textValue("technology"), source("technology"));
        field(identity, tr("Manufacturer"), textValue("manufacturer"), source("manufacturer"));
        field(identity, tr("Model"), textValue("model_name"), source("model_name"));
        field(identity, tr("Serial number"), textValue("serial_number"), source("serial_number"));
    }
    fieldsReport.alignCaptions();
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
