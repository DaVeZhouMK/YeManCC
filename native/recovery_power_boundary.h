#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstddef>
#include <limits>
#include <memory>
#include <mutex>
#include "sleep_power_state.h"

namespace ymcc::sleeppower {
inline constexpr std::uint32_t kRecoveryBoundaryMagic=0x42435759u;
struct RecoveryPowerBoundary {
    std::uint32_t magic=0,size=0,version=0,reserved=0;
    alignas(8) volatile LONG64 liveSerial=0;
    std::uint64_t publicationStamp=0;
};
static_assert(sizeof(RecoveryPowerBoundary)==32);
static_assert(offsetof(RecoveryPowerBoundary,liveSerial)==16);
static_assert(offsetof(RecoveryPowerBoundary,publicationStamp)==24);
inline constexpr bool recoveryWatchdogEligible(const SharedPowerV1& power) noexcept {
    return power.size==sizeof(power)&&power.version==1&&power.generation&&
        (power.flags&kKnown)&&!(power.flags&kNotificationPending)&&(power.phase==0||power.phase==3);
}
// A cancellation watermark of the one Native authority. It cannot change any
// phase, generation, Ready state or hardware permission.
class RecoveryBoundaryMirror final {
    mutable std::mutex mutex;
    std::uint64_t serial;
    RecoveryPowerBoundary* attached=nullptr;
    bool closed=false;
    std::uint64_t advance() noexcept {
        constexpr auto maximum=static_cast<std::uint64_t>((std::numeric_limits<LONG64>::max)());
        if(serial>=maximum) {closed=true;return 0;}
        return ++serial;
    }
    void write(std::uint64_t value) noexcept {
        if(attached) InterlockedExchange64(&attached->liveSerial,static_cast<LONG64>(value));
    }
public:
    explicit RecoveryBoundaryMirror(std::uint64_t initial=1) noexcept:serial(initial),closed(initial==0){}
    RecoveryBoundaryMirror(const RecoveryBoundaryMirror&)=delete;
    RecoveryBoundaryMirror& operator=(const RecoveryBoundaryMirror&)=delete;
    bool attach(RecoveryPowerBoundary* record) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if(!record||closed||attached) return false;
        const auto next=advance();if(!next)return false;
        record->magic=kRecoveryBoundaryMagic;record->size=sizeof(*record);record->version=1;record->reserved=0;record->publicationStamp=0;
        attached=record;write(next);return true;
    }
    void revoke() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        write(advance());
    }
    void detach(RecoveryPowerBoundary* record) noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        if(record!=attached||!attached)return;
        write(advance());attached=nullptr;
    }
    void close() noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        write(advance());closed=true;attached=nullptr;
    }
    std::uint64_t sample() const noexcept {
        std::lock_guard<std::mutex> lock(mutex);
        return attached&&!closed?serial:0;
    }
};
struct RecoveryBoundaryTicket {
    const volatile LONG64* liveSerial=nullptr;
    std::shared_ptr<const std::atomic<bool>> retired;
    std::uint64_t serial=0;
    bool required=false;
    bool current() const noexcept {
        if(!required)return true; // Existing 152/224-byte publishers.
        if(!liveSerial||!retired||!serial||retired->load(std::memory_order_acquire))return false;
        const auto value=ReadAcquire64(liveSerial);
        return value>0&&static_cast<std::uint64_t>(value)==serial;
    }
};
}
