#include "propertyreport.h"
#include "storagepresentation.h"
#include <QFormLayout>

#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

QString propertyToolTip(const QString &source)
{
    if (source.trimmed().isEmpty()) return {};
    return "<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
}

namespace {
QLabel *selectableLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    return label;
}
}
QLabel *plainLabel(const QString &text, QWidget *parent)
{
    auto *label = selectableLabel(text, parent);
    label->setFocusPolicy(Qt::StrongFocus);
    return label;
}
QLabel *propertyValueLabel(const QString &text, QWidget *parent)
{
    auto *label = selectableLabel(text, parent);
    QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    policy.setHeightForWidth(true);
    label->setSizePolicy(policy);
    label->setAlignment(Qt::AlignLeading | Qt::AlignTop);
    label->setMinimumWidth(120);
    return label;
}

PropertyReport::PropertyReport(QVBoxLayout *groups, QStringList &copy)
    : groups_(groups), copy_(copy)
{
}

QGridLayout *PropertyReport::group(const QString &title)
{
    auto *box = new QGroupBox(title, groups_->parentWidget());
    auto *form = new QGridLayout(box);
    form->setColumnStretch(1, 1);
    forms_.append(form);
    groups_->addWidget(box);
    if (!copy_.isEmpty()) copy_.append(QString());
    copy_.append(title);
    return form;
}

void PropertyReport::field(QGridLayout *form, const QString &caption,
                           const QString &text, const QString &source)
{
    auto *name = new QLabel(caption, form->parentWidget());
    name->setTextFormat(Qt::PlainText);
    captions_.append(name);
    auto *value = propertyValueLabel(text, form->parentWidget());
    value->setFocusPolicy(Qt::StrongFocus);
    const QString hint = propertyToolTip(source);
    name->setToolTip(hint);
    value->setToolTip(hint);
    const int row = form->count() / 2;
    form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
    form->addWidget(value, row, 1);
    copy_.append(caption + ": " + text);
}

void PropertyReport::alignCaptions()
{
    int width = 0;
    for (const QLabel *caption : captions_) width = qMax(width, caption->sizeHint().width());
    for (QGridLayout *form : forms_) form->setColumnMinimumWidth(0, width);
}

void showStorageFields(QFormLayout *form, const QVector<StorageDisplayField> &fields)
{
    while (form->rowCount()) form->removeRow(0);
    for (const StorageDisplayField &field : fields) {
        auto *caption = plainLabel(field.label + ':', form->parentWidget());
        auto *value = plainLabel(field.value, form->parentWidget());
        const QString hint = propertyToolTip(field.source);
        caption->setToolTip(hint);
        value->setToolTip(hint);
        form->addRow(caption, value);
    }
}
