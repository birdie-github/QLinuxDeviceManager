#include "propertiesdialog.h"
#include "propertypages.h"
#include "propertypagecontrol.h"
#include "propertiesstatus.h"
#include "terminaltext.h"
#include "devicelabel.h"
#include <algorithm>
#include <array>

namespace {
using namespace finalcut;
class Properties final : public FDialog {
public:
    Properties(const Device &device, FWidget *parent)
        : FDialog(parent), device_(device), entries_(commonPropertyEntries(inventoryCommonProperties(device)))
    {
        setModal();
        general_.addCallback("clicked", [this] { setPage(0); });
        driver_.addCallback("clicked", [this] { setPage(1); });
        details_.addCallback("clicked", [this] { setPage(2); });
        close_.addCallback("clicked", [this] { close(); });
        const std::array<FWidget*, 5> controls {&text_, &general_, &driver_, &details_, &close_};
        for (auto *control : controls) {
            control->addCallback("next-page", [this] { setPage((page_ + 1) % 3); });
            control->addCallback("previous-page", [this] { setPage((page_ + 2) % 3); });
        }
        setPage(0);
    }
private:
    void setPage(int page)
    {
        page_ = page;
        const std::array<const char*, 3> titles {"General", "Driver", "Details"};
        setText(display(deviceDisplayName(device_) + " - " + titles.at(page)));
        const PropertyPage content = page == 2 ? inventoryDetailsPage(device_) : commonPropertyPage(entries_, page);
        const QString &text = content.text;
        preferredHeight_ = std::clamp<std::size_t>(static_cast<std::size_t>(text.count('\n')) + 9, 12, 28);
        text_.setText(display(text));
        // FINAL CUT 0.9.1 supports native line highlights: no custom drawing.
        if (!FTerm::isMonochron()) {
            const auto background = text_.getBackgroundColor() == FColor::White
                ? FColor::LightGray : FColor::White;
            const FColorPair stripe{FColor::Black, background};
            for (int line : content.shadedLines)
                text_.addHighlight(static_cast<std::size_t>(line), FTextView::FTextHighlight{0, stripe});
        }
        text_.scrollToBegin();
        general_.setEnable(page != 0);
        driver_.setEnable(page != 1);
        details_.setEnable(page != 2);
        if (isShown()) {
            adjustSize();
            text_.setFocus();
        }
        redraw();
    }
    void adjustSize() override
    {
        const auto desktopWidth = getDesktopWidth();
        const auto desktopHeight = getDesktopHeight();
        const auto w = std::min<std::size_t>(100, std::max<std::size_t>(50,
            desktopWidth > 4 ? desktopWidth - 4 : 50));
        const auto h = std::min(preferredHeight_, std::max<std::size_t>(10,
            desktopHeight > 4 ? desktopHeight - 4 : 10));
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
        close_.setGeometry(FPoint{int(w) - 12, int(h) - 3}, FSize{10, 1});
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
    std::size_t preferredHeight_ = 12;
    int page_ = 0;
    PropertyPageControl<FTextView> text_{this};
    PropertyPageControl<FButton> general_{"&General", this};
    PropertyPageControl<FButton> driver_{"&Driver", this};
    PropertyPageControl<FButton> details_{"De&tails", this};
    PropertyPageControl<FButton> close_{"&Close", this};
    PropertiesStatus status_{FWidget::getStatusBar(), [this](int step) {
        setPage((page_ + step + 3) % 3);
    }};
};
}

void showProperties(const Device &device, finalcut::FWidget *parent)
{
    Properties dialog(device, parent);
    dialog.exec();
}
