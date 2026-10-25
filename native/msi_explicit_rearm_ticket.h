#pragma once
#include <cstdint>
#include <mutex>
#include <string>
namespace ymcc::msi::rearm {
// Preserve an already-committed ON request after a proven normal OFF release.
// Authority stays with known MSI boards in the Native wrappers. This grants
// no automatic retry after fault/crash and no publication before Start gates.
// Engineering rollback is explicit; normal mainline builds include the fix.
#ifdef YMCC_MSI_EXPLICIT_REARM_DISABLE
inline constexpr bool buildEnabled=false;
#else
inline constexpr bool buildEnabled=true;
#endif
struct Facts {
    bool enabled=false,knownBoard=false,restored=false,quarantined=true,journalActive=true;
    bool hostPresent=true,faultHold=true,transportFault=true,exitFence=true,powerReady=false,wanted=false;
    uint64_t power=0;std::string persona;
};
struct State {
    uint64_t sequence=0,offSequence=0,startedSequence=0,settingPower=0;
    uint64_t releaseFloor=0,releasePower=0,consumedSequence=0;
    bool wanted=false,releaseEligible=false;std::string persona;
};
struct Receipt {bool preserved=false;uint64_t settingSequence=0,releaseFloor=0;};
// A setting receipt is not a controller-insertion receipt. Hold only a newer
// explicit ON/type request across an acknowledged NORMAL user-OFF release.
// This never grants publication or performs I/O; the old Start gates remain.
class Ticket {
    mutable std::mutex mx;State s;
public:
    State snapshot()const {std::lock_guard<std::mutex> l(mx);return s;}
    void restore(const State& v){std::lock_guard<std::mutex> l(mx);s=v;}
    void setting(bool wanted,const std::string& persona,uint64_t power){
        std::lock_guard<std::mutex> l(mx);++s.sequence;s.wanted=wanted;s.persona=persona;s.settingPower=power;
        if(!wanted)s.offSequence=s.sequence;
    }
    // ACK belongs to the intent captured BEFORE the external Start wait, not
    // to any newer setting committed while PREPARE/SUBMIT_NEUTRAL was in flight.
    bool started(uint64_t ownerSequence){std::lock_guard<std::mutex> l(mx);
        if(ownerSequence>s.sequence)return false;
        s.startedSequence=ownerSequence;s.releaseEligible=false;return true;
    }
    void released(bool normalOff,bool quarantined,bool localAck,bool shutdownAck,bool exited,uint64_t power){
        std::lock_guard<std::mutex> l(mx);s.releaseEligible=normalOff&&quarantined&&localAck&&shutdownAck&&exited&&power&&
            s.offSequence>s.startedSequence;
        s.releaseFloor=s.offSequence;s.releasePower=power;
    }
    Receipt take(const Facts& f){
        std::lock_guard<std::mutex> l(mx);Receipt r{false,s.sequence,s.releaseFloor};
        if(!f.enabled||!f.knownBoard||!f.restored||f.quarantined||f.journalActive||f.hostPresent||f.faultHold||
           f.transportFault||f.exitFence||!f.powerReady||!f.wanted||!s.wanted||!s.releaseEligible||
           !f.power||f.power!=s.releasePower||f.power!=s.settingPower||f.persona!=s.persona||f.persona.empty()||
           f.persona=="disabled"||s.sequence<=s.releaseFloor||s.sequence<=s.consumedSequence)return r;
        s.consumedSequence=s.sequence;s.releaseEligible=false;r.preserved=true;return r;
    }
};
}
