#include "propertiesdialog.h"
#include "propertypages.h"
#include "devicepages/resourcespage.h"
#include "devicepages/usbpage.h"
#include "devicepages/efipage.h"
#include "propertypagecontrol.h"
#include "propertiesstatus.h"
#include "terminaltext.h"
#include "devicelabel.h"
#include <algorithm>
#include <array>

namespace {
using namespace finalcut;
class PropertyTextView : public FTextView {
public:
    using FTextView::FTextView;
private:
    void drawBorder() override {}
};
class Properties final : public FDialog {
public:
    Properties(const Device &device, FWidget *parent)
        : FDialog(parent), device_(device), entries_(commonPropertyEntries(inventoryCommonProperties(device), false, PropertyPresentation::Tui)), efiReader_(device)
    {
        setModal();
        const bool hasResources = device_.resources && device_.resources->hasInformation();
        const bool hasUsb = device_.subsystem == "usb" && device_.devtype == "usb_device" && device_.usb;
        if (hasResources) pages_.append(3);
        if (hasUsb) pages_.append(4);
        usb_.setVisible(hasUsb);
        usb_.setEnable(hasUsb);
        usb_.setFocusable(hasUsb);
        usb_.addCallback("clicked", [this] { setPage(pages_.indexOf(4)); });
        resources_.setVisible(hasResources);
        resources_.setEnable(hasResources);
        resources_.setFocusable(hasResources);
        resources_.addCallback("clicked", [this] { setPage(pages_.indexOf(3)); });
        general_.addCallback("clicked", [this] { setPage(0); });
        driver_.addCallback("clicked", [this] { setPage(1); });
        details_.addCallback("clicked", [this] { setPage(2); });
        close_.addCallback("clicked", [this] { close(); });
        const std::array<FWidget*, 7> controls {&text_, &general_, &driver_, &details_, &resources_, &usb_, &close_};
        for (auto *control : controls) {
            control->addCallback("next-page", [this] { setPage((page_ + 1) % pageCount()); });
            control->addCallback("previous-page", [this] { setPage((page_ + pageCount() - 1) % pageCount()); });
        }
        if (device_.subsystem == "efivarfs") {
            efiReader_.start();
            efiTimer_ = addTimer(100);
        }
        setPage(device_.subsystem == "efivarfs" ? 2 : pageCount() > 3 ? 3 : 0);
    }
    ~Properties() override { if (efiTimer_) delTimer(efiTimer_); }
private:
    void onTimer(FTimerEvent *event) override
    {
        if (efiTimer_ && efiReader_.isFinished()) {
            efiReader_.wait();
            efiReady_ = true;
            delTimer(efiTimer_);
            efiTimer_ = 0;
            if (pages_.at(page_) == 2) { adjustSize(); redraw(); }
        }
        FDialog::onTimer(event);
    }
    int pageCount() const
    {
        return static_cast<int>(pages_.size());
    }
    void setPage(int page)
    {
        page_ = page;
        const std::array<const char*, 5> titles {"General", "Driver", "Details", "Resources", "USB"};
        setText(display(deviceDisplayName(device_) + " - " + titles.at(pages_.at(page))));
        general_.setEnable(page != 0);
        driver_.setEnable(page != 1);
        details_.setEnable(page != 2);
        resources_.setEnable(pages_.contains(3) && pages_.at(page) != 3);
        usb_.setEnable(pages_.contains(4) && pages_.at(page) != 4);
        adjustSize();
        text_.scrollToBegin();
        if (isShown()) text_.setFocus();
        redraw();
    }
    PropertyPage pageContent(std::size_t columns = 0) const
    {
        const int kind = pages_.at(page_);
        if (kind == 2 && device_.subsystem == "efivarfs") {
            PropertyPage result = efiReady_ ? efiReader_.page()
                : PropertyPage{QStringLiteral("Loading UEFI variable hex dump…\n"), {}};
            result.text += '\n';
            const int offset = result.text.count('\n');
            const auto metadata = inventoryDetailsPage(device_);
            result.text += metadata.text;
            for (int line : metadata.shadedLines) result.shadedLines.append(offset + line);
            return result;
        }
        return kind == 4 ? usbPropertyPage(*device_.usb, device_.path, columns)
            : kind == 3 ? resourcesPropertyPage(*device_.resources)
            : kind == 2 ? inventoryDetailsPage(device_) : commonPropertyPage(entries_, kind, columns);
    }
    void renderContent(const PropertyPage &content)
    {
        text_.setText(display(content.text));
        if (!FTerm::isMonochron()) {
            const auto background = text_.getBackgroundColor() == FColor::White
                ? FColor::LightGray : FColor::White;
            const FColorPair stripe{FColor::Black, background};
            for (int line : content.shadedLines)
                text_.addHighlight(static_cast<std::size_t>(line), FTextView::FTextHighlight{0, stripe});
        }
    }
    void adjustSize() override
    {
        const auto desktopWidth = getDesktopWidth();
        const auto desktopHeight = getDesktopHeight();
        const auto availableWidth = desktopWidth > 80 ? desktopWidth - 4 : desktopWidth;
        const auto availableHeight = desktopHeight > 25 ? desktopHeight - 4 : desktopHeight;
        std::size_t naturalWidth = 0;
        for (const QString &line : pageContent().text.split('\n'))
            naturalWidth = std::max(naturalWidth, getColumnWidth(display(line)));
        const auto w = std::min(availableWidth, std::clamp<std::size_t>(naturalWidth + 8, 80, 120));
        // FTextView reserves its own border/scrollbar columns even with a blank border.
        const auto scroll = text_.getScrollPos();
        text_.setGeometry(FPoint{2, 2}, FSize{w > 4 ? w - 4 : 1, 6});
        const auto content = pageContent(text_.getTextVisibleSize().getWidth());
        const auto preferredHeight = std::clamp<std::size_t>(content.text.count('\n') + 8, 12, 50);
        const auto h = std::min(preferredHeight, availableHeight);
        renderContent(content);
        const int x = desktopWidth > w ? int((desktopWidth - w) / 2) + 1 : 1;
        const int y = desktopHeight > h ? int((desktopHeight - h) / 2) + 1 : 1;
        if (isShown()) {
            // FDialog restores uncovered terminal cells when resized or moved.
            // Inherited setGeometry bypasses that restoration in FINAL CUT 0.9.1.
            // Suppress recursive adjustSize(): children are laid out below.
            FDialog::setSize(FSize{w, h}, false);
            FDialog::setPos(FPoint{x, y}, false);
        } else {
            setGeometry(FPoint{x, y}, FSize{w, h}, false);
        }
        text_.setGeometry(FPoint{2, 2}, FSize{w - 4, h - 6});
        general_.setGeometry(FPoint{2, int(h) - 3}, FSize{11, 1});
        driver_.setGeometry(FPoint{15, int(h) - 3}, FSize{10, 1});
        details_.setGeometry(FPoint{27, int(h) - 3}, FSize{11, 1});
        resources_.setGeometry(FPoint{40, int(h) - 3}, FSize{13, 1});
        usb_.setGeometry(FPoint{55, int(h) - 3}, FSize{8, 1});
        close_.setGeometry(FPoint{int(w) - 12, int(h) - 3}, FSize{10, 1});
        text_.scrollTo(scroll);
        FDialog::adjustSize();
    }
    void initLayout() override
    {
        adjustSize();
        FDialog::initLayout();
        text_.setFocus();
    }
    const Device device_;
    const QVector<PropertyEntry> entries_;
    EfiPageReader efiReader_;
    int efiTimer_ = 0;
    bool efiReady_ = false;
    int page_ = 0;
    QVector<int> pages_ {0, 1, 2};
    PropertyPageControl<PropertyTextView> text_{this};
    PropertyPageControl<FButton> general_{"&General", this};
    PropertyPageControl<FButton> driver_{"&Driver", this};
    PropertyPageControl<FButton> details_{"De&tails", this};
    PropertyPageControl<FButton> resources_{"&Resources", this};
    PropertyPageControl<FButton> usb_{"&USB", this};
    PropertyPageControl<FButton> close_{"&Close", this};
    PropertiesStatus status_{FWidget::getStatusBar(), [this](int step) {
        setPage((page_ + step + pageCount()) % pageCount());
    }};
};
}

void showProperties(const Device &device, finalcut::FWidget *parent)
{
    Properties dialog(device, parent);
    dialog.exec();
}
