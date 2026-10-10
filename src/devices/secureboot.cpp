#include "secureboot.h"
#include <QSslCertificate>
#include <QSslSocket>
#include <QDateTime>
#include <QThread>
#include <QtEndian>
#include <QStringList>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>

namespace {
constexpr int Limit = 1024 * 1024;
Attribute failure(int error)
{
    return {error == EACCES || error == EPERM ? ReadState::PermissionDenied
        : error == ENOENT ? ReadState::Unavailable : ReadState::Error, {}, error};
}
Attribute readBinary(const QString &path, QByteArray &bytes, bool efivar)
{
    bytes.clear();
    if (QThread::currentThread()->isInterruptionRequested()) return failure(ECANCELED);
    const int fd = open(path.toUtf8().constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) return failure(errno);
    int error = 0;
    while (bytes.size() <= Limit) {
        char buffer[4096];
        const ssize_t n = read(fd, buffer, qMin<int>(sizeof(buffer), Limit + 1 - bytes.size()));
        if (n < 0) { if (errno == EINTR) continue; error = errno; break; }
        if (!n) break;
        bytes.append(buffer, static_cast<int>(n));
        if (QThread::currentThread()->isInterruptionRequested()) { error = ECANCELED; break; }
    }
    close(fd);
    if (!error && bytes.size() > Limit) error = EOVERFLOW;
    if (!error && efivar && bytes.size() < 4) error = EINVAL;
    if (error) { bytes.clear(); return failure(error); }
    if (efivar) bytes.remove(0, 4);
    return {ReadState::Available, {}, 0};
}
QString certificateDisplayName(const QSslCertificate &certificate, bool issuer)
{
    QStringList parts;
    // Organization/unit identify distro certificates better than a generic CN.
    for (auto attribute : {QSslCertificate::OrganizationalUnitName, QSslCertificate::Organization,
                           QSslCertificate::CommonName}) {
        const QStringList values = issuer ? certificate.issuerInfo(attribute) : certificate.subjectInfo(attribute);
        for (const QString &value : values) {
            const QString text = value.simplified();
            if (!text.isEmpty() && !parts.contains(text)) parts.append(text);
        }
    }
    return parts.join(" — ");
}
QString certificateName(const QSslCertificate &certificate, bool issuer)
{
    QStringList fields;
    const auto attributes = issuer ? certificate.issuerInfoAttributes() : certificate.subjectInfoAttributes();
    for (const QByteArray &attribute : attributes) {
        const QStringList values = issuer ? certificate.issuerInfo(attribute) : certificate.subjectInfo(attribute);
        for (const QString &value : values)
            fields.append(QString::fromLatin1(attribute) + '=' + value.simplified());
    }
    return fields.join(", ");
}
}
Attribute parseSecureBoot(const QByteArray &payload)
{
    if (payload.size() != 1 || (payload[0] != 0 && payload[0] != 1)) return failure(EINVAL);
    return {ReadState::Available, payload[0] == 1 ? QStringLiteral("enabled") : QStringLiteral("disabled"), 0};
}
MokCertificates parseMokCertificates(const QByteArray &payload)
{
    MokCertificates result;
    const auto invalid = [](int error) { MokCertificates r; r.status = failure(error); return r; };
    if (payload.size() > Limit) return invalid(EOVERFLOW);
    // EFI_CERT_X509_GUID in EFI's little-endian GUID representation.
    const QByteArray x509 = QByteArray::fromHex("a159c0a5e494a74a87b5ab155c2bf072");
    qsizetype offset = 0;
    int lists = 0, signatures = 0;
    while (offset < payload.size()) {
        if (QThread::currentThread()->isInterruptionRequested()) return invalid(ECANCELED);
        if (++lists > 256) return invalid(EOVERFLOW);
        if (payload.size() - offset < 28) return invalid(EINVAL);
        const auto *header = reinterpret_cast<const uchar *>(payload.constData() + offset);
        const quint32 size = qFromLittleEndian<quint32>(header + 16);
        const quint32 headerSize = qFromLittleEndian<quint32>(header + 20);
        const quint32 signatureSize = qFromLittleEndian<quint32>(header + 24);
        if (size < 28 || size > payload.size() - offset || headerSize > size - 28 || signatureSize <= 16)
            return invalid(EINVAL);
        const quint32 dataSize = size - 28 - headerSize;
        if (dataSize % signatureSize) return invalid(EINVAL);
        for (qsizetype position = offset + 28 + headerSize; position < offset + size; position += signatureSize) {
            if (++signatures > 1024) return invalid(EOVERFLOW);
            if (payload.mid(offset, 16) != x509) { ++result.otherSignatures; continue; }
            if (result.certificates.size() >= 256 || signatureSize > 65536) return invalid(EOVERFLOW);
            if (QSslSocket::availableBackends().isEmpty()) {
                MokCertificates unavailable;
                unavailable.status = {ReadState::Unsupported, {}, 0};
                return unavailable;
            }
            const QByteArray der = payload.mid(position + 16, signatureSize - 16); // Skip signature-owner GUID.
            const auto certificates = QSslCertificate::fromData(der, QSsl::Der);
            if (certificates.size() != 1 || certificates.first().isNull() || certificates.first().toDer() != der)
                return invalid(EINVAL);
            const QSslCertificate &certificate = certificates.first();
            result.certificates.append({certificateName(certificate, false), certificateName(certificate, true),
                certificate.expiryDate().toUTC().toString(Qt::ISODate),
                certificateDisplayName(certificate, false), certificateDisplayName(certificate, true)});
        }
        offset += size;
    }
    result.status = {ReadState::Available, {}, 0};
    return result;
}
SecureBootInformation collectSecureBootInformation()
{
    SecureBootInformation result;
    QByteArray bytes;
    const QString base = "/sys/firmware/efi/efivars/";
    result.stateSource = base + "SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c";
    const Attribute state = readBinary(result.stateSource, bytes, true);
    result.state = state.state == ReadState::Available ? parseSecureBoot(bytes) : state;
    // The kernel's MOK configuration-table copy contains the complete runtime list.
    result.mokSource = "/sys/firmware/efi/mok-variables/MokListRT";
    Attribute mok = readBinary(result.mokSource, bytes, false);
    if (mok.state != ReadState::Available && (mok.error == ENOENT || mok.state == ReadState::PermissionDenied)) {
        const Attribute table = mok;
        result.mokSource = base + "MokListRT-605dab50-e046-4300-abb6-3dd810dd8b23";
        mok = readBinary(result.mokSource, bytes, true);
        if (mok.error == ENOENT && table.state == ReadState::PermissionDenied) {
            mok = table;
            result.mokSource = "/sys/firmware/efi/mok-variables/MokListRT";
        }
        // A split runtime list cannot be presented as a complete certificate list.
        QByteArray extra;
        const Attribute split = readBinary(base + "MokListRT1-605dab50-e046-4300-abb6-3dd810dd8b23", extra, true);
        if (mok.state == ReadState::Available && (split.state == ReadState::Available || split.error != ENOENT))
            mok = failure(EOVERFLOW);
    }
    result.mok = mok.state == ReadState::Available ? parseMokCertificates(bytes) : MokCertificates {mok, {}, 0};
    return result;
}
