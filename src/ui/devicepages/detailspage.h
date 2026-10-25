#pragma once
#include "propertytext.h"
#include <QCoreApplication>
#include <QWidget>
class QCheckBox;
class QComboBox;
class QLabel;
class QPlainTextEdit;

// Owns entries used by the selector and clipboard actions across refreshes.
class DetailsPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(PropertiesDialog)
public:
    explicit DetailsPage(QWidget *parent = nullptr);
    void setSnapshot(const QVector<PropertyEntry> &entries, const Device &device);
private:
    void rebuildDetails();
    void showDetail();
    void copyAll();
    Device device_;
    QVector<PropertyEntry> entries_;
    QComboBox *property_ = nullptr;
    QPlainTextEdit *value_ = nullptr;
    QLabel *source_ = nullptr;
    QCheckBox *advanced_ = nullptr;
};
