#include "systemdialog.h"
#include "propertytext.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <utility>

SystemDialog::SystemDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("System Information"));
    resize(680, 650);
    auto *layout = new QVBoxLayout(this);
    status_ = new QLabel(this);
    status_->setTextFormat(Qt::PlainText);
    status_->setWordWrap(true);
    layout->addWidget(status_);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *page = new QWidget(scroll);
    auto *groups = new QVBoxLayout(page);
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title, page);
        auto *form = new QFormLayout(box);
        form->setFieldGrowthPolicy(QFormLayout::ExpandingFieldsGrow);
        form->setRowWrapPolicy(QFormLayout::WrapLongRows);
        groups->addWidget(box);
        return form;
    };
    const auto field = [&](QFormLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, page);
        name->setTextFormat(Qt::PlainText);
        auto *value = new QLabel(tr("Not collected"), page);
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        value->setWordWrap(true);
        value->setMinimumWidth(120);
        form->addRow(name, value);
        fields_.insert(key, value);
        captions_.insert(key, caption);
        order_.append(key);
    };
    auto *os = group(tr("System"));
    field(os, "hostname", tr("Hostname"));
    field(os, "os", tr("Operating system"));
    field(os, "architecture", tr("Kernel architecture"));
    field(os, "kernel", tr("Kernel version"));
    field(os, "build", tr("Kernel build"));
    field(os, "boot", tr("Boot mode"));
    field(os, "virtualization", tr("Virtualization environment"));
    field(os, "uptime", tr("Uptime"));
    auto *hardware = group(tr("Hardware and firmware"));
    field(hardware, "manufacturer", tr("System manufacturer"));
    field(hardware, "model", tr("System model"));
    field(hardware, "board_vendor", tr("Motherboard manufacturer"));
    field(hardware, "board_model", tr("Motherboard model"));
    field(hardware, "firmware", tr("Firmware version"));
    field(hardware, "firmware_date", tr("Firmware date (reported)"));
    field(hardware, "memory", tr("Usable physical memory"));
    auto *processor = group(tr("Processor"));
    field(processor, "cpu", tr("CPU model(s)"));
    field(processor, "sockets", tr("Sockets (reported topology)"));
    field(processor, "cores", tr("Physical cores (reported topology)"));
    field(processor, "logical", tr("Logical CPUs present"));
    field(processor, "online", tr("Logical CPUs online"));
    field(processor, "caches", tr("CPU caches (online CPUs)"));
    groups->addStretch();
    scroll->setWidget(page);
    layout->addWidget(scroll, 1);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    copy_ = buttons->addButton(tr("Copy all"), QDialogButtonBox::ActionRole);
    refresh_ = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    copy_->setEnabled(false);
    connect(copy_, &QPushButton::clicked, this, &SystemDialog::copyAll);
    connect(refresh_, &QPushButton::clicked, this, &SystemDialog::refreshRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}
void SystemDialog::setBusy(bool busy)
{
    refresh_->setEnabled(!busy);
    if (busy) status_->setText(tr("Collecting system information…"));
}
QString SystemDialog::displayValue(const QString &key, const Attribute &value) const
{
    if (key == "virtualization" && value.state == ReadState::Unavailable)
        return tr("Unknown (no hypervisor identity reported)");
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "boot")
        return value.value == "uefi" ? tr("UEFI")
            : tr("UEFI not exposed (legacy boot or restricted environment)");
    if (key == "virtualization") return tr("Reported hypervisor: %1").arg(value.value);
    if (key == "memory")
        return tr("%1 GiB").arg(QLocale().toString(value.value.toULongLong() / (1024.0 * 1024 * 1024), 'f', 2));
    if (key == "uptime") {
        const qulonglong seconds = value.value.toULongLong();
        return tr("%1 days, %2 hours, %3 minutes").arg(seconds / 86400).arg(seconds / 3600 % 24).arg(seconds / 60 % 60);
    }
    if (key == "caches") {
        QStringList lines;
        for (const QString &raw : value.value.split('\n')) {
            const QStringList parts = raw.split('\t');
            const QString kind = parts.value(0);
            QString type = kind.section(' ', 1);
            if (type == "Data") type = tr("data");
            else if (type == "Instruction") type = tr("instruction");
            else if (type == "Unified") type = tr("unified");
            lines.append(tr("%1 %2: %3 (%4 unique instances)")
                .arg(kind.section(' ', 0, 0), type,
                     QLocale().formattedDataSize(parts.value(1).toLongLong()), parts.value(2)));
        }
        return lines.join('\n');
    }
    return value.value;
}
void SystemDialog::acceptResult(SystemProperties result)
{
    snapshot_ = std::move(result);
    for (const QString &key : order_) {
        fields_.value(key)->setText(displayValue(key, snapshot_.values.value(key)));
        fields_.value(key)->setToolTip(snapshot_.sources.value(key).toHtmlEscaped());
    }
    setBusy(false);
    copy_->setEnabled(true);
    status_->setText(tr("Snapshot collected at %1. Values reflect this process's system view.")
        .arg(QLocale().toString(QDateTime::currentDateTime(), QLocale::ShortFormat)));
}
void SystemDialog::copyAll()
{
    QStringList lines;
    for (const QString &key : order_)
        lines.append(captions_.value(key) + ": " + displayValue(key, snapshot_.values.value(key)));
    QApplication::clipboard()->setText(lines.join('\n'));
}
