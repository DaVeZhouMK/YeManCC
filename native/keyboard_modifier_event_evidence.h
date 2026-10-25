#pragma once
#include <windows.h>
#include "msi_shoulder_modifier_guard.h"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <type_traits>

// Memory-only evidence. No input APIs, hooks, device queries, clocks, JSON or I/O.
// enable() is a control-owner operation with fixed work under the mutex.
// record() and take() each try the mutex once and never wait for another owner.
namespace ymcc::keyboardEvents {
inline constexpr std::size_t capacity = 64;
enum class Kind : std::uint8_t { LowLevel, RawInput, SelfSendInput };
// This callback's decision only. Passing to the next hook is not delivery proof.
enum class HookDecision : std::uint8_t { Unobserved, PassToNextHook, Suppressed };
enum class HookPath : std::uint8_t { Unobserved, PhysicalLedger, InjectedBypass, InvalidKeyBypass, MsiShoulderModifierGuard };
struct HookObservation {
    HookDecision decision;HookPath path;
    bool ledgerObserved;std::uint8_t visibilityBefore,visibilityAfter;
};
enum class SourceId : std::uint8_t {
    Other, NativeKeyboardShortcut, OemKeyboardReplay, KeyboardGamebar,
    FocusAltFallback, ReturnToDesktop, TaskView, ScreenTouchpad, OemMouseReplay
};
inline constexpr const char* sourceName(SourceId source) noexcept {
    switch(source) {
    case SourceId::NativeKeyboardShortcut: return "nativeKeyboardShortcutSend";
    case SourceId::OemKeyboardReplay: return "oemKeyboardConsumeReplay";
    case SourceId::KeyboardGamebar: return "nativeFrontendButtonSendGamebar";
    case SourceId::FocusAltFallback: return "refocusWebView-alt-fallback";
    case SourceId::ReturnToDesktop: return "returnToDesktop";
    case SourceId::TaskView: return "nativeSendTaskView";
    case SourceId::ScreenTouchpad: return "screen-touchpad-output";
    case SourceId::OemMouseReplay: return "oemMouseConsumeReplay";
    default: return "other-self-send";
    }
}
// Bits 0..7 follow the existing left/right modifier snapshot ordering.
inline constexpr std::array<DWORD, 11> modifierKeys{
    VK_LCONTROL, VK_RCONTROL, VK_LSHIFT, VK_RSHIFT, VK_LMENU, VK_RMENU,
    VK_LWIN, VK_RWIN, VK_CONTROL, VK_SHIFT, VK_MENU
};
inline constexpr std::uint16_t allModifierMask = (1u << modifierKeys.size()) - 1u;
inline constexpr int modifierIndex(int vk) noexcept {
    for (std::size_t i = 0; i < modifierKeys.size(); ++i)
        if (static_cast<int>(modifierKeys[i]) == vk) return static_cast<int>(i);
    return -1;
}
inline constexpr std::uint16_t bit(int vk) noexcept {
    const int index = modifierIndex(vk);
    return index < 0 ? 0 : static_cast<std::uint16_t>(1u << index);
}

// Initialize with Event{}: no constructor, pointers to payloads or ownership.
struct Event {
    Kind kind;
    ULONGLONG tick;
    DWORD threadId;
    DWORD foregroundPid;
    std::uintptr_t foregroundWindow;
    std::uint64_t powerGeneration, deviceGeneration;
    DWORD vk, scan, flags, message;
    std::uintptr_t extraInfo, device;
    DWORD requestedInputs, insertedInputs, win32ErrorHint, nonVirtualKeyInputs;
    // Caller-provided masks are observations/assumptions, never consumer receipts.
    std::uint16_t attemptedModifierDownMask, attemptedModifierUpMask;
    std::uint16_t insertedModifierDownMask, insertedModifierUpMask;
    SourceId sourceId;
    DWORD lowLevelEventTime;
    HookObservation hook;
    ymcc::msi::shoulderModifiers::Observation msiShoulder;
};
struct Snapshot {
    std::array<Event, ymcc::keyboardEvents::capacity> events;
    std::size_t count, capacity;
    bool enabled, drained, complete;
    std::uint64_t droppedOverflow, droppedContention, ignoredNonModifiers;
    std::uint64_t discardedOnConfigChange, configEpoch;
    ULONGLONG enabledSinceTick, windowBeginTick, windowEndTick;
};
static_assert(std::is_trivial_v<Event> && std::is_standard_layout_v<Event>);
static_assert(std::is_trivial_v<Snapshot> && std::is_standard_layout_v<Snapshot>);
static_assert(std::atomic<bool>::is_always_lock_free);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
namespace detail { struct RecorderTestAccess; }

class Recorder {
    friend struct detail::RecorderTestAccess; // Only the standalone test defines it.
    std::mutex mutex_;
    std::array<Event, capacity> events_{};
    std::size_t count_ = 0;
    std::atomic<bool> enabled_{false};
    std::atomic<std::uint64_t> configEpoch_{0}, enabledSinceTick_{0};
    std::atomic<std::uint64_t> droppedOverflow_{0}, droppedContention_{0}, ignoredNonModifiers_{0};
    std::uint64_t discardedOnConfigChange_ = 0;
    ULONGLONG windowBeginTick_ = 0;
    bool windowInitialized_ = false;
    // Shared by the real record entry and the test's deterministic resumption of
    // a writer that captured its epoch before a configuration transition.
    void appendAtEpoch(const Event& copy, std::uint64_t capturedEpoch) noexcept {
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        if (!lock.owns_lock()) {
            droppedContention_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (!enabled_.load(std::memory_order_acquire) ||
            configEpoch_.load(std::memory_order_relaxed) != capturedEpoch) {
            ++discardedOnConfigChange_;
            return;
        }
        if (count_ == capacity) {
            droppedOverflow_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        events_[count_++] = copy;
    }
public:
    Recorder() = default;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Idempotent calls do not reset the window. Transitions discard pending rows
    // with an explicit count, so rows from two configurations are never mixed.
    // Drain before disabling if the caller needs those final rows.
    void enable(bool value, ULONGLONG tick) noexcept {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_.load(std::memory_order_relaxed) == value) return;
        enabled_.store(false, std::memory_order_release);
        discardedOnConfigChange_ += count_;
        events_ = {};
        count_ = 0;
        configEpoch_.fetch_add(1, std::memory_order_relaxed);
        if (value) enabledSinceTick_.store(tick, std::memory_order_relaxed);
        if (!windowInitialized_) { windowBeginTick_ = tick; windowInitialized_ = true; }
        enabled_.store(value, std::memory_order_release);
    }

    void record(const Event& event) noexcept {
        const auto capturedEpoch = configEpoch_.load(std::memory_order_acquire);
        if (!enabled_.load(std::memory_order_acquire)) return;
        Event copy = event;
        const bool modifierVk = bit(static_cast<int>(copy.vk)) != 0;
        if (copy.kind == Kind::SelfSendInput) {
            copy.attemptedModifierDownMask &= allModifierMask;
            copy.attemptedModifierUpMask &= allModifierMask;
            copy.insertedModifierDownMask &= allModifierMask;
            copy.insertedModifierUpMask &= allModifierMask;
            if (!modifierVk && !(copy.attemptedModifierDownMask | copy.attemptedModifierUpMask |
                copy.insertedModifierDownMask | copy.insertedModifierUpMask)) {
                ignoredNonModifiers_.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            // A requiresShift batch may name an ordinary key. Keep the modifier
            // masks/result, never retain that key's virtual key or scan code.
            if (!modifierVk) { copy.vk = 0; copy.scan = 0; }
        } else if (!modifierVk) {
            ignoredNonModifiers_.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        appendAtEpoch(copy, capturedEpoch);
    }

    // complete describes only this enabled queue window with no known loss, not callback coverage. Even
    // complete=true/count=0 never proves absence of input or complete hook cover.
    // Failed takes do not consume rows, counters or advance the window.
    Snapshot take(ULONGLONG tick) noexcept {
        Snapshot out{};
        out.capacity = capacity;
        out.windowEndTick = tick;
        std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
        out.enabled = enabled_.load(std::memory_order_acquire);
        out.configEpoch = configEpoch_.load(std::memory_order_relaxed);
        out.enabledSinceTick = enabledSinceTick_.load(std::memory_order_relaxed);
        if (!lock.owns_lock()) return out;
        out.drained = true;
        out.count = count_;
        out.events = events_;
        out.windowBeginTick = windowInitialized_ ? windowBeginTick_ : tick;
        out.droppedOverflow = droppedOverflow_.exchange(0, std::memory_order_relaxed);
        out.droppedContention = droppedContention_.exchange(0, std::memory_order_relaxed);
        out.ignoredNonModifiers = ignoredNonModifiers_.exchange(0, std::memory_order_relaxed);
        out.discardedOnConfigChange = discardedOnConfigChange_;
        out.complete = out.enabled && !out.droppedOverflow && !out.droppedContention &&
            !out.discardedOnConfigChange && tick >= out.windowBeginTick;
        events_ = {};
        count_ = 0;
        discardedOnConfigChange_ = 0;
        windowBeginTick_ = tick;
        windowInitialized_ = true;
        return out;
    }
};
} // namespace ymcc::keyboardEvents
