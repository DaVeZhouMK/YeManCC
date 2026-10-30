#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>
#include "input_signal_activity_evidence.h"
#include "json.hpp"

// Caller-drain diagnostics only. No recorder, lock, hook, polling, injection or
// scan-code conversion. select/capture use bounded arrays and never allocate;
// only toJson allocates, on the existing caller's drain path. Ops must not throw.
namespace ymcc::inputKeyStateEvidence {
inline constexpr std::size_t candidateCapacity = 16;
enum Reason : std::uint32_t {
    CallbackHeld = 1, SelfHeldAssumption = 2, RecentActivity = 4,
    KeyboardRow = 8, MouseButtonRow = 16
};
struct Candidate {
    std::uint16_t virtualKey = 0;
    std::uint32_t reasonBits = 0;
    std::size_t contributingRows = 0, extendedRows = 0, genericRows = 0;
    std::size_t callbackHeldRows = 0, selfHeldAssumptionRows = 0;
    std::uint64_t lastActivityTick = 0;
    bool asyncDownBit = false, callingThreadQueueDown = false;
    std::size_t injectedHookHeldRows = 0;
    std::uint64_t injectedHookHeldEdgeTick = 0;
    bool injectedHookHeldEdgeKnown = false;
};
struct Selection {
    std::array<Candidate, candidateCapacity> candidates{};
    std::size_t count = 0, uniqueEligibleKeys = 0, eligibleRows = 0;
    std::size_t inputRowCount = 0, inspectedRows = 0, uninspectedRetainedRows = 0;
    std::size_t omittedKeys = 0, omittedHeldKeys = 0, omittedEligibleRows = 0;
    std::size_t scanCodeRowsExcluded = 0, nonButtonMouseRowsExcluded = 0;
    std::size_t invalidVirtualKeyRows = 0, invalidMouseButtonRows = 0;
    std::size_t inactiveRows = 0, otherKindRowsExcluded = 0;
    std::size_t upstreamTotalRows = 0, upstreamOmittedRows = 0;
    std::size_t upstreamOmittedActiveRows = 0, upstreamOmittedHeldRows = 0;
    bool upstreamHeldStateMayHaveGaps = false, upstreamCountersSaturated = false;
    bool upstreamContentionCounterWrapped = false;
    std::uint64_t upstreamDroppedContention = 0, upstreamDroppedOverflow = 0;
    std::uint64_t upstreamRejectedInvalid = 0, upstreamDiscardedOnReset = 0;
    std::uint64_t upstreamDiscardedHeldOnReset = 0, upstreamDiscardedStaleEpoch = 0;
};
inline bool heldReason(const Candidate& c) noexcept {
    return (c.reasonBits & (CallbackHeld | SelfHeldAssumption)) != 0;
}
inline bool precedes(const Candidate& a, const Candidate& b) noexcept {
    if (heldReason(a) != heldReason(b)) return heldReason(a);
    if (a.lastActivityTick != b.lastActivityTick) return a.lastActivityTick > b.lastActivityTick;
    return a.virtualKey < b.virtualKey;
}
inline Selection select(const inputActivity::Snapshot& activity) noexcept {
    Selection result;
    result.inputRowCount = activity.count;
    result.inspectedRows = activity.count < activity.rows.size() ? activity.count : activity.rows.size();
    result.uninspectedRetainedRows = activity.count - result.inspectedRows;
    result.upstreamTotalRows = activity.totalRows;
    result.upstreamOmittedRows = activity.omittedRows;
    result.upstreamOmittedActiveRows = activity.omittedActiveRows;
    result.upstreamOmittedHeldRows = activity.omittedHeldRows;
    result.upstreamHeldStateMayHaveGaps = activity.heldStateMayHaveGaps;
    result.upstreamCountersSaturated = activity.countersSaturated;
    result.upstreamContentionCounterWrapped = activity.contentionCounterWrapped;
    result.upstreamDroppedContention = activity.droppedContention;
    result.upstreamDroppedOverflow = activity.droppedOverflow;
    result.upstreamRejectedInvalid = activity.rejectedInvalid;
    result.upstreamDiscardedOnReset = activity.discardedOnReset;
    result.upstreamDiscardedHeldOnReset = activity.discardedHeldOnReset;
    result.upstreamDiscardedStaleEpoch = activity.discardedStaleEpoch;
    std::array<Candidate, inputActivity::capacity> unique{};
    std::size_t uniqueCount = 0;
    for (std::size_t i = 0; i < result.inspectedRows; ++i) {
        const auto& row = activity.rows[i];
        std::uint16_t vk = 0;
        if (row.kind == inputActivity::Kind::Keyboard) {
            if (row.code == 0 || row.code > 255) { ++result.invalidVirtualKeyRows; continue; }
            vk = row.code;
        } else if (row.kind == inputActivity::Kind::MouseButton) {
            constexpr std::array<std::uint16_t, 5> mouseKeys{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2};
            if (row.code < 1 || row.code > mouseKeys.size() || row.extended) { ++result.invalidMouseButtonRows; continue; }
            vk = mouseKeys[row.code - 1];
        } else if (row.kind == inputActivity::Kind::KeyboardScanCode) {
            ++result.scanCodeRowsExcluded; continue;
        } else if (row.kind == inputActivity::Kind::MouseMove || row.kind == inputActivity::Kind::WheelVertical || row.kind == inputActivity::Kind::WheelHorizontal) {
            ++result.nonButtonMouseRowsExcluded; continue;
        } else { ++result.otherKindRowsExcluded; continue; }
        const bool self = inputActivity::isSelfChannel(row.channel);
        const bool callbackHeld = !self && row.observedHeld;
        const bool selfHeld = self && row.selfHeldAssumption;
        if (!callbackHeld && !selfHeld && row.eventCount == 0) { ++result.inactiveRows; continue; }
        ++result.eligibleRows;
        std::size_t slot = 0;
        for (; slot < uniqueCount; ++slot) if (unique[slot].virtualKey == vk) break;
        if (slot == uniqueCount) { unique[slot].virtualKey = vk; ++uniqueCount; }
        auto& candidate = unique[slot];
        ++candidate.contributingRows;
        if (row.extended) ++candidate.extendedRows; else ++candidate.genericRows;
        if (callbackHeld) { candidate.reasonBits |= CallbackHeld; ++candidate.callbackHeldRows; }
        // lastSourceId is the existing adapter's route/flags, not a sender PID.
        // Mixed/unknown routes cannot establish a stable injected-held row.
        if (callbackHeld && row.kind==inputActivity::Kind::Keyboard &&
            row.channel==inputActivity::Channel::LowLevelKeyboard && row.hasEdge &&
            !row.sourceMixed && !row.sourceUnknown && (row.lastSourceId&0x10u)!=0 &&
            vk>=0xc3u && vk<=0xdau) {
            ++candidate.injectedHookHeldRows;
            if(!candidate.injectedHookHeldEdgeKnown || row.lastEdgeTick<candidate.injectedHookHeldEdgeTick)
                candidate.injectedHookHeldEdgeTick=row.lastEdgeTick;
            candidate.injectedHookHeldEdgeKnown=true;
        }
        if (selfHeld) { candidate.reasonBits |= SelfHeldAssumption; ++candidate.selfHeldAssumptionRows; }
        if (row.eventCount != 0) candidate.reasonBits |= RecentActivity;
        candidate.reasonBits |= row.kind == inputActivity::Kind::Keyboard ? KeyboardRow : MouseButtonRow;
        if (row.lastEventTick > candidate.lastActivityTick) candidate.lastActivityTick = row.lastEventTick;
    }
    // At most 64 unique keys; bounded insertion sort preserves the fixed storage.
    for (std::size_t i = 1; i < uniqueCount; ++i) {
        const auto value = unique[i]; std::size_t j = i;
        for (; j > 0 && precedes(value, unique[j - 1]); --j) unique[j] = unique[j - 1];
        unique[j] = value;
    }
    result.uniqueEligibleKeys = uniqueCount;
    result.count = uniqueCount < candidateCapacity ? uniqueCount : candidateCapacity;
    for (std::size_t i = 0; i < result.count; ++i) result.candidates[i] = unique[i];
    for (std::size_t i = result.count; i < uniqueCount; ++i) {
        ++result.omittedKeys; result.omittedEligibleRows += unique[i].contributingRows;
        if (heldReason(unique[i])) ++result.omittedHeldKeys;
    }
    return result;
}
struct Foreground {
    DWORD pid = 0, error = 0;
    bool windowPresent = false, pidKnown = false;
};
struct Capture {
    Selection selection{};
    ULONGLONG beginTick = 0, endTick = 0;
    DWORD callingThreadId = 0, queueError = 0;
    Foreground foregroundBefore{}, foregroundAfter{};
    bool enabled = false, activityEnabled = false, selectionPerformed = false, sampled = false;
    bool queueReadOk = false, foregroundSameWindow = false;
};
static_assert(std::is_trivially_copyable_v<Selection> && std::is_trivially_copyable_v<Capture>);
struct WindowsOps {
    DWORD error() const noexcept { return GetLastError(); }
    void setError(DWORD error) const noexcept { SetLastError(error); }
    ULONGLONG now() const noexcept { return GetTickCount64(); }
    DWORD threadId() const noexcept { return GetCurrentThreadId(); }
    HWND foreground() const noexcept { return GetForegroundWindow(); }
    bool foregroundPid(HWND window, DWORD& pid) const noexcept { return GetWindowThreadProcessId(window, &pid) != 0; }
    SHORT asyncKey(int vk) const noexcept { return GetAsyncKeyState(vk); }
    bool keyboardState(BYTE* keys) const noexcept { return GetKeyboardState(keys) != FALSE; }
};
template<class Ops> inline Foreground readForeground(Ops& ops, HWND window) noexcept {
    Foreground result;
    result.windowPresent = window != nullptr;
    if (window) {
        ops.setError(ERROR_SUCCESS);
        result.pidKnown = ops.foregroundPid(window, result.pid);
        if (!result.pidKnown) result.error = ops.error();
    }
    return result;
}
template<class Ops> inline Capture capture(bool enabled, const inputActivity::Snapshot& activity, Ops& ops) noexcept {
    Capture result; result.enabled = enabled; result.activityEnabled = activity.enabled;
    if (!enabled || !activity.enabled) return result;
    result.selection = select(activity); result.selectionPerformed = true;
    if (result.selection.count == 0) return result;
    const DWORD savedError = ops.error();
    result.sampled = true;
    result.beginTick = ops.now(); result.callingThreadId = ops.threadId();
    const auto before = ops.foreground(); result.foregroundBefore = readForeground(ops, before);
    for (std::size_t i = 0; i < result.selection.count; ++i) {
        auto& candidate = result.selection.candidates[i];
        candidate.asyncDownBit = (static_cast<unsigned short>(ops.asyncKey(candidate.virtualKey)) & 0x8000u) != 0;
    }
    std::array<BYTE, 256> keyboard{};
    ops.setError(ERROR_SUCCESS);
    result.queueReadOk = ops.keyboardState(keyboard.data());
    if (!result.queueReadOk) result.queueError = ops.error();
    else for (std::size_t i = 0; i < result.selection.count; ++i) {
        auto& candidate = result.selection.candidates[i];
        candidate.callingThreadQueueDown = (keyboard[candidate.virtualKey] & 0x80u) != 0;
    }
    const auto after = ops.foreground(); result.foregroundAfter = readForeground(ops, after);
    result.foregroundSameWindow = before == after;
    result.endTick = ops.now(); ops.setError(savedError);
    return result;
}
inline nlohmann::json toJson(const Capture& capture) {
    using Json = nlohmann::json;
    const auto& s = capture.selection;
    auto keys = Json::array();
    constexpr std::uint64_t longHeldReferenceMs=10000;
    std::size_t longHeldInjectedGamepadKeys=0;
    for (std::size_t i = 0; i < s.count; ++i) {
        const auto& c = s.candidates[i];
        keys.push_back({{"virtualKey", c.virtualKey}, {"selectionReasonBits", c.reasonBits},
            {"contributingRows", c.contributingRows}, {"extendedRows", c.extendedRows}, {"genericRows", c.genericRows},
            {"callbackHeldRows", c.callbackHeldRows}, {"selfHeldAssumptionRows", c.selfHeldAssumptionRows},
            {"lastRetainedActivityTick", c.lastActivityTick},
            {"asyncDownBit", capture.sampled ? Json(c.asyncDownBit) : Json(nullptr)},
            {"callingThreadQueueDown", capture.sampled && capture.queueReadOk ? Json(c.callingThreadQueueDown) : Json(nullptr)}});
        if(c.injectedHookHeldRows){
            const bool ageKnown=capture.sampled&&c.injectedHookHeldEdgeKnown&&capture.beginTick>=c.injectedHookHeldEdgeTick;
            const auto age=ageKnown?capture.beginTick-c.injectedHookHeldEdgeTick:0;
            const bool candidate=ageKnown&&c.asyncDownBit&&age>=longHeldReferenceMs;
            if(candidate)++longHeldInjectedGamepadKeys;
            keys.back()["injectedGamepadHold"]={{"version",1},{"hookHeldRows",c.injectedHookHeldRows},
                {"lastHeldEdgeTick",c.injectedHookHeldEdgeKnown?Json(c.injectedHookHeldEdgeTick):Json(nullptr)},
                {"observedAgeMs",ageKnown?Json(age):Json(nullptr)},{"longHeldReferenceMs",longHeldReferenceMs},
                {"longHeldCandidate",candidate},{"senderKnown",false},{"stuckKeyProven",false},
                {"scope","callback-held plus sampled async-down;age from retained edge;not physical hold,continuous OS state,or authorization to release"}};
        }
    }
    auto foreground = [](const Foreground& f) -> Json {
        return {{"windowPresent", f.windowPresent}, {"pidKnown", f.pidKnown},
            {"pid", f.pidKnown ? Json(f.pid) : Json(nullptr)},
            {"pidReadError", f.windowPresent && !f.pidKnown ? Json(f.error) : Json(nullptr)}};
    };
    const bool foregroundKnown = capture.sampled && capture.foregroundBefore.pidKnown && capture.foregroundAfter.pidKnown;
    return {{"schemaVersion", 1}, {"capturePhase", "existing-activity-drain"},
        {"detailedGateEnabled", capture.enabled}, {"activityEnabled", capture.activityEnabled}, {"selectionPerformed", capture.selectionPerformed}, {"sampled", capture.sampled},
        {"captureBeginTick", capture.sampled ? Json(capture.beginTick) : Json(nullptr)},
        {"captureEndTick", capture.sampled ? Json(capture.endTick) : Json(nullptr)},
        {"callingThreadId", capture.sampled ? Json(capture.callingThreadId) : Json(nullptr)},
        {"foregroundBefore", capture.sampled ? foreground(capture.foregroundBefore) : Json(nullptr)},
        {"foregroundAfter", capture.sampled ? foreground(capture.foregroundAfter) : Json(nullptr)},
        {"foregroundStableDuringCapture", foregroundKnown ? Json(capture.foregroundSameWindow && capture.foregroundBefore.pid == capture.foregroundAfter.pid) : Json(nullptr)},
        {"callingThreadQueueReadOk", capture.sampled ? Json(capture.queueReadOk) : Json(nullptr)},
        {"callingThreadQueueError", capture.sampled && !capture.queueReadOk ? Json(capture.queueError) : Json(nullptr)},
        {"keys", std::move(keys)}, {"injectedGamepadHoldEvidenceVersion",1},
        {"longHeldInjectedGamepadKeys",longHeldInjectedGamepadKeys}, {"candidateCapacity", candidateCapacity}, {"retainedRowCapacity", inputActivity::capacity},
        {"selectedKeys", s.count}, {"uniqueEligibleKeys", s.uniqueEligibleKeys}, {"eligibleRows", s.eligibleRows},
        {"inputRowCount", s.inputRowCount}, {"inspectedRows", s.inspectedRows}, {"uninspectedRetainedRows", s.uninspectedRetainedRows},
        {"omittedKeysByCandidateLimit", s.omittedKeys}, {"omittedHeldKeysByCandidateLimit", s.omittedHeldKeys}, {"omittedEligibleRowsByCandidateLimit", s.omittedEligibleRows},
        {"scanCodeRowsExcluded", s.scanCodeRowsExcluded}, {"nonButtonMouseRowsExcluded", s.nonButtonMouseRowsExcluded},
        {"invalidVirtualKeyRows", s.invalidVirtualKeyRows}, {"invalidMouseButtonRows", s.invalidMouseButtonRows},
        {"inactiveRowsExcluded", s.inactiveRows}, {"otherKindRowsExcluded", s.otherKindRowsExcluded},
        {"upstreamTotalRows", s.upstreamTotalRows}, {"upstreamOmittedRows", s.upstreamOmittedRows},
        {"upstreamOmittedActiveRows", s.upstreamOmittedActiveRows}, {"upstreamOmittedHeldRows", s.upstreamOmittedHeldRows},
        {"upstreamHeldStateMayHaveGaps", s.upstreamHeldStateMayHaveGaps}, {"upstreamCountersSaturated", s.upstreamCountersSaturated},
        {"upstreamContentionCounterWrapped", s.upstreamContentionCounterWrapped},
        {"upstreamDroppedContention", s.upstreamDroppedContention}, {"upstreamDroppedOverflow", s.upstreamDroppedOverflow},
        {"upstreamRejectedInvalid", s.upstreamRejectedInvalid}, {"upstreamDiscardedOnReset", s.upstreamDiscardedOnReset},
        {"upstreamDiscardedHeldOnReset", s.upstreamDiscardedHeldOnReset}, {"upstreamDiscardedStaleEpoch", s.upstreamDiscardedStaleEpoch},
        {"upstreamOmissionCountsMayOverlap", true},
        {"selectionReasonBits", {{"callbackHeld", CallbackHeld}, {"selfHeldAssumption", SelfHeldAssumption}, {"recentActivity", RecentActivity}, {"keyboardRow", KeyboardRow}, {"mouseButtonRow", MouseButtonRow}}},
        {"selectionPolicy", "held callback/self-assumption first; descending last retained tick; ascending literal VK for ties"},
        {"selectionScope", "retained Keyboard/MouseButton rows only; no scan-code conversion; no generic/side-key remapping"},
        {"keyboardVirtualKeyFilter", "literal supplied VK 1..255 only; 0/out-of-range excluded"},
        {"mouseButtonPolicy", "1=VK_LBUTTON;2=VK_RBUTTON;3=VK_MBUTTON;4=VK_XBUTTON1;5=VK_XBUTTON2;no swap inference"},
        {"mouseAsyncInterpretationVerified", false}, {"mouseLogicalButtonMappingVerified", false}, {"mouseButtonSwapObserved", false},
        {"mouseAsyncScope", "Unverified mouse interpretation: GetAsyncKeyState button state may differ from logical button mapping; button-swap configuration is unobserved. Standard VK selection is not a physical/logical button mapping proof."},
        {"asyncApi", "GetAsyncKeyState-high-bit-only"}, {"queueApi", "GetKeyboardState-calling-thread-message-queue"},
        {"asyncZeroIsAbsenceProof", false}, {"callingThreadQueueIsGameState", false}, {"crossKeyAtomicSnapshot", false},
        {"eventTimeStateProven", false}, {"eventEdgesCaptured", false}, {"sourceIdentityKnown", false},
        {"physicalOrInjectedOriginKnown", false}, {"gameKeyboardReceiptProven", false}, {"keyReleaseProven", false},
        {"automaticReleaseProven", false}, {"completeKeyCoverageProven", false},
        {"scope", "Bounded drain-time observations only. Selection reasons describe retained callbacks/self assumptions, not OS held state. Zero is not absence proof; async reads can be desktop-restricted, and queue state belongs to the calling thread. Sequential samples miss edges/omitted rows and prove no source identity, game receipt, cross-key atomicity or release."}};
}
} // namespace ymcc::inputKeyStateEvidence
