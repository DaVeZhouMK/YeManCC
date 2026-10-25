#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

// Memory-only, allocation-free activity aggregation. No platform input APIs,
// clocks, text, coordinates, event sequence, I/O, or producer/device registry.
// The caller supplies ticks and identity tokens (0 means unknown). Recording is
// O(1): a disabled check, ONE atomic_flag try-lock, and a direct array lookup.
// Controls and reads also try once; false means no mutation and output unchanged.
namespace ymcc::inputActivity {
inline constexpr std::size_t capacity = 64;
inline constexpr std::size_t keyboardSlots = 256 * 2;
inline constexpr std::size_t mouseSlots = 5 + 2 + 1;
inline constexpr std::size_t scanCodeSlots = 256 * 2;
inline constexpr std::size_t ledgerCapacity = keyboardSlots * 4 + mouseSlots * 3 + scanCodeSlots * 2;

enum class Channel : std::uint8_t {
    LowLevelKeyboard, RawKeyboard, MouseHook,
    SelfSendInputRequest, SelfSendInputReturnedPrefix
};
enum class Kind : std::uint8_t {
    Keyboard, MouseButton, WheelVertical, WheelHorizontal, MouseMove, KeyboardScanCode
};
enum class RecordResult : std::uint8_t {
    Recorded, Disabled, Invalid, DroppedContention, DroppedReset, CountOverflow
};
inline constexpr bool isSelfChannel(Channel channel) noexcept {
    return channel == Channel::SelfSendInputRequest ||
        channel == Channel::SelfSendInputReturnedPrefix;
}

// Value-initialize with Event{}. A keyboard code is vk 0..255, with extended
// forming a separate namespace. KeyboardScanCode uses the original scan 0..255
// plus extended, ONLY in self channels; no VK conversion. Unicode input must be
// skipped/accounted by the caller, never passed as VK/scan. Mouse buttons use
// 1..5. Other mouse kinds use
// code=0, extended=false, down=false. Only wheels permit a nonzero delta.
// LowLevelKeyboard/RawKeyboard accept Keyboard only; MouseHook accepts mouse
// kinds only; the two self channels also accept KeyboardScanCode. No text inference.
// ReturnedPrefix is the caller's count-based PREFIX ASSUMPTION about its own
// batch. Neither self channel, nor any held field, proves OS/consumer key state.
struct Event {
    Channel channel{};
    Kind kind{};
    std::uint16_t code{};
    bool extended{}, down{};
    std::int32_t delta{};
    std::uint64_t tick{}, sourceId{}, deviceId{};
};

struct Row {
    Channel channel{};
    Kind kind{};
    std::uint16_t code{};
    bool extended{};
    std::uint64_t eventCount{}, downCount{}, upCount{}, repeatDownCount{}, orphanUpCount{};
    std::int64_t deltaSum{};
    std::uint64_t positiveDelta{}, negativeDelta{}; // Magnitudes, not event counts.
    std::uint64_t lastEventTick{}, lastEdgeTick{};
    std::uint64_t lastSourceId{}, lastDeviceId{};
    bool observedHeld{}, selfHeldAssumption{}, hasEdge{};
    bool sourceMixed{}, deviceMixed{}, sourceUnknown{}, deviceUnknown{};
};

struct Snapshot {
    std::array<Row, capacity> rows{};
    std::size_t count{}, totalRows{}, omittedRows{};
    // These omission counts overlap when an omitted row is both active and held.
    std::size_t omittedActiveRows{}, omittedHeldRows{};
    std::uint64_t recordedEvents{}, droppedContention{}, droppedOverflow{}, rejectedInvalid{};
    std::uint64_t discardedOnReset{}, discardedHeldOnReset{}, discardedStaleEpoch{};
    std::uint64_t configEpoch{}, windowId{}, resetCount{};
    bool enabled{}, drained{}, countersSaturated{}, contentionCounterWrapped{};
    // Sticky across take(); reset clears it. Even false is NOT coverage proof.
    bool heldStateMayHaveGaps{};
};
static_assert(std::is_trivially_copyable_v<Event> && std::is_trivially_copyable_v<Snapshot>);
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
static_assert(std::atomic<bool>::is_always_lock_free);
namespace detail { struct RecorderTestAccess; }

class Recorder {
    friend struct detail::RecorderTestAccess; // Defined only by the memory selftest.
    struct Guard {
        std::atomic_flag& flag;
        ~Guard() noexcept { flag.clear(std::memory_order_release); }
    };
    std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    // Low bit is enabled; upper bits are the configuration epoch. One atomic
    // capture prevents mixing a pre-reset writer with a post-reset configuration.
    std::atomic<std::uint64_t> state_{0};
    std::atomic<std::uint64_t> contention_{0};
    std::atomic<bool> contentionWrapped_{false}, stateMayHaveGaps_{false};
    std::array<Row, ledgerCapacity> ledger_{};
    std::uint64_t recordedEvents_ = 0, droppedOverflow_ = 0, rejectedInvalid_ = 0;
    std::uint64_t discardedOnReset_ = 0, discardedHeldOnReset_ = 0, discardedStaleEpoch_ = 0;
    std::uint64_t windowId_ = 0, resetCount_ = 0;
    bool countersSaturated_ = false;

    bool tryLock() noexcept { return !lock_.test_and_set(std::memory_order_acquire); }
    static bool held(const Row& row) noexcept {
        return row.observedHeld || row.selfHeldAssumption;
    }
    static bool active(const Row& row) noexcept { return row.eventCount != 0 || held(row); }
    static bool add(std::uint64_t& value, std::uint64_t amount = 1) noexcept {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        if (amount > maximum - value) { value = maximum; return false; }
        value += amount;
        return true;
    }
    void account(std::uint64_t& value, std::uint64_t amount = 1) noexcept {
        if (!add(value, amount)) countersSaturated_ = true;
    }
    static bool addDelta(std::int64_t& value, std::int32_t delta) noexcept {
        constexpr auto maximum = (std::numeric_limits<std::int64_t>::max)();
        constexpr auto minimum = (std::numeric_limits<std::int64_t>::min)();
        if (delta > 0 && value > maximum - delta) { value = maximum; return false; }
        if (delta < 0 && value < minimum - delta) { value = minimum; return false; }
        value += delta;
        return true;
    }
    static std::size_t index(const Event& event) noexcept {
        if (event.kind == Kind::KeyboardScanCode) {
            if (event.code > 255 || event.delta != 0) return ledgerCapacity;
            const auto scanOffset = keyboardSlots * 4 + mouseSlots * 3;
            const auto scan = static_cast<std::size_t>(event.code) * 2 + (event.extended ? 1 : 0);
            if (event.channel == Channel::SelfSendInputRequest) return scanOffset + scan;
            if (event.channel == Channel::SelfSendInputReturnedPrefix) return scanOffset + scanCodeSlots + scan;
            return ledgerCapacity;
        }
        std::size_t offset = 0;
        if (event.kind == Kind::Keyboard) {
            if (event.code > 255 || event.delta != 0) return ledgerCapacity;
            offset = static_cast<std::size_t>(event.code) * 2 + (event.extended ? 1 : 0);
        } else {
            if (event.extended) return ledgerCapacity;
            switch (event.kind) {
            case Kind::MouseButton:
                if (event.code < 1 || event.code > 5 || event.delta != 0) return ledgerCapacity;
                offset = event.code - 1;
                break;
            case Kind::WheelVertical:
            case Kind::WheelHorizontal:
                if (event.code != 0 || event.down) return ledgerCapacity;
                offset = event.kind == Kind::WheelVertical ? 5 : 6;
                break;
            case Kind::MouseMove:
                if (event.code != 0 || event.down || event.delta != 0) return ledgerCapacity;
                offset = 7;
                break;
            default: return ledgerCapacity;
            }
        }
        const bool keyboard = event.kind == Kind::Keyboard;
        switch (event.channel) {
        case Channel::LowLevelKeyboard: return keyboard ? offset : ledgerCapacity;
        case Channel::RawKeyboard: return keyboard ? keyboardSlots + offset : ledgerCapacity;
        case Channel::MouseHook: return keyboard ? ledgerCapacity : keyboardSlots * 2 + offset;
        case Channel::SelfSendInputRequest:
            return keyboardSlots * 2 + mouseSlots + (keyboard ? 0 : keyboardSlots) + offset;
        case Channel::SelfSendInputReturnedPrefix:
            return keyboardSlots * 3 + mouseSlots * 2 + (keyboard ? 0 : keyboardSlots) + offset;
        default: return ledgerCapacity;
        }
    }
    // Separated solely so the selftest can deterministically resume a writer
    // that captured an enabled epoch before reset/disable/re-enable.
    RecordResult recordAtState(const Event& event, std::uint64_t captured) noexcept {
        if (!tryLock()) {
            if (contention_.fetch_add(1, std::memory_order_relaxed) ==
                (std::numeric_limits<std::uint64_t>::max)())
                contentionWrapped_.store(true, std::memory_order_relaxed);
            stateMayHaveGaps_.store(true, std::memory_order_relaxed);
            return RecordResult::DroppedContention;
        }
        const Guard guard{lock_};
        if (state_.load(std::memory_order_relaxed) != captured) {
            account(discardedOnReset_);
            account(discardedStaleEpoch_);
            stateMayHaveGaps_.store(true, std::memory_order_relaxed);
            return RecordResult::DroppedReset;
        }
        const auto slot = index(event);
        if (slot == ledgerCapacity) {
            account(rejectedInvalid_);
            return RecordResult::Invalid;
        }
        Row& row = ledger_[slot];
        if (!active(row)) {
            row.channel = event.channel; row.kind = event.kind;
            row.code = event.code; row.extended = event.extended;
        } else {
            row.sourceMixed = row.sourceMixed || row.lastSourceId != event.sourceId;
            row.deviceMixed = row.deviceMixed || row.lastDeviceId != event.deviceId;
        }
        row.lastSourceId = event.sourceId; row.lastDeviceId = event.deviceId;
        row.sourceUnknown = row.sourceUnknown || event.sourceId == 0;
        row.deviceUnknown = row.deviceUnknown || event.deviceId == 0;
        row.lastEventTick = event.tick; // Last recorded event, not max(tick).
        bool fits = add(row.eventCount);
        fits = add(recordedEvents_) && fits;
        if (event.kind == Kind::Keyboard || event.kind == Kind::KeyboardScanCode || event.kind == Kind::MouseButton) {
            const bool wasHeld = held(row);
            if (event.down) {
                fits = add(row.downCount) && fits;
                if (wasHeld) fits = add(row.repeatDownCount) && fits;
            } else {
                fits = add(row.upCount) && fits;
                if (!wasHeld) fits = add(row.orphanUpCount) && fits;
            }
            const bool self = isSelfChannel(event.channel);
            row.observedHeld = !self && event.down;
            row.selfHeldAssumption = self && event.down;
            row.hasEdge = true;
            // Includes repeated downs and orphan ups: a reported edge, not a
            // verified physical transition. Tick zero is valid (see hasEdge).
            row.lastEdgeTick = event.tick;
        } else if (event.kind == Kind::WheelVertical || event.kind == Kind::WheelHorizontal) {
            fits = addDelta(row.deltaSum, event.delta) && fits;
            if (event.delta > 0)
                fits = add(row.positiveDelta, static_cast<std::uint64_t>(event.delta)) && fits;
            else if (event.delta < 0)
                fits = add(row.negativeDelta, static_cast<std::uint64_t>(-static_cast<std::int64_t>(event.delta))) && fits;
        }
        if (!fits) {
            account(droppedOverflow_);
            countersSaturated_ = true;
            stateMayHaveGaps_.store(true, std::memory_order_relaxed);
            return RecordResult::CountOverflow;
        }
        return RecordResult::Recorded;
    }
    void resetLocked(std::uint64_t previous, bool enabled) noexcept {
        // Preserve loss accounting until take(), including accepted events that
        // a reset discards and carried held rows with zero current-window events.
        account(discardedOnReset_, recordedEvents_);
        for (auto& row : ledger_) {
            if (held(row)) account(discardedHeldOnReset_);
            row = {};
        }
        recordedEvents_ = 0;
        account(resetCount_);
        stateMayHaveGaps_.store(false, std::memory_order_relaxed);
        state_.store(((previous & ~std::uint64_t{1}) + 2) | (enabled ? 1 : 0),
            std::memory_order_release);
    }
    static void drainRow(Row& row) noexcept {
        if (!held(row)) { row = {}; return; }
        row.eventCount = row.downCount = row.upCount = row.repeatDownCount = row.orphanUpCount = 0;
        row.deltaSum = 0; row.positiveDelta = row.negativeDelta = 0;
        // Keep held, last ticks, and conservative identity history until release
        // has been drained (or reset). No event order or per-device attribution.
    }
    bool read(Snapshot& out, bool drain) noexcept {
        if (!tryLock()) return false;
        const Guard guard{lock_};
        out = {};
        const auto state = state_.load(std::memory_order_relaxed);
        out.enabled = (state & 1) != 0;
        out.configEpoch = state >> 1; out.windowId = windowId_;
        out.resetCount = resetCount_; out.drained = drain;
        for (auto& row : ledger_) {
            if (!active(row)) continue;
            ++out.totalRows;
            if (out.count < capacity) out.rows[out.count++] = row;
            else {
                ++out.omittedRows;
                if (row.eventCount != 0) ++out.omittedActiveRows;
                if (held(row)) ++out.omittedHeldRows;
            }
            // take drains ALL active entries, including omitted rows. Held state
            // remains; omitted released activity cannot be retrieved afterward.
            if (drain) drainRow(row);
        }
        out.recordedEvents = recordedEvents_;
        out.droppedContention = drain ? contention_.exchange(0, std::memory_order_relaxed) :
            contention_.load(std::memory_order_relaxed);
        out.droppedOverflow = droppedOverflow_; out.rejectedInvalid = rejectedInvalid_;
        out.discardedOnReset = discardedOnReset_; out.discardedHeldOnReset = discardedHeldOnReset_;
        out.discardedStaleEpoch = discardedStaleEpoch_;
        out.countersSaturated = countersSaturated_;
        out.contentionCounterWrapped = contentionWrapped_.load(std::memory_order_relaxed);
        out.heldStateMayHaveGaps = stateMayHaveGaps_.load(std::memory_order_relaxed);
        if (drain) {
            recordedEvents_ = droppedOverflow_ = rejectedInvalid_ = 0;
            discardedOnReset_ = discardedHeldOnReset_ = discardedStaleEpoch_ = 0;
            countersSaturated_ = false;
            account(windowId_);
        }
        return true;
    }
public:
    Recorder() noexcept = default;
    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    bool enabled() const noexcept { return (state_.load(std::memory_order_acquire) & 1) != 0; }
    // Idempotent enable preserves the window. Transitions/reset discard rows and
    // report the discard counts on the next read. A failed control must be retried
    // by the control owner; do not spin/retry in an input hook.
    [[nodiscard]] bool enable(bool value) noexcept {
        if (!tryLock()) return false;
        const Guard guard{lock_};
        const auto previous = state_.load(std::memory_order_relaxed);
        if (((previous & 1) != 0) != value) resetLocked(previous, value);
        return true;
    }
    [[nodiscard]] bool reset() noexcept {
        if (!tryLock()) return false;
        const Guard guard{lock_};
        const auto previous = state_.load(std::memory_order_relaxed);
        resetLocked(previous, (previous & 1) != 0);
        return true;
    }
    RecordResult record(const Event& event) noexcept {
        const auto captured = state_.load(std::memory_order_acquire);
        if ((captured & 1) == 0) return RecordResult::Disabled;
        return recordAtState(event, captured);
    }
    RecordResult recordKeyboard(Channel channel, std::uint16_t vk, bool extended, bool down,
        std::uint64_t tick, std::uint64_t sourceId = 0, std::uint64_t deviceId = 0) noexcept {
        Event event{};
        event.channel = channel; event.kind = Kind::Keyboard;
        event.code = vk; event.extended = extended; event.down = down;
        event.tick = tick; event.sourceId = sourceId; event.deviceId = deviceId;
        return record(event);
    }
    RecordResult recordMouseButton(Channel channel, std::uint16_t button, bool down,
        std::uint64_t tick, std::uint64_t sourceId = 0, std::uint64_t deviceId = 0) noexcept {
        Event event{};
        event.channel = channel; event.kind = Kind::MouseButton;
        event.code = button; event.down = down;
        event.tick = tick; event.sourceId = sourceId; event.deviceId = deviceId;
        return record(event);
    }
    RecordResult recordWheel(Channel channel, bool horizontal, std::int32_t delta,
        std::uint64_t tick, std::uint64_t sourceId = 0, std::uint64_t deviceId = 0) noexcept {
        Event event{};
        event.channel = channel; event.kind = horizontal ? Kind::WheelHorizontal : Kind::WheelVertical;
        event.delta = delta; event.tick = tick; event.sourceId = sourceId; event.deviceId = deviceId;
        return record(event);
    }
    RecordResult recordMove(Channel channel, std::uint64_t tick,
        std::uint64_t sourceId = 0, std::uint64_t deviceId = 0) noexcept {
        Event event{};
        event.channel = channel; event.kind = Kind::MouseMove;
        event.tick = tick; event.sourceId = sourceId; event.deviceId = deviceId;
        return record(event);
    }
    // Deterministic slot order (base channels, then self scan namespaces), never recency order. Output is capped
    // at 64, work at ledgerCapacity entries. snapshot is nondestructive; take
    // advances a window and clears counters, carrying held state and identity.
    // The contention exchange is the loss-counter window boundary; concurrent
    // failed writers may be counted in this window or the following one.
    // Counters saturate except contention (atomic fetch_add, modulo 2^64 with a
    // lifetime wrap flag); epoch wraps after 2^63 transitions. No counters assert
    // hook coverage, hardware identity, effective OS state, or delivery.
    [[nodiscard]] bool snapshot(Snapshot& out) noexcept { return read(out, false); }
    [[nodiscard]] bool take(Snapshot& out) noexcept { return read(out, true); }
};
} // namespace ymcc::inputActivity
