#pragma once
#include <windows.h>
#include <array>
#include <atomic>
#include <cstdint>
#include "json.hpp"

namespace ymcc::sleepnotifications {
// REQUIRED ordinary-export evidence. Callbacks only update fixed atomic fields:
// no allocation, disk write, locks, timer or polling. Never a power-action gate.
class Evidence {
    using U64=std::uint64_t;
    std::array<std::atomic<U64>,4> callbacks_{},handlers_{};
    std::atomic<U64> posted_{0},postFailed_{0},noWindow_{0},lastCallbackTick_{0},lastHandlerTick_{0};
    std::atomic<DWORD> registrationError_{0},lastPostError_{0};
    std::atomic<bool> registrationAttempted_{false};
public:
    static int index(ULONG type) noexcept {
        switch(type){case PBT_APMSUSPEND:return 0;case PBT_APMRESUMEAUTOMATIC:return 1;
            case PBT_APMRESUMESUSPEND:return 2;case PBT_APMRESUMECRITICAL:return 3;default:return -1;}
    }
    void registered(DWORD error) noexcept {
        registrationError_.store(error,std::memory_order_relaxed);
        registrationAttempted_.store(true,std::memory_order_release);
    }
    void callback(ULONG type,U64 tick) noexcept {
        const auto i=index(type);if(i<0)return;
        callbacks_[i].fetch_add(1,std::memory_order_relaxed);lastCallbackTick_.store(tick,std::memory_order_relaxed);
    }
    void noWindow() noexcept {noWindow_.fetch_add(1,std::memory_order_relaxed);}
    void forwarded(bool ok,DWORD error) noexcept {
        if(ok)posted_.fetch_add(1,std::memory_order_relaxed);
        else {lastPostError_.store(error,std::memory_order_relaxed);postFailed_.fetch_add(1,std::memory_order_relaxed);}
    }
    void handled(ULONG type,U64 tick) noexcept {
        const auto i=index(type);if(i<0)return;
        handlers_[i].fetch_add(1,std::memory_order_relaxed);lastHandlerTick_.store(tick,std::memory_order_relaxed);
    }
    nlohmann::json snapshot() const {
        using Json=nlohmann::json;
        const char* names[]={"suspend","resumeAutomatic","resumeSuspend","resumeCritical"};
        Json callbacks=Json::object(),handlers=Json::object();
        for(unsigned i=0;i<4;++i){callbacks[names[i]]=callbacks_[i].load(std::memory_order_relaxed);handlers[names[i]]=handlers_[i].load(std::memory_order_relaxed);}
        const bool attempted=registrationAttempted_.load(std::memory_order_acquire);
        const auto failed=postFailed_.load(std::memory_order_relaxed);
        return {{"schemaVersion",1},{"registrationAttempted",attempted},
            {"registrationError",attempted?Json(registrationError_.load(std::memory_order_relaxed)):Json(nullptr)},
            {"callbackEntries",std::move(callbacks)},{"windowHandlerEntries",std::move(handlers)},
            {"forwarding",{{"posted",posted_.load(std::memory_order_relaxed)},{"failed",failed},
                {"noWindow",noWindow_.load(std::memory_order_relaxed)},
                {"lastFailureWin32Error",failed?Json(lastPostError_.load(std::memory_order_relaxed)):Json(nullptr)}}},
            {"lastCallbackTickMs",lastCallbackTick_.load(std::memory_order_relaxed)},
            {"lastWindowHandlerTickMs",lastHandlerTick_.load(std::memory_order_relaxed)},
            {"clockScope","process-observed-boot-tick;not-Windows-original-event-time"},
            {"handlerScope","WndProc-entry-not-completion;window-and-forwarded-deliveries-may-duplicate"},
            {"snapshotAtomic",false},{"counterScope","current-process-only;not-complete-Windows-transition-history"},
            {"callbackDiskWrites",false},{"detailLogsRequired",false},{"mandatoryEvidence",true},
            {"userWakeProven",false},{"sleepSuccessProven",false},{"powerActionAuthorized",false}};
    }
};
} // namespace ymcc::sleepnotifications
