#pragma once
#include "systemproperties.h"
#include <QDialog>
#include <QHash>
#include <QStringList>
#include <QVector>
class QGridLayout;
class QLabel;
class QTableWidget;
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
    void rebuildMok();
    void rebuildMemory();
    QString memoryValue(const QString &key, const Attribute &value) const;
    void copyAll();
    void copyInformation(int tab);
    QGridLayout *memoryForm_ = nullptr;
    QGridLayout *mokForm_ = nullptr;
    QHash<QString, QVector<QLabel *>> fields_;
    QHash<QString, QTableWidget *> tables_;
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
