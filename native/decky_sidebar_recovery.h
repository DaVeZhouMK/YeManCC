#pragma once
// One Runtime worker owns recovery; its cheap 30s health snapshot covers missed events. No healthy CDP/business polling.
#include <windows.h>
#include <algorithm>
#include <string>
namespace ymcc::deckysidebar {
inline bool recoverableWatchFailure(const std::string& reason) {
    return reason=="mirror-bootstrap-account-changed"||reason=="mirror-bootstrap-async-error"||reason=="mirror-bootstrap-deadline"||
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
// General fallback depends on observed health, never a list of fault names.
struct ConnectionHealth { bool required=false,connected=false; std::string reason;
    bool paused=false; std::string identity; // Pending reads park; OFF/identity changes end an episode.
    bool transportConnected=false; // A healthy mirror cannot repair a failed official backend by rebinding itself.
    ULONGLONG coldLoaderStarted=0; // Runtime-owned child creation; never a page/status timestamp.
};
// Calibrated from the 2026-11-03 archive review, not 30-second retry counts.
// 189 completed rebinds <=5128ms, with none in (7s,90s]; retain the native
// bootstrap's existing 7000ms bound. Two context recoveries were 13707/13777ms.
// Add existing shared mode read gap 5000 + debounce 600 =>19377ms, rounded20s.
// Older first cold launches include31371ms: +5600 =>36971ms, rounded37s.
// These are observation CAPS, not delays after a definitive failed operation.
// Timing corpus SHA256 884252b4137d2ef10f7c251ed2cc60bb69672da80120d202902425ee3bbe1456
// Evidence: Testing/YMCC/Evidence/DeckySleepRecovery-20261103/bpm-reentry-213336/log-calibration/CALIBRATION-DECISION.json
// Re-evaluate these caps when the actual Loader protocol changes. Cold37s is
// first creation only. Mirror success alone is never overall UI acceptance.
class ConnectionRecoveryWindow {
    ULONGLONG check_=0,due_=0,opened_=0;
    unsigned attempts_=0; unsigned long long episode_=0;
    bool active_=false,paused_=false,failed_=false,started_=false,cold_=false;
    std::string identity_,trigger_;
public:
    enum class Action { none,reconnect,recycle,rebuild,reopen,recovered,stopped,exhausted };
    static constexpr DWORD intervalMs=30000; // Missed-event safety net while idle, NOT a recovery rung.
    static constexpr DWORD evidenceIntervalMs=5000,transportGraceMs=7000,settleGraceMs=20000,coldStartGraceMs=37000;
private:
    Action waiting_=Action::none;
    DWORD budget()const{return cold_?coldStartGraceMs:waiting_==Action::reconnect?transportGraceMs:settleGraceMs;}
    Action issue(Action action,ULONGLONG now){
        waiting_=action;failed_=started_=cold_=false;
        if(action==Action::exhausted){due_=0;check_=now+intervalMs;return action;}
        ++attempts_;due_=now+budget();check_=now+evidenceIntervalMs;return action;
    }
public:
    Action observe(ULONGLONG now,const ConnectionHealth& health){
        if(!health.identity.empty()&&identity_!=health.identity){active_=false;waiting_=Action::none;identity_=health.identity;}
        paused_=health.paused;
        if(paused_){if(!check_||now>=check_||check_>now+evidenceIntervalMs)check_=now+evidenceIntervalMs;return Action::none;}
        if(!health.required||health.connected){
            const auto action=active_?(health.connected?Action::recovered:Action::stopped):Action::none;
            active_=false;waiting_=Action::none;due_=0;
            if(action!=Action::none||!check_||now>=check_)check_=now+intervalMs;
            return action;
        }
        if(!active_){
            active_=true;opened_=now;attempts_=0;++episode_;
            trigger_=health.transportConnected?"mirror-ready-official-unavailable":"connection-unavailable";
            const bool initialChild=health.coldLoaderStarted&&now>=health.coldLoaderStarted&&now-health.coldLoaderStarted<coldStartGraceMs;
            const auto action=issue(health.transportConnected&&!initialChild?Action::recycle:Action::reconnect,now);
            if(action==Action::reconnect&&initialChild)serviceStarted(health.coldLoaderStarted);
            return action;
        }
        if(waiting_==Action::exhausted){if(now>=check_)check_=now+intervalMs;return Action::none;}
        if(!check_||now>=check_)check_=now+evidenceIntervalMs;
        if(!failed_&&now<due_)return Action::none;
        trigger_=failed_?"confirmed-step-failure":"observation-budget-expired";
        // Exactly ONE next step per observation, even after a long stall. A new
        // stage gets a fresh bounded wait; duplicate events cannot skip stages.
        return issue(waiting_==Action::reconnect?Action::recycle:waiting_==Action::recycle?Action::rebuild:
            waiting_==Action::rebuild?Action::reopen:Action::exhausted,now);
    }
    // Actual operation receipts only; an old stage's late reply cannot spend the
    // new stage or refill its deadline. Successful submission is not health.
    void failed(Action stage,ULONGLONG now){if(active_&&waiting_==stage&&stage!=Action::exhausted){failed_=true;due_=now;}}
    void started(Action stage,ULONGLONG now){if(active_&&waiting_==stage&&!started_&&!failed_){started_=true;due_=now+budget();}}
    bool serviceStarted(ULONGLONG now){
        if(!active_||failed_)return false;
        if(waiting_==Action::recycle){started(Action::recycle,now);return false;}
        if(waiting_!=Action::reconnect||cold_)return false;
        // Only actual creation of a NEW owned child can grant cold-start time.
        // Watch reconnects, duplicate ready and an old "loader-started" label cannot.
        cold_=started_=true;due_=now+coldStartGraceMs;return true;
    }
    DWORD timeout(ULONGLONG now)const{
        const auto next=active_&&!paused_&&waiting_!=Action::exhausted?(std::min)(check_,due_):check_;
        return !next||now>=next?0:static_cast<DWORD>(next-now);
    }
    DWORD remaining(ULONGLONG now)const{return due_&&now<due_?static_cast<DWORD>(due_-now):0;}
    DWORD stageBudget()const{return active_&&waiting_!=Action::exhausted?budget():0;}
    const std::string& trigger()const{return trigger_;}
    unsigned attempts()const{return attempts_;}
    unsigned long long episode()const{return episode_;}
    ULONGLONG elapsed(ULONGLONG now)const{return now>=opened_?now-opened_:0;}
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
