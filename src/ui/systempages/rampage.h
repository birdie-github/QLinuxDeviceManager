#pragma once
#include "systemproperties.h"
#include <QCoreApplication>
#include <QWidget>

class QGridLayout;
class QLabel;

// Concrete snapshot page, matching the device-page ownership pattern.
// The dialog coordinates refresh and copying; the page owns its content.
class SystemRamPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SystemDialog)
public:
    explicit SystemRamPage(QWidget *parent = nullptr);
    void setSnapshot(const SystemProperties &snapshot);
    QString copyText() const;
    int captionWidthHint() const;
    void setCaptionWidth(int width);
private:
    QString summaryValue(const QString &key) const;
    QString memoryValue(const QString &key, const Attribute &value) const;
    QString devicesText() const;
    void rebuildMemory();
    QGridLayout *summary_ = nullptr;
    QGridLayout *memoryForm_ = nullptr;
    QLabel *physical_ = nullptr;
    QLabel *usable_ = nullptr;
    SystemProperties snapshot_;
};
