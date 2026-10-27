#include "propertiesdialog.h"
#include "propertypages.h"
#include "devicepages/resourcespage.h"
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
        : FDialog(parent), device_(device), entries_(commonPropertyEntries(inventoryCommonProperties(device), false, PropertyPresentation::Tui))
    {
        setModal();
        const bool hasResources = pageCount() == 4;
        resources_.setVisible(hasResources);
        resources_.setEnable(hasResources);
        resources_.setFocusable(hasResources);
        resources_.addCallback("clicked", [this] { setPage(3); });
        general_.addCallback("clicked", [this] { setPage(0); });
        driver_.addCallback("clicked", [this] { setPage(1); });
        details_.addCallback("clicked", [this] { setPage(2); });
        close_.addCallback("clicked", [this] { close(); });
        const std::array<FWidget*, 6> controls {&text_, &general_, &driver_, &details_, &resources_, &close_};
        for (auto *control : controls) {
            control->addCallback("next-page", [this] { setPage((page_ + 1) % pageCount()); });
            control->addCallback("previous-page", [this] { setPage((page_ + pageCount() - 1) % pageCount()); });
        }
        setPage(0);
    }
private:
    int pageCount() const
    {
        return device_.resources && device_.resources->hasInformation() ? 4 : 3;
    }
    void setPage(int page)
    {
        page_ = page;
        const std::array<const char*, 4> titles {"General", "Driver", "Details", "Resources"};
        setText(display(deviceDisplayName(device_) + " - " + titles.at(page)));
        general_.setEnable(page != 0);
        driver_.setEnable(page != 1);
        details_.setEnable(page != 2);
        resources_.setEnable(pageCount() == 4 && page != 3);
        adjustSize();
        text_.scrollToBegin();
        if (isShown()) text_.setFocus();
        redraw();
    }
    PropertyPage pageContent(std::size_t columns = 0) const
    {
        return page_ == 3 ? resourcesPropertyPage(*device_.resources)
            : page_ == 2 ? inventoryDetailsPage(device_) : commonPropertyPage(entries_, page_, columns);
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
    int page_ = 0;
    PropertyPageControl<PropertyTextView> text_{this};
    PropertyPageControl<FButton> general_{"&General", this};
    PropertyPageControl<FButton> driver_{"&Driver", this};
    PropertyPageControl<FButton> details_{"De&tails", this};
    PropertyPageControl<FButton> resources_{"&Resources", this};
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
