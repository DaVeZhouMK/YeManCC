#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <type_traits>
#pragma comment(lib,"OneCore.lib")

namespace running_trial {
// Receiver-table borrowed exports. These carry cancellation only; they are not
// power, account, process identity, topology or business-selection facts.
struct RoleCancellationWire {
    std::uint32_t size=sizeof(RoleCancellationWire),version=1;
    std::uint64_t sourceGroupEvent=0,sourceProcessRequestEvent=0,sourceBusinessRequestEvent=0;
};
inline bool validRoleCancellationWire(const RoleCancellationWire& wire)noexcept {
    return wire.size==sizeof(wire)&&wire.version==1&&
        wire.sourceGroupEvent&&wire.sourceProcessRequestEvent&&wire.sourceBusinessRequestEvent&&
        wire.sourceGroupEvent!=UINT64_MAX&&wire.sourceProcessRequestEvent!=UINT64_MAX&&
        wire.sourceBusinessRequestEvent!=UINT64_MAX&&
        wire.sourceGroupEvent!=wire.sourceProcessRequestEvent&&
        wire.sourceGroupEvent!=wire.sourceBusinessRequestEvent&&
        wire.sourceProcessRequestEvent!=wire.sourceBusinessRequestEvent;
}
// Legacy disabled packets carry an exact empty cancellation tail. Original-only
// packets use RoleGroupWire version 2 so an old DLL rejects them before ignoring
// a disabled group. The cancellation wire layout/version itself is unchanged.
inline bool emptyRoleCancellationWire(const RoleCancellationWire& wire)noexcept {
    return wire.size==sizeof(wire)&&wire.version==1&&!wire.sourceGroupEvent&&
        !wire.sourceProcessRequestEvent&&!wire.sourceBusinessRequestEvent;
}
inline bool validOriginalCancellationWire(const RoleCancellationWire& wire)noexcept {
    return wire.size==sizeof(wire)&&wire.version==1&&!wire.sourceGroupEvent&&
        wire.sourceProcessRequestEvent&&wire.sourceBusinessRequestEvent&&
        wire.sourceProcessRequestEvent!=UINT64_MAX&&wire.sourceBusinessRequestEvent!=UINT64_MAX&&
        wire.sourceProcessRequestEvent!=wire.sourceBusinessRequestEvent;
}
static_assert(sizeof(void*)==8&&sizeof(RoleCancellationWire)==32&&
              offsetof(RoleCancellationWire,sourceGroupEvent)==8&&
              offsetof(RoleCancellationWire,sourceProcessRequestEvent)==16&&
              offsetof(RoleCancellationWire,sourceBusinessRequestEvent)==24&&
              std::is_standard_layout_v<RoleCancellationWire>&&
              std::is_trivially_copyable_v<RoleCancellationWire>);

// Bind only after independently authenticating and pinning the actual YMCC
// provider. A wire cannot choose its provider. The three source handles are
// dedicated host exports in this receiver's own table, never host-table values.
// This receiver owns only non-inheritable minimal-access duplicates; it never
// closes the input slots. Their host-side exporter retains its original ledger.
class GroupPublicationRevocation final {
    struct State {
        HANDLE owner=nullptr;
        std::array<HANDLE,3> events{};
        DWORD count=0;
        ~State(){for(const auto event:events)if(event)CloseHandle(event);if(owner)CloseHandle(owner);}
    };
    std::atomic<std::shared_ptr<State>> state_{};
    std::atomic<bool> attempted_{false},revoked_{true},retired_{false};
public:
    GroupPublicationRevocation()=default;
    GroupPublicationRevocation(const GroupPublicationRevocation&)=delete;
    GroupPublicationRevocation& operator=(const GroupPublicationRevocation&)=delete;
    ~GroupPublicationRevocation(){retire();}
private:
    bool bindEvents(HANDLE trustedActualOwner,const RoleCancellationWire& wire,bool original)noexcept {
        if(attempted_.exchange(true,std::memory_order_acq_rel)||
           retired_.load(std::memory_order_acquire)||
           !(original?validOriginalCancellationWire(wire):validRoleCancellationWire(wire))||
           !trustedActualOwner||trustedActualOwner==INVALID_HANDLE_VALUE)return false;
        const auto ownerPid=GetProcessId(trustedActualOwner);
        if(!ownerPid)return false;
        try {
            auto next=std::make_shared<State>();
            if(!DuplicateHandle(GetCurrentProcess(),trustedActualOwner,GetCurrentProcess(),&next->owner,
                    PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,0))return false;
            // Do not let a source slot alias the authenticated owner or another
            // cancellation object, even when the numeric handles are distinct.
            if(GetProcessId(next->owner)!=ownerPid||
               !CompareObjectHandles(next->owner,trustedActualOwner))return false;
            next->count=original?2u:3u;
            const std::array<std::uint64_t,3> sources=original?
                std::array<std::uint64_t,3>{wire.sourceProcessRequestEvent,wire.sourceBusinessRequestEvent,0}:
                std::array<std::uint64_t,3>{wire.sourceGroupEvent,wire.sourceProcessRequestEvent,wire.sourceBusinessRequestEvent};
            for(std::size_t i=0;i<next->count;++i){
                if(!DuplicateHandle(GetCurrentProcess(),
                        reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(sources[i])),
                        GetCurrentProcess(),&next->events[i],SYNCHRONIZE,FALSE,0)||
                   CompareObjectHandles(next->owner,next->events[i]))return false;
                for(std::size_t j=0;j<i;++j)
                    if(CompareObjectHandles(next->events[j],next->events[i]))return false;
            }
            state_.store(std::move(next),std::memory_order_release);
            revoked_.store(false,std::memory_order_release);
            if(current())return true;
            retire();return false;
        }catch(...){return false;}
    }
public:
    bool bind(HANDLE trustedActualOwner,const RoleCancellationWire& wire)noexcept {
        return bindEvents(trustedActualOwner,wire,false);
    }
    bool bindOriginal(HANDLE trustedActualOwner,const RoleCancellationWire& wire)noexcept {
        return bindEvents(trustedActualOwner,wire,true);
    }
    bool current()noexcept {
        if(retired_.load(std::memory_order_acquire)||revoked_.load(std::memory_order_acquire))return false;
        // Keep these exact local duplicates alive through concurrent retire.
        const auto held=state_.load(std::memory_order_acquire);
        if(!held){revoked_.store(true,std::memory_order_release);return false;}
        const std::array<HANDLE,4> waits{
            held->owner,held->events[0],held->events[1],held->events[2]};
        // Exactly one nonblocking wait per checkpoint. Any signal, failure or
        // unexpected result revokes permanently; later Ready cannot reset it.
        if(held->count<2||held->count>3||
           WaitForMultipleObjects(held->count+1,waits.data(),FALSE,0)!=WAIT_TIMEOUT){
            revoked_.store(true,std::memory_order_release);return false;
        }
        return !retired_.load(std::memory_order_acquire)&&!revoked_.load(std::memory_order_acquire);
    }
    void retire()noexcept {
        retired_.store(true,std::memory_order_release);
        revoked_.store(true,std::memory_order_release);
        state_.exchange({},std::memory_order_acq_rel);
    }
};
}
