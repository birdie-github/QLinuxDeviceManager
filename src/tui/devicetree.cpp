#include "devicetree.h"

using namespace finalcut;

void DeviceTree::navigate(FKey key)
{
    // firstPos()/stepForward() are private in released FINAL CUT versions.
    // Use the widget's normal keyboard handler to update selection and scrolling.
    FKeyEvent event(Event::KeyPress, key);
    FListView::onKeyPress(&event);
}

bool DeviceTree::selectItem(FListViewItem *item, bool focus)
{
    if (!item) return false;
    FObject *owner = item->getParent();
    while (owner && owner != this) owner = owner->getParent();
    if (!owner) return false;

    if (focus) setFocus();
    // Reset before expanding branches changes the visible row positions.
    navigate(FKey::Home);
    for (auto *ancestor = item->getParent(); ancestor != this;
         ancestor = ancestor->getParent())
        static_cast<FListViewItem*>(ancestor)->expand();
    redraw();

    // Native Down events maintain FINAL CUT's viewport and scrollbar state.
    // Bound traversal and stop if the widget cannot advance.
    const auto count = getCount();
    for (std::size_t i = 0; i < count; ++i) {
        auto *current = getCurrentItem();
        if (current == item) return true;
        navigate(FKey::Down);
        if (getCurrentItem() == current) break;
    }
    return getCurrentItem() == item;
}
