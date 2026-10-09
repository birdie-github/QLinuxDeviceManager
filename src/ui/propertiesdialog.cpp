#include "propertiesdialog.h"
#include "devicelabel.h"
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>
#include <utility>

namespace {
QString readValue(const Attribute &a)
{
    switch (a.state) {
    case ReadState::Available:
        return a.value.isEmpty() ? QCoreApplication::translate("PropertiesDialog", "Empty value") : a.value;
    case ReadState::Unavailable: return QCoreApplication::translate("PropertiesDialog", "Unavailable");
    case ReadState::PermissionDenied: return QCoreApplication::translate("PropertiesDialog", "Permission denied");
    case ReadState::Removed: return QCoreApplication::translate("PropertiesDialog", "Device removed");
    case ReadState::Unsupported: return QCoreApplication::translate("PropertiesDialog", "Unsupported in this build");
    case ReadState::NotApplicable: return QCoreApplication::translate("PropertiesDialog", "Not applicable");
    case ReadState::Error: return QCoreApplication::translate("PropertiesDialog", "Read error (errno %1)").arg(a.error);
    }
    return {};
}
QString hexDump(const QByteArray &bytes)
{
    QStringList lines;
    for (int offset = 0; offset < bytes.size(); offset += 16) {
        QStringList hex;
        QString printable;
        for (int i = offset; i < qMin(offset + 16, static_cast<int>(bytes.size())); ++i) {
            const unsigned char byte = static_cast<unsigned char>(bytes.at(i));
            hex.append(QStringLiteral("%1").arg(byte, 2, 16, QLatin1Char('0')));
            printable += byte >= 32 && byte <= 126 ? QLatin1Char(static_cast<char>(byte)) : QLatin1Char('.');
        }
        lines.append(QStringLiteral("%1  %2  %3").arg(offset, 8, 16, QLatin1Char('0'))
            .arg(hex.join(' ').leftJustified(47), printable));
    }
    return lines.join('\n');
}
void clearForm(QFormLayout *form)
{
    while (form->rowCount()) form->removeRow(0);
}
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setFocusPolicy(Qt::StrongFocus);
    return label;
}
}

PropertiesDialog::PropertiesDialog(const Device &device, QWidget *parent) : QDialog(parent)
{
    snapshot_.device = device;
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("%1 — Properties").arg(deviceDisplayName(device)));
    resize(680, 560);
    auto *layout = new QVBoxLayout(this);
    banner_ = plainLabel(tr("Loading properties…"), this);
    layout->addWidget(banner_);
    auto *tabs = new QTabWidget(this);
    layout->addWidget(tabs, 1);
    const auto addForm = [tabs](const QString &title) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto *page = new QWidget(scroll);
        auto *form = new QFormLayout(page);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
        return form;
    };
    general_ = addForm(tr("General"));
    driver_ = addForm(tr("Driver"));
    auto *details = new QWidget(tabs);
    auto *detailsLayout = new QVBoxLayout(details);
    auto *propertyLabel = new QLabel(tr("&Property:"), details);
    property_ = new QComboBox(details);
    property_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    property_->setMinimumContentsLength(24);
    propertyLabel->setBuddy(property_);
    detailsLayout->addWidget(propertyLabel);
    detailsLayout->addWidget(property_);
    value_ = new QPlainTextEdit(details);
    value_->setReadOnly(true);
    detailsLayout->addWidget(value_, 1);
    source_ = plainLabel(QString(), details);
    detailsLayout->addWidget(source_);
    advanced_ = new QCheckBox(tr("Show &advanced properties"), details);
    detailsLayout->addWidget(advanced_);
    auto *copyLayout = new QHBoxLayout;
    auto *copySelected = new QPushButton(tr("Copy selection"), details);
    auto *copyValue = new QPushButton(tr("Copy value"), details);
    auto *copyEverything = new QPushButton(tr("Copy all values"), details);
    copySelected->setEnabled(false);
    connect(value_, &QPlainTextEdit::copyAvailable, copySelected, &QPushButton::setEnabled);
    connect(copySelected, &QPushButton::clicked, value_, &QPlainTextEdit::copy);
    connect(copyValue, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(value_->toPlainText()); });
    connect(copyEverything, &QPushButton::clicked, this, &PropertiesDialog::copyAll);
    for (auto *button : {copySelected, copyValue, copyEverything}) copyLayout->addWidget(button);
    copyLayout->addStretch();
    detailsLayout->addLayout(copyLayout);
    tabs->addTab(details, tr("Details"));
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    reload_ = buttons->addButton(tr("Reload properties"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(reload_, &QPushButton::clicked, this, &PropertiesDialog::reloadRequested);
    connect(property_, &QComboBox::currentIndexChanged, this, &PropertiesDialog::showDetail);
    connect(advanced_, &QCheckBox::toggled, this, &PropertiesDialog::rebuildDetails);
    layout->addWidget(buttons);
    rebuild();
}
void PropertiesDialog::setBusy(bool busy)
{
    reload_->setEnabled(!busy && !removed_);
    if (busy && !removed_) banner_->setText(tr("Loading properties…"));
}
void PropertiesDialog::acceptResult(DeviceProperties result)
{
    if (removed_ || result.device.path != device().path || result.device.generation != device().generation) return;
    setBusy(false);
    if (result.state == ReadState::Removed) { markRemoved(); return; }
    if (result.state != ReadState::Available) {
        banner_->setText(tr("Properties could not be refreshed: %1. Showing the previous snapshot.")
            .arg(readValue({result.state, {}, result.error})));
        return;
    }
    snapshot_ = std::move(result);
    banner_->setText(tr("Read-only snapshot. Presence and driver binding do not establish hardware health."));
    rebuild();
}
void PropertiesDialog::reconcileInstance(const Device *current)
{
    if (removed_) return;
    if (!current || current->generation != device().generation || current->incarnation != device().incarnation) {
        markRemoved();
        return;
    }
    // Keep the same-instance snapshot until an explicit property reload.
    // An inventory refresh must never silently retarget this dialog.
}
void PropertiesDialog::markRemoved()
{
    removed_ = true;
    reload_->setEnabled(false);
    banner_->setText(tr("Device removed or replaced. This dialog is a read-only snapshot; close it and reopen Properties for the current device."));
    rebuild();
}
void PropertiesDialog::rebuild()
{
    entries_.clear();
    const Device &d = device();
    const bool efi = d.subsystem == "efivarfs";
    const Attribute notApplicable {ReadState::NotApplicable, {}, 0};
    const auto add = [this](const QString &id, const QString &label, const QString &value,
                           const QString &source = QString(), bool advanced = false, int tab = 2) {
        entries_.append({id, label, value, source, advanced, tab});
    };
    const auto raw = [this](const QString &id) { return snapshot_.values.value(id); };
    const auto addRaw = [&](const QString &id, const QString &label, int tab, const Attribute &fallback = Attribute()) {
        const Attribute a = snapshot_.values.value(id, fallback);
        add(id, label, readValue(a), snapshot_.sources.value(id), false, tab);
    };
    add("name", tr("Device name"), deviceDisplayName(d), d.nameSource, false, 0);
    add("category", tr("Category"), categoryLabel(d.category), tr("Application grouping"), false, 0);
    Attribute manufacturer;
    QString manufacturerSource;
    if (efi) manufacturer = notApplicable;
    else {
        for (const QString &id : {QStringLiteral("sysfs/manufacturer"), QStringLiteral("sysfs/vendor_name"),
                                 QStringLiteral("udev/ID_VENDOR_FROM_DATABASE"), QStringLiteral("udev/ID_VENDOR")}) {
            const Attribute candidate = raw(id);
            if (candidate.state == ReadState::Available && !candidate.value.isEmpty()) {
                manufacturer = candidate; manufacturerSource = snapshot_.sources.value(id); break;
            }
            if (candidate.state == ReadState::PermissionDenied || candidate.state == ReadState::Error) {
                manufacturer = candidate; manufacturerSource = snapshot_.sources.value(id); break;
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
    add("manufacturer", tr("Manufacturer"), readValue(manufacturer), manufacturerSource, false, 0);
    Attribute bus = raw("udev/ID_BUS");
    if (bus.state != ReadState::Available) bus = raw("driver/bus");
    if (efi) bus = notApplicable;
    add("bus", tr("Bus / subsystem"), efi ? readValue(bus)
        : bus.state == ReadState::Available ? bus.value + " / " + d.subsystem : d.subsystem,
        tr("Device udev metadata and direct driver bus"), false, 0);
    add("location", tr("Location"), efi ? d.parentPath : d.path, tr("Kernel path"), false, 0);
    QString status;
    if (removed_) status = tr("Removed or replaced; showing a snapshot.");
    else if (snapshot_.state != ReadState::Available) status = tr("Present in the last inventory; properties have not been verified.");
    else {
        status = tr("Present at the last property read.");
        if (!efi) {
            const Attribute binding = raw("driver/name");
            status += '\n' + (binding.state == ReadState::Available
                ? binding.value.isEmpty() ? tr("No directly bound driver; this alone is not a fault.") : tr("Driver bound: %1").arg(binding.value)
                : tr("Driver binding: %1").arg(readValue(binding)));
            const Attribute authorization = raw("sysfs/authorized");
            if (authorization.state == ReadState::Available && authorization.value == "0") status += '\n' + tr("USB device/interface is unauthorized.");
            else if (authorization.state == ReadState::Available && authorization.value == "1") status += '\n' + tr("USB device/interface is authorized.");
            else if (authorization.state == ReadState::PermissionDenied || authorization.state == ReadState::Error)
                status += '\n' + tr("USB authorization: %1").arg(readValue(authorization));
        }
    }
    add("status", tr("Device status"), status, tr("Observed presence, binding and USB authorization only"), false, 0);
    Attribute binding = snapshot_.values.value("driver/name", d.driver.isEmpty() ? Attribute()
        : Attribute{ReadState::Available, d.driver, 0});
    if (efi) binding = notApplicable;
    const QString driverName = !efi && binding.state == ReadState::Available && binding.value.isEmpty()
        ? tr("No directly bound driver") : readValue(binding);
    add("driver/name", tr("Bound kernel driver"), driverName, efi ? QString() : d.path + "/driver", false, 0);
    QStringList identifiers;
    for (const QString &key : {QStringLiteral("PCI_ID"), QStringLiteral("PCI_SUBSYS_ID"), QStringLiteral("PRODUCT"),
                             QStringLiteral("ID_VENDOR_ID"), QStringLiteral("ID_MODEL_ID"), QStringLiteral("ID_SERIAL_SHORT")}) {
        const Attribute a = snapshot_.values.value("udev/" + key,
            d.properties.contains(key) ? Attribute{ReadState::Available, d.properties.value(key), 0} : Attribute());
        if (a.state == ReadState::Available && !a.value.isEmpty()) identifiers.append(key + ": " + a.value);
    }
    for (const QString &key : {QStringLiteral("vendor"), QStringLiteral("device"), QStringLiteral("idVendor"),
                             QStringLiteral("idProduct"), QStringLiteral("serial"), QStringLiteral("hid"), QStringLiteral("uid")}) {
        const Attribute a = raw("sysfs/" + key);
        if (a.state != ReadState::Unavailable && !(a.state == ReadState::Available && a.value.isEmpty()))
            identifiers.append(key + ": " + readValue(a));
    }
    add("identifiers", tr("Hardware identifiers"), efi ? readValue(notApplicable)
        : identifiers.isEmpty() ? tr("Unavailable") : identifiers.join('\n'), tr("Selected udev and direct sysfs identifiers"), false, 0);
    add("driver/binding", tr("Bound kernel driver"), driverName, efi ? QString() : d.path + "/driver", false, 1);
    const Attribute moduleFallback = efi || (binding.state == ReadState::Available && binding.value.isEmpty())
        ? notApplicable : Attribute();
    addRaw("driver/bus", tr("Driver bus"), 1, moduleFallback);
    addRaw("module/name", tr("Owning kernel module"), 1, moduleFallback);
    const Attribute type = snapshot_.values.value("module/type", moduleFallback);
    add("module/type", tr("Built-in or modular"), type.state == ReadState::Available ? tr("Modular (kernel initstate evidence)")
        : type.state == ReadState::NotApplicable || type.state == ReadState::PermissionDenied || type.state == ReadState::Error
            ? readValue(type) : tr("Undetermined; a missing module link/version does not prove a built-in driver."),
        snapshot_.sources.value("module/type"), false, 1);
    addRaw("module/runtime_version", tr("Running module version"), 1, moduleFallback);
    const QStringList fields {"filename", "version", "description", "author", "license", "firmware"};
    const QStringList labels {tr("Installed module filename"), tr("Installed module version"), tr("Installed module description"),
        tr("Installed module author"), tr("Installed module license"), tr("Declared firmware names")};
    for (int i = 0; i < fields.size(); ++i) addRaw("module/" + fields[i], labels[i], 1, moduleFallback);
    addRaw("sysfs/firmware_rev", tr("Reported device firmware revision"), 1,
        d.subsystem == "nvme" ? Attribute() : notApplicable);
    add("module/note", tr("Metadata scope"), efi ? readValue(notApplicable)
        : tr("Installed metadata may differ from code already loaded before an update. Declared firmware names do not prove firmware is loaded or report its version."), {}, false, 1);
    // Interpreted facts plus curated raw values, keeping provenance beside each.
    add("parent", tr("Parent device path"), d.parentPath.isEmpty() ? tr("Unavailable") : d.parentPath, tr("Recorded ancestry"), efi);
    add("raw_name", tr("Original name"), d.name, d.nameSource, true);
    add("name_source", tr("Name source"), d.nameSource, {}, true);
    add("sysname", tr("Kernel name"), d.sysname, {}, true);
    add("path", tr("Full kernel path"), d.path, {}, true);
    if (!efi) add("driver/path", tr("Direct driver path"), readValue(raw("driver/path")),
        snapshot_.sources.value("driver/path"), true);
    QStringList keys = snapshot_.values.keys();
    keys.sort();
    for (const QString &key : keys) {
        if (!key.startsWith("udev/") && !key.startsWith("sysfs/")) continue;
        if (key == "sysfs/firmware_rev") continue; // Already exposed on the Driver tab.
        add(key, key, readValue(raw(key)), snapshot_.sources.value(key), true);
    }
    keys = d.properties.keys();
    keys.sort();
    for (const QString &key : keys) {
        if (snapshot_.values.contains("udev/" + key)) continue;
        add("inventory/" + key, tr("Inventory: %1").arg(key), d.properties.value(key),
            d.propertySources.value(key, QStringLiteral("inventory udev metadata")), true);
    }
    keys = d.attributes.keys();
    keys.sort();
    for (const QString &key : keys)
        add("inventory/sysfs/" + key, tr("Inventory attribute: %1").arg(key), readValue(d.attributes.value(key)), tr("Last inventory; see name source for derived naming metadata"), true);
    if (efi) {
        const Attribute hex = raw("efi/hex");
        QString dump = readValue(hex);
        if (hex.state == ReadState::Available) {
            dump = snapshot_.efiBytes.isEmpty() ? tr("Empty file") : hexDump(snapshot_.efiBytes);
            if (snapshot_.efiTruncated) dump += '\n' + tr("Truncated: first 65536 bytes shown.");
        }
        add("efi/hex", tr("UEFI variable — hex dump"), dump,
            tr("Complete efivarfs file bytes, including the attribute prefix; contents are not decoded."));
    }
    clearForm(general_);
    clearForm(driver_);
    for (const Entry &entry : entries_) {
        if (entry.tab > 1) continue;
        QFormLayout *form = entry.tab == 0 ? general_ : driver_;
        auto *label = plainLabel(entry.value, this);
        label->setToolTip(entry.source.toHtmlEscaped());
        form->addRow(entry.label + ':', label);
    }
    rebuildDetails();
}
void PropertiesDialog::rebuildDetails()
{
    const QString previous = property_->currentData().toString();
    const QSignalBlocker blocker(property_);
    property_->clear();
    for (const Entry &entry : entries_)
        if (!entry.advanced || advanced_->isChecked()) property_->addItem(entry.label, entry.id);
    const int index = property_->findData(previous);
    property_->setCurrentIndex(index < 0 ? 0 : index);
    showDetail();
}
void PropertiesDialog::showDetail()
{
    const QString id = property_->currentData().toString();
    for (const Entry &entry : entries_) {
        if (entry.id != id) continue;
        value_->setFont(id == "efi/hex" ? QFontDatabase::systemFont(QFontDatabase::FixedFont) : QApplication::font());
        value_->setLineWrapMode(id == "efi/hex" ? QPlainTextEdit::NoWrap : QPlainTextEdit::WidgetWidth);
        value_->setPlainText(entry.value);
        source_->setText(entry.source.isEmpty() ? QString() : tr("Source: %1").arg(entry.source));
        return;
    }
    value_->clear();
    source_->clear();
}
void PropertiesDialog::copyAll()
{
    QStringList values;
    for (const Entry &entry : entries_) {
        if (entry.advanced && !advanced_->isChecked()) continue;
        QString text = entry.label + ":\n" + entry.value;
        if (!entry.source.isEmpty()) text += '\n' + tr("Source: %1").arg(entry.source);
        values.append(text);
    }
    QApplication::clipboard()->setText(values.join("\n\n"));
}
