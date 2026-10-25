#include "systemsnapshot.h"

SystemSnapshot::~SystemSnapshot()
{
    reader_.requestInterruption();
    reader_.wait();
}
void SystemSnapshot::ensure()
{
    poll();
    if (!result_ && !busy_) refresh();
}
void SystemSnapshot::refresh()
{
    result_.reset();
    if (busy_) { pending_ = true; return; }
    busy_ = true;
    reader_.start();
}
bool SystemSnapshot::poll()
{
    if (!busy_ || !reader_.isFinished()) return false;
    reader_.wait();
    busy_ = false;
    if (pending_) {
        // A newer application refresh superseded this collection. Keep only
        // one worker and collect again before publishing a result.
        pending_ = false;
        refresh();
        return false;
    }
    result_ = reader_.takeResult();
    return true;
}
