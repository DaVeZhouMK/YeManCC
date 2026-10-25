#pragma once
#include <string>
#include <cstdint>
#include "lossless_window_refresh.h"
namespace ymcc::lossless {
enum class ScaleButton { Unknown, Off, Countdown, On };
struct ScaleSnapshot {
    bool readable = false;
    ScaleButton state = ScaleButton::Unknown;
    bool profileMatches = false;
    std::wstring button, status;
    bool paused = false;
};
struct ScaleInvoke { bool attempted = false; bool accepted = false; };
struct ScaleFailureReset {
    bool requested=false,attempted=false,terminated=false,closed=false;
    unsigned error=0;std::string reason;
};
struct ScaleResult {
    bool ok = false, invoked = false, cancelled = false, pending = false;
    int enabled = -1;
    std::string reason;
    ScaleSnapshot receipt;
    bool stopInvoked = false, restarted = false;
    WindowRefreshResult refresh;
    ScaleFailureReset failureReset;
};
// One explicit action, one optional stop and one start. A repeated ON from an
// observer never enters here. Reapply is opt-in and remains held by this mutex.
template<class Ports> ScaleResult ensureScaling(Ports& p) {
    ScaleResult r;
    if (!p.ready()) { r.reason="context-unavailable"; return r; }
    if (!p.valid()) { r.reason="process-identity-changed"; return r; }
    if (!p.lock()) { r.pending=true; r.reason="scaling-busy"; return r; }
    struct Unlock { Ports& p; ~Unlock(){p.unlock();} } unlock{p};
    const auto attempt=[&]() -> ScaleResult {
    if (!p.begin()) { r.reason="no-focus-automation-unavailable"; return r; }
    const auto started=p.now();auto nextRead=started,stoppedAt=started;
    bool pinned=false,newCycle=false,joinedCountdown=false;
    for (;;) {
        if (!p.ready()) { r.pending=r.invoked||r.stopInvoked; r.reason="context-changed"; return r; }
        if (!p.valid()) { r.reason="process-identity-changed"; return r; }
        const bool fresh=p.now()>=nextRead;
        if(fresh){r.receipt=p.observe();nextRead=p.now()+1000;}
        const auto& snapshot=r.receipt;
        if(!p.ready()||!p.valid()){r.pending=r.invoked||r.stopInvoked;r.reason="context-changed";return r;}
        if(fresh&&snapshot.readable&&snapshot.state==ScaleButton::On&&!snapshot.profileMatches){
            r.enabled=0;r.reason="another-profile-active";return r;
        }
        // Admit only the original foreground game. The explicit refresh below
        // submits one native redraw request; observers never enter this path.
        if (!p.foregroundMatches()) {
            if(!pinned&&p.now()-started<1200){p.pause();continue;}
            if(r.invoked&&p.ready())r.cancelled=p.cancelPending();
            r.pending=!r.cancelled&&(r.invoked||r.stopInvoked);
            r.reason="target-not-foreground";return r;
        }
        pinned=true;
        if(fresh&&snapshot.readable&&snapshot.state==ScaleButton::On){
            if(r.restarted&&!newCycle){
                if(p.now()-stoppedAt>=2500){r.pending=true;r.reason="restart-stop-unconfirmed";return r;}
                nextRead=p.now()+200;
            }else if(!r.restarted&&!r.invoked&&!joinedCountdown&&p.reapply()){
                const auto action=p.stopOnce();r.stopInvoked=action.attempted;
                if(!action.accepted){r.pending=action.attempted;r.reason="restart-stop-unconfirmed";return r;}
                r.restarted=true;stoppedAt=p.now();nextRead=p.now()+150;
            }else{
                if(p.refreshRequested()&&!r.refresh.refreshed){
                    if(p.now()-started+1200>=p.totalBudget()){r.pending=true;r.reason="window-refresh-budget-unavailable";return r;}
                    r.refresh=p.refreshOnce();
                    if(!r.refresh.refreshed){r.pending=true;r.reason=r.refresh.reason;return r;}
                    // Never confirm against the pre-refresh On/status receipt.
                    r.receipt={};p.pause();nextRead=p.now();continue;
                }
                r.enabled=snapshot.paused?-1:1;r.ok=!snapshot.paused;
                r.pending=snapshot.paused;r.reason=snapshot.paused?"scaling-paused":"";return r;
            }
        }else if(!snapshot.readable||snapshot.state==ScaleButton::Unknown){
            if(p.now()-started>=p.windowBudget()){r.pending=r.invoked||r.stopInvoked;r.reason="scaling-state-unavailable";return r;}
        }else if(fresh&&snapshot.state==ScaleButton::Countdown){
            joinedCountdown=true;if(r.restarted)newCycle=true; // LS AutoScale may start the new cycle itself.
        }else if(fresh&&snapshot.state==ScaleButton::Off){
            r.enabled=0;if(r.restarted)newCycle=true;
            if(r.invoked){r.reason="scaling-not-enabled";return r;}
            const auto action=p.invokeOnce();r.invoked=action.attempted;if(r.invoked)r.enabled=-1;
            if(!action.accepted){r.pending=action.attempted;r.reason="scaling-invoke-unconfirmed";return r;}
        }
        if(p.now()-started>=p.totalBudget()){r.pending=true;r.reason="scaling-pending";return r;}
        p.pause();
    }
    };
    r=attempt();
    // A returned paused/unknown/cancelled receipt never authorizes termination.
    // Only the independent IPC-timeout path may reset the exact LS process.
    return r;
}
} // namespace ymcc::lossless
