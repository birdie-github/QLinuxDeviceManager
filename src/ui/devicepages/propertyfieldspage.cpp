#include "propertyfieldspage.h"
#include "propertyreport.h"
#include "propertytext.h"
#include <QFormLayout>
#include <QLabel>

PropertyFieldsPage::PropertyFieldsPage(Section section, QWidget *parent)
    : QScrollArea(parent), section_(section)
{
    setWidgetResizable(true);
    setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget(this);
    form_ = new QFormLayout(content);
    form_->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form_->setRowWrapPolicy(QFormLayout::WrapLongRows);
    setWidget(content);
}
void PropertyFieldsPage::setSnapshot(const QVector<PropertyEntry> &entries)
{
    while (form_->rowCount()) form_->removeRow(0);
    for (const PropertyEntry &entry : entries) {
        if (entry.tab != static_cast<int>(section_)) continue;
        auto *label = plainLabel(entry.value, form_->parentWidget());
        label->setToolTip(entry.source.toHtmlEscaped());
        form_->addRow(entry.label + ':', label);
    }
}
