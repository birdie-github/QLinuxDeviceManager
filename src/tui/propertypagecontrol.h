#pragma once
#include <final/final.h>

// Intercept Tab before FINAL CUT's child-level focus traversal consumes it.
// All other input keeps the native text-view/button behavior.
template<typename Widget>
class PropertyPageControl final : public Widget {
public:
    using Widget::Widget;
protected:
    void onKeyPress(finalcut::FKeyEvent *event) override
    {
        const auto key = event->key();
        if (key == finalcut::FKey::Tab || key == finalcut::FKey::Back_tab) {
            event->accept();
            this->emitCallback(key == finalcut::FKey::Tab ? "next-page" : "previous-page");
            return;
        }
        Widget::onKeyPress(event);
    }
};
