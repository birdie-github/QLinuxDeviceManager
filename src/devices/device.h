#pragma once

#include <QHash>
#include <QMetaType>
#include <QString>
#include <QVector>

enum class ReadState { Available, Unavailable, PermissionDenied, Removed, Error };
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
    QHash<QString, QString> properties;
    QHash<QString, Attribute> attributes;
    QString name;
    QString nameSource;
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
