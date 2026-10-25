#include "systemdialog.h"
#include "systemsnapshot.h"
#include "systempages/pages.h"
#include "propertypagecontrol.h"
#include "propertiesstatus.h"
#include "terminaltext.h"
#include <algorithm>
#include <array>

namespace {
using namespace finalcut;
class SystemTextView : public FTextView {
public:
    using FTextView::FTextView;
private:
    void drawBorder() override {} // Native content/scrollbars, no nested box.
};
class SystemInformation final : public FDialog {
public:
    SystemInformation(SystemSnapshot &snapshot, FWidget *parent) : FDialog(parent), snapshot_(snapshot)
    {
        setModal();
        text_.ignorePadding();
        chips_.unsetEditable();
        chips_.addCallback("row-changed", [this] {
            if (updatingChips_ || chips_.getCount() == 0) return;
            chip_ = chips_.getItemData<int>();
            render();
        });
        const auto titles = systemPageTitles();
        for (int i = 0; i < 4; ++i) {
            tabs_[i].setText(display(titles.at(i)));
            tabs_[i].addCallback("clicked", [this, i] { setPage(i); });
        }
        close_.addCallback("clicked", [this] { close(); });
        const std::array<FWidget*, 7> controls {&text_, &tabs_[0], &tabs_[1], &tabs_[2], &tabs_[3], &chips_, &close_};
        for (auto *control : controls) {
            control->addCallback("next-page", [this] { setPage((page_ + 1) % 4); });
            control->addCallback("previous-page", [this] { setPage((page_ + 3) % 4); });
        }
        snapshot_.ensure();
        updateChips();
        render();
        if (snapshot_.collecting()) timer_ = addTimer(100);
    }
    ~SystemInformation() override { delTimer(timer_); }
private:
    void onTimer(FTimerEvent *event) override
    {
        if (snapshot_.poll()) {
            updateChips();
            render();
            delTimer(timer_);
            timer_ = 0;
        }
        FDialog::onTimer(event);
    }
    void updateChips()
    {
        updatingChips_ = true;
        chips_.clear();
        if (const auto *snapshot = snapshot_.get()) {
            const auto &devices = snapshot->memoryDevices.devices;
            if (chip_ < 0 || chip_ >= devices.size()) chip_ = 0;
            for (int i = 0; i < devices.size(); ++i)
                chips_.insert(display(systemMemoryTitle(devices.at(i).index)), int(i));
            chips_.setMaxVisibleItems(8);
            if (!devices.isEmpty()) chips_.setCurrentItem(static_cast<std::size_t>(chip_ + 1));
        }
        updatingChips_ = false;
    }
    void setPage(int page)
    {
        page_ = page;
        render();
        if (isShown()) text_.setFocus();
    }
    void render()
    {
        setText(display(systemInformationTitle() + " - " + systemPageTitles().at(page_)));
        PropertyPage content;
        if (const auto *snapshot = snapshot_.get()) {
            switch (static_cast<SystemPage>(page_)) {
            case SystemPage::OperatingSystem: content = systemOperatingSystemPage(*snapshot); break;
            case SystemPage::Motherboard: content = systemMotherboardPage(*snapshot); break;
            case SystemPage::Processor: content = systemProcessorPage(*snapshot); break;
            case SystemPage::Ram: content = systemRamPage(*snapshot, chip_); break;
            }
        } else content.text = systemCollectingText();
        text_.setText(display(content.text));
        if (!FTerm::isMonochron()) {
            const auto background = text_.getBackgroundColor() == FColor::White ? FColor::LightGray : FColor::White;
            const FColorPair stripe{FColor::Black, background};
            for (int line : content.shadedLines)
                text_.addHighlight(static_cast<std::size_t>(line), FTextView::FTextHighlight{0, stripe});
        }
        text_.scrollToBegin();
        preferredHeight_ = std::clamp<std::size_t>(static_cast<std::size_t>(content.text.count('\n')) + 7, 14, 32);
        for (int i = 0; i < 4; ++i) tabs_[i].setEnable(i != page_);
        const bool showChips = page_ == 3 && chips_.getCount() > 0;
        chips_.setEnable(showChips);
        chips_.setFocusable(showChips);
        if (!showChips && chips_.hasFocus()) text_.setFocus();
        if (showChips) { chips_.setVisible(); if (isShown()) chips_.show(); }
        else { chips_.hide(); chips_.setVisible(false); }
        if (isShown()) adjustSize();
        redraw();
    }
    void adjustSize() override
    {
        const auto dw = getDesktopWidth(), dh = getDesktopHeight();
        const auto w = std::min<std::size_t>(110, std::max<std::size_t>(76, dw > 4 ? dw - 4 : 76));
        const auto h = std::min(preferredHeight_, std::max<std::size_t>(12, dh > 4 ? dh - 4 : 12));
        const FSize size{w, h};
        const FPoint pos{dw > w ? int((dw - w) / 2) + 1 : 1, dh > h ? int((dh - h) / 2) + 1 : 1};
        if (isShown()) { FDialog::setSize(size, false); FDialog::setPos(pos, false); }
        else setGeometry(pos, size, false);
        int x = 1;
        const std::array<std::size_t, 4> widths {20, 15, 14, 7};
        for (int i = 0; i < 4; ++i) {
            tabs_[i].setGeometry(FPoint{x, 1}, FSize{widths[i], 1});
            x += int(widths[i]) + 1;
        }
        text_.setGeometry(FPoint{2, 4}, FSize{w - 2, h - 5});
        chips_.setGeometry(FPoint{2, int(h) - 3}, FSize{26, 1});
        close_.setGeometry(FPoint{int(w) - 12, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); text_.setFocus(); }
    SystemSnapshot &snapshot_;
    int page_ = 0;
    int chip_ = 0;
    int timer_ = 0;
    bool updatingChips_ = false;
    std::size_t preferredHeight_ = 14;
    PropertyPageControl<SystemTextView> text_{this};
    std::array<PropertyPageControl<FButton>, 4> tabs_ {
        PropertyPageControl<FButton>{this}, PropertyPageControl<FButton>{this},
        PropertyPageControl<FButton>{this}, PropertyPageControl<FButton>{this}};
    PropertyPageControl<FComboBox> chips_{this};
    PropertyPageControl<FButton> close_{"&Close", this};
    PropertiesStatus status_{FWidget::getStatusBar(), [this](int step) { setPage((page_ + step + 4) % 4); }};
};
}
void showSystemInformation(SystemSnapshot &snapshot, finalcut::FWidget *parent)
{
    SystemInformation dialog(snapshot, parent);
    dialog.exec();
}
