#pragma once

#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct DisplayProperties;
struct MonitorProperties;

// Renders supplied monitor and desktop-session snapshots without querying the
// hardware/session or retaining references to the caller's data.
class DisplayPage final : public QWidget {
    // Preserve existing translations after extracting the rendering.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit DisplayPage(QWidget *parent = nullptr);
    void setSnapshot(const DisplayProperties &display, const MonitorProperties &monitor,
                     const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
