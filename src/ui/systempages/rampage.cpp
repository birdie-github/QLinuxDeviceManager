#include "rampage.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QFont>
#include <limits>

namespace {
QVector<QPair<QString, QString>> memoryFields()
{
    using D = SystemRamPage;
    return {{"PRESENT", D::tr("Presence")}, {"SIZE", D::tr("Capacity (total)")},
        {"VOLATILE_SIZE", D::tr("Volatile capacity")}, {"NON_VOLATILE_SIZE", D::tr("Nonvolatile capacity")},
        {"LOCATOR", D::tr("Device locator")}, {"BANK_LOCATOR", D::tr("Bank locator")},
        {"TYPE", D::tr("Memory type")}, {"FORM_FACTOR", D::tr("Form factor")},
        {"SPEED_MTS", D::tr("Rated transfer rate")}, {"CONFIGURED_SPEED_MTS", D::tr("Configured transfer rate")},
        {"MANUFACTURER", D::tr("Manufacturer")}, {"PART_NUMBER", D::tr("Part number")}};
}
QString memoryScope()
{
    return SystemRamPage::tr("Firmware-reported records; these do not establish removable modules, physical slots, "
        "memory channels, upgradeability or active ECC. Transfer rates are firmware declarations, not live measurements.");
}
}

SystemRamPage::SystemRamPage(QWidget *parent) : QWidget(parent)
{
    const auto page = reportPageLayout(this);
    auto *fields = new QWidget(this);
    summary_ = new QGridLayout(fields);
    summary_->setContentsMargins(0, 0, 0, 0);
    summary_->setColumnStretch(1, 1);
    page.contents->addWidget(fields);
    const auto field = [&](int row, const QString &caption) {
        auto *name = new QLabel(caption, fields);
        name->setTextFormat(Qt::PlainText);
        auto *value = propertyValueLabel(tr("Not collected"), fields);
        summary_->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
        summary_->addWidget(value, row, 1);
        return value;
    };
    physical_ = field(0, tr("Physical RAM"));
    usable_ = field(1, tr("Usable RAM"));
    auto *memory = new QWidget(fields);
    memoryForm_ = new QGridLayout(memory);
    memoryForm_->setContentsMargins(0, 0, 0, 0);
    memoryForm_->setColumnStretch(0, 1);
    summary_->addWidget(memory, 2, 0, 1, 2);
    page.contents->addStretch();
    rebuildMemory();
}
int SystemRamPage::captionWidthHint() const
{
    int width = 0;
    for (const QString &caption : {tr("Physical RAM"), tr("Usable RAM")})
        width = qMax(width, QLabel(caption).sizeHint().width());
    for (const auto &field : memoryFields())
        width = qMax(width, QLabel(field.second).sizeHint().width());
    return width;
}
void SystemRamPage::setCaptionWidth(int width)
{
    summary_->setColumnMinimumWidth(0, width);
}
void SystemRamPage::setSnapshot(const SystemProperties &snapshot)
{
    snapshot_ = snapshot;
    for (const QString &key : {QStringLiteral("physical_memory"), QStringLiteral("memory")}) {
        QLabel *label = key == "memory" ? usable_ : physical_;
        label->setText(summaryValue(key));
        QString source = snapshot_.sources.value(key);
        if (key == "memory")
            source += '\n' + tr("System reserved is reported physical RAM minus Linux MemTotal. "
                                 "It includes all memory unavailable to Linux, not just firmware reservations. "
                                 "It is omitted if physical capacity is unavailable or smaller than usable RAM.");
        label->setToolTip(propertyToolTip(source));
    }
    rebuildMemory();
}
QString SystemRamPage::summaryValue(const QString &key) const
{
    const Attribute value = snapshot_.values.value(key);
    if (value.state != ReadState::Available) return propertyReadValue(value);
    constexpr qulonglong gib = 1024ULL * 1024 * 1024;
    constexpr qulonglong mib = 1024ULL * 1024;
    const qulonglong bytes = value.value.toULongLong();
    const int precision = key == "physical_memory" && bytes % gib == 0 ? 0 : 2;
    QString text = tr("%1 GiB").arg(QLocale().toString(bytes / static_cast<double>(gib), 'f', precision));
    const Attribute physical = snapshot_.values.value("physical_memory");
    if (key == "memory" && physical.state == ReadState::Available) {
        const qulonglong installed = physical.value.toULongLong();
        if (installed >= bytes)
            text += tr(" (%1 MiB is system reserved)")
                .arg(QLocale().toString((installed - bytes) / static_cast<double>(mib), 'f', 0));
    }
    return text;
}
QString SystemRamPage::devicesText() const
{
    QStringList lines {memoryScope()};
    const auto &memory = snapshot_.memoryDevices;
    if (memory.status.state != ReadState::Available) lines.append(propertyReadValue(memory.status));
    else if (memory.devices.isEmpty()) lines.append(tr("No memory-device records reported"));
    else for (const FirmwareMemoryDevice &device : memory.devices) {
        lines.append(tr("Memory device %1 (firmware record %2)").arg(device.index + 1).arg(device.index));
        for (const auto &field : memoryFields())
            lines.append(field.second + ": " + memoryValue(field.first, device.values.value(field.first)));
    }
    return lines.join('\n');
}
QString SystemRamPage::copyText() const
{
    return tr("Physical RAM") + ": " + summaryValue("physical_memory") + '\n'
        + tr("Usable RAM") + ": " + summaryValue("memory") + '\n'
        + tr("RAM") + ": " + devicesText();
}
QString SystemRamPage::memoryValue(const QString &key, const Attribute &value) const
{
    if (key == "PRESENT" && value.state == ReadState::Unavailable)
        return tr("Unknown (no explicit presence flag)");
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "PRESENT") return value.value == "0" ? tr("No device reported present") : tr("Device reported present");
    if (key == "SPEED_MTS" || key == "CONFIGURED_SPEED_MTS")
        return tr("%1 MT/s").arg(QLocale().toString(value.value.toULongLong()));
    if (key == "SIZE" || key == "VOLATILE_SIZE" || key == "NON_VOLATILE_SIZE") {
        const qulonglong bytes = value.value.toULongLong();
        if (bytes <= static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
            return QLocale().formattedDataSize(static_cast<qlonglong>(bytes), 2, QLocale::DataSizeIecFormat);
        return tr("%1 bytes").arg(QLocale().toString(bytes));
    }
    return value.value;
}
void SystemRamPage::rebuildMemory()
{
    while (QLayoutItem *item = memoryForm_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto *heading = new QLabel(tr("Memory Devices"), memoryForm_->parentWidget());
    QFont font = heading->font(); font.setBold(true); heading->setFont(font);
    heading->setToolTip(memoryScope());
    memoryForm_->addWidget(heading, 0, 0);
    const FirmwareMemoryInformation &memory = snapshot_.memoryDevices;
    if (memory.status.state != ReadState::Available || memory.devices.isEmpty()) {
        auto *message = new QLabel(memory.status.state != ReadState::Available
            ? propertyReadValue(memory.status) : tr("No memory-device records reported"), memoryForm_->parentWidget());
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        message->setToolTip(snapshot_.sources.value("memory_devices") + '\n' + memoryScope());
        memoryForm_->addWidget(message, 1, 0);
        return;
    }
    auto *tabs = new QTabWidget(memoryForm_->parentWidget());
    tabs->setUsesScrollButtons(true);
    memoryForm_->addWidget(tabs, 1, 0);
    for (const FirmwareMemoryDevice &device : memory.devices) {
        auto *table = new ReportTable(tabs);
        table->setColumnCount(2);
        table->setHorizontalHeaderLabels({tr("Property"), tr("Value")});
        const auto fields = memoryFields();
        table->setRowCount(fields.size());
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(device.index);
        for (int row = 0; row < fields.size(); ++row) {
            const auto &field = fields[row];
            auto *name = new QTableWidgetItem(field.second);
            auto *value = new QTableWidgetItem(memoryValue(field.first, device.values.value(field.first)));
            const QString hint = QStringLiteral("libudev: dmi/id ") + prefix + field.first + '\n' + memoryScope();
            name->setToolTip(hint); value->setToolTip(hint);
            table->setItem(row, 0, name);
            table->setItem(row, 1, value);
        }
        table->fitContents();
        const int tab = tabs->addTab(table, tr("Chip %1").arg(device.index + 1));
        tabs->setTabToolTip(tab, tr("Memory device %1 (firmware record %2)")
            .arg(device.index + 1).arg(device.index) + '\n' + memoryScope());
    }
}
