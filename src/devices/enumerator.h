#pragma once
#include "device.h"
#include <QThread>
#include <atomic>

// A persistent, worker-owned udev monitor and scanner. No widget/model access.
// At most one snapshot is queued: the GUI acknowledges it after applying it.
class Enumerator final : public QThread {
    Q_OBJECT
public:
    explicit Enumerator(QObject *parent = nullptr) : QThread(parent) {}
    quint64 requestRefresh() { return ++request_; }
    void acknowledge() { acknowledged_.store(true); }
signals:
    void inventoryReady(const Inventory &inventory);
protected:
    void run() override;
private:
    Inventory collect();
    std::atomic<quint64> request_{0};
    std::atomic<bool> acknowledged_{true};
};
