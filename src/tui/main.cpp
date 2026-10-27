#include "browser.h"
#include <QCoreApplication>
#include <iostream>
#include <utility>

int main(int argc, char **argv)
{
    // QtCore supplies synchronous utilities and translation context only.
    // FINAL CUT exclusively owns terminal setup and event dispatch.
    QCoreApplication core(argc, argv);
    Inventory inventory = collectInventory();
    if (!inventory.error.isEmpty()) {
        std::cerr << inventory.error.toStdString() << '\n';
        return 1;
    }
    finalcut::FApplication app(argc, argv);
    return runBrowser(std::move(inventory), app);
}
