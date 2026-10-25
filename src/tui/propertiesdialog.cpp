#include "propertiesdialog.h"
#include "terminaltext.h"
#include "devicelabel.h"
#include <algorithm>
#include <QStringList>

namespace {
using namespace finalcut;
QString known(const QString &value)
{
    return value.isEmpty() ? QStringLiteral("Unavailable / undetermined") : value;
}
QString general(const Device &d)
{
    QString text;
    const auto field = [&text](const QString &label, const QString &value) {
        text += label + ": " + known(value) + '\n';
    };
    field("Name", deviceDisplayName(d, true));
    field("Category", categoryLabel(d.category));
    field("Bus / subsystem", d.subsystem);
    field("Location", d.path);
    field("Recorded parent", d.parentPath);
    field("Bound driver", d.driver);
    text += "\nInventory snapshot; current presence and health are not monitored.\n";
    text += "\nUdev properties (hardware identifiers and manufacturer where reported):\n";
    auto keys = d.properties.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) field(key, d.properties.value(key));
    text += "\nKernel attributes:\n";
    keys = d.attributes.keys();
    std::sort(keys.begin(), keys.end());
    for (const QString &key : keys) {
        const Attribute &a = d.attributes[key];
        QString value = a.value;
        if (a.state != ReadState::Available) {
            switch (a.state) {
            case ReadState::PermissionDenied: value = "Permission denied"; break;
            case ReadState::Removed: value = "Device removed"; break;
            case ReadState::Error: value = "Read error"; break;
            case ReadState::Unsupported: value = "Unsupported"; break;
            case ReadState::NotApplicable: value = "Not applicable"; break;
            default: value = "Unavailable"; break;
            }
            if (a.error) value += QStringLiteral(" (errno %1)").arg(a.error);
        }
        field(key, value);
    }
    return text;
}
QString driver(const Device &d)
{
    return "Directly bound kernel driver: " + known(d.driver)
        + "\nOwning kernel module: " + known(d.driverModule)
        + "\n\nAn absent module link does not establish that a driver is built-in."
          "\nModule loadability / built-in status: undetermined"
          "\n\nNo module loading, unloading or device-management actions are performed.";
}

class Properties final : public FDialog {
public:
    Properties(const Device &device, FWidget *parent) : FDialog(parent), device_(device)
    {
        setModal();
        setText("Device properties");
        toggle_.addCallback("clicked", [this] {
            driver_ = !driver_;
            update();
        });
        close_.addCallback("clicked", [this] { close(); });
        update();
    }
private:
    void update()
    {
        text_.setText(display(driver_ ? driver(device_) : general(device_)));
        text_.scrollToBegin();
        toggle_.setText(driver_ ? "&General" : "&Driver");
        redraw();
    }
    void adjustSize() override
    {
        const auto w = std::max<std::size_t>(24, getDesktopWidth() > 4 ? getDesktopWidth() - 4 : 24);
        const auto h = std::max<std::size_t>(10, getDesktopHeight() > 4 ? getDesktopHeight() - 4 : 10);
        setGeometry(FPoint{2, 2}, FSize{w, h}, false);
        text_.setGeometry(FPoint{2, 2}, FSize{w - 4, h - 6});
        toggle_.setGeometry(FPoint{2, int(h) - 3}, FSize{12, 1});
        close_.setGeometry(FPoint{int(w) - 14, int(h) - 3}, FSize{10, 1});
        FDialog::adjustSize();
    }
    void initLayout() override { adjustSize(); FDialog::initLayout(); }
    const Device device_;
    bool driver_ = false;
    FTextView text_{this};
    FButton toggle_{this};
    FButton close_{"&Close", this};
};

}

void showProperties(const Device &device, finalcut::FWidget *parent)
{
    Properties dialog(device, parent);
    dialog.exec();
}
