#include "mainwindow.h"
#include "devicemodel.h"
#include "propertiesdialog.h"
#include "projectmetadata.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QIcon>
#include <QItemSelectionModel>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QStatusBar>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>
#include <utility>
#include <unistd.h>

MainWindow::MainWindow()
{
    setWindowTitle(tr("QLinuxDeviceManager"));
    if (geteuid() == 0) setWindowTitle(windowTitle() + tr(" — Administrative mode"));
    resize(850, 650);
    model_ = new DeviceModel(this);
    tree_ = new QTreeView(this);
    tree_->setModel(model_);
    tree_->setUniformRowHeights(true);
    tree_->setSelectionMode(QAbstractItemView::SingleSelection);
    tree_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    tree_->setTextElideMode(Qt::ElideMiddle);
    setCentralWidget(tree_);

    auto *file = menuBar()->addMenu(tr("&File"));
    auto *quit = file->addAction(tr("&Quit"));
    quit->setShortcut(QKeySequence::Quit);
    connect(quit, &QAction::triggered, this, &QWidget::close);
    auto *action = menuBar()->addMenu(tr("&Action"));
    refreshAction_ = action->addAction(QIcon::fromTheme("view-refresh",
        style()->standardIcon(QStyle::SP_BrowserReload)), tr("&Refresh"));
    refreshAction_->setShortcut(QKeySequence(Qt::Key_F5));
    refreshAction_->setToolTip(tr("Re-enumerate existing kernel devices; does not rescan buses."));
    connect(refreshAction_, &QAction::triggered, this, &MainWindow::refresh);
    propertiesAction_ = action->addAction(QIcon::fromTheme("document-properties",
        style()->standardIcon(QStyle::SP_FileDialogInfoView)), tr("&Properties…"));
    propertiesAction_->setShortcut(QKeySequence(Qt::ALT | Qt::Key_Return));
    propertiesAction_->setEnabled(false);
    connect(propertiesAction_, &QAction::triggered, this, &MainWindow::openProperties);
    connect(tree_, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.data(DeviceModel::PathRole).toString().isEmpty()) openProperties();
    });
    tree_->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(tree_, &QWidget::customContextMenuRequested, this, [this](const QPoint &position) {
        const QModelIndex index = tree_->indexAt(position);
        if (index.data(DeviceModel::PathRole).toString().isEmpty()) return;
        tree_->setCurrentIndex(index);
        QMenu menu(this);
        menu.addAction(propertiesAction_);
        menu.exec(tree_->viewport()->mapToGlobal(position));
    });
    auto *view = menuBar()->addMenu(tr("&View"));
    internalAction_ = view->addAction(tr("Show &virtual and internal devices"));
    internalAction_->setCheckable(true);
    internalAction_->setToolTip(tr("Include virtual devices, partitions, interface endpoints and internal kernel objects."));
    connect(internalAction_, &QAction::toggled, this, [this](bool show) {
        rememberTree();
        model_->setShowInternal(show);
        restoreTree();
        updateStatus();
    });
    view->addSeparator();
    auto *expand = view->addAction(tr("Expand all"));
    auto *collapse = view->addAction(tr("Collapse all"));
    connect(expand, &QAction::triggered, tree_, &QTreeView::expandAll);
    connect(collapse, &QAction::triggered, tree_, &QTreeView::collapseAll);
    auto *help = menuBar()->addMenu(tr("&Help"));
    auto *about = help->addAction(tr("&About"));
    connect(about, &QAction::triggered, this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(tr("About QLinuxDeviceManager"));
        box.setTextFormat(Qt::PlainText);
        box.setText(QStringLiteral("%1 %2\n").arg(QCoreApplication::applicationName(), QCoreApplication::applicationVersion())
                    + tr("A read-only Linux hardware viewer.\n"
                       "Categories are application groupings, not native Linux device classes.\n"
                       "Device presence does not establish hardware health.\n"
                       "Licensed under GNU GPL version 3; see LICENSE."));
        box.exec();
    });
    auto *toolbar = addToolBar(tr("Main toolbar"));
    toolbar->setObjectName("mainToolbar");
    toolbar->setIconSize(QSize(20, 20));
    toolbar->addAction(refreshAction_);
    toolbar->addAction(propertiesAction_);
    view->addAction(toolbar->toggleViewAction());
    statusBar();

    QSettings settings;
    restoreGeometry(settings.value("window/geometry").toByteArray());
    restoreState(settings.value("window/state").toByteArray());
    // Every launch starts collapsed; only refreshes within this session restore expansion.
    settings.remove("tree/expanded");
    internalAction_->setChecked(settings.value("view/showInternal", false).toBool());
    connect(tree_->selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &, const QModelIndex &) { updateStatus(); });
    connect(&worker_, &QThread::finished, this, &MainWindow::acceptInventory, Qt::QueuedConnection);
    connect(&propertiesWorker_, &QThread::finished, this, &MainWindow::acceptProperties, Qt::QueuedConnection);
    QTimer::singleShot(0, this, &MainWindow::refresh);
}
MainWindow::~MainWindow()
{
    worker_.requestInterruption();
    propertiesWorker_.requestInterruption();
    worker_.wait(); // Fallback for destruction without a normal window close.
    propertiesWorker_.wait();
}
void MainWindow::refresh()
{
    if (closing_) return;
    ++requested_;
    if (busy_) {
        pending_ = true;
        return;
    }
    busy_ = true; // Includes the interval before queued finished() delivery.
    worker_.setRequest(requested_);
    refreshAction_->setEnabled(false);
    statusBar()->showMessage(tr("Refreshing devices…"));
    worker_.start();
}
void MainWindow::acceptInventory()
{
    worker_.wait(); // finished() can precede final thread-local cleanup.
    Inventory inventory = worker_.takeResult();
    if (closing_) { close(); return; }
    if (pending_ || inventory.request != requested_) {
        pending_ = false;
        worker_.setRequest(requested_);
        worker_.start();
        return;
    }
    busy_ = false;
    refreshAction_->setEnabled(true);
    if (!inventory.error.isEmpty()) {
        statusBar()->showMessage(inventory.error);
        return;
    }
    rememberTree();
    model_->setInventory(std::move(inventory.devices));
    if (propertiesDialog_) {
        const Device &target = propertiesDialog_->device();
        const auto current = model_->device(target.path, target.generation);
        propertiesDialog_->reconcileInstance(current ? &*current : nullptr);
        if (propertiesDialog_->isRemoved()) {
            pendingProperties_.reset();
            ++propertiesRequest_;
            propertiesWorker_.requestInterruption();
        }
    }
    scanNote_ = inventory.skipped ? tr("%1 records unavailable or removed during enumeration").arg(inventory.skipped) : QString();
    restoreTree();
    updateStatus();
}
void MainWindow::rememberTree()
{
    // Remember categories not currently visible as well (e.g. a hidden-only group).
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QModelIndex i = model_->index(row, 0);
        const QString id = i.data(DeviceModel::CategoryRole).toString();
        if (tree_->isExpanded(i)) expanded_.insert(id);
        else expanded_.remove(id);
    }
    const QModelIndex current = tree_->currentIndex();
    selectedPath_ = current.data(DeviceModel::PathRole).toString();
    selectedGeneration_ = current.data(DeviceModel::GenerationRole).toULongLong();
}
void MainWindow::restoreTree()
{
    for (int row = 0; row < model_->rowCount(); ++row) {
        const QModelIndex i = model_->index(row, 0);
        if (expanded_.contains(i.data(DeviceModel::CategoryRole).toString())) tree_->expand(i);
    }
    const QModelIndex selected = model_->findDevice(selectedPath_, selectedGeneration_);
    if (selected.isValid()) {
        tree_->setCurrentIndex(selected);
        tree_->scrollTo(selected);
    }
}
void MainWindow::updateStatus()
{
    if (closing_) return;
    propertiesAction_->setEnabled(!tree_->currentIndex().data(DeviceModel::PathRole).toString().isEmpty());
    if (worker_.isRunning()) { statusBar()->showMessage(tr("Refreshing devices…")); return; }
    QString message = tr("%1 shown / %2 discovered").arg(model_->visibleCount()).arg(model_->inventoryCount());
    if (!scanNote_.isEmpty()) message += QStringLiteral(" — ") + scanNote_;
    const QString path = tree_->currentIndex().data(DeviceModel::PathRole).toString();
    if (!path.isEmpty()) message += QStringLiteral(" — ") + path;
    statusBar()->showMessage(message);
}
void MainWindow::saveSettings()
{
    rememberTree();
    QSettings settings;
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    settings.setValue("view/showInternal", internalAction_->isChecked());
}
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (!closing_) saveSettings();
    closing_ = true;
    pendingProperties_.reset();
    if (worker_.isRunning() || propertiesWorker_.isRunning()) {
        worker_.requestInterruption();
        propertiesWorker_.requestInterruption();
        setEnabled(false);
        statusBar()->showMessage(tr("Waiting for device metadata reads to finish…"));
        event->ignore(); // Keep the event loop alive; finished() closes the window.
    } else event->accept();
}

void MainWindow::openProperties()
{
    if (closing_) return;
    const QModelIndex index = tree_->currentIndex();
    const auto record = model_->device(index.data(DeviceModel::PathRole).toString(),
                                      index.data(DeviceModel::GenerationRole).toULongLong());
    if (!record) return;
    if (propertiesDialog_ && propertiesDialog_->device().path == record->path
        && propertiesDialog_->device().generation == record->generation) {
        propertiesDialog_->show();
        propertiesDialog_->raise();
        propertiesDialog_->activateWindow();
        return;
    }
    if (propertiesDialog_) propertiesDialog_->close();
    auto *dialog = new PropertiesDialog(*record, this);
    propertiesDialog_ = dialog;
    connect(dialog, &PropertiesDialog::reloadRequested, this, &MainWindow::requestProperties);
    connect(dialog, &QDialog::finished, this, [this, dialog] {
        if (propertiesDialog_ != dialog) return;
        propertiesDialog_.clear();
        pendingProperties_.reset();
        ++propertiesRequest_;
        propertiesWorker_.requestInterruption();
    });
    dialog->show(); // Modeless: tree Refresh stays available while properties are open.
    requestProperties();
}
void MainWindow::requestProperties()
{
    if (closing_ || !propertiesDialog_ || propertiesDialog_->isRemoved()) return;
    pendingProperties_ = propertiesDialog_->device();
    ++propertiesRequest_;
    propertiesDialog_->setBusy(true);
    if (propertiesBusy_) {
        propertiesWorker_.requestInterruption();
        return; // Only the latest pending request survives.
    }
    propertiesWorker_.setRequest(*pendingProperties_, propertiesRequest_);
    pendingProperties_.reset();
    propertiesBusy_ = true;
    propertiesWorker_.start();
}
void MainWindow::acceptProperties()
{
    propertiesWorker_.wait(); // Join final thread-local cleanup before reusing it.
    DeviceProperties result = propertiesWorker_.takeResult();
    propertiesBusy_ = false;
    if (closing_) { close(); return; }
    if (pendingProperties_ && propertiesDialog_ && !propertiesDialog_->isRemoved()) {
        propertiesWorker_.setRequest(*pendingProperties_, propertiesRequest_);
        pendingProperties_.reset();
        propertiesBusy_ = true;
        propertiesWorker_.start();
        return;
    }
    pendingProperties_.reset();
    if (propertiesDialog_ && result.request == propertiesRequest_)
        propertiesDialog_->acceptResult(std::move(result));
}
