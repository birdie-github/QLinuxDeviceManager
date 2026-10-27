#pragma once
#include "inventory.h"
#include "devicetree.h"
#include <QSet>
#include <array>

// Builds frontend projections and preserves the unfiltered state of each view.
// Keys are paths/group identities; no item pointer survives a rebuild.
class TreeProjection final {
public:
    explicit TreeProjection(DeviceTree &tree) : tree_(tree) {}
    int rebuild(const Inventory &inventory, bool connection, const QString &query, bool showAll);
private:
    struct State { QSet<QString> expanded; QString selected; bool valid = false; };
    void saveState();
    void restoreState();
    finalcut::FListViewItem *insertItem(const finalcut::FString &label, int index,
        finalcut::FListViewItem *parent = nullptr);
    DeviceTree &tree_;
    const Inventory *inventory_ = nullptr; // Valid only inside rebuild().
    QHash<finalcut::FListViewItem*, QString> keys_;
    std::array<State, 2> states_;
    bool initialized_ = false;
    bool connection_ = false;
    bool showAll_ = false;
    QString query_;
};
