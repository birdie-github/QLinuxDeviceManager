#pragma once

#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct BluetoothProperties;

// Renders supplied snapshots only; collection and device identity checks belong
// to the dialog/worker. Widgets and clipboard text are owned by this page.
class BluetoothPage final : public QWidget {
    // Keep existing translations when moving rendering out of PropertiesDialog.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit BluetoothPage(QWidget *parent = nullptr);
    void setSnapshot(const BluetoothProperties &snapshot, const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
