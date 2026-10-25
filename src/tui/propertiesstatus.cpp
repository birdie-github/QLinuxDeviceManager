#include "propertiesstatus.h"

using namespace finalcut;

PropertiesStatus::PropertiesStatus(FStatusBar *bar, std::function<void(int)> step) : bar_(bar)
{
    if (!bar_) return;
    message_ = bar_->getMessage();
    for (std::size_t i = 1; i <= bar_->getCount(); ++i)
        saved_.push_back(bar_->getStatusKey(static_cast<int>(i)));
    for (auto *key : saved_) {
        bar_->delAccelerator(key);
        // insert() installs a bar-owned callback; remove it before reinserting
        // later, retaining the browser's own callback without duplication.
        // FINAL CUT 0.9.1's pointer constraint does not decay references.
        key->delCallback("activate", static_cast<FStatusBar*>(bar_));
    }
    // clear() leaves the key objects and their connected-bar pointers intact.
    bar_->clear();
    previous_ = std::make_unique<FStatusKey>(FKey::F7, "Previous", bar_);
    next_ = std::make_unique<FStatusKey>(FKey::F8, "Next", bar_);
    previous_->addCallback("activate", [step] { step(-1); });
    next_->addCallback("activate", [step] { step(1); });
    bar_->clearMessage();
    bar_->redraw();
}

PropertiesStatus::~PropertiesStatus()
{
    if (!bar_) return;
    next_.reset();
    previous_.reset();
    for (auto *key : saved_) bar_->insert(key);
    bar_->setMessage(message_);
    bar_->redraw();
}
