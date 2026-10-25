#include "detailspage.h"
#include "propertyreport.h"
#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QComboBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

DetailsPage::DetailsPage(QWidget *parent) : QWidget(parent)
{
    auto *detailsLayout = new QVBoxLayout(this);
    auto *propertyLabel = new QLabel(tr("&Property:"), this);
    property_ = new QComboBox(this);
    property_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    property_->setMinimumContentsLength(24);
    propertyLabel->setBuddy(property_);
    detailsLayout->addWidget(propertyLabel);
    detailsLayout->addWidget(property_);
    value_ = new QPlainTextEdit(this);
    value_->setReadOnly(true);
    detailsLayout->addWidget(value_, 1);
    source_ = plainLabel(QString(), this);
    detailsLayout->addWidget(source_);
    advanced_ = new QCheckBox(tr("Show &advanced properties"), this);
    detailsLayout->addWidget(advanced_);
    auto *copyLayout = new QHBoxLayout;
    auto *copySelected = new QPushButton(tr("Copy selection"), this);
    auto *copyValue = new QPushButton(tr("Copy value"), this);
    auto *copyEverything = new QPushButton(tr("Copy all values"), this);
    copySelected->setEnabled(false);
    connect(value_, &QPlainTextEdit::copyAvailable, copySelected, &QPushButton::setEnabled);
    connect(copySelected, &QPushButton::clicked, value_, &QPlainTextEdit::copy);
    connect(copyValue, &QPushButton::clicked, this, [this] { QApplication::clipboard()->setText(value_->toPlainText()); });
    connect(copyEverything, &QPushButton::clicked, this, &DetailsPage::copyAll);
    for (auto *button : {copySelected, copyValue, copyEverything}) copyLayout->addWidget(button);
    copyLayout->addStretch();
    detailsLayout->addLayout(copyLayout);
    connect(property_, &QComboBox::currentIndexChanged, this, &DetailsPage::showDetail);
    connect(advanced_, &QCheckBox::toggled, this, &DetailsPage::rebuildDetails);
}
void DetailsPage::setSnapshot(const QVector<PropertyEntry> &entries, const Device &device)
{
    entries_ = entries;
    device_ = device;
    rebuildDetails();
}
void DetailsPage::rebuildDetails()
{
    const QString previous = property_->currentData().toString();
    const QSignalBlocker blocker(property_);
    property_->clear();
    QVector<const PropertyEntry *> available, other;
    for (const PropertyEntry &entry : entries_) {
        if (!isDetailsProperty(entry, device_) || (entry.advanced && !advanced_->isChecked())) continue;
        if (detailsPropertyStatus(entry).isEmpty()) available.append(&entry);
        else other.append(&entry);
    }
    for (const PropertyEntry *entry : available) property_->addItem(entry->label, entry->id);
    if (!available.isEmpty() && !other.isEmpty()) property_->insertSeparator(property_->count());
    for (const PropertyEntry *entry : other)
        property_->addItem(tr("%1 (%2)").arg(entry->label, detailsPropertyStatus(*entry)), entry->id);
    const int index = property_->findData(previous);
    property_->setCurrentIndex(index < 0 ? 0 : index);
    showDetail();
}
void DetailsPage::showDetail()
{
    const QString id = property_->currentData().toString();
    for (const PropertyEntry &entry : entries_) {
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
void DetailsPage::copyAll()
{
    QStringList values;
    for (const PropertyEntry &entry : entries_) {
        if (!isDetailsProperty(entry, device_) || (entry.advanced && !advanced_->isChecked())) continue;
        QString text = entry.label + ":\n" + entry.value;
        if (!entry.source.isEmpty()) text += '\n' + tr("Source: %1").arg(entry.source);
        values.append(text);
    }
    QApplication::clipboard()->setText(values.join("\n\n"));
}
