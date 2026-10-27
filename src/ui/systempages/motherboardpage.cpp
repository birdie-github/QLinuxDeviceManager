#include "motherboardpage.h"
#include "systemreport.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
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
        it.value()->setText(systemMotherboardValue(snapshot_.values.value(it.key())));
        it.value()->setToolTip(propertyToolTip(snapshot_.sources.value(it.key())));
    }
    rebuildMok();
}
QString SystemMotherboardPage::copyText() const
{
    QStringList lines;
    for (const auto &field : systemFields(SystemPage::Motherboard))
        lines.append(field.second + ": " + systemMotherboardValue(snapshot_.values.value(field.first)));
    lines.append(systemMokTitle() + ": " + mokText());
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
    const QString scope = snapshot_.sources.value("mok") + '\n'
        + tr("Exposed runtime MOK certificates. Issuer and expiration are certificate metadata, "
             "not verification of the signer or proof that this key signed the running kernel. Dates are UTC. "
             "Firmware db certificates and pending enrollments are not included.");
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
