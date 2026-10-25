#pragma once
#include "systemproperties.h"
#include <QCoreApplication>
#include <QHash>
#include <QStringList>
#include <QVector>
#include <QWidget>

class QGridLayout;
class QLabel;

// Owns operating-system fields and GPU ReBAR presentation from supplied snapshots.
class SystemOperatingSystemPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SystemDialog)
public:
    explicit SystemOperatingSystemPage(QWidget *parent = nullptr);
    void setSnapshot(const SystemProperties &snapshot);
    QString copyText() const;
    int captionWidthHint() const;
    void setCaptionWidth(int width);
private:
    QString displayValue(const QString &key, const Attribute &value) const;
    QGridLayout *form_ = nullptr;
    QVector<QLabel *> captionWidgets_;
    QHash<QString, QVector<QLabel *>> fields_;
    QHash<QString, QString> captions_;
    QStringList order_;
    SystemProperties snapshot_;
};
