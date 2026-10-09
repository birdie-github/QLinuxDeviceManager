#pragma once
#include "device.h"
#include <QThread>

// A single dedicated worker; no widget/model access and no concurrent scans.
class Enumerator final : public QThread {
    Q_OBJECT
public:
    explicit Enumerator(QObject *parent = nullptr) : QThread(parent) {}
    void setRequest(quint64 request) { request_ = request; } // Only while stopped.
    Inventory takeResult(); // Only after finished(), on the GUI thread.
protected:
    void run() override;
private:
    quint64 request_ = 0;
    Inventory result_;
};
