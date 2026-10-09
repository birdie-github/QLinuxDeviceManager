#pragma once
#include "enumerator.h"
#include "deviceproperties.h"
#include "systemproperties.h"
#include <QPointer>
#include <optional>
#include <QMainWindow>
#include <QSet>
#include <QTimer>
#include "devicesearch.h"

class QAction;
class QLineEdit;
class QCheckBox;
class DeviceFilter;
class DeviceModel;
class QTreeView;
class QCloseEvent;
class PropertiesDialog;
class SystemDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void refresh();
    void applyFilter();
    void cancelSearch();
    void startSearch();
    void acceptSearchBatch(const SearchBatch &batch);
    void searchFinished();
    void openProperties();
    void openSystemInformation();
    void requestSystemInformation();
    void acceptSystemInformation();
    void requestProperties();
    void acceptProperties();
    void acceptInventory();
    void rememberTree();
    void restoreTree();
    void updateStatus();
    void saveSettings();
    DeviceModel *model_ = nullptr;
    QTreeView *tree_ = nullptr;
    DeviceFilter *filter_ = nullptr;
    QWidget *filterBar_ = nullptr;
    QLineEdit *filterEdit_ = nullptr;
    QCheckBox *deepSearch_ = nullptr;
    QAction *filterAction_ = nullptr;
    QTimer filterTimer_;
    DeepSearchWorker searchWorker_;
    quint64 searchRequest_ = 0;
    quint64 searchRevision_ = 1;
    bool searchBusy_ = false;
    bool searchPending_ = false;
    int searchDone_ = 0;
    int searchTotal_ = 0;
    int searchUnavailable_ = 0;
    QAction *refreshAction_ = nullptr;
    QAction *internalAction_ = nullptr;
    QAction *propertiesAction_ = nullptr;
    QPointer<PropertiesDialog> propertiesDialog_;
    PropertiesReader propertiesWorker_;
    std::optional<Device> pendingProperties_;
    quint64 propertiesRequest_ = 0;
    bool propertiesBusy_ = false;
    SystemDialog *systemDialog_ = nullptr;
    SystemPropertiesReader systemWorker_;
    bool systemBusy_ = false;
    Enumerator worker_;
    quint64 requested_ = 0;
    bool busy_ = false;
    bool pending_ = false;
    bool closing_ = false;
    QSet<QString> expanded_;
    QString selectedPath_;
    quint64 selectedGeneration_ = 0;
    QString scanNote_;
};
