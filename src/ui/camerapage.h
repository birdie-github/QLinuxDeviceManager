#pragma once

#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct CameraProperties;

// Renders supplied capability snapshots only. Owns its widgets and copy text;
// does not retain references to the caller's data or query camera hardware.
class CameraPage final : public QWidget {
    // Keep existing translations after moving rendering out of the dialog.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit CameraPage(QWidget *parent = nullptr);
    void setSnapshot(const CameraProperties &snapshot, const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
