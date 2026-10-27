#pragma once
#include "efivalue.h"
#include "propertypages.h"
#include <QThread>

class EfiPageReader final : public QThread {
public:
    explicit EfiPageReader(const Device &device) : device_(device) {}
    ~EfiPageReader() override { requestInterruption(); wait(); }
    PropertyPage page() const { return {QStringLiteral("UEFI variable — hex dump\n") + text_ + '\n', {}}; }
protected:
    void run() override { text_ = readEfiHexDump(device_); }
private:
    const Device device_;
    QString text_;
};
