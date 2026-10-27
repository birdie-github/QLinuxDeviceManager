#include "report.h"

PropertyPage systemMotherboardPage(const SystemProperties &snapshot)
{
    SystemFields fields;
    for (const auto &field : systemFields(SystemPage::Motherboard))
        fields.append({field.second, systemMotherboardValue(snapshot.values.value(field.first))});
    SystemReport report;
    report.fields(fields);
    const auto &mok = snapshot.mok;
    if (mok.status.state != ReadState::Available || mok.certificates.isEmpty()) {
        report.fields({{systemMokTitle(), systemMokText(snapshot)}});
    } else {
        int index = 0;
        for (const auto &certificate : mok.certificates) {
            const auto formatted = systemCertificateReport(certificate, ++index, mok.certificates.size());
            report.heading(formatted.title);
            report.fields(formatted.fields);
        }
        if (mok.otherSignatures) report.heading(systemMokOmitted(mok.otherSignatures));
    }
    return report.take();
}
