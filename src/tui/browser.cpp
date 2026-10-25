#include "browser.h"
#include "browsermenu.h"
#include "systemdialog.h"
#include "systemsnapshot.h"
#include "treeprojection.h"
#include "inventoryfilter.h"
#include "terminaltext.h"
#include "propertiesdialog.h"
#include <algorithm>
#include <functional>
#include <utility>

namespace {
using namespace finalcut;
class Browser final : public FDialog {
public:
    Browser(Inventory inventory, FWidget *parent) : FDialog(parent), inventory_(std::move(inventory)),
        menu_(this, [this](bool connection) { setView(connection); },
            [this](bool show) { showAll_ = show; populate(); },
            [this] { systemInformation(); })
    {
        // The dialog supplies the only frame and its caption. An unnamed
        // column keeps FINAL CUT from drawing a separate tree headline.
        tree_.addColumn("");
        tree_.ignorePadding();
        treeCaption_.ignorePadding();
        tree_.setTreeView();
        filter_.setLabelText("&Filter");
        // Keep the field and its separate label out of initial layout/focus.
        filter_.setVisible(false);
        filter_.setEnable(false);
        filter_.setFocusable(false);
        filter_.getLabelObject()->setVisible(false);
        filter_.addCallback("changed", [this] { filterChanged(); });
        filter_.addCallback("activate", [this] {
            if (query_.isEmpty()) tree_.setFocus();
            else findNext(false, true);
            redraw();
        });
        tree_.addCallback("clicked", [this] { properties(); });
        switch_.addCallback("clicked", [this] { setView(!connection_); });
        refresh_.addCallback("clicked", [this] {
            Inventory next = collectInventory();
            if (!next.error.isEmpty()) {
                FMessageBox::error(this, display(next.error));
                return;
            }
            inventory_ = std::move(next);
            systemSnapshot_.refresh();
            if (!systemTimer_) systemTimer_ = addTimer(200);
            populate();
        });
        quit_.addCallback("clicked", [this] { close(); });
        viewKey_.addCallback("activate", [this] { setView(!connection_); });
        menuKey_.addCallback("activate", [this] {
            if (!modalOpen_) menu_.open();
        });
        refreshKey_.addCallback("activate", [this] { refresh_.emitCallback("clicked"); });
        quitKey_.addCallback("activate", [this] { close(); });
        findKey_.addCallback("activate", [this] { find(); });
        nextKey_.addCallback("activate", [this] { findNext(false); });
        previousKey_.addCallback("activate", [this] { findNext(true); });
        populate();
    }
    ~Browser() override { delTimer(systemTimer_); }
private:
    void onTimer(FTimerEvent *event) override
    {
        // The System Information dialog polls while modal; do not consume its
        // completion notification here. Other dialogs can defer publication.
        if (!modalOpen_) {
            systemSnapshot_.poll();
            if (!systemSnapshot_.collecting() && systemTimer_) {
                delTimer(systemTimer_);
                systemTimer_ = 0;
            }
        }
        FDialog::onTimer(event);
    }
    void systemInformation()
    {
        if (modalOpen_) return;
        modalOpen_ = true;
        showSystemInformation(systemSnapshot_, this);
        modalOpen_ = false;
        if (systemSnapshot_.collecting() && !systemTimer_) systemTimer_ = addTimer(200);
    }
    void setView(bool connection)
    {
        if (connection_ == connection) return;
        connection_ = connection;
        populate();
    }
    void find()
    {
        if (modalOpen_) return;
        if (!filterVisible_) {
            filterVisible_ = true;
            filter_.setEnable(true);
            filter_.setFocusable(true);
            filter_.setVisible(true);
            filter_.getLabelObject()->setVisible(true);
            adjustSize();
            filter_.show();
            filter_.getLabelObject()->show();
        }
        filter_.setFocus();
        redraw();
    }
    void filterChanged()
    {
        const QString query = QString::fromStdWString(filter_.getText().toWString()).trimmed();
        if (query == query_) return;
        query_ = query;
        populate();
    }
    void clearFilter()
    {
        filter_.setText("");
        filterChanged(); // Also covers toolkit versions where setText emits no change.
        tree_.setFocus();
        if (filterVisible_) {
            filterVisible_ = false;
            filter_.hide(); // FINAL CUT also hides the field's separate label.
            filter_.setVisible(false);
            filter_.setEnable(false);
            filter_.setFocusable(false);
            filter_.getLabelObject()->setVisible(false);
            adjustSize();
        }
        redraw();
    }
    void onKeyPress(FKeyEvent *event) override
    {
        if (event->key() == FKey::Meta_i && !modalOpen_) {
            systemInformation();
            event->accept();
            return;
        }
        if (event->key() == FKey::Ctrl_f && !modalOpen_) {
            find();
            event->accept();
            return;
        }
        if (event->key() == FKey::Escape && !modalOpen_) {
            clearFilter();
            event->accept();
            return;
        }
        FDialog::onKeyPress(event);
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
        modalOpen_ = true;
        showProperties(inventory_.devices.at(index), this);
        modalOpen_ = false;
    }
    void populate()
    {
        FString title = connection_ ? "Devices by connection" : "Devices by type";
        if (showAll_) title += " (All Devices)";
        setText(title);
        menu_.setState(connection_, showAll_);
        switch_.setText(connection_ ? "By &type" : "By &connection");
        const bool editing = filter_.hasFocus();
        const int count = projection_.rebuild(inventory_, connection_, query_, showAll_);
        if (editing) filter_.setFocus();
        status_.setMessage(display(query_.isEmpty() ? QStringLiteral("F4: filter; Enter: properties")
            : QStringLiteral("%1 matches. Esc: clear filter").arg(count)));
        redraw();
    }
    void adjustSize() override
    {
        const auto w = std::max<std::size_t>(40, getDesktopWidth());
        const auto h = std::max<std::size_t>(12, getDesktopHeight() > 1 ? getDesktopHeight() - 1 : 12);
        setGeometry(FPoint{1, 1}, FSize{w, h}, false);
        filter_.setGeometry(FPoint{10, 2}, FSize{w - 13, 1});
        // Place the tree's unused header on the outer frame when unfiltered.
        // Its native bottom scrollbar sits immediately above the buttons.
        tree_.setGeometry(FPoint{2, filterVisible_ ? 5 : 2},
            FSize{w - 2, h - (filterVisible_ ? 6 : 3)});
        treeCaption_.setGeometry(FPoint{3, 2},
            FSize{std::min<std::size_t>(w - 5, treeCaption_.getText().getLength()), 1});
        switch_.setGeometry(FPoint{2, int(h) - 3}, FSize{20, 1});
        refresh_.setGeometry(FPoint{25, int(h) - 3}, FSize{12, 1});
        quit_.setGeometry(FPoint{int(w) - 13, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); }
    Inventory inventory_;
    bool connection_ = false;
    bool showAll_ = false;
    int systemTimer_ = 0;
    bool modalOpen_ = false;
    bool filterVisible_ = false;
    QString query_;
    FLineEdit filter_{this};
    DeviceTree tree_{this};
    FLabel treeCaption_{" Device (Enter: expand/collapse branch; properties for leaves) ", this};
    TreeProjection projection_{tree_};
    SystemSnapshot systemSnapshot_;
    BrowserMenu menu_;
    FButton switch_{this};
    FButton refresh_{"&Refresh", this};
    FButton quit_{"&Quit", this};
    FStatusBar status_{this};
    FStatusKey viewKey_{FKey::F2, "View", &status_};
    FStatusKey findKey_{FKey::F4, "Filter", &status_};
    FStatusKey nextKey_{FKey::F3, "Next", &status_};
    FStatusKey previousKey_{FKey::F6, "Previous", &status_};
    FStatusKey refreshKey_{FKey::F5, "Refresh", &status_};
    FStatusKey menuKey_{FKey::F9, "Menu", &status_};
    FStatusKey quitKey_{FKey::F10, "Quit", &status_};
};
}

int runBrowser(Inventory inventory, finalcut::FApplication &app)
{
    Browser browser(std::move(inventory), &app);
    finalcut::FWidget::setMainWidget(&browser);
    browser.show();
    return app.exec();
}
