#pragma once
#include <final/final.h>

// FINAL CUT 0.9.1's dropdown window needs the same modal-event exemption as
// native menus. Keep this compatibility adjustment out of individual dialogs.
class ModalComboBox : public finalcut::FComboBox {
public:
    explicit ModalComboBox(finalcut::FWidget *parent);
    bool isDropDownShown() const;
protected:
    void onKeyPress(finalcut::FKeyEvent *event) override;
private:
    finalcut::FWidget *popup_ = nullptr; // Owned by FComboBox for its lifetime.
};
