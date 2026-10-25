#include "browsermenu.h"
#include "systemreport.h"
#include "terminaltext.h"
#include <utility>

using namespace finalcut;

BrowserMenu::BrowserMenu(FWidget *parent, std::function<void(bool)> selectView,
    std::function<void(bool)> showAll, std::function<void()> systemInformation) : FMenu("View", parent)
{
    separator_.setSeparator();
    systemSeparator_.setSeparator();
    system_.setText(display(systemInformationTitle()));
    system_.addCallback("clicked", std::move(systemInformation));
    type_.addCallback("clicked", [selectView] { selectView(false); });
    connection_.addCallback("clicked", [selectView] { selectView(true); });
    all_.addCallback("clicked", [this, showAll = std::move(showAll)] {
        showAll(all_.isChecked());
    });
}

void BrowserMenu::setState(bool connection, bool allDevices)
{
    if (connection) {
        type_.unsetChecked();
        connection_.setChecked();
    } else {
        connection_.unsetChecked();
        type_.setChecked();
    }
    if (allDevices) all_.setChecked();
    else all_.unsetChecked();
}

void BrowserMenu::open()
{
    if (isShown()) return;
    // Follow FINAL CUT's native dialog-menu opening path. Its close path
    // restores the parent dialog and its last focused control, also on Escape.
    setPos(FPoint{getParentWidget()->getTermX() + 1,
        getParentWidget()->getTermY() + 1});
    setOpenMenu(this);
    setVisible();
    show();
    raiseWindow();
    selectFirstItem();
    if (auto *item = getSelectedItem()) item->setFocus();
    redraw();
}
