#include "inventory.h"
#include "searchdialog.h"
#include "devicetree.h"
#include "devicelabel.h"
#include <final/final.h>
#include <QCoreApplication>
#include <QSysInfo>
#include <QStringList>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace {
using namespace finalcut;

// Never pass terminal control characters from firmware/udev through to rendering.
FString display(QString value)
{
    for (qsizetype i = 0; i < value.size(); ++i) {
        const QChar c = value.at(i);
        const auto category = c.category();
        if ((category == QChar::Other_Control || category == QChar::Other_Format)
            && c != '\n' && c != '\t')
            value[i] = QChar(0xfffd);
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

// Snapshot-only matching: do not trigger deep collectors or filesystem reads.
bool matches(const Device &d, const QString &query)
{
    const auto contains = [&query](const QString &text) {
        return text.contains(query, Qt::CaseInsensitive);
    };
    for (const QString &text : {deviceDisplayName(d, true), d.name, d.sysname,
            d.path, d.parentPath, d.driver, d.driverModule, d.subsystem,
            d.category, categoryLabel(d.category)})
        if (contains(text)) return true;
    for (auto it = d.properties.cbegin(); it != d.properties.cend(); ++it)
        if (contains(it.value())) return true;
    for (auto it = d.attributes.cbegin(); it != d.attributes.cend(); ++it)
        if (it->state == ReadState::Available && contains(it->value)) return true;
    return false;
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
        findKey_.addCallback("activate", [this] { find(); });
        nextKey_.addCallback("activate", [this] { findNext(false); });
        previousKey_.addCallback("activate", [this] { findNext(true); });
        populate();
    }
private:
    // FINAL CUT returns vector iterators. Retain only item addresses between
    // insertions: adding siblings may reallocate the containing vector.
    FListViewItem *insertItem(const FString &label, int index, FListViewItem *parent = nullptr)
    {
        if (!parent) {
            const auto inserted = tree_.insert(FStringList{label}, int(index));
            return static_cast<FListViewItem*>(*inserted);
        }
        auto &siblings = parent->getParent()->getChildren();
        const auto position = std::find(siblings.begin(), siblings.end(), parent);
        if (position == siblings.end())
            throw std::logic_error("Device tree parent is not attached");
        const auto inserted = tree_.insert(FStringList{label}, int(index), position);
        return static_cast<FListViewItem*>(*inserted);
    }
    void find()
    {
        if (modalOpen_) return;
        modalOpen_ = true;
        SearchDialog dialog(query_, this);
        const auto result = dialog.exec();
        modalOpen_ = false;
        if (result != FDialog::ResultCode::Accept) return;
        query_ = dialog.query();
        if (query_.isEmpty()) {
            status_.setMessage("Search cleared. Enter: properties; F4: Find");
            redraw();
            return;
        }
        findNext(false, true);
    }
    void findNext(bool previous, bool includeCurrent = false)
    {
        if (modalOpen_) return;
        if (query_.isEmpty()) { find(); return; }
        // Traverse actual widget order (including collapsed branches), so header
        // sorting is respected. Category/computer rows never count as matches.
        QVector<FListViewItem*> items;
        std::function<void(FListViewItem*)> visit = [&](FListViewItem *item) {
            items.append(item);
            for (auto *child : item->getChildren())
                visit(static_cast<FListViewItem*>(child));
        };
        for (auto *item : tree_.getData()) visit(item);
        QVector<int> hits;
        int current = -1;
        for (int i = 0; i < items.size(); ++i) {
            if (items[i] == tree_.getCurrentItem()) current = i;
            const int index = items[i]->getData<int>();
            if (index >= 0 && index < inventory_.devices.size()
                && matches(inventory_.devices.at(index), query_)) hits.append(i);
        }
        if (hits.isEmpty()) {
            status_.setMessage("No matches in current view. F4: change search");
            redraw();
            return;
        }
        int hit = previous ? int(hits.size()) - 1 : 0;
        if (previous) {
            for (int i = int(hits.size()) - 1; i >= 0; --i)
                if (hits[i] < current || (includeCurrent && hits[i] == current)) { hit = i; break; }
        } else {
            for (int i = 0; i < hits.size(); ++i)
                if (hits[i] > current || (includeCurrent && hits[i] == current)) { hit = i; break; }
        }
        auto *target = items[hits[hit]];
        if (!tree_.selectItem(target)) {
            status_.setMessage("Could not select match. F4: change search");
            redraw();
            return;
        }
        status_.setMessage(display(QStringLiteral("Match %1/%2. F3: next; F6: previous")
            .arg(hit + 1).arg(hits.size())));
        redraw();
    }
    void properties()
    {
        const auto *item = tree_.getCurrentItem();
        if (!item) return;
        const int index = item->getData<int>();
        if (index < 0 || index >= inventory_.devices.size()) return;
        Properties dialog(inventory_.devices.at(index), this);
        modalOpen_ = true;
        dialog.exec();
        modalOpen_ = false;
    }
    void populate()
    {
        // Rebuild in projection order, even after a header-click sort. FINAL
        // CUT sorts on insertion; disable it before consuming returned positions.
        tree_.setColumnSort(-1);
        tree_.clear();
        setText(connection_ ? "Devices by connection" : "Devices by type");
        switch_.setText(connection_ ? "By &type" : "By &connection");
        const QString hostname = QSysInfo::machineHostName();
        auto *root = insertItem(display(hostname.isEmpty() ? "This computer" : hostname), -1);
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
                FListViewItem *group = nullptr;
                bool created = false;
                for (int i : order) {
                    const Device &d = devices[i];
                    if (d.hidden || d.category != QLatin1String(category.id)) continue;
                    if (!created) {
                        group = insertItem(display(categoryLabel(d.category)), -1, root);
                        created = true;
                    }
                    insertItem(display(deviceDisplayName(d, true)), i, group);
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
            QHash<QString, FListViewItem*> nodes;
            QSet<QString> building;
            FListViewItem *efi = nullptr;
            bool haveEfi = false;
            std::function<FListViewItem*(int)> ensure = [&](int i) -> FListViewItem* {
                const Device &d = devices[i];
                if (nodes.contains(d.path)) return nodes.value(d.path);
                building.insert(d.path);
                auto parent = root;
                const QString path = parents.value(d.path);
                if (d.subsystem == "efivarfs") {
                    if (!haveEfi) {
                        efi = insertItem("UEFI variables (firmware records)", -1, root);
                        haveEfi = true;
                    }
                    parent = efi;
                } else if (included.contains(path) && !building.contains(path)) parent = ensure(lookup.value(path));
                auto *item = insertItem(display(deviceDisplayName(d, true)), i, parent);
                nodes.insert(d.path, item); building.remove(d.path);
                return item;
            };
            for (int i : order) if (included.contains(devices[i].path)) ensure(i);
        }
        root->expand();
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
    bool modalOpen_ = false;
    QString query_;
    DeviceTree tree_{this};
    FButton switch_{this};
    FButton refresh_{"&Refresh", this};
    FButton quit_{"&Quit", this};
    FStatusBar status_{this};
    FStatusKey viewKey_{FKey::F2, "View", &status_};
    FStatusKey findKey_{FKey::F4, "Find", &status_};
    FStatusKey nextKey_{FKey::F3, "Next", &status_};
    FStatusKey previousKey_{FKey::F6, "Previous", &status_};
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
