#include "motherboardpage.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>

namespace {
QString motherboardValue(const Attribute &value)
{
    return value.state == ReadState::Available ? value.value : propertyReadValue(value);
}
QVector<QPair<QString, QString>> motherboardFields()
{
    using D = SystemMotherboardPage;
    return {{"manufacturer", D::tr("System manufacturer")},
        {"model", D::tr("System model")},
        {"board_vendor", D::tr("Motherboard manufacturer")},
        {"board_model", D::tr("Motherboard model")},
        {"firmware", D::tr("Firmware version")},
        {"firmware_date", D::tr("Firmware date (reported)")}};
}
}
SystemMotherboardPage::SystemMotherboardPage(QWidget *parent) : QWidget(parent)
{
    const auto page = reportPageLayout(this);
    auto *fields = new QWidget(this);
    summary_ = new QGridLayout(fields);
    summary_->setContentsMargins(0, 0, 0, 0);
    summary_->setColumnStretch(1, 1);
    page.contents->addWidget(fields);
    int row = 0;
    for (const auto &field : motherboardFields()) {
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
    for (const auto &field : motherboardFields())
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
        it.value()->setText(motherboardValue(snapshot_.values.value(it.key())));
        it.value()->setToolTip(propertyToolTip(snapshot_.sources.value(it.key())));
    }
    rebuildMok();
}
QString SystemMotherboardPage::copyText() const
{
    QStringList lines;
    for (const auto &field : motherboardFields())
        lines.append(field.second + ": " + motherboardValue(snapshot_.values.value(field.first)));
    lines.append(tr("MOK certificates") + ": " + mokText());
    return lines.join('\n');
}
QString SystemMotherboardPage::mokText() const
{
    const MokCertificates &mok = snapshot_.mok;
    if (mok.status.state != ReadState::Available) return propertyReadValue(mok.status);
    QStringList certificates;
    const auto known = [this](const QString &text) { return text.isEmpty() ? tr("Unavailable") : text; };
    for (const MokCertificate &certificate : mok.certificates)
        certificates.append(tr("Owner: %1\nIssuer: %2\nExpires: %3")
            .arg(known(certificate.subject), known(certificate.issuer), known(certificate.expires)));
    if (certificates.isEmpty()) certificates.append(tr("No X.509 certificates in the exposed MOK list"));
    if (mok.otherSignatures) certificates.append(tr("%1 non-certificate signatures omitted").arg(mok.otherSignatures));
    return certificates.join("\n\n");
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
    const auto known = [this](const QString &text) { return text.isEmpty() ? tr("Unavailable") : text; };
    int index = 0;
    for (const MokCertificate &certificate : mok.certificates) {
        const QString title = mok.certificates.size() == 1 ? tr("MOK Certificate")
            : tr("MOK Certificate %1").arg(++index);
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
        const QString owner = certificate.subjectDisplay.isEmpty() ? certificate.subject : certificate.subjectDisplay;
        const QString issuer = !certificate.subject.isEmpty() && certificate.subject == certificate.issuer
            ? tr("Same as owner")
            : (certificate.issuerDisplay.isEmpty() ? certificate.issuer : certificate.issuerDisplay);
        const QDateTime expires = QDateTime::fromString(certificate.expires, Qt::ISODate);
        const QString date = expires.isValid()
            ? tr("%1 UTC").arg(QLocale().toString(expires.toUTC(), "d MMM yyyy, HH:mm:ss")) : known(certificate.expires);
        field(tr("Owner:"), known(owner), certificate.subject);
        field(tr("Issuer:"), known(issuer), certificate.issuer);
        field(tr("Expires:"), date, certificate.expires);
    }
    if (mok.otherSignatures)
        message(tr("%1 non-certificate signatures omitted").arg(mok.otherSignatures));
}
