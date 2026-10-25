#include "systemreport.h"
#include <QCoreApplication>
#include <QLocale>
#include <cstdio>
#include <cerrno>

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    QLocale::setDefault(QLocale::c());
    int failures = 0;
    const auto check = [&](bool ok, const char *message) {
        if (!ok) { std::fprintf(stderr, "%s\n", message); ++failures; }
    };
    const auto available = [](const QString &text) { return Attribute{ReadState::Available, text, 0}; };
    SystemProperties snapshot;
    check(systemMemoryValue("PRESENT", {}) == "Unknown (no explicit presence flag)",
          "Missing firmware presence does not imply an empty slot");
    check(systemMemoryValue("SIZE", {ReadState::PermissionDenied, {}, EACCES}) == "Permission denied",
          "Permission failure is not reported as zero capacity");
    snapshot.values["physical_memory"] = available("8589934592");
    snapshot.values["memory"] = available("17179869184");
    check(!systemRamSummaryValue(snapshot, "memory").contains("reserved"),
          "Inconsistent firmware capacity must not yield negative reserved memory");
    snapshot.values["online"] = available("4");
    check(systemProcessorValue(snapshot, "logical", available("8")) == "8 present; 4 online",
          "Offline CPUs remain distinguished from online CPUs");
    check(systemOsValue(snapshot, "gpu_rebar", {}).contains("Unavailable"),
          "Unavailable ReBAR metadata must not become a disabled verdict");
    check(systemOsValue(snapshot, "virtualization", {}).startsWith("Unknown"),
          "Missing hypervisor identity must not prove bare metal");
    snapshot.mok.status = available("present");
    MokCertificate certificate;
    certificate.subject = "CN=Fixture";
    certificate.issuer = certificate.subject;
    certificate.subjectDisplay = "Fixture";
    certificate.expires = "2030-01-02T03:04:05Z";
    const auto cert = systemCertificateReport(certificate, 1, 1);
    check(cert.title == "MOK Certificate" && cert.fields[1].second == "Same as owner"
          && cert.fields[2].second.endsWith(" UTC"), "Certificate presentation preserves identity and UTC scope");
    // SMT fixture: two cores, each with two logical CPUs and a private cache.
    QVector<CpuTopologyRecord> members;
    for (int cpu = 0; cpu < 4; ++cpu)
        members.append({cpu, "0", QString::number(cpu % 2), 0, available("4000000")});
    snapshot.cpuGroups.append({members});
    snapshot.cpuCaches = {{1, "Data", 32768, {0, 2}}, {1, "Data", 32768, {1, 3}}};
    const QString compact = systemProcessorValue(snapshot, "cache_topology", available("present"), true);
    const QString detailed = systemProcessorValue(snapshot, "cache_topology", available("present"), false);
    check(compact.contains("Private per core") && detailed.contains("0,2") && detailed.contains("1,3"),
          "Compact SMT topology retains explicit domains in detailed output");
    return failures ? 1 : 0;
}
