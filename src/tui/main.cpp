#include "inventory.h"
#include "devicelabel.h"
#include <final/final.h>
#include <QCoreApplication>
#include <QSysInfo>
#include <QStringList>
#include <algorithm>
#include <iostream>

namespace {
using namespace finalcut;

// Never pass terminal control characters from firmware/udev through to rendering.
FString display(QString value)
{
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        if (c.isOther() && c != '\n' && c != '\t') value[i] = QChar(0xfffd);
    }
    return FString(value.toStdWString());
}
QString known(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("Unavailable / undetermined") : value;
}
QString general(const Device &d)
{
    QString text;
    const auto field = [&text](const QString &label, const QString &value) {
        text += label + ": " + known(value) + '\n';
    };
    field("Name", deviceDisplayName(d, true));
    field("Category", categoryLabel(d.category));
    field("Bus / subsystem", d.subsystem);
    field("Location", d.path);
    field("Recorded parent", d.parentPath);
    field("Bound driver", d.driver);
    text += "\nInventory snapshot; current presence and health are not monitored.\n";
    text += "\nUdev properties (hardware identifiers and manufacturer where reported):\n";
    auto keys = d.properties.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) field(key, d.properties.value(key));
    text += "\nKernel attributes:\n";
    keys = d.attributes.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) {
        const Attribute &a = d.attributes[key];
        QString value = a.value;
        if (a.state != ReadState::Available) {
            switch (a.state) {
            case ReadState::PermissionDenied: value = "Permission denied"; break;
            case ReadState::Removed: value = "Device removed"; break;
            case ReadState::Error: value = "Read error"; break;
            case ReadState::Unsupported: value = "Unsupported"; break;
            case ReadState::NotApplicable: value = "Not applicable"; break;
            default: value = "Unavailable"; break;
            }
            if (a.error) value += QStringLiteral(" (errno %1)").arg(a.error);
        }
        field(key, value);
    }
    return text;
}
QString driver(const Device &d)
{
    return "Directly bound kernel driver: " + known(d.driver)
        + "\nOwning kernel module: " + known(d.driverModule)
        + "\n\nAn absent module link does not establish that a driver is built-in."
          "\nModule loadability / built-in status: undetermined"
          "\n\nNo module loading, unloading or device-management actions are performed.";
}

class Properties final : public FDialog {
public:
    Properties(const Device &device, FWidget *parent) : FDialog(parent), device_(device)
    {
        setText("Device properties");
        toggle_.addCallback("clicked", [this] {
            driver_ = !driver_;
            update();
        });
        close_.addCallback("clicked", [this] { close(); });
        update();
    }
private:
    void update()
    {
        text_.setText(display(driver_ ? driver(device_) : general(device_)));
        text_.scrollToBegin();
        toggle_.setText(driver_ ? "&General" : "&Driver");
        redraw();
    }
    void adjustSize() override
    {
        const auto w = std::max<std::size_t>(24, getDesktopWidth() > 4 ? getDesktopWidth() - 4 : 24);
        const auto h = std::max<std::size_t>(10, getDesktopHeight() > 4 ? getDesktopHeight() - 4 : 10);
        setGeometry(FPoint{2, 2}, FSize{w, h}, false);
        text_.setGeometry(FPoint{2, 2}, FSize{w - 4, h - 6});
        toggle_.setGeometry(FPoint{2, int(h) - 3}, FSize{12, 1});
        close_.setGeometry(FPoint{int(w) - 14, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); }
    const Device device_;
    bool driver_ = false;
    FTextView text_{this};
    FButton toggle_{this};
    FButton close_{"&Close", this};
};

class Browser final : public FDialog {
public:
    Browser(Inventory inventory, FWidget *parent) : FDialog(parent), inventory_(std::move(inventory))
    {
        tree_.addColumn("Device (Enter: properties; Left/Right: collapse/expand)");
        tree_.setTreeView();
        tree_.addCallback("clicked", [this] { properties(); });
        switch_.addCallback("clicked", [this] { connection_ = !connection_; populate(); });
        refresh_.addCallback("clicked", [this] {
            Inventory next = collectInventory();
            if (!next.error.isEmpty()) {
                FMessageBox::error(this, display(next.error));
                return;
            }
            inventory_ = std::move(next);
            populate();
        });
        quit_.addCallback("clicked", [this] { close(); });
        viewKey_.addCallback("activate", [this] { connection_ = !connection_; populate(); });
        refreshKey_.addCallback("activate", [this] { refresh_.emitCallback("clicked"); });
        quitKey_.addCallback("activate", [this] { close(); });
        populate();
    }
private:
    using Iter = FListView::iterator;
    void properties()
    {
        const auto *item = tree_.getCurrentItem();
        if (!item) return;
        const int index = item->getData<int>();
        if (index < 0 || index >= inventory_.devices.size()) return;
        Properties dialog(inventory_.devices.at(index), this);
        dialog.exec();
    }
    void populate()
    {
        tree_.clear();
        setText(connection_ ? "Devices by connection" : "Devices by type");
        switch_.setText(connection_ ? "By &type" : "By &connection");
        const QString hostname = QSysInfo::machineHostName();
        const auto root = tree_.insert(FStringList{display(hostname.isEmpty() ? "This computer" : hostname)}, -1);
        const auto &devices = inventory_.devices;
        QHash<QString, int> lookup;
        QVector<int> order;
        for (int i = 0; i < devices.size(); ++i) { lookup.insert(devices[i].path, i); order.append(i); }
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const int cmp = QString::localeAwareCompare(devices[a].name, devices[b].name);
            return cmp ? cmp < 0 : devices[a].path < devices[b].path;
        });
        if (!connection_) {
            for (const Category &category : categories()) {
                Iter group{};
                bool created = false;
                for (int i : order) {
                    const Device &d = devices[i];
                    if (d.hidden || d.category != QLatin1String(category.id)) continue;
                    if (!created) {
                        group = tree_.insert(FStringList{display(categoryLabel(d.category))}, -1, root);
                        created = true;
                    }
                    tree_.insert(FStringList{display(deviceDisplayName(d, true))}, int(i), group);
                }
            }
        } else {
            const auto parents = connectionParents(devices);
            QSet<QString> included;
            for (const Device &d : devices) {
                if (d.hidden) continue;
                QSet<QString> visited;
                QString path = d.path;
                while (lookup.contains(path) && !visited.contains(path)) {
                    visited.insert(path); included.insert(path); path = parents.value(path);
                }
            }
            QHash<QString, Iter> nodes;
            QSet<QString> building;
            Iter efi{};
            bool haveEfi = false;
            std::function<Iter(int)> ensure = [&](int i) -> Iter {
                const Device &d = devices[i];
                if (nodes.contains(d.path)) return nodes.value(d.path);
                building.insert(d.path);
                auto parent = root;
                const QString path = parents.value(d.path);
                if (d.subsystem == "efivarfs") {
                    if (!haveEfi) {
                        efi = tree_.insert(FStringList{"UEFI variables (firmware records)"}, -1, root);
                        haveEfi = true;
                    }
                    parent = efi;
                } else if (included.contains(path) && !building.contains(path)) parent = ensure(lookup.value(path));
                auto item = tree_.insert(FStringList{display(deviceDisplayName(d, true))}, int(i), parent);
                nodes.insert(d.path, item); building.remove(d.path);
                return item;
            };
            for (int i : order) if (included.contains(devices[i].path)) ensure(i);
        }
        static_cast<FListViewItem*>(*root)->expand();
        status_.setMessage(display(QStringLiteral("%1 records; %2 skipped. Arrows navigate/expand; Enter properties.")
            .arg(devices.size()).arg(inventory_.skipped)));
        redraw();
    }
    void adjustSize() override
    {
        const auto w = std::max<std::size_t>(40, getDesktopWidth());
        const auto h = std::max<std::size_t>(12, getDesktopHeight() > 1 ? getDesktopHeight() - 1 : 12);
        setGeometry(FPoint{1, 1}, FSize{w, h}, false);
        tree_.setGeometry(FPoint{2, 2}, FSize{w - 4, h - 6});
        switch_.setGeometry(FPoint{2, int(h) - 3}, FSize{20, 1});
        refresh_.setGeometry(FPoint{25, int(h) - 3}, FSize{12, 1});
        quit_.setGeometry(FPoint{int(w) - 13, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); }
    Inventory inventory_;
    bool connection_ = false;
    FListView tree_{this};
    FButton switch_{this};
    FButton refresh_{"&Refresh", this};
    FButton quit_{"&Quit", this};
    FStatusBar status_{this};
    FStatusKey viewKey_{FKey::F2, "View", &status_};
    FStatusKey refreshKey_{FKey::F5, "Refresh", &status_};
    FStatusKey quitKey_{FKey::F10, "Quit", &status_};
};
}

int main(int argc, char **argv)
{
    // QtCore supplies synchronous utilities and translation context only.
    // FINAL CUT exclusively owns terminal setup and event dispatch.
    QCoreApplication core(argc, argv);
    Inventory inventory = collectInventory();
    if (!inventory.error.isEmpty()) {
        std::cerr << inventory.error.toStdString() << '\n';
        return 1;
    }
    finalcut::FApplication app(argc, argv);
    Browser browser(std::move(inventory), &app);
    finalcut::FWidget::setMainWidget(&browser);
    browser.show();
    return app.exec();
}
