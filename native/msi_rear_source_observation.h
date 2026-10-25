#pragma once
// Read-only, lifetime counters. No API, I/O, lock, timer or input authority.
#include <array>
#include <atomic>
#include <cstdint>
namespace ymcc::msi::rearObservation {
struct Snapshot { uint64_t sequence=0,samples=0,generation=0,activityReservations=0,lastReservedSequence=0; uint32_t rawMask=0,lastVk=0,lastScan=0,lastFlags=0;
    std::array<uint64_t,8> keyboard{}; std::array<uint64_t,32> buttonDown{}; };
class Counters {
    std::atomic<bool> enabled_{false};
    std::atomic<uint64_t> sequence_{0},samples_{0},generation_{0},activityReservations_{0},lastReservedSequence_{0};
    std::atomic<uint32_t> mask_{0},vk_{0},scan_{0},flags_{0};
    std::array<std::atomic<uint64_t>,8> keyboard_{};
    std::array<std::atomic<uint64_t>,32> down_{};
public:
    void enable(bool enabled) noexcept { enabled_.store(enabled,std::memory_order_release); }
    void keyboard(uint32_t vk,uint32_t scan,uint32_t flags,bool down,bool injected) noexcept {
        if(!enabled_.load(std::memory_order_acquire)||(vk!=0x7a&&vk!=0x7d))return;
        const unsigned slot=(vk==0x7d?4u:0u)+(injected?2u:0u)+(down?0u:1u);
        keyboard_[slot].fetch_add(1,std::memory_order_relaxed);
        vk_=vk;scan_=scan;flags_=flags;sequence_.fetch_add(1,std::memory_order_release);
    }
    void dinput(uint64_t generation,uint32_t mask) noexcept {
        if(!enabled_.load(std::memory_order_acquire)||!generation)return;
        ++samples_;for(unsigned i=0;i<32;++i)if(mask&(uint32_t(1)<<i))++down_[i];
        const auto priorGeneration=generation_.exchange(generation);
        const auto priorMask=mask_.exchange(mask);
        if(priorGeneration!=generation||priorMask!=mask)sequence_.fetch_add(1,std::memory_order_release);
    }
    void reserveActivity(uint64_t sequence) noexcept { ++activityReservations_;lastReservedSequence_=sequence; }
    Snapshot snapshot() const noexcept {
        Snapshot s;s.sequence=sequence_.load(std::memory_order_acquire);s.samples=samples_;s.generation=generation_;s.rawMask=mask_;
        s.lastVk=vk_;s.lastScan=scan_;s.lastFlags=flags_;s.activityReservations=activityReservations_;s.lastReservedSequence=lastReservedSequence_;
        for(unsigned i=0;i<8;++i)s.keyboard[i]=keyboard_[i];
        for(unsigned i=0;i<32;++i)s.buttonDown[i]=down_[i];return s;
    }
};
} // namespace ymcc::msi::rearObservation
