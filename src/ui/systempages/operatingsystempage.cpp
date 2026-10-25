#include "operatingsystempage.h"
#include "propertypage.h"
#include "propertytext.h"
#include "propertyreport.h"
#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QLocale>
#include <QVBoxLayout>
#include <algorithm>
#include <limits>

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
    field(os, "hostname", tr("Hostname"));
    field(os, "os", tr("Linux distribution"));
    auto *separator = new QFrame(os->parentWidget());
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    os->addWidget(separator, os->rowCount(), 0, 1, 2);
    field(os, "architecture", tr("Kernel architecture"));
    field(os, "kernel", tr("Kernel version"));
    field(os, "build", tr("Kernel build"));
    field(os, "boot", tr("Boot mode"));
    field(os, "secure_boot", tr("Secure Boot"));
    field(os, "virtualization", tr("Virtualization environment"));
    field(os, "uptime", tr("Uptime"));
    field(os, "gpu_rebar", tr("Resizable BAR (GPU)"));

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
    if (key == "virtualization" && value.state == ReadState::Unavailable)
        return tr("Unknown (no hypervisor identity reported)");
    if (key == "gpu_rebar") {
        if (snapshot_.gpuRebarStatus.state != ReadState::Available)
            return propertyReadValue(snapshot_.gpuRebarStatus);
        if (snapshot_.gpuRebar.isEmpty()) return tr("No PCI display controller reported");
        const auto size = [this](const Attribute &value) {
            if (value.state != ReadState::Available) return propertyReadValue(value);
            const qulonglong bytes = value.value.toULongLong();
            if (bytes <= static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
                return QLocale().formattedDataSize(static_cast<qlonglong>(bytes), 0, QLocale::DataSizeIecFormat);
            return tr("%1 bytes").arg(QLocale().toString(bytes));
        };
        QStringList devices;
        for (const GpuResizableBar &gpu : snapshot_.gpuRebar) {
            QStringList lines {tr("GPU [%1]").arg(gpu.pciAddress)};
            if (gpu.status.state == ReadState::Unavailable) lines.append(tr("Capability: Not exposed by kernel"));
            else if (gpu.status.state != ReadState::Available) lines.append(propertyReadValue(gpu.status));
            else {
                lines.append(tr("Capability: Supported"));
                QList<int> bars = gpu.supportedSizes.keys();
                std::sort(bars.begin(), bars.end());
                for (int bar : bars) {
                    const Attribute bitmap = gpu.supportedSizes.value(bar);
                    QString supported = propertyReadValue(bitmap);
                    if (bitmap.state == ReadState::Available) {
                        const qulonglong bits = bitmap.value.toULongLong(nullptr, 16);
                        QStringList sizes;
                        for (int bit = 0; bit < 44; ++bit)
                            if (bits & (1ULL << bit)) sizes.append(size({ReadState::Available, QString::number(1ULL << (bit + 20)), 0}));
                        supported = sizes.join(", ");
                    }
                    lines.append(tr("BAR %1 — Current allocation: %2; supported sizes: %3")
                        .arg(bar).arg(size(gpu.currentSizes.value(bar)), supported));
                }
            }
            lines.append(tr("BIOS setting: Unknown"));
            devices.append(lines.join('\n'));
        }
        return devices.join("\n\n");
    }

    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "secure_boot") return value.value == "enabled" ? tr("Enabled") : tr("Disabled");
    if (key == "boot")
        return value.value == "uefi" ? tr("UEFI")
            : tr("UEFI not exposed (legacy boot or restricted environment)");
    if (key == "virtualization") return tr("Reported hypervisor: %1").arg(value.value);
    if (key == "uptime") {
        const qulonglong seconds = value.value.toULongLong();
        return tr("%1 days, %2 hours, %3 minutes").arg(seconds / 86400).arg(seconds / 3600 % 24).arg(seconds / 60 % 60);
    }
    return value.value;
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
