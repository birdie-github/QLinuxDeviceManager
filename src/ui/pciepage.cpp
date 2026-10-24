#include "pciepage.h"
#include "propertypage.h"
#include "propertyreport.h"
#include "propertytext.h"
#include "gpulinktest.h"
#include "pcielinktext.h"
#include "gpulinksample.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <cerrno>
#include <cmath>
#include <memory>
#include <unistd.h>

PciePage::PciePage(QWidget *parent) : QWidget(parent)
{
    const auto widgets = setupPropertyPage(this,
        tr("ℹ️ PCIe link snapshot for this device"),
        tr("Current link readings can reflect power management and topology; a lower speed is not a fault verdict. "
        "Device maxima do not establish the capacity of the upstream path. Reading these sysfs attributes may temporarily "
        "runtime-resume the device. Reload properties to refresh."),
        tr("Copy PCIe properties"));
    groups_ = widgets.groups;
    connect(widgets.copyButton, &QPushButton::clicked, this, [this] {
        QApplication::clipboard()->setText(copyText_);
    });
}

void PciePage::setSnapshot(const PciePageSnapshot &snapshot)
{
    snapshot_ = snapshot;
    rebuild();
}

void PciePage::markRemoved()
{
    removed_ = true;
    if (gpuTest_) gpuTest_->kill();
}

void PciePage::rebuild()
{
    gpuTestButton_ = nullptr;
    while (QLayoutItem *item = groups_->takeAt(0)) {
        delete item->widget();
        delete item;
    }
    copyText_.clear();
    if (!snapshot_.pcieApplicable) return;
    QVector<QGridLayout *> forms;
    QVector<QWidget *> captions;
    QStringList copy;
    const auto group = [&](const QString &title) {
        auto *box = new QGroupBox(title, groups_->parentWidget());
        auto *form = new QGridLayout(box);
        form->setColumnStretch(1, 1);
        forms.append(form);
        groups_->addWidget(box);
        if (!copy.isEmpty()) copy.append(QString());
        copy.append(title);
        return form;
    };
    const auto field = [&](QGridLayout *form, const QString &caption, const QString &text, const QString &source = QString(), bool test = false) {
        auto *name = new QLabel(caption, form->parentWidget());
        name->setTextFormat(Qt::PlainText);
        QWidget *captionWidget = name;
        if (test) {
            captionWidget = new QWidget(form->parentWidget());
            auto *actions = new QHBoxLayout(captionWidget);
            actions->setContentsMargins(0, 0, 0, 0);
            actions->addWidget(name);
            gpuTestButton_ = new QPushButton(gpuTestText_.isEmpty() ? tr("Test") : tr("Retest"), captionWidget);
            gpuTestButton_->setAutoDefault(false);
            gpuTestButton_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
            gpuTestButton_->setEnabled(!removed_ && !gpuTest_ && gpuLinkTestBuilt() && geteuid() != 0);
            QString hint = tr("Apply a short graphics load to this GPU and observe its PCIe link. Power use and fan noise may increase.");
            if (!gpuLinkTestBuilt())
                hint = tr("This build has no GPU link test. Build with Vulkan headers, glslangValidator and Qt Vulkan support.");
            else if (geteuid() == 0)
                hint = tr("Run the application as an ordinary user to test the GPU link.");
            gpuTestButton_->setToolTip(hint);
            actions->addWidget(gpuTestButton_);
            connect(gpuTestButton_, &QPushButton::clicked, this, [this] {
                // Rebuild after the button's mouse/key event has returned.
                QTimer::singleShot(0, this, &PciePage::testGpuLink);
            });
        }
        captions.append(captionWidget);
        auto *value = plainLabel(text, form->parentWidget());
        QSizePolicy policy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        policy.setHeightForWidth(true);
        value->setSizePolicy(policy);
        value->setAlignment(Qt::AlignLeading | (test ? Qt::AlignVCenter : Qt::AlignTop));
        value->setMinimumWidth(120);
        const QString hint = propertyToolTip(source);
        name->setToolTip(hint);
        value->setToolTip(hint);
        const int row = form->count() / 2;
        form->addWidget(captionWidget, row, 0, Qt::AlignLeading | Qt::AlignTop);
        form->addWidget(value, row, 1);
        copy.append(caption + ": " + text);
    };
    const auto linkValue = [&](const QString &key, bool speed) {
        const Attribute value = snapshot_.values.value("sysfs/" + key);
        if (value.state == ReadState::Unsupported) return tr("Query unsupported by the kernel/device");
        if (value.state != ReadState::Available) return propertyReadValue(value);
        const QString raw = value.value;
        const auto malformed = [&] { return tr("Malformed reported value: %1").arg(raw.isEmpty() ? tr("(empty)") : raw); };
        if (raw.isEmpty()) return malformed();
        if (speed) {
            if (raw.compare("Unknown speed", Qt::CaseInsensitive) == 0
                || raw.compare("unknown", Qt::CaseInsensitive) == 0)
                return tr("Unavailable (%1)").arg(raw);
            // Parse the kernel's C-locale rate, including its usual .0 suffix.
            // Unfamiliar formats remain visible; unknown rates are not assigned
            // a generation by rounding or extrapolation.
            QString rateText = raw;
            if (rateText.endsWith(" PCIe")) rateText.chop(5);
            if (!rateText.endsWith(" GT/s")) return raw;
            rateText.chop(5);
            bool valid = false;
            const double rate = rateText.toDouble(&valid);
            if (!valid || !std::isfinite(rate) || rate <= 0) return malformed();
            int lanes = 0;
            if (key == "current_link_speed") {
                const Attribute width = snapshot_.values.value("sysfs/current_link_width");
                bool widthValid = false;
                const uint number = width.value.toUInt(&widthValid);
                if (width.state == ReadState::Available && widthValid && number > 0 && number <= 63)
                    lanes = static_cast<int>(number);
            }
            return pcieLinkText(rate, lanes);
        }
        bool valid = false;
        const qulonglong lanes = raw.toULongLong(&valid);
        if (!valid || raw.startsWith('-')) return malformed();
        // max_link_width exposes PCIE_LNK_WIDTH_UNKNOWN (0xff) verbatim.
        // It is a kernel sentinel, not a 255-lane capability. Actual PCIe
        // link width fields are six bits wide.
        if (key == "max_link_width" && lanes == 255)
            return tr("Unavailable (kernel reports unknown width: %1)").arg(raw);
        if (lanes > 63) return malformed();
        if (!lanes) return tr("Unavailable (reported width: %1)").arg(raw);
        return tr("x%1 (%2 lanes)").arg(QString::number(lanes), QLocale().toString(lanes));
    };
    const auto entry = [&](QGridLayout *form, const QString &key, const QString &caption, bool speed, const QString &scope) {
        QString source = snapshot_.sources.value("sysfs/" + key) + '\n' + scope;
        const Attribute value = snapshot_.values.value("sysfs/" + key);
        if (speed && value.state == ReadState::Available)
            source += '\n' + tr("Reported signalling rate per lane: %1").arg(value.value);
        field(form, caption, linkValue(key, speed), source);
    };
    const Attribute type = snapshot_.pcieType;
    bool validType = false;
    const uint number = type.value.toUInt(&validType);
    validType = validType && type.state == ReadState::Available;
    QString typeText = propertyReadValue(type);
    if (type.state == ReadState::Error && type.error == EPROTO)
        typeText = tr("Malformed PCI capability data");
    else if (type.state == ReadState::Error && type.error == EMSGSIZE)
        typeText = tr("Incomplete PCI configuration data");
    if (validType) {
        switch (number) {
        case 0: typeText = tr("PCIe endpoint"); break;
        case 1: typeText = tr("Legacy PCIe endpoint"); break;
        case 4: typeText = tr("Root port"); break;
        case 5: typeText = tr("Upstream switch port"); break;
        case 6: typeText = tr("Downstream switch port"); break;
        case 7: typeText = tr("PCIe-to-PCI/PCI-X bridge"); break;
        case 8: typeText = tr("PCI/PCI-X-to-PCIe bridge"); break;
        case 9: typeText = tr("Root Complex Integrated Endpoint"); break;
        case 10: typeText = tr("Root Complex Event Collector"); break;
        default: typeText = tr("Unrecognized PCIe device type: %1").arg(type.value); break;
        }
    }
    auto *connection = group(tr("Connection"));
    field(connection, tr("Connection type"), typeText, snapshot_.pcieTypeSource);
    if (snapshot_.pcieTypeRequiresRoot) {
        field(connection, tr("Access"), tr("Run the application as root or with sudo to read this device's PCIe connection type."), snapshot_.pcieTypeSource);
    }
    // Both root-complex types lack conventional PCIe link registers. This
    // identifies connection topology, not GPU integration or performance.
    const bool internal = validType && (number == 9 || number == 10);
    if (internal) {
        field(connection, tr("PCIe link"), tr("Not applicable — integrated into the root complex; no external PCIe link."), snapshot_.pcieTypeSource);
    } else {
        const QString currentScope = tr("Current link reported for this device; an on-demand reading, not measured throughput or a fault verdict.");
        auto *current = group(tr("Current link"));
        entry(current, "current_link_speed", tr("Link speed"), true, currentScope);
        if (device().category == "display") {
            QString source = tr("Highest link speed and width observed together during graphics load; not a guaranteed maximum or the device's advertised capability.");
            if (!gpuTestRate_.isEmpty()) source += '\n' + gpuTestRate_;
            if (!gpuTestTime_.isEmpty()) source += '\n' + tr("Observed: %1").arg(gpuTestTime_);
            const Attribute speed = snapshot_.values.value("sysfs/current_link_speed");
            const Attribute width = snapshot_.values.value("sysfs/current_link_width");
            GpuLinkSample sample;
            const bool readableLink = speed.state == ReadState::Available
                && width.state == ReadState::Available
                && parseGpuLinkSample(speed.value.toLatin1(), width.value.toLatin1(), sample);
            const QString observation = readableLink
                ? (gpuTestText_.isEmpty() ? tr("Not tested") : gpuTestText_)
                : tr("Unavailable — no readable PCIe link speed and width for this device.");
            field(current, tr("Maximum observed link"), observation, source, readableLink);
            if (!gpuTestTime_.isEmpty()) copy.append(tr("Observed: %1").arg(gpuTestTime_));
        }
        entry(current, "current_link_width", tr("Link width"), false, currentScope);
        const QString capabilityScope = tr("Maximum capability reported for this device, not the slot or entire upstream path. Speed and width maxima are separate declarations.");
        auto *capability = group(tr("Device capability"));
        entry(capability, "max_link_speed", tr("Maximum link speed"), true, capabilityScope);
        entry(capability, "max_link_width", tr("Maximum link width"), false, capabilityScope);
        auto *scope = group(tr("Interpretation"));
        field(scope, tr("Upstream path"), tr("Not assessed; upstream links may impose additional limits."));
        field(scope, tr("Current vs. maximum"), tr("A lower current link speed or width is not automatically a fault. Power management and topology can explain differences."));
    }
    int captionWidth = 0;
    for (const QWidget *caption : captions) captionWidth = qMax(captionWidth, caption->sizeHint().width());
    for (QGridLayout *form : forms) form->setColumnMinimumWidth(0, captionWidth);
    groups_->addStretch();
    copyText_ = copy.join('\n');
}

void PciePage::testGpuLink()
{
    if (removed_ || gpuTest_ || !gpuLinkTestBuilt() || geteuid() == 0 || !gpuTestButton_) return;
    // Recheck the snapshot when the queued button action runs.
    const Attribute speed = snapshot_.values.value("sysfs/current_link_speed");
    const Attribute width = snapshot_.values.value("sysfs/current_link_width");
    GpuLinkSample sample;
    if (speed.state != ReadState::Available || width.state != ReadState::Available
        || !parseGpuLinkSample(speed.value.toLatin1(), width.value.toLatin1(), sample)) return;
    gpuTestTime_.clear();
    gpuTestRate_.clear();
    auto *process = startGpuLinkTest(device(), this);
    if (!process) {
        gpuTestText_ = tr("Another GPU link test is already running.");
        rebuild();
        return;
    }
    gpuTest_ = process;
    gpuTestText_ = tr("Testing…");
    rebuild();
    auto output = std::make_shared<QByteArray>();
    auto failed = std::make_shared<bool>(false);
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, output, failed] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 8192) {
            *failed = true;
            output->clear();
            gpuTestText_ = tr("The GPU test returned an invalid report.");
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, this, [process] {
        process->readAllStandardError(); // Do not accumulate unbounded driver diagnostics.
    });
    connect(process, &QProcess::errorOccurred, this, [this, failed](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart) return;
        *failed = true;
        gpuTestText_ = tr("Could not start the GPU link test.");
        gpuTest_.clear();
        rebuild();
    });
    connect(process, &QProcess::finished, this, [this, process, output, failed](int exitCode, QProcess::ExitStatus status) {
        output->append(process->readAllStandardOutput());
        if (removed_) {
            gpuTestText_ = tr("GPU link test cancelled: device removed or replaced.");
        } else if (!*failed) {
            const auto report = QJsonDocument::fromJson(*output).object();
            const QString summary = report.value("summary").toString();
            const bool valid = output->size() <= 8192 && status == QProcess::NormalExit
                && (exitCode == 0 || exitCode == 1) && !summary.isEmpty() && summary.size() <= 2048
                && report.value("ok").isBool() && report.value("ok").toBool() == (exitCode == 0);
            if (valid && exitCode == 0) {
                const double speed = report.value("speed_gt_s").toDouble(-1);
                const double width = report.value("width").toDouble(-1);
                const QString stamp = report.value("observed_at").toString();
                if (std::isfinite(speed) && speed > 0 && speed <= 1024
                    && std::isfinite(width) && width >= 1 && width <= 64 && std::floor(width) == width
                    && stamp.size() <= 64 && QDateTime::fromString(stamp, Qt::ISODate).isValid()) {
                    gpuTestText_ = pcieLinkText(speed, static_cast<int>(width));
                    gpuTestRate_ = tr("Signalling rate: %1 GT/s per lane").arg(QLocale().toString(speed, 'g', 5));
                    gpuTestTime_ = stamp;
                } else gpuTestText_ = tr("The GPU test returned an invalid observation.");
            } else gpuTestText_ = valid ? summary : tr("GPU link test failed or did not return a valid observation.");
        }
        gpuTest_.clear();
        rebuild();
    });
    // Includes startup and teardown. SIGKILL ends the child without waiting on
    // Vulkan cleanup; no rendering-library call is made in this process.
    QTimer::singleShot(25000, this, [this, process = QPointer<QProcess>(process), failed] {
        if (!process || process->state() == QProcess::NotRunning) return;
        *failed = true;
        gpuTestText_ = tr("GPU link test timed out; no result was retained.");
        rebuild();
        process->kill();
    });
}
