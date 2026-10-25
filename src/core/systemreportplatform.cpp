#include "systemreport.h"
#include "commonproperties.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QLocale>
#include <algorithm>
#include <limits>

namespace {
struct Text { Q_DECLARE_TR_FUNCTIONS(SystemDialog) };
}

static SystemFields motherboardFields()
{
    return {{"manufacturer", Text::tr("System manufacturer")},
        {"model", Text::tr("System model")},
        {"board_vendor", Text::tr("Motherboard manufacturer")},
        {"board_model", Text::tr("Motherboard model")},
        {"firmware", Text::tr("Firmware version")},
        {"firmware_date", Text::tr("Firmware date (reported)")}};
}

QString systemOsValue(const SystemProperties &snapshot, const QString &key, const Attribute &value)
{
    if (key == "virtualization" && value.state == ReadState::Unavailable)
        return Text::tr("Unknown (no hypervisor identity reported)");
    if (key == "gpu_rebar") {
        if (snapshot.gpuRebarStatus.state != ReadState::Available)
            return propertyReadValue(snapshot.gpuRebarStatus);
        if (snapshot.gpuRebar.isEmpty()) return Text::tr("No PCI display controller reported");
        const auto size = [](const Attribute &value) {
            if (value.state != ReadState::Available) return propertyReadValue(value);
            const qulonglong bytes = value.value.toULongLong();
            if (bytes <= static_cast<qulonglong>(std::numeric_limits<qlonglong>::max()))
                return QLocale().formattedDataSize(static_cast<qlonglong>(bytes), 0, QLocale::DataSizeIecFormat);
            return Text::tr("%1 bytes").arg(QLocale().toString(bytes));
        };
        QStringList devices;
        for (const GpuResizableBar &gpu : snapshot.gpuRebar) {
            QStringList lines {Text::tr("GPU [%1]").arg(gpu.pciAddress)};
            if (gpu.status.state == ReadState::Unavailable) lines.append(Text::tr("Capability: Not exposed by kernel"));
            else if (gpu.status.state != ReadState::Available) lines.append(propertyReadValue(gpu.status));
            else {
                lines.append(Text::tr("Capability: Supported"));
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
                    lines.append(Text::tr("BAR %1 — Current allocation: %2; supported sizes: %3")
                        .arg(bar).arg(size(gpu.currentSizes.value(bar)), supported));
                }
            }
            lines.append(Text::tr("BIOS setting: Unknown"));
            devices.append(lines.join('\n'));
        }
        return devices.join("\n\n");
    }

    if (value.state != ReadState::Available) return propertyReadValue(value);
    if (key == "secure_boot") return value.value == "enabled" ? Text::tr("Enabled") : Text::tr("Disabled");
    if (key == "boot")
        return value.value == "uefi" ? Text::tr("UEFI")
            : Text::tr("UEFI not exposed (legacy boot or restricted environment)");
    if (key == "virtualization") return Text::tr("Reported hypervisor: %1").arg(value.value);
    if (key == "uptime") {
        const qulonglong seconds = value.value.toULongLong();
        return Text::tr("%1 days, %2 hours, %3 minutes").arg(seconds / 86400).arg(seconds / 3600 % 24).arg(seconds / 60 % 60);
    }
    return value.value;
}

QString systemMokText(const SystemProperties &snapshot)
{
    const MokCertificates &mok = snapshot.mok;
    if (mok.status.state != ReadState::Available) return propertyReadValue(mok.status);
    QStringList certificates;
    const auto known = [](const QString &text) { return text.isEmpty() ? Text::tr("Unavailable") : text; };
    for (const MokCertificate &certificate : mok.certificates)
        certificates.append(Text::tr("Owner: %1\nIssuer: %2\nExpires: %3")
            .arg(known(certificate.subject), known(certificate.issuer), known(certificate.expires)));
    if (certificates.isEmpty()) certificates.append(Text::tr("No X.509 certificates in the exposed MOK list"));
    if (mok.otherSignatures) certificates.append(systemMokOmitted(mok.otherSignatures));
    return certificates.join("\n\n");
}

QStringList systemPageTitles()
{
    return {Text::tr("Operating System"), Text::tr("Motherboard"), Text::tr("Processor"), Text::tr("RAM")};
}
SystemFields systemFields(SystemPage page)
{
    switch (page) {
    case SystemPage::OperatingSystem: return {
        {"hostname", Text::tr("Hostname")},
        {"os", Text::tr("Linux distribution")},
        {"architecture", Text::tr("Kernel architecture")},
        {"kernel", Text::tr("Kernel version")},
        {"build", Text::tr("Kernel build")},
        {"boot", Text::tr("Boot mode")},
        {"secure_boot", Text::tr("Secure Boot")},
        {"virtualization", Text::tr("Virtualization environment")},
        {"uptime", Text::tr("Uptime")},
        {"gpu_rebar", Text::tr("Resizable BAR (GPU)")}};
    case SystemPage::Motherboard: return motherboardFields();
    case SystemPage::Processor: return {
        {"cpu", Text::tr("CPU model(s)")},
        {"sockets", Text::tr("Sockets")},
        {"cores", Text::tr("Physical cores")},
        {"logical", Text::tr("Logical CPUs")},
        {"smt", Text::tr("SMT/HT")},
        {"instruction_sets", Text::tr("Instruction sets / CPU features")},
        {"core_groups", Text::tr("Core groups (online CPUs)")},
        {"cache_topology", Text::tr("Cache hierarchy (online CPUs)")}};
    case SystemPage::Ram: return {{"physical_memory", Text::tr("Physical RAM")},
        {"memory", Text::tr("Usable RAM")}};
    }
    return {};
}
QString systemFieldCaption(SystemPage page, const QString &key)
{
    for (const auto &field : systemFields(page)) if (field.first == key) return field.second;
    return {};
}

SystemCertificateReport systemCertificateReport(const MokCertificate &certificate, int index, int count)
{
    const auto known = [](const QString &text) { return text.isEmpty() ? Text::tr("Unavailable") : text; };
    const QString owner = certificate.subjectDisplay.isEmpty() ? certificate.subject : certificate.subjectDisplay;
    const QString issuer = !certificate.subject.isEmpty() && certificate.subject == certificate.issuer
        ? Text::tr("Same as owner")
        : (certificate.issuerDisplay.isEmpty() ? certificate.issuer : certificate.issuerDisplay);
    const QDateTime expires = QDateTime::fromString(certificate.expires, Qt::ISODate);
    const QString date = expires.isValid()
        ? Text::tr("%1 UTC").arg(QLocale().toString(expires.toUTC(), "d MMM yyyy, HH:mm:ss")) : known(certificate.expires);
    return {count == 1 ? Text::tr("MOK Certificate") : Text::tr("MOK Certificate %1").arg(index),
        {{Text::tr("Owner:"), known(owner)},
        {Text::tr("Issuer:"), known(issuer)},
        {Text::tr("Expires:"), date}}};
}

QString systemMotherboardValue(const Attribute &value)
{
    return value.state == ReadState::Available ? value.value : propertyReadValue(value);
}
QString systemMokOmitted(int count)
{
    return Text::tr("%1 non-certificate signatures omitted").arg(count);
}
QString systemInformationTitle() { return Text::tr("System Information"); }
QString systemCollectingText() { return Text::tr("Collecting…"); }

QString systemMokTitle() { return Text::tr("MOK certificates"); }
