#pragma once
#include "systemproperties.h"
#include <QCoreApplication>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;
class QPushButton;
class ReportTable;

// Owns CPU fields, topology tables and the feature-copy action.
class SystemProcessorPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SystemDialog)
public:
    explicit SystemProcessorPage(QWidget *parent = nullptr);
    void setSnapshot(const SystemProperties &snapshot);
    QString copyText() const;
    int captionWidthHint() const;
    void setCaptionWidth(int width);
private:
    QString displayValue(const QString &key, const Attribute &value, bool compact = false) const;
    QGridLayout *form_ = nullptr;
    QVector<QLabel *> captionsWidgets_;
    QHash<QString, QVector<QLabel *>> fields_;
    QHash<QString, ReportTable *> tables_;
    QHash<QString, QString> captions_;
    QStringList order_;
    QPushButton *featuresCopy_ = nullptr;
    SystemProperties snapshot_;
};
