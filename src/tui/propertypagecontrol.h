#pragma once
#include <final/final.h>

// Reserve F7/F8 for page cycling within Properties.
// All other input keeps the native text-view/button behavior.
template<typename Widget>
class PropertyPageControl final : public Widget {
public:
    using Widget::Widget;
protected:
    void onKeyPress(finalcut::FKeyEvent *event) override
    {
        const auto key = event->key();
        if (key == finalcut::FKey::F7 || key == finalcut::FKey::F8) {
            event->accept();
            this->emitCallback(key == finalcut::FKey::F8 ? "next-page" : "previous-page");
            return;
        }
        Widget::onKeyPress(event);
    }
};
