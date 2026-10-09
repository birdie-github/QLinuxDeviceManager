#pragma once

#include "deviceproperties.h"
#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QFormLayout;
class QLabel;
class QPlainTextEdit;
class QPushButton;

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
    struct Entry {
        QString id;
        QString label;
        QString value;
        QString source;
        bool advanced = false;
        int tab = 2;
    };
    void rebuild();
    void rebuildDetails();
    void showDetail();
    void copyAll();
    DeviceProperties snapshot_;
    QVector<Entry> entries_;
    QLabel *banner_ = nullptr;
    QFormLayout *general_ = nullptr;
    QFormLayout *driver_ = nullptr;
    QComboBox *property_ = nullptr;
    QPlainTextEdit *value_ = nullptr;
    QLabel *source_ = nullptr;
    QCheckBox *advanced_ = nullptr;
    QPushButton *reload_ = nullptr;
    bool removed_ = false;
};
