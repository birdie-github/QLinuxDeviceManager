#pragma once
#include "enumerator.h"
#include "deviceproperties.h"
#include <QPointer>
#include <optional>
#include <QMainWindow>
#include <QSet>

class QAction;
class DeviceModel;
class QTreeView;
class QCloseEvent;
class PropertiesDialog;

class MainWindow final : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();
    ~MainWindow() override;
protected:
    void closeEvent(QCloseEvent *event) override;
private:
    void refresh();
    void openProperties();
    void requestProperties();
    void acceptProperties();
    void acceptInventory();
    void rememberTree();
    void restoreTree();
    void updateStatus();
    void saveSettings();
    DeviceModel *model_ = nullptr;
    QTreeView *tree_ = nullptr;
    QAction *refreshAction_ = nullptr;
    QAction *internalAction_ = nullptr;
    QAction *propertiesAction_ = nullptr;
    QPointer<PropertiesDialog> propertiesDialog_;
    PropertiesReader propertiesWorker_;
    std::optional<Device> pendingProperties_;
    quint64 propertiesRequest_ = 0;
    bool propertiesBusy_ = false;
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
