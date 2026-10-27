#include "usbpage.h"
#include "deviceproperties.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"

#include <QApplication>
#include <QClipboard>
#include <QPushButton>
#include <QVBoxLayout>

UsbPage::UsbPage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ USB configuration snapshot"),
        tr("Power information is declared by the active USB configuration, not measured consumption. "
        "USB version and speed are also available as raw values on Details. Reload properties to refresh."),
        tr("Copy USB properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void UsbPage::clear()
{
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
}

void UsbPage::setSnapshot(const UsbProperties &data, const Attribute &version,
                          const Attribute &speed, const QString &devicePath)
{
    clear();
    QStringList copy;
    PropertyReport fieldsReport(groups_, copy);
    const auto group = [&](const QString &title) { return fieldsReport.group(title); };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text,
                           const QString &source = QString()) {
        fieldsReport.field(form, caption, text, source);
    };
    for (const UsbSection &section : usbPropertySections(data, version, speed, devicePath)) {
        auto *form = group(section.title);
        for (const PropertyEntry &entry : section.fields)
            field(form, entry.label, entry.value, entry.source);
    }
    fieldsReport.alignCaptions();
    groups_->addStretch();
    copyText_ = copy.join('\n');
}
