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
    void navigate(finalcut::FKey key);
};
