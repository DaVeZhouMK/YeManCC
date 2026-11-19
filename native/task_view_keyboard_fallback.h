#pragma once
#include <climits>
#include "task_view_dispatch.h"

// Ops are synchronous/nonthrowing. current() owns the original power/exit/
// request lease; interactive() verifies the current Default input desktop.
// keysClear() covers all modifiers and Tab. observe() records every send in
// the existing self-send owner. No OS calls, worker, timer or persona here.
namespace ymcc::taskView::keyboard {
inline constexpr UINT maximumAttempts=3;
inline constexpr DWORD retryIntervalMs=250,pauseSliceMs=25;
inline constexpr ULONGLONG recoveryWindowMs=1500;
enum class Status { Ineligible,Busy,Cancelled,Expired,NotInteractive,KeysBusy,
    ZeroInserted,Partial,CleanupUnproven,CleanedPending,Accepted };
inline const char* statusName(Status s) noexcept {
    switch(s){
    case Status::Ineligible:return "ineligible";
    case Status::Busy:return "busy";
    case Status::Cancelled:return "cancelled";
    case Status::Expired:return "expired";
    case Status::NotInteractive:return "not-interactive";
    case Status::KeysBusy:return "keys-busy";
    case Status::ZeroInserted:return "zero-inserted";
    case Status::Partial:return "partial";
    case Status::CleanupUnproven:return "cleanup-unproven";
    case Status::CleanedPending:return "cleaned-pending";
    case Status::Accepted:return "accepted";
    }return "unknown";
}
struct Result {
    Status status=Status::Ineligible;
    bool attempted=false,accepted=false,cleanupAttempted=false;
    bool cleanupVerified=true,cleanupRetained=false;
    UINT attempts=0,insertedInputs=0,cleanupInputs=0,pendingKeys=0;
    DWORD error=0,cleanupError=0,elapsedMs=0;
};
class Owner {
    static constexpr unsigned win=1,tab=2;
    std::atomic_flag busy_=ATOMIC_FLAG_INIT;
    std::atomic<unsigned> pending_{0};
    struct Lease {
        std::atomic_flag& flag;
        ~Lease() noexcept {flag.clear(std::memory_order_release);}
    };
    static INPUT key(WORD vk,bool up) noexcept {
        INPUT input{};input.type=INPUT_KEYBOARD;input.ki.wVk=vk;
        input.ki.dwFlags=(up?KEYEVENTF_KEYUP:0)|(vk==VK_LWIN?KEYEVENTF_EXTENDEDKEY:0);
        return input;
    }
    void snapshot(Result& r) const noexcept {
        const auto p=pending_.load(std::memory_order_acquire);
        r.pendingKeys=UINT((p&win)!=0)+UINT((p&tab)!=0);
        r.cleanupVerified=p==0;r.cleanupRetained=p!=0;
    }
    template<class Ops> static bool current(Ops& ops,Result& r) noexcept {
        if(!ops.current()){r.status=Status::Cancelled;r.error=ERROR_CANCELLED;return false;}
        if(!ops.interactive()){r.status=Status::NotInteractive;r.error=ERROR_ACCESS_DENIED;return false;}
        // Desktop verification may yield; recheck the original action lease.
        if(!ops.current()){r.status=Status::Cancelled;r.error=ERROR_CANCELLED;return false;}
        return true;
    }
    template<class Ops> static bool leased(Ops& ops,ULONGLONG start,ULONGLONG end,Result& r) noexcept {
        const auto tick=ops.now();
        if(tick<start||tick>=end){r.status=Status::Expired;r.error=ERROR_TIMEOUT;return false;}
        if(!ops.current()){r.status=Status::Cancelled;r.error=ERROR_CANCELLED;return false;}
        return true;
    }
    template<class Ops> static bool live(Ops& ops,ULONGLONG start,ULONGLONG end,Result& r) noexcept {
        return leased(ops,start,end,r)&&current(ops,r)&&leased(ops,start,end,r);
    }
    template<class Ops> Result finish(Ops& ops,Result r,ULONGLONG start) const noexcept {
        snapshot(r);const auto tick=ops.now();
        const auto elapsed=tick>=start?tick-start:0;
        r.elapsedMs=static_cast<DWORD>((std::min)(elapsed,ULONGLONG(MAXDWORD)));
        ops.setError(r.error?r.error:r.cleanupError);return r;
    }
    template<class Ops> void repair(Ops& ops,Result& r) noexcept {
        auto p=pending_.load(std::memory_order_relaxed);
        if(!p)return;
        INPUT inputs[2]{};UINT count=0;
        if(p&tab)inputs[count++]=key(VK_TAB,true);
        if(p&win)inputs[count++]=key(VK_LWIN,true);
        ops.setError(ERROR_SUCCESS);const UINT inserted=ops.send(inputs,count);
        const DWORD error=inserted==count?ERROR_SUCCESS:ops.error();
        r.cleanupAttempted=true;r.cleanupInputs+=inserted;r.cleanupError=error;
        // SendInput's accepted prefix is bookkeeping, not an OS key-state or
        // visible TaskView receipt. Release only downs inserted by this owner.
        for(UINT i=0;i<(std::min)(inserted,count);++i)
            p&=~(inputs[i].ki.wVk==VK_TAB?tab:win);
        pending_.store(p,std::memory_order_release);
        ops.observe(inputs,count,inserted);
    }
    // Across calls a retained bit cannot prove who owns the current down.
    // Ask the user to release all modifiers/Tab; acknowledge without key-up.
    template<class Ops> static bool retainedKeysClear(Ops& ops,Result& r) noexcept {
        if(!ops.keysClear()){
            r.status=Status::KeysBusy;r.error=r.cleanupError=ERROR_BUSY;return false;
        }
        if(!ops.current()){
            r.status=Status::CleanupUnproven;r.cleanupError=ERROR_CANCELLED;return false;
        }
        return true;
    }
    template<class Ops> static bool wait(Ops& ops,ULONGLONG start,ULONGLONG end,Result& r) noexcept {
        // A bounded number of short sleeps on the original async thread.
        // Long suspension consumes the same deadline and never catches up.
        for(DWORD waited=0;waited<retryIntervalMs;){
            if(!leased(ops,start,end,r))return false;
            const auto tick=ops.now();
            if(tick<start||tick>=end){r.status=Status::Expired;r.error=ERROR_TIMEOUT;return false;}
            const DWORD slice=static_cast<DWORD>((std::min)(end-tick,
                ULONGLONG((std::min)(pauseSliceMs,retryIntervalMs-waited))));
            ops.pause(slice);waited+=slice;
            if(!leased(ops,start,end,r))return false;
        }
        return true;
    }
public:
    Owner() noexcept=default;
    Owner(const Owner&)=delete;Owner& operator=(const Owner&)=delete;
    bool hasPending() const noexcept {return pending_.load(std::memory_order_acquire)!=0;}
    bool pending() const noexcept {return hasPending();}
    // Call under main's cross-API/fallback action lease before a new Shell API.
    // Retained debt is acknowledged only after observed release; never inject here.
    template<class Ops> Result cleanup(Ops& ops) noexcept {
        Result r;const auto start=ops.now();
        if(busy_.test_and_set(std::memory_order_acquire)){
            r.status=Status::Busy;r.error=ERROR_BUSY;return finish(ops,r,start);
        }
        const Lease lease{busy_};r.status=Status::CleanedPending;
        if(hasPending()){
            Result guard;
            if(!current(ops,guard)){
                r.status=Status::CleanupUnproven;r.cleanupError=guard.error;
            }else if(retainedKeysClear(ops,r))pending_.store(0,std::memory_order_release);
        }
        return finish(ops,r,start);
    }
    template<class Ops> Result request(Ops& ops,const ymcc::taskView::Result& api,ULONGLONG deadline) noexcept {
        Result r;const auto start=ops.now();
        if(busy_.test_and_set(std::memory_order_acquire)){
            r.status=Status::Busy;r.error=ERROR_BUSY;return finish(ops,r,start);
        }
        const Lease lease{busy_};
        if(api.status!=ymcc::taskView::Status::ForegroundGrantFailed||api.executionReleased||
           !api.cleanupVerified||api.cleanupRetained||!api.helperStarted||!api.helperReady)
            return finish(ops,r,start);
        const auto end=(std::min)(deadline,start>ULLONG_MAX-recoveryWindowMs?ULLONG_MAX:start+recoveryWindowMs);
        if(!leased(ops,start,end,r))return finish(ops,r,start);
        if(hasPending()){
            if(!live(ops,start,end,r))return finish(ops,r,start);
            // A retained acknowledgement consumes this request, with no input.
            if(!retainedKeysClear(ops,r)||!leased(ops,start,end,r))return finish(ops,r,start);
            pending_.store(0,std::memory_order_release);r.status=Status::CleanedPending;
            return finish(ops,r,start);
        }
        const INPUT inputs[]{key(VK_LWIN,false),key(VK_TAB,false),key(VK_TAB,true),key(VK_LWIN,true)};
        for(UINT round=0;round<maximumAttempts;++round){
            if(round&&!wait(ops,start,end,r))break;
            if(!leased(ops,start,end,r))break;
            ++r.attempts;
            if(!ops.keysClear()){r.status=Status::KeysBusy;r.error=ERROR_BUSY;continue;}
            if(!live(ops,start,end,r))break;
            // Desktop/session queries may yield while the user presses a key.
            if(!ops.keysClear()){r.status=Status::KeysBusy;r.error=ERROR_BUSY;continue;}
            if(!leased(ops,start,end,r))break;
            // Sampling and SendInput are not atomic against OS scheduling/input.
            r.attempted=true;ops.setError(ERROR_SUCCESS);
            const UINT inserted=ops.send(inputs,4);
            r.error=inserted==4?ERROR_SUCCESS:ops.error();r.insertedInputs+=inserted;
            // All four prefixes are tracked before observe can reenter us.
            const unsigned p=inserted==1||inserted==3?win:inserted==2?win|tab:0;
            pending_.store(p,std::memory_order_release);ops.observe(inputs,4,inserted);
            if(inserted==4){r.status=Status::Accepted;r.accepted=true;break;}
            if(!inserted){r.status=Status::ZeroInserted;continue;}
            r.status=Status::Partial;
            Result guard;
            // Only this same request may repair its freshly inserted downs.
            if(live(ops,start,end,guard))repair(ops,r);
            else r.cleanupError=guard.error;
            if(hasPending())r.status=Status::CleanupUnproven;
            break; // A partial/unknown gesture is never replayed.
        }
        return finish(ops,r,start);
    }
};
using Dispatcher=Owner;
} // namespace ymcc::taskView::keyboard
