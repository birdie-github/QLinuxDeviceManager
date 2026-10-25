#include "propertiesdialog.h"
#include "propertypages.h"
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
        setPage(0);
    }
private:
    void setPage(int page)
    {
        const std::array<const char*, 3> titles {"General", "Driver", "Details"};
        setText(display(deviceDisplayName(device_) + " - " + titles.at(page)));
        QString text = "Inventory snapshot; no additional property reads.\n\n";
        text += page == 2 ? inventoryDetailsPage(device_) : commonPropertyPage(entries_, page);
        text_.setText(display(text));
        text_.scrollToBegin();
        general_.setEnable(page != 0);
        driver_.setEnable(page != 1);
        details_.setEnable(page != 2);
        redraw();
    }
    void adjustSize() override
    {
        const auto w = std::max<std::size_t>(50, getDesktopWidth() > 4 ? getDesktopWidth() - 4 : 50);
        const auto h = std::max<std::size_t>(10, getDesktopHeight() > 4 ? getDesktopHeight() - 4 : 10);
        setGeometry(FPoint{2, 2}, FSize{w, h}, false);
        text_.setGeometry(FPoint{2, 2}, FSize{w - 4, h - 6});
        general_.setGeometry(FPoint{2, int(h) - 3}, FSize{11, 1});
        driver_.setGeometry(FPoint{15, int(h) - 3}, FSize{10, 1});
        details_.setGeometry(FPoint{27, int(h) - 3}, FSize{11, 1});
        close_.setGeometry(FPoint{int(w) - 12, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); }
    const Device device_;
    const QVector<PropertyEntry> entries_;
    FTextView text_{this};
    FButton general_{"&General", this};
    FButton driver_{"&Driver", this};
    FButton details_{"De&tails", this};
    FButton close_{"&Close", this};
};
}

void showProperties(const Device &device, finalcut::FWidget *parent)
{
    Properties dialog(device, parent);
    dialog.exec();
}
