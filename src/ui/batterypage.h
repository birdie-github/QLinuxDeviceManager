#pragma once

#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct BatteryProperties;

// Displays supplied battery snapshots without collecting readings or retaining
// references to the caller's data. Owns its widgets and clipboard text.
class BatteryPage final : public QWidget {
    // Preserve the existing translation context after extracting rendering.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit BatteryPage(QWidget *parent = nullptr);
    void setSnapshot(const BatteryProperties &snapshot, const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
