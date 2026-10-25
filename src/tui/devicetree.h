#pragma once
#include <final/final.h>

// Native tree navigation shared by search and future selection restoration.
class DeviceTree final : public finalcut::FListView {
public:
    explicit DeviceTree(finalcut::FWidget *parent) : FListView(parent) {}
    bool selectItem(finalcut::FListViewItem *item, bool focus = true);
protected:
    void onKeyPress(finalcut::FKeyEvent *event) override;
private:
    // The browser provides the surrounding frame. Keep native item and
    // scrollbar rendering; suppress only the redundant inner border.
    void drawBorder() override {}
    void navigate(finalcut::FKey key);
};
