#pragma once
// One Runtime worker owns recovery; its cheap 30s health snapshot covers missed events. No healthy CDP/business polling.
#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <functional>
#include <optional>
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
// Raw account loss is uncertainty, not a new authenticated Steam session.
// Retain the last nonzero account across gaps; a new verified process or a
// genuinely different known account may renew the existing crash budget once.
class LoaderSessionBudget {
    std::wstring root_;
    DWORD pid_=0,account_=0;
    ULONGLONG birth_=0;
public:
    bool observe(const std::wstring& root,DWORD pid,ULONGLONG birth,DWORD account) {
        if(root.empty()||!pid||!birth)return false;
        if(root_!=root||pid_!=pid||birth_!=birth){root_=root;pid_=pid;birth_=birth;account_=account;return true;}
        if(!account||account_==account)return false;
        account_=account;return true;
    }
};
// A defective/foreign plugin must not keep launching the owned Loader forever.
// Verified child health, explicit enable or a verified new Steam session renews it.
class LoaderCrashBudget {
    unsigned unstableExits_ = 0;
public:
    static constexpr ULONGLONG stableUptimeMs = 30000;
    static constexpr unsigned maxUnstableExits = 5;
    bool blocked() const { return unstableExits_ >= maxUnstableExits; }
    unsigned failures() const { return unstableExits_; }
    void healthy(ULONGLONG uptime) { if(uptime >= stableUptimeMs) unstableExits_ = 0; }
    bool exited(ULONGLONG uptime, bool wasHealthy = false) {
        // A live but unusable Loader is not health, even after thirty seconds.
        if(wasHealthy)healthy(uptime);
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
    // Runtime-only evidence of the same live binding, before first complete UI
    // proof. A socket peer alone must not skip its original seven-second window.
    ULONGLONG freshBindingStarted=0;
    ULONGLONG pausedReadDue=0; // Next existing shared read for a usable mirror with unconfirmed official state.
    std::optional<ULONGLONG> authenticatedPeerAgeMs; // Diagnostics only, not Steam UI proof.
    std::optional<bool> sidebarVerifiedDuringBackendOutage; // Diagnostic only; not a business-ready receipt.
    std::optional<ULONGLONG> sharedModeObservationAgeMs; // Age of the publication consumed, not of the first positive proof.
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
// the initial creation window and the hard ceiling for a rebuilt page's first
// owned startup. Mirror success alone is never overall UI acceptance.
class ConnectionRecoveryWindow {
    ULONGLONG check_=0,due_=0,opened_=0,rebuildConfirmedAt_=0,commandRetryAt_=0;
    ULONGLONG rebuildLoaderStartedAt_=0,rebuildIdentityRefusedAt_=0;
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
        waiting_=action;failed_=started_=cold_=false;rebuildConfirmedAt_=rebuildLoaderStartedAt_=rebuildIdentityRefusedAt_=commandRetryAt_=0;
        if(action==Action::exhausted){due_=0;check_=now+intervalMs;return action;}
        ++attempts_;due_=now+budget();check_=now+evidenceIntervalMs;return action;
    }
public:
    Action observe(ULONGLONG now,const ConnectionHealth& health,const std::function<bool(Action)>& reserve={}){
        if(!health.identity.empty()&&identity_!=health.identity){active_=false;waiting_=Action::none;identity_=health.identity;}
        paused_=health.paused;
        if(paused_){
            const auto next=health.pausedReadDue>now?(std::min)(health.pausedReadDue,now+intervalMs):now+evidenceIntervalMs;
            if(!check_||now>=check_||check_>next)check_=next;
            return Action::none;
        }
        if(!health.required||health.connected){
            const auto action=active_?(health.connected?Action::recovered:Action::stopped):Action::none;
            active_=false;waiting_=Action::none;due_=commandRetryAt_=0;
            if(action!=Action::none||!check_||now>=check_)check_=now+intervalMs;
            return action;
        }
        if(!active_){
            active_=true;opened_=now;attempts_=0;++episode_;
            trigger_=health.transportConnected?"mirror-ready-official-unavailable":"connection-unavailable";
            const bool initialChild=health.coldLoaderStarted&&now>=health.coldLoaderStarted&&now-health.coldLoaderStarted<coldStartGraceMs;
            const bool initialBinding=health.freshBindingStarted&&now>=health.freshBindingStarted&&now-health.freshBindingStarted<transportGraceMs;
            const auto action=issue(health.transportConnected&&!initialChild&&!initialBinding?Action::recycle:Action::reconnect,now);
            if(action==Action::reconnect&&initialChild)serviceStarted(health.coldLoaderStarted);
            else if(action==Action::reconnect&&initialBinding)started(action,health.freshBindingStarted);
            return action;
        }
        if(waiting_==Action::exhausted){if(now>=check_)check_=now+intervalMs;return Action::none;}
        if(!check_||now>=check_||check_>now+evidenceIntervalMs)check_=now+evidenceIntervalMs;
        // Win5 2026-11-18: page rebuild consumed 10.467s before our new
        // Loader ran, so the 12s frontend fallback never got its first probe.
        // Align only once on the Runtime's live owned child, not a page or
        // socket claim. A late read needs the matching on-time start receipt.
        // The original cold cap still bounds the whole stage.
        if(waiting_==Action::rebuild&&rebuildConfirmedAt_&&!failed_&&
           (now<=due_||health.coldLoaderStarted==rebuildLoaderStartedAt_)&&
           health.coldLoaderStarted>=rebuildConfirmedAt_&&health.coldLoaderStarted<=now&&health.coldLoaderStarted<due_){
            due_=(std::min)(health.coldLoaderStarted+settleGraceMs,rebuildConfirmedAt_+coldStartGraceMs);
            rebuildConfirmedAt_=0;
        }
        // The page never changed. Only an actual, still-live replacement Loader
        // may use the remainder of this rung; do not reset the clock or retry it.
        if(awaitingRefusedRebuildLoader()&&rebuildLoaderStartedAt_&&health.coldLoaderStarted==rebuildLoaderStartedAt_&&
           now>=rebuildLoaderStartedAt_&&now<due_)return Action::none;
        // Fresh health may end the episode above, but repeated hints cannot
        // bypass (or postpone) the existing retry of a refused command slot.
        if((!failed_&&now<due_)||now<commandRetryAt_)return Action::none;
        trigger_=failed_?"confirmed-step-failure":"observation-budget-expired";
        // Exactly ONE next step per observation, even after a long stall. A new
        // stage gets a fresh bounded wait; duplicate events cannot skip stages.
        const auto next=waiting_==Action::reconnect?Action::recycle:waiting_==Action::recycle?Action::rebuild:
            waiting_==Action::rebuild?Action::reopen:Action::exhausted;
        if((next==Action::rebuild||next==Action::reopen)&&reserve){
            bool reserved=false;try{reserved=reserve(next);}catch(...){/* Failed reservation grants no step. */}
            if(!reserved){commandRetryAt_=check_=now+evidenceIntervalMs;return Action::none;}
        }
        return issue(next,now);
    }
    // Actual operation receipts only; an old stage's late reply cannot spend the
    // new stage or refill its deadline. Successful submission is not health.
    void failed(Action stage,ULONGLONG now,bool identityDeferred=false){
        if(!active_||waiting_!=stage||stage==Action::exhausted)return;
        if(stage==Action::rebuild&&identityDeferred&&!started_&&now<due_){
            if(!failed_){failed_=true;rebuildIdentityRefusedAt_=now;}
            return;
        }
        failed_=true;rebuildIdentityRefusedAt_=0;due_=now;
    }
    bool awaitingRefusedRebuildLoader()const{return active_&&waiting_==Action::rebuild&&rebuildIdentityRefusedAt_!=0;}
    void parkUnstartedPage(Action stage,ULONGLONG now){
        if(!active_||waiting_!=stage||started_||failed_||(stage!=Action::rebuild&&stage!=Action::reopen))return;
        // No child, durable claim or effect exists. Restore the pending
        // command slot, not a spent action, and reuse its original backoff.
        waiting_=stage==Action::rebuild?Action::recycle:Action::rebuild;
        --attempts_;failed_=true;due_=now;commandRetryAt_=check_=now+evidenceIntervalMs;
    }
    void started(Action stage,ULONGLONG now){
        if(active_&&waiting_==stage&&!started_&&!failed_){
            started_=true;due_=now+budget();
            if(stage==Action::rebuild)rebuildConfirmedAt_=now;
        }
    }
    bool serviceStarted(ULONGLONG now){
        if(!active_)return false;
        if(awaitingRefusedRebuildLoader()&&!rebuildLoaderStartedAt_&&now>=rebuildIdentityRefusedAt_&&now<due_)
            rebuildLoaderStartedAt_=now;
        if(failed_)return false;
        if(waiting_==Action::rebuild){
            if(rebuildConfirmedAt_&&!rebuildLoaderStartedAt_&&now>=rebuildConfirmedAt_&&now<due_)
                rebuildLoaderStartedAt_=now;
            return false; // Record proof only; the health read must still see that live child.
        }
        if(waiting_==Action::recycle){started(Action::recycle,now);return false;}
        if(waiting_!=Action::reconnect||cold_)return false;
        // Only actual creation of a NEW owned child can grant cold-start time.
        // Watch reconnects, duplicate ready and an old "loader-started" label cannot.
        cold_=started_=true;due_=now+coldStartGraceMs;return true;
    }
    DWORD timeout(ULONGLONG now)const{
        // A shared read can move check_ while paused. When proof returns,
        // the existing slot retry must still wake at its own deadline.
        const auto next=active_&&!paused_&&waiting_!=Action::exhausted?
            (std::min)(check_,commandRetryAt_?commandRetryAt_:due_):check_;
        return !next||now>=next?0:static_cast<DWORD>(next-now);
    }
    DWORD remaining(ULONGLONG now)const{return due_&&now<due_?static_cast<DWORD>(due_-now):0;}
    DWORD stageBudget()const{return active_&&waiting_!=Action::exhausted?budget():0;}
    const std::string& trigger()const{return trigger_;}
    unsigned attempts()const{return attempts_;}
    unsigned long long episode()const{return episode_;}
    ULONGLONG elapsed(ULONGLONG now)const{return now>=opened_?now-opened_:0;}
};
// This is evidence on the existing Runtime owner, not a second monitor.
// A verified mirror may survive the official service. Only a definite closed
// official socket starts its ORIGINAL transport grace; connecting is not a
// successful reconnect. Exact scope/document changes and real success reset it.
class OfficialBackendRecoveryWindow {
    std::string scope_,document_;
    ULONGLONG closedAt_=0;
    std::uint64_t closedPublication_=0;
public:
    enum class Observation { none,pending,failed,unconfirmed,awaitingPublication };
    void reset(){scope_.clear();document_.clear();closedAt_=0;closedPublication_=0;}
    Observation observe(const std::string& scope,const std::string& document,ULONGLONG now,
            bool known,bool connected,bool socketClosed,std::uint64_t publication,ULONGLONG observedAt){
        if(scope.empty())return closedPublication_?Observation::unconfirmed:Observation::none;
        if(scope_!=scope){scope_=scope;document_.clear();closedPublication_=0;}
        if(connected){closedPublication_=0;return Observation::none;}
        if(!document.empty()&&document_!=document){document_=document;closedPublication_=0;}
        if(!closedPublication_&&known&&!document.empty()&&socketClosed&&publication){closedPublication_=publication;closedAt_=now;}
        if(!closedPublication_)return Observation::none;
        const bool expired=now>=closedAt_&&now-closedAt_>=ConnectionRecoveryWindow::transportGraceMs;
        // Unknown proof grants no page operation and never refills this deadline.
        // Past the fast window it parks on the original idle cadence until
        // current proof returns; the old fault is retained, not marked healthy.
        if(!known||document.empty())return expired?Observation::unconfirmed:Observation::pending;
        // A follow-up read completed inside the grace cannot certify failure
        // after it, even when its publication arrived late or is newer than Closed.
        if(!expired)return Observation::pending;
        const bool observedAfterGrace=observedAt>=closedAt_&&observedAt<=now&&
            observedAt-closedAt_>=ConnectionRecoveryWindow::transportGraceMs;
        return publication>closedPublication_&&observedAfterGrace?Observation::failed:Observation::awaitingPublication;
    }
};
// Local launch refusals are not broken Steam documents. Recheck on the same
// idle cadence; clearing this park does not grant launch authority.
class LaunchPolicyStop {
public:
    enum class Cause { none, elevatedShell, loaderPathUnavailable };
private:
    Cause cause_=Cause::none;
    ULONGLONG due_=0;
public:
    static constexpr DWORD intervalMs=ConnectionRecoveryWindow::intervalMs;
    bool failed(const std::string& stage,DWORD error,ULONGLONG now){
        const auto cause=stage=="shell-token-elevated"&&error==ERROR_ACCESS_DENIED?Cause::elevatedShell:
            (stage=="create-process"||stage=="create-process-with-token")&&
            (error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND)?Cause::loaderPathUnavailable:Cause::none;
        if(cause==Cause::none)return false;
        if(!blocked()){cause_=cause;due_=now+intervalMs;}return true;
    }
    template<class Probe> bool recheck(ULONGLONG now,Probe&& probe){
        if(!blocked()||now<due_)return false;
        due_=now+intervalMs;
        bool candidate=false;try{candidate=probe();}catch(...){return false;}
        if(!candidate)return false;
        reset();return true;
    }
    Cause cause()const{return cause_;}
    bool blocked()const{return cause_!=Cause::none;}
    DWORD timeout(ULONGLONG now)const{return !blocked()?INFINITE:now>=due_?0:static_cast<DWORD>(due_-now);}
    void reset(){cause_=Cause::none;due_=0;}
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
