#pragma once

#include <QCoreApplication>
#include <QWidget>

struct DeviceResources;
class QTableWidget;

// Renders supplied resource metadata only; widgets and copy text own all data
// needed after setSnapshot returns.
class ResourcesPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit ResourcesPage(QWidget *parent = nullptr);
    void setSnapshot(const DeviceResources &resources);

private:
    void copyResources(bool selectedOnly);
    QTableWidget *resourcesTable_ = nullptr;
    QString resourcesText_;
};
