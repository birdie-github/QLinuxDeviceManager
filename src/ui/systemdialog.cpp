#include "systemdialog.h"
#include "systempages/rampage.h"
#include "systempages/motherboardpage.h"
#include "systempages/processorpage.h"
#include "systempages/operatingsystempage.h"
#include <QApplication>
#include <QThread>
#include <QClipboard>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QStringList>
#include <QTabWidget>
#include <QVBoxLayout>

SystemDialog::SystemDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("System Information"));
    resize(680, 650);
    auto *layout = new QVBoxLayout(this);
    tabs_ = new QTabWidget(this);
    auto *tabs = tabs_;
    tabs->setUsesScrollButtons(true);
    layout->addWidget(tabs, 1);
    osPage_ = new SystemOperatingSystemPage(tabs);
    tabs->addTab(osPage_, tr("Operating System"));

    motherboardPage_ = new SystemMotherboardPage(tabs);
    tabs->addTab(motherboardPage_, tr("Motherboard"));

    processorPage_ = new SystemProcessorPage(tabs);
    tabs->addTab(processorPage_, tr("Processor"));

    ramPage_ = new SystemRamPage(tabs);
    tabs->addTab(ramPage_, tr("RAM"));
    // Use one font-derived caption width for every section, so values align.
    int captionWidth = 0;
    captionWidth = qMax(captionWidth, osPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, processorPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, motherboardPage_->captionWidthHint());
    captionWidth = qMax(captionWidth, ramPage_->captionWidthHint());
    processorPage_->setCaptionWidth(captionWidth);
    ramPage_->setCaptionWidth(captionWidth);
    osPage_->setCaptionWidth(captionWidth);
    motherboardPage_->setCaptionWidth(captionWidth);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    copyTab_ = buttons->addButton(tr("Copy"), QDialogButtonBox::ActionRole);
    copyTab_->setToolTip(tr("Copy information from the current tab"));
    copy_ = buttons->addButton(tr("Copy all"), QDialogButtonBox::ActionRole);
    refresh_ = buttons->addButton(tr("Refresh"), QDialogButtonBox::ActionRole);
    copyTab_->setEnabled(false);
    copy_->setEnabled(false);
    connect(copyTab_, &QPushButton::clicked, this, [this] { copyInformation(tabs_->currentIndex()); });
    connect(copy_, &QPushButton::clicked, this, &SystemDialog::copyAll);
    connect(refresh_, &QPushButton::clicked, this, &SystemDialog::refreshRequested);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
}
void SystemDialog::setBusy(bool busy)
{
    refresh_->setEnabled(!busy);
    refresh_->setText(busy ? tr("Refreshing…") : tr("Refresh"));
}
void SystemDialog::acceptResult(SystemProperties result)
{
    Q_ASSERT(QThread::isMainThread());
    osPage_->setSnapshot(result);
    motherboardPage_->setSnapshot(result);
    processorPage_->setSnapshot(result);
    ramPage_->setSnapshot(result);
    setBusy(false);
    copyTab_->setEnabled(true);
    copy_->setEnabled(true);
}
void SystemDialog::copyAll()
{
    copyInformation(-1);
}
void SystemDialog::copyInformation(int tab)
{
    QStringList lines;
    for (int index = 0; index < tabs_->count(); ++index) {
        if (tab >= 0 && tab != index) continue;
        QWidget *page = tabs_->widget(index);
        if (page == osPage_) lines.append(osPage_->copyText());
        else if (page == motherboardPage_) lines.append(motherboardPage_->copyText());
        else if (page == processorPage_) lines.append(processorPage_->copyText());
        else if (page == ramPage_) lines.append(ramPage_->copyText());
    }
    QApplication::clipboard()->setText(lines.join('\n'));
}
