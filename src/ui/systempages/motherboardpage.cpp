#include "motherboardpage.h"
#include "systemreport.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include "reporttable.h"
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QVBoxLayout>

SystemMotherboardPage::SystemMotherboardPage(QWidget *parent) : QWidget(parent)
{
    const auto page = reportPageLayout(this);
    auto *fields = new QWidget(this);
    summary_ = new QGridLayout(fields);
    summary_->setContentsMargins(0, 0, 0, 0);
    summary_->setColumnStretch(1, 1);
    page.contents->addWidget(fields);
    int row = 0;
    for (const auto &field : systemFields(SystemPage::Motherboard)) {
        auto *name = new QLabel(field.second, fields);
        name->setTextFormat(Qt::PlainText);
        auto *value = propertyValueLabel(tr("Not collected"), fields);
        summary_->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
        summary_->addWidget(value, row++, 1);
        fields_.insert(field.first, value);
    }
    auto *mok = new QWidget(fields);
    mokForm_ = new QGridLayout(mok);
    mokForm_->setContentsMargins(0, 0, 0, 0);
    mokForm_->setColumnStretch(1, 1);
    summary_->addWidget(mok, row, 0, 1, 2);
    auto *expansion = new QGroupBox(tr("Expansion slots"), this);
    auto *expansionLayout = new QVBoxLayout(expansion);
    expansionTable_ = new ReportTable(0, expansionSlotHeaders().size(), expansion);
    expansionTable_->setHorizontalHeaderLabels(expansionSlotHeaders());
    expansionTable_->hide();
    expansionStatus_ = propertyValueLabel(tr("Not collected"), expansion);
    expansionLayout->addWidget(expansionTable_);
    expansionLayout->addWidget(expansionStatus_);
    expansion->setToolTip(propertyToolTip(tr(
        "Firmware fields come from SMBIOS Type 9. Linux slot names come from /sys/bus/pci/slots. "
        "The PCI address can identify an upstream bridge. Current width describes the link of the exact firmware address, "
        "which may differ from the link of a downstream card.")));
    page.contents->addWidget(expansion);
    page.contents->addStretch();
    rebuildMok();
}
int SystemMotherboardPage::captionWidthHint() const
{
    int width = 0;
    for (const auto &field : systemFields(SystemPage::Motherboard))
        width = qMax(width, QLabel(field.second).sizeHint().width());
    for (const QString &caption : {tr("Owner:"), tr("Issuer:"), tr("Expires:")})
        width = qMax(width, QLabel(caption).sizeHint().width());
    return width;
}
void SystemMotherboardPage::setCaptionWidth(int width)
{
    summary_->setColumnMinimumWidth(0, width);
    mokForm_->setColumnMinimumWidth(0, width);
    rebuildMok();
}
void SystemMotherboardPage::setSnapshot(const SystemProperties &snapshot)
{
    snapshot_ = snapshot;
    for (auto it = fields_.cbegin(); it != fields_.cend(); ++it) {
        const auto value = snapshot_.values.value(it.key());
        it.value()->setText(systemMotherboardValue(it.key(), value));
        QString source = snapshot_.sources.value(it.key());
        if (it.key() == "tpm_firmware" && value.state == ReadState::PermissionDenied)
            source += '\n' + administrativeReadHint(value);
        it.value()->setToolTip(propertyToolTip(source));
    }
    rebuildMok();
    const auto &records = snapshot_.expansionSlots.records;
    expansionTable_->setRowCount(records.size());
    for (int row = 0; row < records.size(); ++row) {
        const auto cells = expansionSlotCells(records.at(row));
        for (int column = 0; column < cells.size(); ++column)
            expansionTable_->setItem(row, column, new QTableWidgetItem(cells.at(column)));
    }
    expansionTable_->setVisible(!records.isEmpty());
    if (!records.isEmpty()) expansionTable_->fitContents();
    expansionStatus_->setText(expansionSlotStatus());
    expansionStatus_->setToolTip(propertyToolTip(administrativeReadHint(snapshot_.expansionSlots.firmwareStatus)));
}
QStringList SystemMotherboardPage::expansionSlotHeaders() const
{
    return {tr("Designation"), tr("Type"), tr("Length"), tr("Firmware usage"), tr("Width"), tr("PCI address")};
}

QStringList SystemMotherboardPage::expansionSlotCells(const ExpansionSlot &slot) const
{
    const QString unknown = tr("Unknown");
    QString type = unknown;
    const int laneWidths[] = {1, 2, 4, 8, 16};
    const int pcieTypes[] = {0xa5, 0xab, 0xb1, 0xb8, 0xbe};
    for (int gen = 0; gen < 5; ++gen) {
        const int offset = slot.type - pcieTypes[gen];
        if (offset < 0 || offset > 5) continue;
        type = gen == 0 ? tr("PCIe") : tr("PCIe Gen %1").arg(gen + 1);
        if (offset) type += ' ' + tr("x%1").arg(laneWidths[offset - 1]);
        break;
    }
    switch (slot.type) {
    case 0x06: type = tr("PCI"); break;
    case 0x12: type = tr("PCI-X"); break;
    case 0x14: type = tr("M.2 Socket 1-DP"); break;
    case 0x15: type = tr("M.2 Socket 1-SD"); break;
    case 0x16: type = tr("M.2 Socket 2"); break;
    case 0x17: type = tr("M.2 Socket 3"); break;
    case 0xc4: type = tr("PCIe Gen 6 or later"); break;
    default:
        if (type == unknown && slot.type > 2)
            type = tr("Type 0x%1").arg(slot.type, 2, 16, QLatin1Char('0'));
        break;
    }
    QString usage = unknown;
    if (slot.usage == 3) usage = tr("Available");
    else if (slot.usage == 4) usage = tr("In use");
    else if (slot.usage == 5) usage = tr("Unavailable");
    QString length = unknown;
    if (slot.length == 3) length = tr("Short");
    else if (slot.length == 4) length = tr("Long");
    else if (slot.length == 5) length = tr("2.5-inch drive");
    else if (slot.length == 6) length = tr("3.5-inch drive");

    const auto codedWidth = [&](int code) -> QString {
        const int bits[] = {8, 16, 32, 64, 128};
        const int lanes[] = {1, 2, 4, 8, 12, 16, 32};
        if (code >= 3 && code <= 7) return tr("%1 bit").arg(bits[code - 3]);
        if (code >= 8 && code <= 14) return tr("x%1").arg(lanes[code - 8]);
        return {};
    };
    QStringList widths;
    const QString physical = codedWidth(slot.physicalWidth);
    const QString reported = codedWidth(slot.reportedWidth);
    if (!physical.isEmpty()) widths.append(tr("%1 physical").arg(physical));
    if (!reported.isEmpty()) widths.append(tr("%1 reported").arg(reported));
    if (slot.baseLanes) widths.append(tr("x%1 base").arg(slot.baseLanes));
    bool validWidth = false;
    const uint current = slot.currentLinkWidth.value.toUInt(&validWidth);
    if (slot.currentLinkWidth.state == ReadState::Available && validWidth && current)
        widths.append(tr("x%1 current at address").arg(current));

    QString name = slot.designation;
    if (!slot.kernelName.isEmpty()) {
        const QString kernel = tr("Linux slot %1").arg(slot.kernelName);
        name = name.isEmpty() ? kernel : tr("%1 (%2)").arg(name, kernel);
    }
    return {name.isEmpty() ? unknown : name, type, length, usage,
        widths.isEmpty() ? unknown : widths.join(", "), slot.address.isEmpty() ? unknown : slot.address};
}

QString SystemMotherboardPage::expansionSlotStatus() const
{
    const auto &firmware = snapshot_.expansionSlots.firmwareStatus;
    const auto &kernel = snapshot_.expansionSlots.kernelStatus;
    return tr("SMBIOS Type 9: %1 · Linux PCI slots: %2")
        .arg(firmware.state == ReadState::Available ? tr("Read") : administrativeReadValue(firmware),
             kernel.state == ReadState::Available ? tr("Read") : propertyReadValue(kernel));
}

QString SystemMotherboardPage::copyText() const
{
    QStringList lines;
    for (const auto &field : systemFields(SystemPage::Motherboard))
        lines.append(field.second + ": " + systemMotherboardValue(field.first, snapshot_.values.value(field.first)));
    lines.append(systemMokTitle() + ": " + mokText());
    lines.append(tr("Expansion slots"));
    if (!snapshot_.expansionSlots.records.isEmpty()) {
        lines.append(expansionSlotHeaders().join('\t'));
        for (const auto &slot : snapshot_.expansionSlots.records)
            lines.append(expansionSlotCells(slot).join('\t'));
    }
    lines.append(expansionSlotStatus());
    return lines.join('\n');
}
QString SystemMotherboardPage::mokText() const
{
    return systemMokText(snapshot_);
}
void SystemMotherboardPage::rebuildMok()
{
    while (QLayoutItem *item = mokForm_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    const MokCertificates &mok = snapshot_.mok;
    QString scope = snapshot_.sources.value("mok") + '\n'
        + tr("Exposed runtime MOK certificates. Issuer and expiration are certificate metadata, "
             "not verification of the signer or proof that this key signed the running kernel. Dates are UTC. "
             "Firmware db certificates and pending enrollments are not included.");
    if (mok.status.state == ReadState::PermissionDenied)
        scope += '\n' + administrativeReadHint(mok.status);
    int row = 0;
    const auto hint = [&scope](QLabel *value, const QString &details) {
        value->setToolTip("<qt>" + (scope + "\n\n" + details).toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    };
    const auto message = [&](const QString &text) {
        auto *value = propertyValueLabel(text, mokForm_->parentWidget());
        hint(value, {});
        mokForm_->addWidget(value, row++, 0, 1, 2);
    };
    if (mok.status.state != ReadState::Available || mok.certificates.isEmpty()) {
        message(mokText());
        return;
    }
    int index = 0;
    for (const MokCertificate &certificate : mok.certificates) {
        const auto report = systemCertificateReport(certificate, ++index, mok.certificates.size());
        const QString title = report.title;
        auto *box = new QGroupBox(title, mokForm_->parentWidget());
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        form->setColumnMinimumWidth(0, mokForm_->columnMinimumWidth(0));
        mokForm_->addWidget(box, row++, 0, 1, 2);
        int certificateRow = 0;
        const auto field = [&](const QString &caption, const QString &text, const QString &details) {
            auto *name = new QLabel(caption, box);
            name->setTextFormat(Qt::PlainText);
            auto *value = propertyValueLabel(text, box);
            hint(name, details);
            hint(value, details);
            form->addWidget(name, certificateRow, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, certificateRow++, 1);
        };
        field(report.fields.at(0).first, report.fields.at(0).second, certificate.subject);
        field(report.fields.at(1).first, report.fields.at(1).second, certificate.issuer);
        field(report.fields.at(2).first, report.fields.at(2).second, certificate.expires);
    }
    if (mok.otherSignatures)
        message(systemMokOmitted(mok.otherSignatures));
}
