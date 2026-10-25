#pragma once
#include <final/final.h>
#include <functional>

// On-demand native menu: consumes no permanent terminal row.
class BrowserMenu final : public finalcut::FMenu {
public:
    BrowserMenu(finalcut::FWidget *parent, std::function<void(bool)> selectView,
        std::function<void(bool)> showAll, std::function<void()> systemInformation);
    void setState(bool connection, bool allDevices);
    void open();
private:
    finalcut::FRadioMenuItem type_{"Devices by &type", this};
    finalcut::FRadioMenuItem connection_{"Devices by &connection", this};
    finalcut::FMenuItem separator_{this};
    finalcut::FCheckMenuItem all_{"&All Devices", this};
    finalcut::FMenuItem systemSeparator_{this};
    finalcut::FMenuItem system_{this};
};
