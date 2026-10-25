#pragma once
#include <final/final.h>
#include <functional>
#include <memory>
#include <vector>

// Temporarily use the existing application status bar for a modal dialog.
// Preserve the main window's key objects, callbacks and accelerator ownership.
class PropertiesStatus final {
public:
    PropertiesStatus(finalcut::FStatusBar *bar, std::function<void(int)> step);
    ~PropertiesStatus();
    PropertiesStatus(const PropertiesStatus &) = delete;
    PropertiesStatus &operator=(const PropertiesStatus &) = delete;
private:
    finalcut::FStatusBar *bar_;
    finalcut::FString message_;
    std::vector<finalcut::FStatusKey*> saved_;
    std::unique_ptr<finalcut::FStatusKey> previous_;
    std::unique_ptr<finalcut::FStatusKey> next_;
};
