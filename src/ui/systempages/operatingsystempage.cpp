#include "operatingsystempage.h"
#include "systemreport.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

SystemOperatingSystemPage::SystemOperatingSystemPage(QWidget *parent) : QWidget(parent)
{
    const auto page = reportPageLayout(this);
    auto *fields = new QWidget(this);
    form_ = new QGridLayout(fields);
    form_->setContentsMargins(0, 0, 0, 0);
    form_->setColumnStretch(1, 1);
    page.contents->addWidget(fields);
    page.contents->addStretch();
    const auto field = [&](QGridLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        captionWidgets_.append(name);
        auto *value = propertyValueLabel(tr("Not collected"), form->parentWidget());
        const int row = form->count() ? form->rowCount() : 0;
        form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
        form->addWidget(value, row, 1);
        if (!fields_.contains(key)) {
            captions_.insert(key, caption);
            order_.append(key);
        }
        fields_[key].append(value);
    };

    auto *os = form_;
    field(os, "hostname", systemFieldCaption(SystemPage::OperatingSystem, "hostname"));
    field(os, "os", systemFieldCaption(SystemPage::OperatingSystem, "os"));
    auto *separator = new QFrame(os->parentWidget());
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    os->addWidget(separator, os->rowCount(), 0, 1, 2);
    field(os, "architecture", systemFieldCaption(SystemPage::OperatingSystem, "architecture"));
    field(os, "kernel", systemFieldCaption(SystemPage::OperatingSystem, "kernel"));
    field(os, "build", systemFieldCaption(SystemPage::OperatingSystem, "build"));
    field(os, "boot", systemFieldCaption(SystemPage::OperatingSystem, "boot"));
    field(os, "secure_boot", systemFieldCaption(SystemPage::OperatingSystem, "secure_boot"));
    field(os, "virtualization", systemFieldCaption(SystemPage::OperatingSystem, "virtualization"));
    field(os, "uptime", systemFieldCaption(SystemPage::OperatingSystem, "uptime"));
    field(os, "gpu_rebar", systemFieldCaption(SystemPage::OperatingSystem, "gpu_rebar"));

}
int SystemOperatingSystemPage::captionWidthHint() const
{
    int width = 0;
    for (const QLabel *label : captionWidgets_) width = qMax(width, label->sizeHint().width());
    return width;
}
void SystemOperatingSystemPage::setCaptionWidth(int width)
{
    form_->setColumnMinimumWidth(0, width);
}
QString SystemOperatingSystemPage::displayValue(const QString &key, const Attribute &value) const
{
    return systemOsValue(snapshot_, key, value);
}
void SystemOperatingSystemPage::setSnapshot(const SystemProperties &snapshot)
{
    snapshot_ = snapshot;
    for (const QString &key : order_) {
        const QString text = displayValue(key, snapshot_.values.value(key));
        QString source = snapshot_.sources.value(key);
        if (key == "secure_boot")
            source += '\n' + tr("Firmware SecureBoot state; does not establish shim validation or kernel lockdown policy.");
        if (key == "gpu_rebar")
            source += '\n' + tr("GPU capability and current OS resource allocation are separate from the firmware setting. "
                "A small BAR is not proof of disabled ReBAR; a large BAR is not proof of driver use. "
                "No portable BIOS Enabled/Disabled value is inferred. Only sysfs metadata is read; BAR memory is never accessed.");
        const QString hint = "<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
        for (QLabel *value : fields_.value(key)) {
            value->setText(text);
            value->setToolTip(hint);
        }
    }
}
QString SystemOperatingSystemPage::copyText() const
{
    QStringList lines;
    for (const QString &key : order_)
        lines.append(captions_.value(key) + ": " + displayValue(key, snapshot_.values.value(key)));
    return lines.join('\n');
}
