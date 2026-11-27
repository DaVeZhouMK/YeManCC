#pragma once
#include <string>
#include <string_view>

// Pure log classification only. This policy never admits, retries or stops a device.
namespace ymcc::logging {
enum class NativeLane { suppressed, event, detail };

constexpr NativeLane nativeLane(bool baseEvent, bool streamingRecord, bool detailedEnabled) noexcept {
    if (baseEvent && !streamingRecord) return NativeLane::event;
    return detailedEnabled ? NativeLane::detail : NativeLane::suppressed;
}

// The timer may repeat only the edge that actually published its cached state.
struct ControllerConnectionEdge {
    unsigned long long sequence=0,tick=0,power=0;
    std::string_view powerPhase;
    bool connected=false,written=false;
    const char* event() const noexcept {return connected?"gamepad-controller-wake":"gamepad-controller-sleep";}
    constexpr unsigned long long confirmedDisconnectSequence(bool currentEdge, unsigned long long currentPower,
                                                              std::string_view currentPhase) const noexcept {
        return currentEdge && !connected && written && power && power == currentPower && powerPhase == currentPhase
            ? sequence : 0;
    }
    const char* timerEvidence(bool state,unsigned long long now,unsigned long long currentPower,
                             std::string_view currentPhase) const noexcept {
        if(!sequence||connected!=state)return "no-matching-edge";
        if(!written)return "edge-write-unconfirmed";
        if(!power||currentPower!=power||currentPhase!=powerPhase)return "edge-context-changed";
        if(now<tick||now-tick>150)return "edge-outside-recent-window";
        return nullptr;
    }
};
struct ControllerConnectionAppendScope {
    static inline thread_local ControllerConnectionEdge* pending=nullptr;
    ControllerConnectionEdge* previous;
    explicit ControllerConnectionAppendScope(ControllerConnectionEdge& edge) noexcept:previous(pending){pending=&edge;}
    ~ControllerConnectionAppendScope(){pending=previous;}
    ControllerConnectionAppendScope(const ControllerConnectionAppendScope&)=delete;
    ControllerConnectionAppendScope& operator=(const ControllerConnectionAppendScope&)=delete;
};

class ResidentAppendScope {
    static inline thread_local ResidentAppendScope* pending=nullptr;
    ResidentAppendScope* previous;
    std::string_view event;
    bool& flushed;
public:
    ResidentAppendScope(std::string_view eventName,bool& confirmed) noexcept
        :previous(pending),event(eventName),flushed(confirmed){pending=this;}
    ~ResidentAppendScope(){pending=previous;}
    ResidentAppendScope(const ResidentAppendScope&)=delete;
    ResidentAppendScope& operator=(const ResidentAppendScope&)=delete;
    static void confirm(const char* name) noexcept {
        if (pending && name && pending->event==name) pending->flushed=true;
    }
};

struct HidHideDisconnectLogMemo {
    struct Context {
        unsigned long long power=0,host=0,delivery=0;
        unsigned long previousAdmittedMask=0;
        std::string_view phase;
        bool operator==(const Context&) const = default;
    };
    Context last;
    bool known=false;
};

// Both records are streaming samples; neither may force a flush per BUS frame.
constexpr bool inputSampleKind(std::string_view kind) noexcept {
    return kind == "sample" || kind == "controller-family-imu";
}

// Repeated snapshots are detailed samples; first/change/new power generation
// are event evidence. Fixed instances only: no unbounded per-device key map.
struct SnapshotLogState {
    std::string signature;
    unsigned long long generation = 0;
    bool known = false;
    bool observe(std::string_view next, unsigned long long nextGeneration) {
        if (known && generation == nextGeneration && signature == next) return false;
        signature.assign(next); generation = nextGeneration; known = true;
        return true;
    }
};

// Generic device-tree notifications are samples unless they carry sleep intent.
// Arrival/removal messages stay factual events regardless of their frequency.
constexpr bool sleepDeviceTreeSample(unsigned long long code, bool userStandby, bool ageEligible) noexcept {
    return code == 7 /* DBT_DEVNODES_CHANGED */ && !userStandby && !ageEligible;
}

// Markers are explicit opt-ins, not mere file existence. No locale/config side effects.
constexpr bool explicitLogFlag(std::string_view value) noexcept {
    const auto space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; };
    while (!value.empty() && space(value.front())) value.remove_prefix(1);
    while (!value.empty() && space(value.back())) value.remove_suffix(1);
    if (value == "1") return true;
    if (value.size() != 7) return false;
    constexpr std::string_view enabled = "enabled";
    for (size_t i = 0; i < enabled.size(); ++i) {
        const char c = value[i] >= 'A' && value[i] <= 'Z' ? static_cast<char>(value[i] + ('a' - 'A')) : value[i];
        if (c != enabled[i]) return false;
    }
    return true;
}

constexpr bool detailNeedsImmediateFlush(std::string_view event, bool explicitFailure) noexcept {
    return explicitFailure || event.find("fault") != std::string_view::npos ||
        event.find("fail") != std::string_view::npos || event.find("error") != std::string_view::npos ||
        event.find("reject") != std::string_view::npos || event.find("timeout") != std::string_view::npos;
}

// Only category edges belong to the always-on lane; window values stay detailed.
struct BusRateLogState {
    static constexpr unsigned maxComparableWindowMs=2000;
    struct Window {
        const char* interruption=nullptr;
        bool firstInterrupted=false;
        unsigned long long startReadyPower=0;
        bool transition=false;
    };
    unsigned flags = 0;
    unsigned long long startReadyPower=0;
    bool interrupted=false;
    void beginWindow(unsigned long long readyPower) noexcept {startReadyPower=readyPower;}
    Window closeWindow(unsigned long long elapsedMs,unsigned long long readyPower,unsigned next) noexcept {
        const char* reason=!startReadyPower||startReadyPower!=readyPower ? "power-context-changed" :
            elapsedMs>maxComparableWindowMs ? "window-too-long" : nullptr;
        const Window result{reason,reason&&!interrupted,startReadyPower,!reason&&observe(next)};
        startReadyPower=readyPower;
        interrupted=reason!=nullptr;
        if(interrupted)flags=0;
        return result;
    }
    bool observe(unsigned next) noexcept {
        if (flags == next) return false;
        flags = next;
        return true;
    }
};
}
