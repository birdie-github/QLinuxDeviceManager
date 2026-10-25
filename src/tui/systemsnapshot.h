#pragma once
#include "systemproperties.h"
#include <optional>

// Worker results are consumed only on the terminal thread. No Qt event loop,
// queued callbacks, or dialog pointers are used.
class SystemSnapshot final {
public:
    ~SystemSnapshot();
    void ensure();
    void refresh();
    bool poll();
    bool collecting() const { return busy_; }
    const SystemProperties *get() const { return result_ ? &*result_ : nullptr; }
private:
    SystemPropertiesReader reader_;
    std::optional<SystemProperties> result_;
    bool busy_ = false;
    bool pending_ = false;
};
