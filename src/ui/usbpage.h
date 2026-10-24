#pragma once

#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;
struct Attribute;
struct UsbProperties;

// Renders supplied USB snapshots without querying hardware or retaining
// references to the caller's data. Owns its widgets and clipboard text.
class UsbPage final : public QWidget {
    // Preserve the existing translation context after extracting rendering.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit UsbPage(QWidget *parent = nullptr);
    void setSnapshot(const UsbProperties &usb, const Attribute &version,
                     const Attribute &speed, const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
