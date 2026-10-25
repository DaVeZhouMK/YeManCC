#pragma once
// Recovery exists only while an enabled connection is unavailable. No healthy polling.
#include <windows.h>
#include <algorithm>
#include <string>
namespace ymcc::deckysidebar {
inline bool recoverableWatchFailure(const std::string& reason) {
    return reason=="mirror-bootstrap-async-error"||reason=="mirror-bootstrap-deadline"||
        reason=="mirror-bootstrap-ws-receive-failed"||reason=="mirror-bootstrap-ws-closed";
}
// Decky-only renewal of a paused shared mode read, while waiting to start.
// The same monotonic 30-second cap applies even under repeated window events.
class AdmissionReadRefresh {
    ULONGLONG last_ = 0;
    bool seen_ = false;
public:
    bool take(ULONGLONG now, bool recovering) {
        if (!recovering) { reset(); return false; }
        if (seen_ && now - last_ < 30000) return false;
        seen_ = true; last_ = now; return true;
    }
    void reset() { seen_ = false; last_ = 0; }
};
// A defective/foreign plugin must not keep launching the owned Loader forever.
// Only an explicit enable or a verified new Steam session renews this budget.
class LoaderCrashBudget {
    unsigned unstableExits_ = 0;
public:
    static constexpr ULONGLONG stableUptimeMs = 30000;
    static constexpr unsigned maxUnstableExits = 5;
    bool blocked() const { return unstableExits_ >= maxUnstableExits; }
    unsigned failures() const { return unstableExits_; }
    void healthy(ULONGLONG uptime) { if(uptime >= stableUptimeMs) unstableExits_ = 0; }
    bool exited(ULONGLONG uptime) {
        healthy(uptime);
        if(unstableExits_ < maxUnstableExits) ++unstableExits_;
        return !blocked();
    }
    void reset() { unstableExits_ = 0; }
};
class ConnectionRetryWindow {
    ULONGLONG due_ = 0;
    unsigned attempt_ = 0;
public:
    void begin(ULONGLONG now) {
        if(due_)return;
        constexpr DWORD delays[]{250,1000,3000,10000,30000};
        due_=now+delays[(std::min)(attempt_,4u)];
        if(attempt_<4)++attempt_;
    }
    DWORD timeout(ULONGLONG now) const { return !due_?INFINITE:due_<=now?0:static_cast<DWORD>((std::min)(due_-now,static_cast<ULONGLONG>(MAXDWORD-1))); }
    bool take(ULONGLONG now) { if(!due_||now<due_)return false;due_=0;return true; }
    void cancel() {due_=0;}
    void reset() {due_=0;attempt_=0;}
};
}
