#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include "../sleep_power_state.h"
#pragma comment(lib,"OneCore.lib")

namespace running_trial {
struct PublicationRevocationWire {
    std::uint32_t size=sizeof(PublicationRevocationWire),version=2;
    std::uint64_t sourcePowerEvent=0,sourceIdentityEvent=0;
    ymcc::sleeppower::SharedPowerV1 power{};
};
inline bool validPublicationRevocation(const PublicationRevocationWire& wire)noexcept {
    return wire.size==sizeof(wire)&&wire.version==2&&wire.sourcePowerEvent&&wire.sourceIdentityEvent&&
        wire.sourcePowerEvent!=UINT64_MAX&&wire.sourceIdentityEvent!=UINT64_MAX&&
        wire.sourcePowerEvent!=wire.sourceIdentityEvent&&ymcc::sleeppower::powerAdmitted(wire.power);
}
// Only bind after the caller has independently pinned and verified the original
// YMCC provider. Raw source handle numbers and the captured stamp alone grant
// nothing. The two input handles are dedicated host exports in this receiver's
// own table; only copies are owned here. Provider needs query/wait rights only.
class PublicationRevocation final {
    struct State {
        HANDLE owner=nullptr,power=nullptr,identity=nullptr;
        ~State(){if(identity)CloseHandle(identity);if(power)CloseHandle(power);if(owner)CloseHandle(owner);}
    };
    std::atomic<std::shared_ptr<State>> state_{};
    std::atomic<bool> attempted_{false},revoked_{true},retired_{false};
    static bool live(HANDLE value)noexcept {
        return value&&value!=INVALID_HANDLE_VALUE&&WaitForSingleObject(value,0)==WAIT_TIMEOUT;
    }
public:
    PublicationRevocation()=default;
    PublicationRevocation(const PublicationRevocation&)=delete;
    PublicationRevocation& operator=(const PublicationRevocation&)=delete;
    ~PublicationRevocation(){retire();}
    bool bind(HANDLE trustedActualOwner,const PublicationRevocationWire& wire)noexcept {
        if(attempted_.exchange(true,std::memory_order_acq_rel)||retired_.load(std::memory_order_acquire)||!validPublicationRevocation(wire)||
           !live(trustedActualOwner)||!GetProcessId(trustedActualOwner))return false;
        try {
            auto next=std::make_shared<State>();
            if(!DuplicateHandle(GetCurrentProcess(),trustedActualOwner,GetCurrentProcess(),&next->owner,
                    PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0)||
               !DuplicateHandle(GetCurrentProcess(),reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(wire.sourcePowerEvent)),
                    GetCurrentProcess(),&next->power,SYNCHRONIZE,FALSE,0)||
               !DuplicateHandle(GetCurrentProcess(),reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(wire.sourceIdentityEvent)),
                    GetCurrentProcess(),&next->identity,SYNCHRONIZE,FALSE,0))return false;
            if(!live(next->owner)||!live(next->power)||!live(next->identity)||
               CompareObjectHandles(next->power,next->identity)||CompareObjectHandles(next->owner,next->power)||
               CompareObjectHandles(next->owner,next->identity))return false;
            state_.store(std::move(next),std::memory_order_release);
            revoked_.store(false,std::memory_order_release);
            if(current())return true;
            retire();return false;
        }catch(...){return false;}
    }
    bool current()noexcept {
        if(retired_.load(std::memory_order_acquire)||revoked_.load(std::memory_order_acquire))return false;
        // A reader keeps its exact local copies alive across concurrent retire.
        // Closing an unrelated source handle cannot turn a reused number into a
        // new capability, and a later Ready cannot revive a cancelled instance.
        const auto held=state_.load(std::memory_order_acquire);
        if(!held||!live(held->owner)||!live(held->power)||!live(held->identity)){
            revoked_.store(true,std::memory_order_release);return false;
        }
        return !retired_.load(std::memory_order_acquire)&&!revoked_.load(std::memory_order_acquire);
    }
    void retire()noexcept {
        retired_.store(true,std::memory_order_release);revoked_.store(true,std::memory_order_release);
        state_.exchange({},std::memory_order_acq_rel);
    }
};
}
