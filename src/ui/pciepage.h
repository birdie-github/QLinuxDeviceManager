#pragma once

#include "device.h"
#include <QCoreApplication>
#include <QPointer>
#include <QProcess>
#include <QWidget>

class QPushButton;
class QVBoxLayout;

// Only PCIe fields are copied from the accepted dialog snapshot. Device identity
// is retained for the existing GPU helper's instance checks.
struct PciePageSnapshot {
    Device device;
    bool pcieApplicable = false;
    Attribute pcieType;
    QString pcieTypeSource;
    bool pcieTypeRequiresRoot = false;
    QHash<QString, Attribute> values;
    QHash<QString, QString> sources;
};

class PciePage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit PciePage(QWidget *parent = nullptr);
    void setSnapshot(const PciePageSnapshot &snapshot);
    // Called before rebuilding a removed device's retained snapshot. Terminal
    // for this page: subsequent snapshots must not re-enable GPU testing.
    void markRemoved();

private:
    const Device &device() const { return snapshot_.device; }
    void rebuild();
    void testGpuLink();
    PciePageSnapshot snapshot_;
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
    QPushButton *gpuTestButton_ = nullptr;
    QPointer<QProcess> gpuTest_;
    QString gpuTestText_, gpuTestTime_, gpuTestRate_;
    bool removed_ = false;
};
