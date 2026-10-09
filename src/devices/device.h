#pragma once

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>
#include <QSet>

enum class ReadState { Available, Unavailable, PermissionDenied, Removed, Error, Unsupported, NotApplicable };
struct Attribute {
    ReadState state = ReadState::Unavailable;
    QString value;
    int error = 0;
};

struct Device {
    QString path;             // Canonical current-instance lookup key, not a hardware ID.
    QString parentPath;
    QString subsystem;
    QString sysname;
    QString devtype;
    QString driver;           // Direct binding only; never inherited.
    QString driverModule;     // Observed driver/module link; absence is undetermined.
    QHash<QString, QString> properties;
    QHash<QString, Attribute> attributes;
    QString name;
    QString nameSource;
    bool nameTranslated = false; // Static function caption; translate only in the view.
    bool nameCandidate = false;  // An installed-module alias match, not a binding claim.
    QString representedByPath; // Presentation grouping only, never a new ownership edge.
    QHash<QString, QString> propertySources;
    QString category;         // Stable, untranslated application category ID.
    QString incarnation;      // Directory identity plus optional udev initialization stamp.
    quint64 generation = 0;   // Assigned by the GUI's inventory reconciliation.
    bool hidden = false;
};

struct Inventory {
    QVector<Device> devices;
    QString error;
    int skipped = 0;
    quint64 request = 0;
    QSet<QString> removedPaths; // Includes descendants when an ancestor was removed.
    bool identityLost = false; // Event loss: no old generation can be trusted.
    QString monitorNote;
};
Q_DECLARE_METATYPE(Inventory)

struct Category {
    const char *id;
    const char *label;
    const char *icon;
};
const QVector<Category> &categories();
QString categoryLabel(const QString &id);
QString categoryIcon(const QString &id);
void classify(Device &device);
void nameDevice(Device &device);
void reconcile(QVector<Device> &next, const QVector<Device> &previous, quint64 &generation);
