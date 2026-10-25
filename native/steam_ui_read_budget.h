#pragma once
// No timer: callers supply events. Continuous events cannot create high-rate
// Steam discovery, and failed automatic reads stop until an explicit renewal.
#include <windows.h>
namespace ymcc::steamui {
class ReadBudget {
    ULONGLONG started_ = 0; bool seen_ = false; unsigned failures_ = 0;
public:
    bool begin(ULONGLONG now, bool force = false) {
        if (!force && (failures_ >= 3 || (seen_ && now - started_ < 5000))) return false;
        if (force) failures_ = 0;
        seen_ = true; started_ = now; return true;
    }
    void finish(bool ok) { failures_ = ok ? 0 : failures_ + 1; }
    void reset() { seen_ = false; failures_ = 0; }
    unsigned failures() const { return failures_; }
};
}
