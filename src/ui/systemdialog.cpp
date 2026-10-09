#include "systemdialog.h"
#include "propertytext.h"
#include <QApplication>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QSizePolicy>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <utility>

SystemDialog::SystemDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("System Information"));
    resize(680, 650);
    auto *layout = new QVBoxLayout(this);
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    auto *page = new QWidget(scroll);
    auto *groups = new QVBoxLayout(page);
    groups->setSizeConstraint(QLayout::SetMinAndMaxSize);
    QVector<QGridLayout *> forms;
    QVector<QLabel *> names;
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title, page);
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        forms.append(form);
        groups->addWidget(box);
        return form;
    };
    const auto field = [&](QGridLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, page);
        name->setTextFormat(Qt::PlainText);
        names.append(name);
        auto *value = new QLabel(tr("Not collected"), page);
        value->setTextFormat(Qt::PlainText);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
        value->setMinimumWidth(120);
        const int row = form->count() / 2;
        form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
        form->addWidget(value, row, 1);
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
    field(hardware, "physical_memory", tr("Physical RAM"));
    field(hardware, "memory", tr("Usable RAM"));
    auto *processor = group(tr("Processor"));
    field(processor, "cpu", tr("CPU model(s)"));
    field(processor, "sockets", tr("Sockets (reported topology)"));
    field(processor, "cores", tr("Physical cores (reported topology)"));
    field(processor, "logical", tr("Logical CPUs present"));
    field(processor, "online", tr("Logical CPUs online"));
    field(processor, "caches", tr("CPU caches (online CPUs)"));
    // Use one font-derived caption width for every section, so values align.
    int captionWidth = 0;
    for (const QLabel *name : names) captionWidth = qMax(captionWidth, name->sizeHint().width());
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
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
    refresh_->setText(busy ? tr("Refreshing…") : tr("Refresh"));
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
    if (key == "memory" || key == "physical_memory") {
        constexpr qulonglong gib = 1024ULL * 1024 * 1024;
        constexpr qulonglong mib = 1024ULL * 1024;
        const qulonglong bytes = value.value.toULongLong();
        const int precision = key == "physical_memory" && bytes % gib == 0 ? 0 : 2;
        QString text = tr("%1 GiB").arg(QLocale().toString(bytes / static_cast<double>(gib), 'f', precision));
        const Attribute physical = snapshot_.values.value("physical_memory");
        if (key == "memory" && physical.state == ReadState::Available) {
            const qulonglong installed = physical.value.toULongLong();
            if (installed >= bytes)
                text += tr(" (%1 MiB is system reserved)")
                    .arg(QLocale().toString((installed - bytes) / static_cast<double>(mib), 'f', 0));
        }
        return text;
    }
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
        QString source = snapshot_.sources.value(key);
        if (key == "memory")
            source += '\n' + tr("System reserved is reported physical RAM minus Linux MemTotal. "
                                 "It includes all memory unavailable to Linux, not just firmware reservations. "
                                 "It is omitted if physical capacity is unavailable or smaller than usable RAM.");
        fields_.value(key)->setToolTip(source.toHtmlEscaped());
    }
    setBusy(false);
    copy_->setEnabled(true);
}
void SystemDialog::copyAll()
{
    QStringList lines;
    for (const QString &key : order_)
        lines.append(captions_.value(key) + ": " + displayValue(key, snapshot_.values.value(key)));
    QApplication::clipboard()->setText(lines.join('\n'));
}
