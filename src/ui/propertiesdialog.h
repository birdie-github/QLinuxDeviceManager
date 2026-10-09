#pragma once

#include "deviceproperties.h"
#include "propertytext.h"
#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QTabWidget;
class QTableWidget;

class PropertiesDialog final : public QDialog {
    Q_OBJECT
public:
    explicit PropertiesDialog(const Device &device, QWidget *parent = nullptr);
    const Device &device() const { return snapshot_.device; }
    bool isRemoved() const { return removed_; }
    void acceptResult(DeviceProperties result);
    void reconcileInstance(const Device *current); // Borrowed only for this call.
    void setBusy(bool busy);
    void markRemoved();
signals:
    void reloadRequested();
private:
    void rebuild();
    void rebuildDetails();
    void rebuildResources();
    void copyResources(bool selectedOnly);
    void showDetail();
    void copyAll();
    DeviceProperties snapshot_;
    QVector<PropertyEntry> entries_;
    QLabel *banner_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QWidget *resourcesPage_ = nullptr;
    QTableWidget *resourcesTable_ = nullptr;
    QString resourcesText_;
    QFormLayout *general_ = nullptr;
    QFormLayout *driver_ = nullptr;
    QComboBox *property_ = nullptr;
    QPlainTextEdit *value_ = nullptr;
    QLabel *source_ = nullptr;
    QCheckBox *advanced_ = nullptr;
    QPushButton *reload_ = nullptr;
    bool removed_ = false;
};
