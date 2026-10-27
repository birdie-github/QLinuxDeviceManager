#include "rampage.h"
#include "systemreport.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QGridLayout>
#include <QLabel>
#include <QTabBar>
#include <QVBoxLayout>
#include <QFont>


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
    physical_ = field(0, systemFieldCaption(SystemPage::Ram, "physical_memory"));
    usable_ = field(1, systemFieldCaption(SystemPage::Ram, "memory"));
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
    for (const QString &caption : {systemFieldCaption(SystemPage::Ram, "physical_memory"), systemFieldCaption(SystemPage::Ram, "memory")})
        width = qMax(width, QLabel(caption).sizeHint().width());
    for (const auto &field : systemMemoryFields())
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
    return systemRamSummaryValue(snapshot_, key);
}
QString SystemRamPage::devicesText() const
{
    QStringList lines {systemMemoryScope()};
    const auto &memory = snapshot_.memoryDevices;
    if (memory.status.state != ReadState::Available || memory.devices.isEmpty())
        lines.append(systemMemoryStatus(memory));
    else for (const FirmwareMemoryDevice &device : memory.devices) {
        lines.append(tr("Memory device %1 (firmware record %2)").arg(device.index + 1).arg(device.index));
        for (const auto &field : systemMemoryFields())
            lines.append(field.second + ": " + memoryValue(field.first, device.values.value(field.first)));
    }
    return lines.join('\n');
}
QString SystemRamPage::copyText() const
{
    return systemFieldCaption(SystemPage::Ram, "physical_memory") + ": " + summaryValue("physical_memory") + '\n'
        + systemFieldCaption(SystemPage::Ram, "memory") + ": " + summaryValue("memory") + '\n'
        + tr("RAM") + ": " + devicesText();
}
QString SystemRamPage::memoryValue(const QString &key, const Attribute &value) const
{
    return systemMemoryValue(key, value);
}
void SystemRamPage::rebuildMemory()
{
    while (QLayoutItem *item = memoryForm_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    auto *heading = new QLabel(systemMemoryHeading(), memoryForm_->parentWidget());
    QFont font = heading->font(); font.setBold(true); heading->setFont(font);
    heading->setToolTip(systemMemoryScope());
    memoryForm_->addWidget(heading, 0, 0);
    const FirmwareMemoryInformation &memory = snapshot_.memoryDevices;
    if (memory.status.state != ReadState::Available || memory.devices.isEmpty()) {
        auto *message = new QLabel(systemMemoryStatus(memory), memoryForm_->parentWidget());
        message->setTextFormat(Qt::PlainText);
        message->setWordWrap(true);
        message->setToolTip(snapshot_.sources.value("memory_devices") + '\n' + systemMemoryScope());
        memoryForm_->addWidget(message, 1, 0);
        return;
    }
    auto *chips = new QWidget(memoryForm_->parentWidget());
    auto *layout = new QVBoxLayout(chips);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *tabs = new QTabBar(chips);
    tabs->setExpanding(false);
    tabs->setUsesScrollButtons(true);
    layout->addWidget(tabs);
    // Use one table: hidden chip tables otherwise retain heights calculated at
    // obsolete widths, which QTabWidget includes in the whole stack's minimum.
    auto *table = new ReportTable(chips);
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels(systemPropertyHeaders());
    table->setRowCount(systemMemoryFields().size());
    layout->addWidget(table);
    memoryForm_->addWidget(chips, 1, 0);
    for (const FirmwareMemoryDevice &device : memory.devices) {
        const int tab = tabs->addTab(systemMemoryTitle(device.index));
        tabs->setTabToolTip(tab, tr("Memory device %1 (firmware record %2)")
            .arg(device.index + 1).arg(device.index) + '\n' + systemMemoryScope());
    }
    const auto showChip = [this, table](int index) {
        const auto &devices = snapshot_.memoryDevices.devices;
        if (index < 0 || index >= devices.size()) return;
        const auto &device = devices.at(index);
        table->clearContents();
        table->setHorizontalHeaderLabels({systemPropertyHeaders().at(0), tr("Value — Chip %1").arg(device.index + 1)});
        const auto fields = systemMemoryFields();
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(device.index);
        for (int row = 0; row < fields.size(); ++row) {
            const auto &field = fields[row];
            auto *name = new QTableWidgetItem(field.second);
            auto *value = new QTableWidgetItem(memoryValue(field.first, device.values.value(field.first)));
            const QString hint = QStringLiteral("libudev: dmi/id ") + prefix + field.first + '\n' + systemMemoryScope();
            name->setToolTip(hint); value->setToolTip(hint);
            table->setItem(row, 0, name);
            table->setItem(row, 1, value);
        }
        table->fitContents();
    };
    connect(tabs, &QTabBar::currentChanged, table, showChip);
    showChip(tabs->currentIndex());
}
