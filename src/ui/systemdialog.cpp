#include "systemdialog.h"
#include "systempages/rampage.h"
#include "systempages/motherboardpage.h"
#include "reporttable.h"
#include "propertytext.h"
#include <QApplication>
#include <QThread>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QFont>
#include <QFrame>
#include <QSizePolicy>
#include <QTabWidget>
#include <QTableWidget>
#include <QHeaderView>
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
            auto *view = new ReportTable(form->parentWidget());
            view->setSelectionBehavior(QAbstractItemView::SelectItems);
            tables_.insert(key, view);
            form->addWidget(view, row + 1, 0, 1, 2);
        } else {
            form->addWidget(name, row, 0, Qt::AlignLeading | Qt::AlignTop);
            form->addWidget(value, row, 1);
        }
        if (!fields_.contains(key)) {
            captions_.insert(key, caption);
            registerKey(key);
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

    motherboardPage_ = new SystemMotherboardPage(tabs);
    tabs->addTab(motherboardPage_, tr("Motherboard"));

    auto *processor = group(tr("Processor"));
    field(processor, "cpu", tr("CPU model(s)"));
    field(processor, "sockets", tr("Sockets"));
    field(processor, "cores", tr("Physical cores"));
    field(processor, "logical", tr("Logical CPUs"));
    field(processor, "smt", tr("SMT/HT"));
    const QString featuresCaption = tr("Instruction sets / CPU features");
    auto *featuresLabel = new QLabel(featuresCaption, processor->parentWidget());
    names.append(featuresLabel);
    featuresCopy_ = new QPushButton(tr("Copy"), processor->parentWidget());
    featuresCopy_->setAccessibleName(tr("Copy instruction sets and CPU features"));
    featuresCopy_->setEnabled(false);
    const int featuresRow = processor->rowCount();
    processor->addWidget(featuresLabel, featuresRow, 0);
    processor->addWidget(featuresCopy_, featuresRow, 1, Qt::AlignLeading);
    captions_.insert("instruction_sets", featuresCaption);
    registerKey("instruction_sets");
    connect(featuresCopy_, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(displayValue("instruction_sets", snapshot_.values.value("instruction_sets")));
    });
    field(processor, "core_groups", tr("Core groups (online CPUs)"));
    field(processor, "cache_topology", tr("Cache hierarchy (online CPUs)"));


    ramPage_ = new SystemRamPage(tabs);
    tabs->addTab(ramPage_, tr("RAM"));
    // Use one font-derived caption width for every section, so values align.
    int captionWidth = 0;
    for (const QLabel *name : names) captionWidth = qMax(captionWidth, name->sizeHint().width());
    captionWidth = qMax(captionWidth, motherboardPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, ramPage_->captionWidthHint());
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
QString SystemDialog::displayValue(const QString &key, const Attribute &value, bool compact) const
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
        QMap<int, QString> cpuCores;
        QMap<QString, QSet<int>> coreCpus;
        QSet<int> onlineCpus;
        for (const auto &group : snapshot_.cpuGroups) {
            for (const auto &member : group.members) {
                cpuCores.insert(member.cpu, member.core);
                coreCpus[member.core].insert(member.cpu);
                onlineCpus.insert(member.cpu);
            }
        }
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
            QString domains = allPrivate ? tr("Private to CPUs %1").arg(cpuListText(privateCpus))
                : instances.size() == 1 ? tr("Shared by CPUs %1").arg(sharing.constFirst())
                : tr("CPU groups %1").arg(sharing.join("; "));
            if (compact && !onlineCpus.isEmpty()) {
                // Require an exact match to a known core's online threads. Unknown
                // or offline sharing IDs keep the explicit kernel sharing sets.
                bool perCore = true;
                QSet<QString> seenCores;
                for (const auto &instance : instances) {
                    const int cpu = *instance.cpus.cbegin();
                    const QString core = cpuCores.value(cpu);
                    if (!cpuCores.contains(cpu) || instance.cpus != coreCpus.value(core)
                        || seenCores.contains(core)) {
                        perCore = false;
                        break;
                    }
                    seenCores.insert(core);
                }
                if (perCore)
                    domains = privateCpus == onlineCpus ? tr("Private per core")
                        : tr("Private per core · CPUs %1").arg(cpuListText(privateCpus));
                else if (instances.size() == 1 && cache.cpus == onlineCpus)
                    domains = tr("Shared by all online cores");
            }
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
        const QString hint = "<qt>" + source.toHtmlEscaped().replace('\n', "<br>") + "</qt>";
        if (ReportTable *view = tables_.value(key, nullptr)) {
            view->clear();
            const bool available = snapshot_.values.value(key).state == ReadState::Available;
            const QStringList lines = displayValue(key, snapshot_.values.value(key), true).split('\n');
            const QStringList detailedLines = text.split('\n');
            const QStringList headers = lines.constFirst().split('\t');
            const bool hasRows = available && lines.size() > 1;
            view->setColumnCount(hasRows ? headers.size() : 1);
            view->setRowCount(hasRows ? lines.size() - 1 : 1);
            view->horizontalHeader()->setVisible(hasRows);
            if (hasRows) view->setHorizontalHeaderLabels(headers);
            for (int row = 0; row < view->rowCount(); ++row) {
                const QStringList cells = hasRows ? lines[row + 1].split('\t') : QStringList{text};
                for (int column = 0; column < cells.size(); ++column) {
                    auto *item = new QTableWidgetItem(cells[column]);
                    if (key == "cache_topology" && hasRows && column == 2)
                        item->setToolTip(detailedLines.value(row + 1).split('\t').value(column));
                    view->setItem(row, column, item);
                }
            }
            view->fitContents();
            view->setToolTip(hint);
        }
        for (QLabel *value : fields_.value(key)) {
            value->setText(text);
            value->setToolTip(hint);
        }
    }
    motherboardPage_->setSnapshot(snapshot_);
    ramPage_->setSnapshot(snapshot_);
    setBusy(false);
    const Attribute features = snapshot_.values.value("instruction_sets");
    featuresCopy_->setEnabled(features.state == ReadState::Available && !features.value.isEmpty());
    featuresCopy_->setToolTip(features.state == ReadState::Available
        ? tr("Copy kernel-reported instruction sets and CPU features to the clipboard.")
        : propertyReadValue(features));
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
        else if (page == ramPage_) lines.append(ramPage_->copyText());
        else for (const QString &key : order_) {
            if (keyTabs_.value(key, -1) != index) continue;
            lines.append(captions_.value(key) + (key == "core_groups" || key == "cache_topology" ? ":\n" : ": ")
                         + displayValue(key, snapshot_.values.value(key)));
        }
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}
