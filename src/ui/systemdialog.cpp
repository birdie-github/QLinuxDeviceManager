#include "systemdialog.h"
#include "systempages/rampage.h"
#include "systempages/motherboardpage.h"
#include "systempages/processorpage.h"
#include "propertytext.h"
#include <QApplication>
#include <QThread>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFrame>
#include <QSizePolicy>
#include <QTabWidget>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <utility>
#include <limits>
#include <algorithm>

SystemDialog::SystemDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("System Information"));
    resize(680, 650);
    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    auto *tabs = tabs_;
    tabs->setUsesScrollButtons(true);
    layout->addWidget(tabs, 1);
    QVector<QGridLayout *> forms;
    QVector<QLabel *> names;
    const auto group = [&](const QString &title) {
        auto *scroll = new QScrollArea(tabs);
        scroll->setWidgetResizable(true);
        auto *page = new QWidget(scroll);
        auto *contents = new QVBoxLayout(page);
        auto *fields = new QWidget(page);
        auto *form = new QGridLayout(fields);
        form->setContentsMargins(0, 0, 0, 0);
        contents->addWidget(fields);
        contents->addStretch();
        scroll->setWidget(page);
        tabs->addTab(scroll, title);
        form->setColumnStretch(1, 1);
        forms.append(form);
        return form;
    };
    const auto registerKey = [&](const QString &key) {
        order_.append(key);
        keyTabs_.insert(key, tabs->count() - 1);
    };
    const auto field = [&](QGridLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        names.append(name);
        auto *value = new QLabel(tr("Not collected"), form->parentWidget());
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
        value->setMinimumWidth(120);
        const int row = form->count() ? form->rowCount() : 0;
        form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
        form->addWidget(value, row, 1);
        if (!fields_.contains(key)) {
            captions_.insert(key, caption);
            registerKey(key);
        }
        fields_[key].append(value);
    };

    auto *os = group(tr("Operating System"));
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

    motherboardPage_ = new SystemMotherboardPage(tabs);
    tabs->addTab(motherboardPage_, tr("Motherboard"));

    processorPage_ = new SystemProcessorPage(tabs);
    tabs->addTab(processorPage_, tr("Processor"));

    ramPage_ = new SystemRamPage(tabs);
    tabs->addTab(ramPage_, tr("RAM"));
    // Use one font-derived caption width for every section, so values align.
    int captionWidth = 0;
    for (const QLabel *name : names) captionWidth = qMax(captionWidth, name->sizeHint().width());
    captionWidth = qMax(captionWidth, processorPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, motherboardPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, ramPage_->captionWidthHint());
    processorPage_->setCaptionWidth(captionWidth);
    ramPage_->setCaptionWidth(captionWidth);
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    motherboardPage_->setCaptionWidth(captionWidth);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    copyTab_ = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    copyTab_->setToolTip(tr("Copy information from the current tab"));
    copy_ = buttons->addButton(tr("Copy all"), QDialogButtonBox::ActionRole);
    refresh_ = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    copyTab_->setEnabled(false);
    copy_->setEnabled(false);
    connect(copyTab_, &QPushButton::clicked, this, [this] { copyInformation(tabs_->currentIndex()); });
    connect(copy_, &QPushButton::clicked, this, &SystemDialog::copyAll);
    connect(refresh_, &QPushButton::clicked, this, &SystemDialog::refreshRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}
void SystemDialog::setBusy(bool busy)
{
    refresh_->setEnabled(!busy);
    refresh_->setText(busy ? tr("Refreshing…") : tr("Refresh"));
}
QString SystemDialog::displayValue(const QString &key, const Attribute &value) const
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
void SystemDialog::acceptResult(SystemProperties result)
{
    Q_ASSERT(QThread::isMainThread());
    snapshot_ = std::move(result);
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
    processorPage_->setSnapshot(snapshot_);
    motherboardPage_->setSnapshot(snapshot_);
    ramPage_->setSnapshot(snapshot_);
    setBusy(false);
    copyTab_->setEnabled(true);
    copy_->setEnabled(true);
}
void SystemDialog::copyAll()
{
    copyInformation(-1);
}
void SystemDialog::copyInformation(int tab)
{
    QStringList lines;
    for (int index = 0; index < tabs_->count(); ++index) {
        if (tab >= 0 && tab != index) continue;
        QWidget *page = tabs_->widget(index);
        if (page == motherboardPage_) lines.append(motherboardPage_->copyText());
        else if (page == processorPage_) lines.append(processorPage_->copyText());
        else if (page == ramPage_) lines.append(ramPage_->copyText());
        else for (const QString &key : order_) {
            if (keyTabs_.value(key, -1) != index) continue;
            lines.append(captions_.value(key) + QStringLiteral(": ")
                         + displayValue(key, snapshot_.values.value(key)));
        }
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}
