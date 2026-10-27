#include "modalcombobox.h"

using namespace finalcut;

ModalComboBox::ModalComboBox(FWidget *parent) : FComboBox(parent)
{
    for (auto *child : getChildren()) {
        if (!child->isWidget()) continue;
        auto *widget = static_cast<FWidget*>(child);
        if (!widget->isInstanceOf("FDropDownListBox")) continue;
        popup_ = widget;
        // Public flags API, matching FMenu::setMenuWidget(). Without this,
        // FApplication rejects the popup's key, mouse and focus events while
        // any dialog is modal. Do not mark it modal or alter the dialog counter.
        popup_->setFlags().type.menu_widget = true;
        break;
    }
}
bool ModalComboBox::isDropDownShown() const
{
    return popup_ && popup_->isShown();
}
void ModalComboBox::onKeyPress(FKeyEvent *event)
{
    const auto key = event->key();
    if (isDropDownShown() && (key == FKey::Tab || key == FKey::Back_tab)) {
        hideDropDown();
        FComboBox::onKeyPress(event);
        event->accept();
        return;
    }
    FComboBox::onKeyPress(event);
}
