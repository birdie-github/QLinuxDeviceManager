#pragma once
#include "systemproperties.h"
#include <QDialog>
#include <QHash>
#include <QStringList>
class QGridLayout;
class QLabel;
class QPushButton;

class SystemDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SystemDialog(QWidget *parent = nullptr);
    void setBusy(bool busy);
    void acceptResult(SystemProperties result);
signals:
    void refreshRequested();
private:
    QString displayValue(const QString &key, const Attribute &value) const;
    void rebuildMok();
    void copyAll();
    QGridLayout *mokForm_ = nullptr;
    QHash<QString, QLabel *> fields_;
    QStringList order_;
    QHash<QString, QString> captions_;
    SystemProperties snapshot_;
    QPushButton *refresh_ = nullptr;
    QPushButton *copy_ = nullptr;
};
