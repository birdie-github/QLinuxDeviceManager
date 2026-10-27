#include "systemdialog.h"
#include "systemsnapshot.h"
#include "modalcombobox.h"
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
void setContent(FTextView &view, const PropertyPage &content)
{
    view.setText(display(content.text));
    if (!FTerm::isMonochron()) {
        const auto background = view.getBackgroundColor() == FColor::White ? FColor::LightGray : FColor::White;
        const FColorPair stripe{FColor::Black, background};
        for (int line : content.shadedLines)
            view.addHighlight(static_cast<std::size_t>(line), FTextView::FTextHighlight{0, stripe});
    }
    view.scrollToBegin();
}
class SystemInformation final : public FDialog {
public:
    SystemInformation(SystemSnapshot &snapshot, FWidget *parent) : FDialog(parent), snapshot_(snapshot)
    {
        setModal();
        text_.ignorePadding();
        ramSummary_.ignorePadding();
        chips_.ignorePadding();
        chips_.unsetEditable();
        chips_.addCallback("row-changed", [this] {
            if (updatingChips_ || chips_.getCount() == 0) return;
            chip_ = chips_.getItemData<int>();
            if (page_ == 3) {
                if (const auto *data = snapshot_.get()) {
                    // Native row-changed fires while the popup is navigating.
                    // Update only the table; moving/resizing its owner here
                    // would disturb the popup's geometry and focus.
                    setContent(text_, systemRamPage(*data, chip_));
                    text_.redraw();
                }
            }
        });
        chips_.addCallback("clicked", [this] { render(); });
        const auto titles = systemPageTitles();
        for (int i = 0; i < 4; ++i) {
            tabs_[i].setText(display(titles.at(i)));
            tabs_[i].addCallback("clicked", [this, i] { setPage(i); });
        }
        close_.addCallback("clicked", [this] { close(); });
        const std::array<FWidget*, 8> controls {&text_, &ramSummary_, &tabs_[0], &tabs_[1], &tabs_[2], &tabs_[3], &chips_, &close_};
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
        chips_.hideDropDown();
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
        setContent(text_, content);
        const auto *data = snapshot_.get();
        ramLayout_ = page_ == 3 && data;
        ramSummaryHeight_ = 0;
        if (ramLayout_) {
            const auto summary = systemRamSummaryPage(*data);
            setContent(ramSummary_, summary);
            ramSummaryHeight_ = static_cast<std::size_t>(summary.text.count('\n')) + 2;
            ramSummary_.setVisible();
            ramSummary_.setFocusable(true);
            if (isShown() && !ramSummary_.isShown()) ramSummary_.show();
        } else {
            if (ramSummary_.hasFocus()) text_.setFocus();
            ramSummary_.hide();
            ramSummary_.setVisible(false);
            ramSummary_.setFocusable(false);
        }
        const bool showChips = ramLayout_ && data->memoryDevices.status.state == ReadState::Available
            && chips_.getCount() > 0;
        preferredHeight_ = std::clamp<std::size_t>(static_cast<std::size_t>(content.text.count('\n'))
            + 7 + ramSummaryHeight_ + (showChips ? 1 : 0), 14, 32);
        for (int i = 0; i < 4; ++i) tabs_[i].setEnable(i != page_);
        chips_.setEnable(showChips);
        chips_.setFocusable(showChips);
        if (!showChips && chips_.hasFocus()) text_.setFocus();
        if (showChips) { chips_.setVisible(); if (isShown() && !chips_.isShown()) chips_.show(); }
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
        std::size_t bodyTop = 4;
        if (ramLayout_) {
            ramSummary_.setGeometry(FPoint{2, 4}, FSize{w - 2, ramSummaryHeight_});
            if (chips_.isVisible()) {
                // Replace the former static Chip heading with the selector.
                const auto selectorY = 4 + ramSummaryHeight_;
                chips_.setGeometry(FPoint{3, int(selectorY)}, FSize{26, 1});
                bodyTop = selectorY + 1; // Its shadow shares the unused text header.
            } else bodyTop += ramSummaryHeight_ - 1;
        }
        text_.setGeometry(FPoint{2, int(bodyTop)}, FSize{w - 2, h > bodyTop + 1 ? h - bodyTop - 1 : 3});
        close_.setGeometry(FPoint{int(w) - 12, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); text_.setFocus(); }
    SystemSnapshot &snapshot_;
    int page_ = 0;
    int chip_ = 0;
    int timer_ = 0;
    bool updatingChips_ = false;
    bool ramLayout_ = false;
    std::size_t ramSummaryHeight_ = 0;
    std::size_t preferredHeight_ = 14;
    PropertyPageControl<SystemTextView> text_{this};
    PropertyPageControl<SystemTextView> ramSummary_{this};
    std::array<PropertyPageControl<FButton>, 4> tabs_ {
        PropertyPageControl<FButton>{this}, PropertyPageControl<FButton>{this},
        PropertyPageControl<FButton>{this}, PropertyPageControl<FButton>{this}};
    PropertyPageControl<ModalComboBox> chips_{this};
    PropertyPageControl<FButton> close_{"&Close", this};
    PropertiesStatus status_{FWidget::getStatusBar(), [this](int step) { setPage((page_ + step + 4) % 4); }};
};
}
void showSystemInformation(SystemSnapshot &snapshot, finalcut::FWidget *parent)
{
    SystemInformation dialog(snapshot, parent);
    dialog.exec();
}
