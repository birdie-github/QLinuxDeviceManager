#pragma once
#include "systemproperties.h"
#include <QCoreApplication>
#include <QHash>
#include <QWidget>

class QGridLayout;
class QLabel;
class ReportTable;

// Owns motherboard fields and MOK certificate widgets from supplied snapshots.
// Refresh and clipboard actions remain with the dialog, as for SystemRamPage.
class SystemMotherboardPage final : public QWidget {
    Q_DECLARE_TR_FUNCTIONS(SystemDialog)
public:
    explicit SystemMotherboardPage(QWidget *parent = nullptr);
    void setSnapshot(const SystemProperties &snapshot);
    QString copyText() const;
    int captionWidthHint() const;
    void setCaptionWidth(int width);
private:
    QString mokText() const;
    void rebuildMok();
    QStringList expansionSlotHeaders() const;
    QStringList expansionSlotCells(const ExpansionSlot &slot) const;
    QString expansionSlotStatus() const;
    ReportTable *expansionTable_ = nullptr;
    QLabel *expansionStatus_ = nullptr;
    QGridLayout *summary_ = nullptr;
    QGridLayout *mokForm_ = nullptr;
    QHash<QString, QLabel *> fields_;
    SystemProperties snapshot_;
};
