#pragma once
#include "systemproperties.h"
#include <QDialog>
class SystemRamPage;
class SystemMotherboardPage;
class SystemOperatingSystemPage;
class SystemProcessorPage;
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
    void copyAll();
    void copyInformation(int tab);
    SystemOperatingSystemPage *osPage_ = nullptr;
    SystemRamPage *ramPage_ = nullptr;
    SystemMotherboardPage *motherboardPage_ = nullptr;
    SystemProcessorPage *processorPage_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QPushButton *refresh_ = nullptr;
    QPushButton *copy_ = nullptr;
    QPushButton *copyTab_ = nullptr;
};
