#pragma once
#include <atomic>
#include <cstdint>

// Capture-process ownership only. This is not a virtual persona request or an
// alternative to VC-LIFECYCLE-20261020-R3. No resource is settled by ready=false.
namespace ymcc::captureLifecycle {
enum class AdmissionState : unsigned { Idle, Starting, Running, StopPendingStart, Stopping };
struct Admission {
    std::atomic<AdmissionState> state{AdmissionState::Idle};
    bool beginStart() noexcept {
        auto expected=AdmissionState::Idle;
        return state.compare_exchange_strong(expected,AdmissionState::Starting,std::memory_order_acq_rel);
    }
    void finishStart(bool running) noexcept {
        auto expected=AdmissionState::Starting;
        if(state.compare_exchange_strong(expected,running?AdmissionState::Running:AdmissionState::Idle,std::memory_order_acq_rel))return;
        expected=AdmissionState::StopPendingStart;
        (void)state.compare_exchange_strong(expected,AdmissionState::Stopping,std::memory_order_acq_rel);
    }
    bool requestStop() noexcept {
        auto observed=state.load(std::memory_order_acquire);
        for(;;){
            if(observed==AdmissionState::StopPendingStart)return false;
            if(observed==AdmissionState::Stopping)return true;
            const auto next=observed==AdmissionState::Starting?AdmissionState::StopPendingStart:AdmissionState::Stopping;
            if(state.compare_exchange_weak(observed,next,std::memory_order_acq_rel))return next==AdmissionState::Stopping;
        }
    }
};
enum class Completion : unsigned { Pending, Settled, RetainedFault };
enum Debt : std::uint64_t {
    StartReservation=1ULL<<0, Worker=1ULL<<1, WorkerHandle=1ULL<<2,
    Timer=1ULL<<3, Dsu=1ULL<<4, SensorSta=1ULL<<5, SensorProviders=1ULL<<6,
    Census=1ULL<<7, OemIo=1ULL<<8, Motion=1ULL<<9, Gamebar=1ULL<<10,
    StopOwner=1ULL<<11
};
struct StopReceipt {
    std::uint64_t generation=0,debts=0;
    std::uint32_t waitResult=0,waitWin32=0,closeWin32=0;
    bool waitPerformed=false,workerExited=false,workerHandleClosed=false;
    Completion completion=Completion::Pending;
    // A resource receipt can become settled after an earlier retained fault;
    // it never publishes another terminal result for an R3 player request.
    bool settled() const noexcept {
        return completion==Completion::Settled && debts==0 && workerExited && workerHandleClosed;
    }
    void classify() noexcept {
        completion=debts==0&&workerExited&&workerHandleClosed?Completion::Settled:
            (waitWin32||closeWin32?Completion::RetainedFault:Completion::Pending);
    }
};
// Nonblocking single consumer; neither Start nor the capture worker needs this
// lease. It cannot deadlock their progress and never abandons a live wait.
struct StopLease {
    std::atomic_flag& flag; bool owned;
    explicit StopLease(std::atomic_flag& value) noexcept:flag(value),owned(!flag.test_and_set(std::memory_order_acquire)){}
    ~StopLease(){if(owned)flag.clear(std::memory_order_release);}
    StopLease(const StopLease&)=delete;StopLease& operator=(const StopLease&)=delete;
};
} // namespace ymcc::captureLifecycle
