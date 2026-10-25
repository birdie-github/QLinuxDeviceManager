#pragma once
#include "systemproperties.h"
#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVector>
class SystemRamPage;
class SystemMotherboardPage;
class QLabel;
class ReportTable;
class QTabWidget;
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
    QString displayValue(const QString &key, const Attribute &value, bool compact = false) const;
    void copyAll();
    void copyInformation(int tab);
    SystemRamPage *ramPage_ = nullptr;
    SystemMotherboardPage *motherboardPage_ = nullptr;
    QHash<QString, QVector<QLabel *>> fields_;
    QHash<QString, ReportTable *> tables_;
    QStringList order_;
    QHash<QString, int> keyTabs_;
    QTabWidget *tabs_ = nullptr;
    QHash<QString, QString> captions_;
    SystemProperties snapshot_;
    QPushButton *refresh_ = nullptr;
    QPushButton *copy_ = nullptr;
    QPushButton *copyTab_ = nullptr;
    QPushButton *featuresCopy_ = nullptr;
};
