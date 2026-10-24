#pragma once
#include <QHash>
#include <QString>

struct AcpiVendor { QString name, source; };

// Narrow reader of four-character vendor registry records, not a hwdb rule engine.
class AcpiVendorRegistry {
public:
    static AcpiVendorRegistry parse(const QString &text, const QString &source);
    AcpiVendor lookup(const QString &subsystem, const QString &modalias) const;
private:
    QHash<QString, QString> vendors_;
    QString source_;
};

// Worker-only file access on first use; the resulting registry is immutable.
AcpiVendor acpiVendor(const QString &subsystem, const QString &modalias);
