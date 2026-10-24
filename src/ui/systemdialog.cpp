#include "systemdialog.h"
#include "propertytext.h"
#include <QApplication>
#include <QThread>
#include <QClipboard>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFont>
#include <QFrame>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
#include <QScrollBar>
#include <QGroupBox>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QVector>
#include <utility>
#include <limits>
#include <algorithm>

namespace {
QVector<QPair<QString, QString>> memoryFields()
{
    using D = SystemDialog;
    return {{"PRESENT", D::tr("Presence")}, {"SIZE", D::tr("Capacity (total)")},
        {"VOLATILE_SIZE", D::tr("Volatile capacity")}, {"NON_VOLATILE_SIZE", D::tr("Nonvolatile capacity")},
        {"LOCATOR", D::tr("Device locator")}, {"BANK_LOCATOR", D::tr("Bank locator")},
        {"TYPE", D::tr("Memory type")}, {"FORM_FACTOR", D::tr("Form factor")},
        {"SPEED_MTS", D::tr("Rated transfer rate")}, {"CONFIGURED_SPEED_MTS", D::tr("Configured transfer rate")},
        {"MANUFACTURER", D::tr("Manufacturer")}, {"PART_NUMBER", D::tr("Part number")}};
}
QString memoryScope()
{
    return SystemDialog::tr("Firmware-reported records; these do not establish removable modules, physical slots, "
        "memory channels, upgradeability or active ECC. Transfer rates are firmware declarations, not live measurements.");
}
}

SystemDialog::SystemDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("System Information"));
    resize(680, 650);
    auto *layout = new QVBoxLayout(this);
    auto *tabs = new QTabWidget(this);
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
    const auto field = [&](QGridLayout *form, const QString &key, const QString &caption) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        const bool table = key == "core_groups" || key == "cache_topology";
        if (!table) names.append(name);
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
        if (table) {
            QFont font = name->font(); font.setBold(true); name->setFont(font);
            form->addWidget(name, row, 0, 1, 2);
            delete value;
            auto *view = new QTableWidget(form->parentWidget());
            view->setEditTriggers(QAbstractItemView::NoEditTriggers);
            // Size rows independently of column widths during initial layout.
            view->setWordWrap(false);
            view->setSelectionBehavior(QAbstractItemView::SelectItems);
            view->setAlternatingRowColors(true);
            view->setShowGrid(true);
            view->setGridStyle(Qt::SolidLine);
            view->setFrameStyle(QFrame::Box | QFrame::Plain);
            view->setLineWidth(1);
            view->verticalHeader()->hide();
            view->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
            view->horizontalHeader()->setStretchLastSection(true);
            view->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            view->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
            tables_.insert(key, view);
            form->addWidget(view, row + 1, 0, 1, 2);
        } else {
            form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, row, 1);
        }
        if (!fields_.contains(key)) {
            captions_.insert(key, caption);
            order_.append(key);
        }
        if (!table) fields_[key].append(value);
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

    auto *hardware = group(tr("Hardware"));
    field(hardware, "manufacturer", tr("System manufacturer"));
    field(hardware, "model", tr("System model"));
    field(hardware, "board_vendor", tr("Motherboard manufacturer"));
    field(hardware, "board_model", tr("Motherboard model"));
    field(hardware, "firmware", tr("Firmware version"));
    field(hardware, "firmware_date", tr("Firmware date (reported)"));

    auto *mokPage = new QWidget(hardware->parentWidget());
    mokForm_ = new QGridLayout(mokPage);
    mokForm_->setContentsMargins(0, 0, 0, 0);
    mokForm_->setColumnStretch(1, 1);
    hardware->addWidget(mokPage, hardware->rowCount(), 0, 1, 2);
    order_.append("mok");
    captions_.insert("mok", tr("MOK certificates"));

    auto *processor = group(tr("Processor"));
    field(processor, "cpu", tr("CPU model(s)"));
    field(processor, "sockets", tr("Sockets"));
    field(processor, "cores", tr("Physical cores"));
    field(processor, "logical", tr("Logical CPUs"));
    field(processor, "smt", tr("SMT/HT"));
    field(processor, "core_groups", tr("Core groups (online CPUs)"));
    field(processor, "cache_topology", tr("Cache hierarchy (online CPUs)"));
    field(processor, "instruction_sets", tr("Instruction sets / CPU features"));

    auto *memory = group(tr("RAM/Memory"));
    field(memory, "physical_memory", tr("Physical RAM"));
    field(memory, "memory", tr("Usable RAM"));
    // Keep the summary labels outside the variable-length records rebuilt on refresh.
    auto *memoryPage = new QWidget(memory->parentWidget());
    memoryForm_ = new QGridLayout(memoryPage);
    memoryForm_->setContentsMargins(0, 0, 0, 0);
    memoryForm_->setColumnStretch(1, 1);
    memory->addWidget(memoryPage, memory->rowCount(), 0, 1, 2);
    forms.append(memoryForm_);
    order_.append("memory_devices");
    captions_.insert("memory_devices", tr("RAM/Memory"));
    // Use one font-derived caption width for every section, so values align.
    int captionWidth = 0;
    for (const QLabel *name : names) captionWidth = qMax(captionWidth, name->sizeHint().width());
    for (const QString &caption : {tr("Owner:"), tr("Issuer:"), tr("Expires:")}) {
        const QLabel name(caption);
        captionWidth = qMax(captionWidth, name.sizeHint().width());
    }
    for (const auto &entry : memoryFields()) {
        const QLabel name(entry.second);
        captionWidth = qMax(captionWidth, name.sizeHint().width());
    }
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    mokForm_->setColumnMinimumWidth(0, captionWidth);
    rebuildMok();
    rebuildMemory();
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
    if (key == "memory_devices") {
        QStringList lines {memoryScope()};
        const auto &memory = snapshot_.memoryDevices;
        if (memory.status.state != ReadState::Available) lines.append(propertyReadValue(memory.status));
        else if (memory.devices.isEmpty()) lines.append(tr("No memory-device records reported"));
        else for (const FirmwareMemoryDevice &device : memory.devices) {
            lines.append(tr("Memory device %1 (firmware record %2)").arg(device.index + 1).arg(device.index));
            for (const auto &field : memoryFields())
                lines.append(field.second + ": " + memoryValue(field.first, device.values.value(field.first)));
        }
        return lines.join('\n');
    }
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
    if (key == "mok") {
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
    if (key == "logical") {
        const Attribute online = snapshot_.values.value("online");
        if (value.state == ReadState::Available && online.state == ReadState::Available && value.value == online.value)
            return value.value;
        return tr("%1 present; %2 online").arg(propertyReadValue(value), propertyReadValue(online));
    }
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "secure_boot") return value.value == "enabled" ? tr("Enabled") : tr("Disabled");
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
    if (key == "smt") {
        if (value.value == "on") return tr("Enabled (kernel policy)");
        if (value.value == "off") return tr("Disabled");
        if (value.value == "forceoff") return tr("Disabled by system policy");
        if (value.value == "notsupported") return tr("Not supported");
        if (value.value == "notimplemented") return tr("Status reporting not implemented by kernel");
        return tr("Unknown (%1)").arg(value.value);
    }
    // Tab-separated cells are shared by the native views and Copy All.
    const auto tableText = [](const QVector<QStringList> &rows) {
        QStringList lines;
        for (const auto &row : rows) lines.append(row.join('\t'));
        return lines.join('\n');
    };
    if (key == "core_groups") {
        QVector<QStringList> rows {{tr("Core group"), tr("Cores / threads"), tr("Maximum frequency"), tr("Logical CPUs")}};
        const Attribute sockets = snapshot_.values.value("sockets");
        const bool showPackage = sockets.state != ReadState::Available || sockets.value.toUInt() != 1;
        for (const CpuCoreGroup &group : snapshot_.cpuGroups) {
            if (group.members.isEmpty()) continue;
            const auto &first = group.members.constFirst();
            const QString kind = first.kind == 1 ? tr("Performance cores")
                : first.kind == 2 ? tr("Efficiency cores") : tr("Cores (type unspecified)");
            QSet<int> cpus;
            QMap<QString, int> threads;
            qulonglong minimum = std::numeric_limits<qulonglong>::max(), maximum = 0;
            Attribute frequencyError;
            bool frequencyComplete = true;
            for (const auto &member : group.members) {
                cpus.insert(member.cpu);
                ++threads[member.core];
                if (member.maximumKhz.state != ReadState::Available) {
                    if (frequencyComplete) frequencyError = member.maximumKhz;
                    frequencyComplete = false;
                } else {
                    const qulonglong khz = member.maximumKhz.value.toULongLong();
                    minimum = qMin(minimum, khz);
                    maximum = qMax(maximum, khz);
                }
            }
            const auto ghz = [](qulonglong khz) { return QLocale().toString(khz / 1000000.0, 'g', 6); };
            const QString frequency = !frequencyComplete ? propertyReadValue(frequencyError)
                : minimum == maximum ? tr("%1 GHz").arg(ghz(maximum))
                : tr("%1–%2 GHz").arg(ghz(minimum), ghz(maximum));
            const QString name = showPackage ? tr("%1 (package %2)").arg(kind, first.package) : kind;
            rows.append({name, tr("%1 / %2").arg(threads.size()).arg(cpus.size()), frequency, cpuListText(cpus)});
        }
        return rows.size() == 1 ? tr("Unavailable") : tableText(rows);
    }
    if (key == "cache_topology") {
        // Keep each sharing domain explicit; aggregate equal-sized cache instances.
        QMap<QString, QVector<CpuCacheRecord>> kinds;
        for (const auto &cache : snapshot_.cpuCaches)
            kinds[QStringLiteral("%1/%2/%3").arg(cache.level).arg(cache.type).arg(cache.bytes)].append(cache);
        QVector<QStringList> rows {{tr("Cache"), tr("Configuration"), tr("Sharing (logical CPUs)")}};
        for (auto it = kinds.cbegin(); it != kinds.cend(); ++it) {
            auto instances = it.value();
            std::sort(instances.begin(), instances.end(), [](const auto &a, const auto &b) {
                return *std::min_element(a.cpus.cbegin(), a.cpus.cend())
                    < *std::min_element(b.cpus.cbegin(), b.cpus.cend());
            });
            const auto &cache = instances.constFirst();
            const QString type = cache.type == "Data" ? tr("data")
                : cache.type == "Instruction" ? tr("instruction") : tr("unified");
            QStringList sharing;
            QSet<int> privateCpus;
            bool allPrivate = true;
            for (const auto &instance : instances) {
                sharing.append(cpuListText(instance.cpus));
                allPrivate = allPrivate && instance.cpus.size() == 1;
                privateCpus.unite(instance.cpus);
            }
            const QString domains = allPrivate ? tr("Private to CPUs %1").arg(cpuListText(privateCpus))
                : instances.size() == 1 ? tr("Shared by CPUs %1").arg(sharing.constFirst())
                : tr("CPU groups %1").arg(sharing.join("; "));
            const QString size = QLocale().formattedDataSize(static_cast<qlonglong>(cache.bytes), 0, QLocale::DataSizeIecFormat);
            rows.append({tr("L%1 %2").arg(cache.level).arg(type),
                         instances.size() == 1 ? size : tr("%1 × %2").arg(instances.size()).arg(size), domains});
        }
        return rows.size() == 1 ? tr("Unavailable") : tableText(rows);
    }
    if (key == "instruction_sets") {
        const QStringList groups = value.value.split('\n');
        QStringList lines;
        for (const QString &group : groups) {
            const QStringList parts = group.split('\t');
            lines.append(groups.size() == 1 ? parts.value(1)
                : tr("Logical CPUs %1: %2").arg(parts.value(0), parts.value(1)));
        }
        return lines.join("\n\n");
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
QString SystemDialog::memoryValue(const QString &key, const Attribute &value) const
{
    if (key == "PRESENT" && value.state == ReadState::Unavailable)
        return tr("Unknown (no explicit presence flag)");
    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "PRESENT") return value.value == "0" ? tr("No device reported present") : tr("Device reported present");
    if (key == "SPEED_MTS" || key == "CONFIGURED_SPEED_MTS")
        return tr("%1 MT/s").arg(QLocale().toString(value.value.toULongLong()));
    if (key == "SIZE" || key == "VOLATILE_SIZE" || key == "NON_VOLATILE_SIZE") {
        const qulonglong bytes = value.value.toULongLong();
        if (bytes <= static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
            return QLocale().formattedDataSize(static_cast<qlonglong>(bytes), 2, QLocale::DataSizeIecFormat);
        return tr("%1 bytes").arg(QLocale().toString(bytes));
    }
    return value.value;
}
void SystemDialog::rebuildMemory()
{
    while (QLayoutItem *item = memoryForm_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    int row = 0;
    const auto label = [this](const QString &text) {
        auto *value = new QLabel(text, memoryForm_->parentWidget());
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setMinimumWidth(120);
        return value;
    };
    const auto message = [&](const QString &text, bool heading = false) {
        auto *value = label(text);
        if (heading) { QFont font = value->font(); font.setBold(true); value->setFont(font); }
        memoryForm_->addWidget(value, row++, 0, 1, 2);
    };
    const FirmwareMemoryInformation &memory = snapshot_.memoryDevices;
    if (memory.status.state != ReadState::Available) {
        auto *value = label(propertyReadValue(memory.status));
        value->setToolTip("<qt>" + snapshot_.sources.value("memory_devices").toHtmlEscaped() + "</qt>");
        memoryForm_->addWidget(value, row, 0, 1, 2);
        return;
    }
    if (memory.devices.isEmpty()) message(tr("No memory-device records reported"));
    for (const FirmwareMemoryDevice &device : memory.devices) {
        message(tr("Memory device %1").arg(device.index + 1), true);
        const QString prefix = QStringLiteral("MEMORY_DEVICE_%1_").arg(device.index);
        for (const auto &field : memoryFields()) {
            auto *name = new QLabel(field.second, memoryForm_->parentWidget());
            name->setTextFormat(Qt::PlainText);
            auto *value = label(memoryValue(field.first, device.values.value(field.first)));
            const QString hint = "<qt>" + (QStringLiteral("libudev: dmi/id ") + prefix + field.first
                + '\n' + memoryScope()).toHtmlEscaped().replace('\n', "<br>") + "</qt>";
            name->setToolTip(hint); value->setToolTip(hint);
            memoryForm_->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
            memoryForm_->addWidget(value, row++, 1);
        }
    }
}
void SystemDialog::rebuildMok()
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
    const auto label = [](const QString &text, QWidget *parent) {
        auto *value = new QLabel(text, parent);
        value->setTextFormat(Qt::PlainText);
        value->setWordWrap(true);
        value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
        value->setAlignment(Qt::AlignLeading | Qt::AlignTop);
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setMinimumWidth(120);
        return value;
    };
    const auto hint = [&scope](QLabel *value, const QString &details) {
        value->setToolTip("<qt>" + (scope + "\n\n" + details).toHtmlEscaped().replace('\n', "<br>") + "</qt>");
    };
    const auto message = [&](const QString &text) {
        auto *value = label(text, mokForm_->parentWidget());
        hint(value, {});
        mokForm_->addWidget(value, row++, 0, 1, 2);
    };
    if (mok.status.state != ReadState::Available || mok.certificates.isEmpty()) {
        message(displayValue("mok", {}));
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
            auto *value = label(text, box);
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
void SystemDialog::acceptResult(SystemProperties result)
{
    Q_ASSERT(QThread::isMainThread());
    snapshot_ = std::move(result);
    for (const QString &key : order_) {
        if (key == "mok" || key == "memory_devices") continue; // Variable-length sections are rebuilt separately.
        const QString text = displayValue(key, snapshot_.values.value(key));
        QString source = snapshot_.sources.value(key);
        if (key == "secure_boot")
            source += '\n' + tr("Firmware SecureBoot state; does not establish shim validation or kernel lockdown policy.");
        if (key == "gpu_rebar")
            source += '\n' + tr("GPU capability and current OS resource allocation are separate from the firmware setting. "
                "A small BAR is not proof of disabled ReBAR; a large BAR is not proof of driver use. "
                "No portable BIOS Enabled/Disabled value is inferred. Only sysfs metadata is read; BAR memory is never accessed.");
        if (key == "smt")
            source += '\n' + tr("SMT/HT support and policy reported by the kernel; core-group thread counts include online CPUs only.");
        if (key == "cores" || key == "sockets")
            source += '\n' + tr("Counts from kernel-reported topology for present CPUs.");
        if (key == "logical")
            source += '\n' + tr("Present and online counts are combined when equal; otherwise both are shown.");
        if (key == "core_groups")
            source += '\n' + tr("Maximum frequencies are kernel-reported limits, not live clocks. Core and thread counts include online CPUs only.");
        if (key == "instruction_sets")
            source += '\n' + tr("Kernel-exposed instruction-set and CPU-feature flags for processors listed in /proc/cpuinfo. "
                "These include non-instruction features and may be limited by the kernel, operating system or hypervisor. "
                "Different reported feature lists remain separate; this is not a complete raw hardware capability inventory.");
        if (key == "memory")
            source += '\n' + tr("System reserved is reported physical RAM minus Linux MemTotal. "
                                 "It includes all memory unavailable to Linux, not just firmware reservations. "
                                 "It is omitted if physical capacity is unavailable or smaller than usable RAM.");
        const QString hint = "<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
        if (QTableWidget *view = tables_.value(key, nullptr)) {
            view->clear();
            const bool available = snapshot_.values.value(key).state == ReadState::Available;
            const QStringList lines = text.split('\n');
            const QStringList headers = lines.constFirst().split('\t');
            const bool hasRows = available && lines.size() > 1;
            view->setColumnCount(hasRows ? headers.size() : 1);
            view->setRowCount(hasRows ? lines.size() - 1 : 1);
            view->horizontalHeader()->setVisible(hasRows);
            if (hasRows) view->setHorizontalHeaderLabels(headers);
            for (int row = 0; row < view->rowCount(); ++row) {
                const QStringList cells = hasRows ? lines[row + 1].split('\t') : QStringList{text};
                for (int column = 0; column < cells.size(); ++column)
                    view->setItem(row, column, new QTableWidgetItem(cells[column]));
            }
            view->resizeRowsToContents();
            // Fit all rows, leaving room for a horizontal scrollbar on narrow dialogs.
            view->setFixedHeight(view->verticalHeader()->length()
                + (hasRows ? view->horizontalHeader()->sizeHint().height() : 0)
                + 2 * view->frameWidth() + view->horizontalScrollBar()->sizeHint().height());
            view->setToolTip(hint);
        }
        for (QLabel *value : fields_.value(key)) {
            value->setText(text);
            value->setToolTip(hint);
        }
    }
    rebuildMok();
    rebuildMemory();
    setBusy(false);
    copy_->setEnabled(true);
}
void SystemDialog::copyAll()
{
    QStringList lines;
    for (const QString &key : order_)
        lines.append(captions_.value(key) + (key == "core_groups" || key == "cache_topology" ? ":\n" : ": ")
                     + displayValue(key, snapshot_.values.value(key)));
    QApplication::clipboard()->setText(lines.join('\n'));
}
