#include "bluetoothpage.h"
#include "bluetoothproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"
#include "reporttable.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

BluetoothPage::BluetoothPage(QWidget *parent) : QWidget(parent)
{
    PropertyPageOptions bluetoothOptions;
    bluetoothOptions.frame = QFrame::NoFrame;
    bluetoothOptions.contentConstraint = QLayout::SetDefaultConstraint;
    bluetoothOptions.headingPolicy = QSizePolicy::Maximum;
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ Bluetooth controller and known-device snapshot"),
        tr("Reload properties to refresh. Known devices are current BlueZ objects, not a pairing history. "
        "Signal and advertising data are last reported values. Services are advertised/supported, not necessarily active. "
        "No scanning, pairing, connection changes or remote characteristic reads are performed."),
        tr("Copy Bluetooth properties"), bluetoothOptions);
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void BluetoothPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void BluetoothPage::setSnapshot(const BluetoothProperties &bt, const QString &devicePath)
{
    clear();
    QStringList copy {tr("Bluetooth — read-only snapshot"), devicePath};
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title, groups_->parentWidget());
        auto *form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        groups_->addWidget(box);
        copy.append('\n' + title);
        return form;
    };
    const auto renderField = [](QFormLayout *form, const QString &caption, const QString &text, const QString &source) {
        auto *label = plainLabel(caption + ':', form->parentWidget());
        auto *value = plainLabel(text, form->parentWidget());
        label->setToolTip(propertyToolTip(source));
        value->setToolTip(propertyToolTip(source));
        form->addRow(label, value);
    };
    const auto renderServices = [](QFormLayout *form, const Attribute &value, const QString &source) {
        if (value.state != ReadState::Available || value.value.isEmpty()) {
            auto *status = plainLabel(bluetoothPropertyText("UUIDs", value), form->parentWidget());
            status->setToolTip(propertyToolTip(source));
            form->addRow(status);
            return;
        }
        auto *table = new ReportTable(0, 2, form->parentWidget());
        table->setHorizontalHeaderLabels({tr("Service"), tr("UUID")});
        table->setSelectionBehavior(QAbstractItemView::SelectRows);
        for (const QString &uuid : value.value.split('\n', Qt::SkipEmptyParts)) {
            const int row = table->rowCount();
            table->insertRow(row);
            const QString name = bluetoothServiceName(uuid);
            auto *service = new QTableWidgetItem(name.isEmpty() ? tr("Unknown service") : name);
            auto *identifier = new QTableWidgetItem(uuid);
            const QString hint = propertyToolTip(source + '\n' + uuid);
            service->setToolTip(hint);
            identifier->setToolTip(hint);
            table->setItem(row, 0, service);
            table->setItem(row, 1, identifier);
        }
        form->addRow(table);
    };
    const auto field = [&](QFormLayout *form, const QString &caption, const QString &key,
                           const QString &fallback = QString()) {
        const QString selected = !fallback.isEmpty() && bt.values.value(key).state != ReadState::Available
            && bt.values.value(fallback).state == ReadState::Available ? fallback : key;
        const Attribute raw = bt.values.value(selected);
        const QString text = bluetoothPropertyText(selected, raw);
        QString source = bt.sources.value(selected);
        if (raw.state == ReadState::Available) source += tr("\nRaw value: %1").arg(raw.value);
        renderField(form, caption, text, source);
        copy.append(caption + ": " + text + (source.isEmpty() ? QString() : '\n' + tr("Source: %1").arg(source)));
    };
    auto *controller = group(tr("Controller"));
    field(controller, tr("Interface"), "Interface");
    field(controller, tr("Transport"), "Transport");
    field(controller, tr("Bluetooth address"), "Address", "KernelAddress");
    field(controller, tr("Address type"), "AddressType");
    field(controller, tr("Core specification version"), "Version", "ManagementVersion");
    field(controller, tr("SIG manufacturer"), "Manufacturer", "ManagementManufacturer");
    field(controller, tr("Controller name"), "Name", "ManagementName");
    field(controller, tr("Alias"), "Alias");
    field(controller, tr("Device class"), "Class", "ManagementClass");
    field(controller, tr("Supported roles"), "Roles");
    controller->addRow(plainLabel(tr("The Core version does not guarantee every optional feature in that specification."), controller->parentWidget()));
    auto *localServices = group(tr("Local Services"));
    renderServices(localServices, bt.values.value("UUIDs"), bt.sources.value("UUIDs"));
    copy.append(bluetoothPropertyText("UUIDs", bt.values.value("UUIDs"))
        + '\n' + tr("Source: %1").arg(bt.sources.value("UUIDs")));
    auto *radio = group(tr("Radio state"));
    for (const auto &entry : QVector<QPair<QString, QString>> {
             {"Powered", tr("Powered")}, {"Discoverable", tr("Discoverable")}, {"Pairable", tr("Pairable")},
             {"Discovering", tr("Discovering")}, {"SoftBlocked", tr("Software blocked")}, {"HardBlocked", tr("Hardware blocked")}})
        field(radio, entry.second, entry.first, entry.first == "Powered" ? QStringLiteral("enabled/0")
            : entry.first == "Discoverable" ? QStringLiteral("enabled/3") : QString());
    const QString bluezStatus = bt.bluezState.state == ReadState::Available ? tr("Available")
        : bt.bluezState.state == ReadState::Unsupported ? tr("BlueZ service unavailable or D-Bus support disabled")
        : propertyReadValue(bt.bluezState);
    renderField(radio, tr("BlueZ metadata"), bluezStatus, bt.bluezPath);
    copy.append(tr("BlueZ metadata: %1").arg(bluezStatus));
    auto *features = group(tr("Kernel-reported capabilities and settings"));
    if (bt.managementState.state == ReadState::Available) {
        const QVector<int> bits {6, 7, 9, 10, 11, 13, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25};
        auto *table = new ReportTable(bits.size(), 3, features->parentWidget());
        table->setHorizontalHeaderLabels({tr("Setting"), tr("Supported"), tr("Enabled")});
        for (int row = 0; row < bits.size(); ++row) {
            const int bit = bits[row];
            const QString supported = "supported/" + QString::number(bit), enabled = "enabled/" + QString::number(bit);
            const QStringList values {bluetoothSettingName(bit), bluetoothPropertyText(supported, bt.values.value(supported)),
                                     bluetoothPropertyText(enabled, bt.values.value(enabled))};
            for (int column = 0; column < 3; ++column) {
                auto *item = new QTableWidgetItem(values[column]);
                item->setToolTip(propertyToolTip(bt.sources.value(supported)));
                table->setItem(row, column, item);
            }
            copy.append(values.join(" | "));
        }
        features->addRow(table);
        field(features, tr("Supported settings mask"), "SupportedMask");
        field(features, tr("Current settings mask"), "CurrentMask");
    } else {
        const QString status = propertyReadValue(bt.managementState);
        renderField(features, tr("Management API"), status,
            tr("Read Controller Information only. Access commonly requires CAP_NET_ADMIN (root/sudo). "
               "Missing access does not mean the controller lacks these features."));
        copy.append(tr("Management API: %1").arg(status));
    }
    const QVector<QPair<QString, QString>> remoteFields {
        {"Name", tr("Name")}, {"Alias", tr("Alias")}, {"Address", tr("Address")}, {"AddressType", tr("Address type")},
        {"Connected", tr("Connected")}, {"Paired", tr("Paired")}, {"Bonded", tr("Bonded")}, {"Trusted", tr("Trusted")},
        {"Blocked", tr("Blocked")}, {"WakeAllowed", tr("Wake allowed")}, {"Class", tr("Device class")},
        {"Appearance", tr("BLE appearance code")}, {"UUIDs", tr("Services")}, {"ServicesResolved", tr("Services resolved")},
        {"RSSI", tr("Last reported RSSI")}, {"TxPower", tr("Advertised transmit power")}, {"ManufacturerData", tr("Advertising manufacturer data")}};
    auto *known = group(tr("Known devices"));
    if (bt.bluezState.state != ReadState::Available) {
        known->addRow(plainLabel(bluezStatus, known->parentWidget()));
    } else if (bt.devices.isEmpty()) {
        known->addRow(plainLabel(tr("No devices currently known to BlueZ for this controller."), known->parentWidget()));
        copy.append(tr("No known devices"));
    } else {
        auto *selector = new QComboBox(known->parentWidget());
        selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        selector->setMinimumContentsLength(24);
        for (const auto &remote : bt.devices) {
            QString name = remote.values.value("Alias").value;
            if (name.isEmpty()) name = remote.values.value("Name").value;
            if (name.isEmpty()) name = remote.values.value("Address").value;
            selector->addItem(name + " — " + remote.values.value("Address").value);
            copy.append('\n' + name);
            for (const auto &entry : remoteFields)
                copy.append(entry.second + ": " + bluetoothPropertyText(entry.first, remote.values.value(entry.first))
                    + '\n' + tr("Source: %1").arg(remote.path + " : org.bluez.Device1." + entry.first));
        }
        known->addRow(tr("Device:"), selector);
        auto *page = new QWidget(known->parentWidget());
        auto *details = new QFormLayout(page);
        details->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        details->setRowWrapPolicy(QFormLayout::WrapLongRows);
        known->addRow(page);
        const auto show = [details, devices = bt.devices, remoteFields, renderField, renderServices](int index) {
            while (details->rowCount()) details->removeRow(0);
            if (index < 0 || index >= devices.size()) return;
            const auto &remote = devices[index];
            for (const auto &entry : remoteFields) {
                const Attribute value = remote.values.value(entry.first);
                const QString source = remote.path + " : org.bluez.Device1." + entry.first
                    + (value.state == ReadState::Available ? "\n" + value.value : QString());
                if (entry.first == "UUIDs") {
                    auto *box = new QGroupBox(tr("Services"), details->parentWidget());
                    auto *services = new QFormLayout(box);
                    renderServices(services, value, source);
                    details->addRow(box);
                } else renderField(details, entry.second, bluetoothPropertyText(entry.first, value), source);
            }
        };
        connect(selector, &QComboBox::currentIndexChanged, selector, show);
        show(selector->currentIndex());
    }
    if (bt.devicesTruncated) {
        known->addRow(plainLabel(tr("Known-device data was truncated (limit: 256 devices or 2 MiB of text)."), known->parentWidget()));
        copy.append(tr("Known-device data truncated"));
    }
    copy.append(tr("Known devices are current BlueZ objects, not a complete history. Signal and advertising data may be stale. "
                   "Services listed here need not be active. No discovery or connection changes were requested."));
    copyText_ = copy.join('\n');
    groups_->addStretch();
}
