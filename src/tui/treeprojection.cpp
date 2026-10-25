#include "treeprojection.h"
#include "inventoryfilter.h"
#include "terminaltext.h"
#include "devicelabel.h"
#include <QSysInfo>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <utility>

using namespace finalcut;

// FINAL CUT returns vector iterators. Retain only item addresses between
// insertions: adding siblings may reallocate the containing vector.
FListViewItem *TreeProjection::insertItem(const FString &label, int index, FListViewItem *parent)
{
    if (!parent) {
        const auto inserted = tree_.insert(FStringList{label}, int(index));
        auto *item = static_cast<FListViewItem*>(*inserted);
        keys_.insert(item, "computer");
        return item;
    }
    auto &siblings = parent->getParent()->getChildren();
    const auto position = std::find(siblings.begin(), siblings.end(), parent);
    if (position == siblings.end())
        throw std::logic_error("Device tree parent is not attached");
    const auto inserted = tree_.insert(FStringList{label}, int(index), position);
    auto *item = static_cast<FListViewItem*>(*inserted);
    keys_.insert(item, index >= 0 ? "device:" + inventory_->devices.at(index).path
        : "group:" + QString::fromStdWString(label.toWString()));
    return item;
}

void TreeProjection::saveState()
{
    auto &state = states_[connection_ ? 1 : 0];
    state = {};
    state.valid = true;
    state.selected = keys_.value(tree_.getCurrentItem());
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it)
        if (it.key()->isExpand()) state.expanded.insert(it.value());
}

void TreeProjection::restoreState()
{
    const auto &state = states_[connection_ ? 1 : 0];
    if (!state.valid) return;
    FListViewItem *selected = nullptr;
    for (auto it = keys_.cbegin(); it != keys_.cend(); ++it) {
        if (state.expanded.contains(it.value())) it.key()->expand();
        else it.key()->collapse();
        if (it.value() == state.selected) selected = it.key();
    }
    tree_.redraw();
    if (selected) tree_.selectItem(selected, false);
}

int TreeProjection::rebuild(const Inventory &inventory, bool connection, const QString &query)
{
    if (initialized_ && query_.isEmpty()) saveState();
    initialized_ = true;
    connection_ = connection;
    query_ = query;
    inventory_ = &inventory;
    int count = 0;
    // Rebuild in projection order, even after a header-click sort. FINAL
    // CUT sorts on insertion; disable it before consuming returned positions.
    tree_.setColumnSort(-1);
    keys_.clear();
    tree_.clear();
    const QString hostname = QSysInfo::machineHostName();
    auto *root = insertItem(display(hostname.isEmpty() ? "This computer" : hostname), -1);
    const auto &devices = inventory.devices;
    QHash<QString, int> lookup;
    QVector<int> order;
    for (int i = 0; i < devices.size(); ++i) { lookup.insert(devices[i].path, i); order.append(i); }
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const int cmp = QString::localeAwareCompare(devices[a].name, devices[b].name);
        return cmp ? cmp < 0 : devices[a].path < devices[b].path;
    });
    if (!connection) {
        for (const Category &category : categories()) {
            FListViewItem *group = nullptr;
            bool created = false;
            for (int i : order) {
                const Device &d = devices[i];
                if (d.hidden || d.category != QLatin1String(category.id)
                    || (!query.isEmpty() && !matches(d, query))) continue;
                if (!created) {
                    group = insertItem(display(categoryLabel(d.category)), -1, root);
                    created = true;
                }
                insertItem(display(deviceDisplayName(d, true)), i, group);
                ++count;
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
        if (!query.isEmpty()) {
            QSet<QString> filtered;
            for (const Device &d : devices) {
                if (!included.contains(d.path) || !matches(d, query)) continue;
                ++count;
                QString path = d.path;
                QSet<QString> visited;
                while (included.contains(path) && !visited.contains(path)) {
                    visited.insert(path);
                    filtered.insert(path);
                    path = parents.value(path);
                }
            }
            included = std::move(filtered);
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

    if (query.isEmpty()) restoreState();
    else {
        for (auto *item : keys_.keys()) item->expand();
        tree_.redraw();
    }
    inventory_ = nullptr;
    return count;
}
