#pragma once
#include <atomic>
#include <cstdint>
#include "sleep_power_state.h"

// MSI-only evidence handoff. No input admission, I/O, timers or device mutation.
namespace ymcc::msi::ownedReturnOff {
struct Proof {
    std::uint64_t episode=0,power=0,mode=0,intent=0,host=0,sourceEvent=0,readSequence=0;
    unsigned topology=0,identity=0,mask=0;
    sleeppower::SharedPowerV1 powerScope;
};
class Publication {
    std::atomic<std::uint64_t> revision_{0},episode_{0},power_{0},mode_{0},intent_{0},host_{0},sourceEvent_{0},readSequence_{0};
    std::atomic<unsigned> topology_{0},identity_{0},mask_{0};
    std::atomic<std::uint32_t> powerSize_{0},powerVersion_{0},powerPhase_{0},powerFlags_{0};
    std::atomic<std::uint64_t> scopeGeneration_{0},powerPublication_{0},powerSequence_{0},powerSuspend_{0};
public:
    // Single existing lifecycle writer; all members stay atomic for nonblocking
    // BUS readers. Never return an odd/mixed revision as usable evidence.
    void publish(const Proof& p) noexcept {
        revision_.fetch_add(1,std::memory_order_seq_cst);
        episode_.store(p.episode,std::memory_order_seq_cst);power_.store(p.power,std::memory_order_seq_cst);
        mode_.store(p.mode,std::memory_order_seq_cst);intent_.store(p.intent,std::memory_order_seq_cst);
        host_.store(p.host,std::memory_order_seq_cst);sourceEvent_.store(p.sourceEvent,std::memory_order_seq_cst);
        readSequence_.store(p.readSequence,std::memory_order_seq_cst);topology_.store(p.topology,std::memory_order_seq_cst);
        identity_.store(p.identity,std::memory_order_seq_cst);mask_.store(p.mask,std::memory_order_seq_cst);
        powerSize_.store(p.powerScope.size,std::memory_order_seq_cst);powerVersion_.store(p.powerScope.version,std::memory_order_seq_cst);
        scopeGeneration_.store(p.powerScope.generation,std::memory_order_seq_cst);powerPublication_.store(p.powerScope.publicationRevision,std::memory_order_seq_cst);
        powerSequence_.store(p.powerScope.observedNotificationSequence,std::memory_order_seq_cst);powerPhase_.store(p.powerScope.phase,std::memory_order_seq_cst);
        powerFlags_.store(p.powerScope.flags,std::memory_order_seq_cst);powerSuspend_.store(p.powerScope.suspendSerial,std::memory_order_seq_cst);
        revision_.fetch_add(1,std::memory_order_seq_cst);
    }
    bool snapshot(Proof& p) const noexcept {
        const auto before=revision_.load(std::memory_order_seq_cst);if(!before||(before&1))return false;
        p={episode_.load(std::memory_order_seq_cst),power_.load(std::memory_order_seq_cst),mode_.load(std::memory_order_seq_cst),
            intent_.load(std::memory_order_seq_cst),host_.load(std::memory_order_seq_cst),sourceEvent_.load(std::memory_order_seq_cst),
            readSequence_.load(std::memory_order_seq_cst),topology_.load(std::memory_order_seq_cst),identity_.load(std::memory_order_seq_cst),mask_.load(std::memory_order_seq_cst)};
        p.powerScope={powerSize_.load(std::memory_order_seq_cst),powerVersion_.load(std::memory_order_seq_cst),
            scopeGeneration_.load(std::memory_order_seq_cst),powerPublication_.load(std::memory_order_seq_cst),powerSequence_.load(std::memory_order_seq_cst),
            powerPhase_.load(std::memory_order_seq_cst),powerFlags_.load(std::memory_order_seq_cst),powerSuspend_.load(std::memory_order_seq_cst)};
        return revision_.load(std::memory_order_seq_cst)==before;
    }
};
inline constexpr bool sameContext(const Proof& a,const Proof& b) noexcept {
    return a.episode==b.episode && a.power==b.power && a.mode==b.mode && a.intent==b.intent &&
        a.host==b.host && a.sourceEvent==b.sourceEvent && a.topology==b.topology && a.identity==b.identity &&
        a.powerScope.suspendSerial==b.powerScope.suspendSerial;
}
inline constexpr bool exactSinglePhysicalMask(unsigned matchedMask,unsigned connectedMask) noexcept {
    return matchedMask && matchedMask<16 && (matchedMask&(matchedMask-1u))==0 &&
        (connectedMask&matchedMask)!=0 && (connectedMask&~matchedMask)==0;
}
}
