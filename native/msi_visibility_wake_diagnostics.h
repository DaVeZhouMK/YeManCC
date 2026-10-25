#pragma once
#include <cstdint>
#include <mutex>
namespace ymcc::msi::visibility::wake {
#if defined(YMCC_MSI_VISIBILITY_DIAGNOSTIC) && defined(YMCC_MSI_VISIBILITY_WAKE_DIAGNOSTIC)
inline constexpr bool buildEnabled=true;
#else
inline constexpr bool buildEnabled=false;
#endif
// Engineering diagnostic ceilings only. Never used by controller admission,
// mode recovery, quarantine, publication, or any other family's wait budget.
inline constexpr uint64_t pendingMaxAgeMs=60000;
inline constexpr unsigned maxWakeAttempts=2,sessionSeconds=300;
enum class Active {Absent,Running,Unknown};
enum class Action {Idle,Launch,Wait,Refuse};
struct Decision {Action action=Action::Idle;const char* reason="no-pending";uint64_t generation=0;unsigned episode=0;bool log=false;uint64_t armedTick=0;};
struct State {
    bool startupAttempted=false,pending=false,waitLogged=false;
    uint64_t observedGeneration=0,observedTick=0,offeredGeneration=0,pendingGeneration=0,armedTick=0;
    unsigned wakeAttempts=0;
};
class Policy {
    mutable std::mutex mx;State s;
public:
    State snapshot()const {std::lock_guard<std::mutex> l(mx);return s;}
    void restore(const State& v){std::lock_guard<std::mutex> l(mx);s=v;}
    void noteResume(bool eligible,uint64_t generation,uint64_t now){std::lock_guard<std::mutex> l(mx);
        if(!eligible||!generation||!now||generation==s.observedGeneration)return;
        s.observedGeneration=generation;s.observedTick=now;
    }
    bool arm(bool eligible,uint64_t generation,uint64_t now){std::lock_guard<std::mutex> l(mx);
        if(!eligible||!generation||generation!=s.observedGeneration||!s.observedTick||now<s.observedTick||
            now-s.observedTick>=pendingMaxAgeMs||generation==s.offeredGeneration||s.wakeAttempts>=maxWakeAttempts)return false;
        s.offeredGeneration=generation;s.pendingGeneration=generation;s.armedTick=now;s.pending=true;s.waitLogged=false;++s.wakeAttempts;
        return true;
    }
    bool reserveStartup(bool eligible){std::lock_guard<std::mutex> l(mx);
        if(!eligible||s.startupAttempted||s.pending)return false;s.startupAttempted=true;return true;
    }
    void cancel(uint64_t expectedPending){std::lock_guard<std::mutex> l(mx);
        if(s.pendingGeneration==expectedPending)s.pending=false;
    }
    Decision startup(bool eligible,Active active){std::lock_guard<std::mutex> l(mx);
        if(!eligible||s.startupAttempted)return {};
        s.startupAttempted=true;
        return {active==Active::Absent?Action::Launch:Action::Refuse,
            active==Active::Absent?"initial-entry":active==Active::Running?"observer-already-running":"observer-status-unknown",0,0,true};
    }
    Decision poll(bool eligible,uint64_t generation,uint64_t now,Active active,uint64_t expectedPending=0){std::lock_guard<std::mutex> l(mx);
        if(!s.pending||(expectedPending&&expectedPending!=s.pendingGeneration))return {};
        Decision d{Action::Refuse,"wake-ineligible",s.pendingGeneration,s.wakeAttempts,true};d.armedTick=s.armedTick;
        if(!eligible||generation!=s.pendingGeneration||now<s.armedTick){s.pending=false;return d;}
        if(now-s.armedTick>=pendingMaxAgeMs){s.pending=false;d.reason="wake-diagnostic-pending-expired";return d;}
        if(active==Active::Unknown){s.pending=false;d.reason="observer-status-unknown";return d;}
        if(active==Active::Running){d.action=Action::Wait;d.reason="prior-observer-still-running";d.log=!s.waitLogged;s.waitLogged=true;return d;}
        s.pending=false;s.startupAttempted=true;d.action=Action::Launch;d.reason="committed-resume";return d;
    }
};
}
