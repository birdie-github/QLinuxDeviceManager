#pragma once

#include "networkproperties.h"
#include <QCoreApplication>
#include <QWidget>

class QVBoxLayout;

// Displays supplied interface snapshots without collecting data or retaining
// references to the caller's snapshot. The page owns its widgets and copy text.
class NetworkPage final : public QWidget {
    // Preserve the existing translation context after extracting the page.
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit NetworkPage(QWidget *parent = nullptr);
    void setSnapshot(const QVector<NetworkProperties> &interfaces,
                     const Attribute &state, const QString &devicePath);
    void clear();

private:
    QVBoxLayout *groups_ = nullptr;
    QString copyText_;
};
