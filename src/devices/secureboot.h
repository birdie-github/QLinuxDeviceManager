#pragma once
#include "device.h"
#include <QByteArray>
#include <QVector>

struct MokCertificate {
    QString subject, issuer, expires;
    QString subjectDisplay, issuerDisplay;
};
struct MokCertificates {
    Attribute status;
    QVector<MokCertificate> certificates;
    int otherSignatures = 0;
};
// Payload parsers: efivarfs's four-byte attributes must already be removed.
Attribute parseSecureBoot(const QByteArray &payload);
MokCertificates parseMokCertificates(const QByteArray &payload);
struct SecureBootInformation {
    Attribute state;
    MokCertificates mok;
    QString stateSource, mokSource;
};
SecureBootInformation collectSecureBootInformation();
