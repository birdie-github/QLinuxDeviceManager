#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QVector>

struct Device;

// An owned udev observation, not evidence of hardware health. Wall time is the
// receipt time; monotonic elapsed time and sequence retain observation ordering.
struct DeviceEvent {
    QDateTime observed;
    qint64 elapsedMs = 0;
    quint64 sequence = 0;
    quint64 instance = 0;
    QString path;
    QString action;
    QString subsystem;
    QString devtype;
    QString driver;
    QString initialized;
    QString incarnation;
    QString oldPath;
};
struct DeviceEventsSnapshot {
    static constexpr int Limit = 1024;
    static constexpr int PerDeviceLimit = 256;
    QDateTime started;
    QVector<DeviceEvent> records;
    quint64 evicted = 0;
    quint64 unassociated = 0;
    quint64 gaps = 0;
    bool monitoring = false;
};

// Enumerator-thread ownership only. Tokens are session-local, independent of
// paths and serial numbers. Monitor gaps retire every association conservatively.
class DeviceEventHistory {
public:
    static constexpr int IdentityLimit = 16384;
    explicit DeviceEventHistory(const QDateTime &started = QDateTime::currentDateTime());
    void observe(DeviceEvent event);
    void lose();
    void reconcile(QVector<Device> &devices);
    DeviceEventsSnapshot snapshot(bool monitoring) const;
private:
    struct Identity {
        quint64 token = 0;
        QString incarnation;
        QString initialized;
        bool retired = false;
        quint64 sequence = 0; // Last observed sequence for this path; holes are normal.
    };
    Identity *identity(const QString &path);
    void retire(const QString &path);
    QHash<QString, Identity> identities_;
    DeviceEventsSnapshot snapshot_;
    quint64 nextToken_ = 0;
};
