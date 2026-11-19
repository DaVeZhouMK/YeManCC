#pragma once
// Diagnostic-only same-call branch observations. No IO, clock, query or allocation.
#include <array>
#include <atomic>
#include <cstdint>
#include "json.hpp"
namespace ymcc::logging {
enum class UiInputGate : unsigned { RadialOwner, UiIneligible, UiEligible, Count };
class UiInputGateEvidence {
    struct Row { std::atomic<std::uint64_t> calls{0},nonzeroButtons{0},changedButtons{0}; };
    std::array<Row,3> rows_{};
public:
    struct Counts { std::uint64_t calls=0,nonzeroButtons=0,changedButtons=0; };
    using Snapshot=std::array<Counts,3>;
    void observe(UiInputGate gate,std::uint16_t buttons,std::uint16_t previousButtons) noexcept {
        const auto index=static_cast<unsigned>(gate);if(index>=rows_.size())return;
        auto& row=rows_[index];row.calls.fetch_add(1,std::memory_order_relaxed);
        if(buttons)row.nonzeroButtons.fetch_add(1,std::memory_order_relaxed);
        if(buttons!=previousButtons)row.changedButtons.fetch_add(1,std::memory_order_relaxed);
    }
    Snapshot snapshot() const noexcept {
        Snapshot out{};
        for(std::size_t i=0;i<rows_.size();++i){const auto& row=rows_[i];out[i]={row.calls.load(std::memory_order_relaxed),row.nonzeroButtons.load(std::memory_order_relaxed),row.changedButtons.load(std::memory_order_relaxed)};}
        return out;
    }
    static nlohmann::json describe(const Snapshot& view,bool detailedEnabledAtExport) {
        static constexpr const char* names[]={"radial-owner","ui-ineligible","ui-eligible"};
        nlohmann::json out=nlohmann::json::object();out["schema"]=1;
        out["scope"]="original-gamepadProcessUiInput-gate-return-observations;not-full-input-path-or-actions";
        out["detailedEnabledAtExport"]=detailedEnabledAtExport;out["counters"]=nlohmann::json::object();
        for(std::size_t i=0;i<view.size();++i){const auto& row=view[i];auto& target=out["counters"][names[i]];target=nlohmann::json::object();target["calls"]=row.calls;target["nonzeroButtons"]=row.nonzeroButtons;target["changedButtons"]=row.changedButtons;}
        out["observationWindow"]="process-lifetime-only-while-detailed-enabled;not-reset-on-config-or-export";
        out["buttonScope"]="existing-w-and-g_prevW-at-original-gate;not-analog-or-human-edge-proof";
        out["snapshotAtomicAcrossCounters"]=false;out["countersModulo64Bit"]=true;
        out["callsArePresses"]=false;out["zeroIsNoDeviceProof"]=false;
        out["uiEligibleIsActionOrDeliveryProof"]=false;out["upstreamCallsNotObserved"]=true;
        return out;
    }
};
}
