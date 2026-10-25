#include "../native/input_signal_activity_evidence.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <utility>

namespace activity = ymcc::inputActivity;
using activity::Channel;
using activity::Kind;
using activity::RecordResult;
static std::atomic<bool> watchAllocations{false};
static std::atomic<unsigned> allocationCount{0};
void* operator new(std::size_t size) {
    if (watchAllocations.load(std::memory_order_relaxed)) allocationCount.fetch_add(1);
    if (void* memory = std::malloc(size ? size : 1)) return memory;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace ymcc::inputActivity::detail {
struct RecorderTestAccess {
    static bool lock(Recorder& recorder) noexcept { return recorder.tryLock(); }
    static void unlock(Recorder& recorder) noexcept { recorder.lock_.clear(std::memory_order_release); }
    static std::uint64_t state(Recorder& recorder) noexcept { return recorder.state_.load(); }
    static RecordResult resume(Recorder& recorder, const Event& event, std::uint64_t state) noexcept {
        return recorder.recordAtState(event, state);
    }
    static void saturate(Recorder& recorder, const Event& event) noexcept {
        auto& row = recorder.ledger_[recorder.index(event)];
        row.eventCount = (std::numeric_limits<std::uint64_t>::max)();
        row.downCount = row.eventCount;
    }
    static void deltaLimit(Recorder& recorder, const Event& event, std::int64_t value) noexcept {
        recorder.ledger_[recorder.index(event)].deltaSum = value;
    }
    static void contentionLimit(Recorder& recorder) noexcept {
        recorder.contention_.store((std::numeric_limits<std::uint64_t>::max)());
    }
};
}
using Access = activity::detail::RecorderTestAccess;
static unsigned checks = 0, failures = 0;
static void check(bool condition, const char* description) {
    ++checks;
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", description); }
}
static activity::Event key(std::uint16_t code, bool down, std::uint64_t tick = 1,
    Channel channel = Channel::LowLevelKeyboard, bool extended = false,
    std::uint64_t source = 7, std::uint64_t device = 11) {
    activity::Event event{};
    event.channel = channel; event.kind = Kind::Keyboard;
    event.code = code; event.extended = extended; event.down = down;
    event.tick = tick; event.sourceId = source; event.deviceId = device;
    return event;
}
static activity::Event mouse(Kind kind, std::uint16_t code = 0, bool down = false,
    std::int32_t delta = 0, Channel channel = Channel::MouseHook) {
    activity::Event event{};
    event.channel = channel; event.kind = kind; event.code = code;
    event.down = down; event.delta = delta; event.tick = 42;
    event.sourceId = 12; event.deviceId = 0;
    return event;
}
static const activity::Row* find(const activity::Snapshot& snapshot, const activity::Event& event) {
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const auto& row = snapshot.rows[i];
        if (row.channel == event.channel && row.kind == event.kind && row.code == event.code &&
            row.extended == event.extended) return &row;
    }
    return nullptr;
}
static const activity::Row& rowFor(const activity::Snapshot& snapshot, const activity::Event& event) {
    const auto* row = find(snapshot, event);
    check(row != nullptr, "expected snapshot row exists");
    static const activity::Row missing{};
    return row ? *row : missing;
}
static void record(activity::Recorder& recorder, const activity::Event& event) {
    check(recorder.record(event) == RecordResult::Recorded, "stub event accepted");
}

static void shortPressAndDisabled() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(!recorder.enabled(), "default disabled");
    check(recorder.record(key(65, true)) == RecordResult::Disabled, "disabled record ignored");
    check(recorder.take(snapshot) && !snapshot.enabled && snapshot.count == 0 &&
        snapshot.recordedEvents == 0 && snapshot.configEpoch == 0, "disabled empty take");
    check(recorder.enable(true), "enable succeeds");
    record(recorder, key(65, true, 100));
    record(recorder, key(65, false, 101));
    check(recorder.enable(true), "enable idempotent");
    check(recorder.snapshot(snapshot) && !snapshot.drained, "nondestructive snapshot");
    const auto& row = rowFor(snapshot, key(65, true));
    check(row.eventCount == 2 && row.downCount == 1 && row.upCount == 1 &&
        row.repeatDownCount == 0 && row.orphanUpCount == 0 && !row.observedHeld &&
        !row.selfHeldAssumption && row.lastEdgeTick == 101 && row.lastEventTick == 101,
        "released ordinary short press remains in window");
    check(snapshot.recordedEvents == 2 && snapshot.configEpoch == 1 && snapshot.resetCount == 1,
        "idempotent enable preserves counts and epoch");
    check(recorder.snapshot(snapshot) && snapshot.recordedEvents == 2, "snapshot does not consume");
    check(recorder.take(snapshot) && snapshot.drained && snapshot.count == 1, "take returns short press");
    check(recorder.take(snapshot) && snapshot.count == 0 && snapshot.recordedEvents == 0,
        "short press absent only after drain");
}

static void heldRepeatOrphanRelease() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable held test");
    record(recorder, key(0, true, 0));
    check(recorder.take(snapshot), "take initial held key");
    auto row = rowFor(snapshot, key(0, true));
    check(row.observedHeld && row.hasEdge && row.lastEdgeTick == 0 && row.eventCount == 1,
        "vk zero and tick zero are valid");
    check(recorder.take(snapshot), "empty activity window retains held");
    row = rowFor(snapshot, key(0, true));
    check(row.observedHeld && row.eventCount == 0 && row.downCount == 0 && row.lastEdgeTick == 0,
        "held survives window with original tick and no duplicate counts");
    record(recorder, key(0, true, 8));
    record(recorder, key(0, true, 7));
    check(recorder.take(snapshot), "take repeats");
    row = rowFor(snapshot, key(0, true));
    check(row.downCount == 2 && row.repeatDownCount == 2 && row.lastEdgeTick == 7,
        "repeats cross windows and caller tick is not silently normalized");
    record(recorder, key(0, false, 9));
    check(recorder.take(snapshot), "take release");
    row = rowFor(snapshot, key(0, false));
    check(!row.observedHeld && row.upCount == 1 && row.orphanUpCount == 0,
        "release matches held from previous window");
    check(recorder.snapshot(snapshot) && snapshot.count == 0, "released row disappears after take");
    record(recorder, key(255, false, 10, Channel::RawKeyboard, true));
    record(recorder, key(255, false, 11, Channel::RawKeyboard, true));
    check(recorder.take(snapshot), "take orphan ups");
    row = rowFor(snapshot, key(255, false, 0, Channel::RawKeyboard, true));
    check(row.upCount == 2 && row.orphanUpCount == 2 && !row.observedHeld && row.lastEdgeTick == 11,
        "vk255 extended orphan ups counted");
}

static void allKeysAndNamespaces() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable all-key test");
    for (std::uint16_t base = 0; base < 256; base += 32) {
        for (std::uint16_t code = base; code < base + 32; ++code) {
            for (bool extended : {false, true}) {
                record(recorder, key(code, true, 1, Channel::RawKeyboard, extended));
                record(recorder, key(code, false, 2, Channel::RawKeyboard, extended));
            }
        }
        check(recorder.take(snapshot) && snapshot.count == 64 && snapshot.totalRows == 64 &&
            snapshot.omittedRows == 0, "all ordinary/modifier vk and extended variants preserved");
        for (std::uint16_t code = base; code < base + 32; ++code) {
            for (bool extended : {false, true}) {
                const auto& row = rowFor(snapshot, key(code, true, 0, Channel::RawKeyboard, extended));
                check(row.downCount == 1 && row.upCount == 1 && !row.observedHeld,
                    "independent full-vk bucket");
            }
        }
    }
    for (auto channel : {Channel::LowLevelKeyboard, Channel::RawKeyboard,
        Channel::SelfSendInputRequest, Channel::SelfSendInputReturnedPrefix}) {
        record(recorder, key(1, true, 5, channel));
        record(recorder, key(1, true, 6, channel, true));
    }
    for (auto channel : {Channel::MouseHook, Channel::SelfSendInputRequest,
        Channel::SelfSendInputReturnedPrefix}) record(recorder, mouse(Kind::MouseButton, 1, true, 0, channel));
    check(recorder.snapshot(snapshot) && snapshot.count == 11, "channel/kind/extended namespaces do not alias");
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const auto& row = snapshot.rows[i];
        check(row.eventCount == 1 && row.observedHeld == !activity::isSelfChannel(row.channel) &&
            row.selfHeldAssumption == activity::isSelfChannel(row.channel),
            "self channels never set observedHeld");
    }
}

static void prefixAssumption() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable prefix test");
    const std::array<activity::Event, 3> batch{
        key(65, true, 10, Channel::SelfSendInputRequest),
        key(65, false, 10, Channel::SelfSendInputRequest),
        key(66, true, 10, Channel::SelfSendInputRequest)};
    for (const auto& event : batch) record(recorder, event);
    // In-memory SendInput return-count stub only. No actual input or OS API.
    constexpr std::size_t returnedCount = 1;
    for (std::size_t i = 0; i < returnedCount; ++i) {
        auto event = batch[i]; event.channel = Channel::SelfSendInputReturnedPrefix;
        record(recorder, event);
    }
    check(recorder.take(snapshot) && snapshot.count == 3, "only assumed prefix recorded in returned namespace");
    const auto& requestedA = rowFor(snapshot, batch[0]);
    const auto& requestedB = rowFor(snapshot, batch[2]);
    auto prefixA = batch[0]; prefixA.channel = Channel::SelfSendInputReturnedPrefix;
    const auto& inferredA = rowFor(snapshot, prefixA);
    check(!requestedA.selfHeldAssumption && requestedA.downCount == 1 && requestedA.upCount == 1 &&
        requestedB.selfHeldAssumption && inferredA.selfHeldAssumption && inferredA.upCount == 0 &&
        !inferredA.observedHeld && !requestedB.observedHeld, "request and inferred-prefix divergence is explicit");
    auto prefixB = batch[2]; prefixB.channel = Channel::SelfSendInputReturnedPrefix;
    check(find(snapshot, prefixB) == nullptr, "unreturned suffix is not inferred");
}

static void scanCodeNamespaces() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable scan-code test");
    for (auto channel : {Channel::SelfSendInputRequest, Channel::SelfSendInputReturnedPrefix}) {
        for (std::uint16_t base = 0; base < 256; base += 16) {
            for (std::uint16_t code = base; code < base + 16; ++code) {
                for (bool extended : {false, true}) {
                    auto scan = key(code, true, 10, channel, extended);
                    scan.kind = Kind::KeyboardScanCode;
                    record(recorder, scan);
                    record(recorder, key(code, true, 11, channel, extended));
                    scan.down = false; scan.tick = 12;
                    record(recorder, scan);
                    record(recorder, key(code, false, 13, channel, extended));
                }
            }
            check(recorder.take(snapshot) && snapshot.count == 64 && snapshot.totalRows == 64,
                "VK and scan have independent slots across all code/extended combinations");
            for (std::uint16_t code = base; code < base + 16; ++code) {
                for (bool extended : {false, true}) {
                    auto scan = key(code, true, 0, channel, extended);
                    scan.kind = Kind::KeyboardScanCode;
                    const auto& scanRow = rowFor(snapshot, scan);
                    const auto& vkRow = rowFor(snapshot, key(code, true, 0, channel, extended));
                    check(scanRow.eventCount == 2 && scanRow.lastEdgeTick == 12 &&
                        scanRow.downCount == 1 && scanRow.upCount == 1 && !scanRow.observedHeld &&
                        !scanRow.selfHeldAssumption && vkRow.eventCount == 2 && vkRow.lastEdgeTick == 13,
                        "scan never aliases VK or claims observed OS state");
                }
            }
        }
    }
    auto scan = key(0x11, true, 20, Channel::SelfSendInputRequest);
    scan.kind = Kind::KeyboardScanCode;
    record(recorder, scan);
    check(recorder.take(snapshot), "drain held scan");
    auto prefixScan = scan; prefixScan.channel = Channel::SelfSendInputReturnedPrefix;
    record(recorder, scan); record(recorder, prefixScan);
    check(recorder.take(snapshot) && snapshot.count == 2, "scan request/prefix namespaces remain independent");
    check(rowFor(snapshot, scan).repeatDownCount == 1 && rowFor(snapshot, scan).selfHeldAssumption &&
        rowFor(snapshot, prefixScan).repeatDownCount == 0 && !rowFor(snapshot, prefixScan).observedHeld,
        "scan repeat across window and prefix assumption are separate");
    scan.down = false; record(recorder, scan); record(recorder, scan);
    check(recorder.take(snapshot) && rowFor(snapshot, scan).upCount == 2 &&
        rowFor(snapshot, scan).orphanUpCount == 1 && !rowFor(snapshot, scan).selfHeldAssumption &&
        rowFor(snapshot, prefixScan).selfHeldAssumption, "scan release/orphan cannot release prefix assumption");
    for (auto channel : {Channel::LowLevelKeyboard, Channel::RawKeyboard, Channel::MouseHook}) {
        scan.channel = channel;
        check(recorder.record(scan) == RecordResult::Invalid, "observed channels reject scan-code kind");
    }
    scan.channel = Channel::SelfSendInputRequest; scan.code = 256;
    check(recorder.record(scan) == RecordResult::Invalid, "scan namespace rejects code above 255");
    scan.code = 0xffff;
    check(recorder.record(scan) == RecordResult::Invalid, "wide code cannot be truncated into scan namespace");
    scan.code = 0x11; scan.delta = 1;
    check(recorder.record(scan) == RecordResult::Invalid, "scan rejects wheel payload");
    check(recorder.take(snapshot) && snapshot.rejectedInvalid == 6, "invalid scan tuples are counted");
}

static void convenienceMethods() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable convenience methods");
    check(recorder.recordKeyboard(Channel::RawKeyboard, 65, true, true, 10, 20, 30) == RecordResult::Recorded,
        "recordKeyboard wrapper");
    check(recorder.recordMouseButton(Channel::MouseHook, 5, true, 11, 21, 31) == RecordResult::Recorded,
        "recordMouseButton wrapper");
    check(recorder.recordWheel(Channel::MouseHook, true, -120, 12, 22, 32) == RecordResult::Recorded,
        "recordWheel horizontal wrapper");
    check(recorder.recordWheel(Channel::MouseHook, false, 240, 13) == RecordResult::Recorded,
        "recordWheel vertical/default identity wrapper");
    check(recorder.recordMove(Channel::SelfSendInputRequest, 14, 23, 33) == RecordResult::Recorded,
        "recordMove wrapper");
    check(recorder.take(snapshot) && snapshot.count == 5, "wrapper namespaces");
    const auto& keyboard = rowFor(snapshot, key(65, true, 0, Channel::RawKeyboard, true));
    const auto& button = rowFor(snapshot, mouse(Kind::MouseButton, 5));
    const auto& horizontal = rowFor(snapshot, mouse(Kind::WheelHorizontal));
    const auto& vertical = rowFor(snapshot, mouse(Kind::WheelVertical));
    const auto& move = rowFor(snapshot, mouse(Kind::MouseMove, 0, false, 0, Channel::SelfSendInputRequest));
    check(keyboard.observedHeld && keyboard.lastSourceId == 20 && keyboard.lastDeviceId == 30 &&
        keyboard.lastEdgeTick == 10 && button.observedHeld && button.lastSourceId == 21 &&
        horizontal.deltaSum == -120 && horizontal.lastDeviceId == 32 && vertical.deltaSum == 240 &&
        vertical.sourceUnknown && vertical.deviceUnknown && move.eventCount == 1 && move.lastEventTick == 14,
        "wrappers preserve parameters without extra interpretation");
}
static void mouseActivity() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable mouse test");
    for (std::uint16_t button = 1; button <= 5; ++button) {
        record(recorder, mouse(Kind::MouseButton, button, true));
        record(recorder, mouse(Kind::MouseButton, button, true));
        record(recorder, mouse(Kind::MouseButton, button, false));
        record(recorder, mouse(Kind::MouseButton, button, false));
    }
    record(recorder, mouse(Kind::WheelVertical, 0, false, 120));
    record(recorder, mouse(Kind::WheelVertical, 0, false, -240));
    record(recorder, mouse(Kind::WheelHorizontal, 0, false, (std::numeric_limits<std::int32_t>::min)()));
    record(recorder, mouse(Kind::WheelHorizontal, 0, false, (std::numeric_limits<std::int32_t>::max)()));
    for (int i = 0; i < 3; ++i) record(recorder, mouse(Kind::MouseMove));
    check(recorder.take(snapshot) && snapshot.count == 8 && snapshot.recordedEvents == 27,
        "five buttons, two wheel axes and move each aggregate");
    for (std::uint16_t button = 1; button <= 5; ++button) {
        const auto& row = rowFor(snapshot, mouse(Kind::MouseButton, button));
        check(row.eventCount == 4 && row.downCount == 2 && row.upCount == 2 &&
            row.repeatDownCount == 1 && row.orphanUpCount == 1 && !row.observedHeld,
            "mouse repeat/orphan/release accounting");
    }
    const auto& vertical = rowFor(snapshot, mouse(Kind::WheelVertical));
    check(vertical.eventCount == 2 && vertical.deltaSum == -120 && vertical.positiveDelta == 120 &&
        vertical.negativeDelta == 240 && !vertical.hasEdge, "signed vertical wheel counts");
    const auto& horizontal = rowFor(snapshot, mouse(Kind::WheelHorizontal));
    check(horizontal.deltaSum == -1 && horizontal.positiveDelta == 2147483647ULL &&
        horizontal.negativeDelta == 2147483648ULL, "horizontal delta INT32_MIN magnitude has no overflow");
    const auto& move = rowFor(snapshot, mouse(Kind::MouseMove));
    check(move.eventCount == 3 && !move.hasEdge && move.lastEdgeTick == 0 && move.lastEventTick == 42,
        "moves retain only count and last tick");
    check(recorder.take(snapshot) && snapshot.count == 0, "mouse activity clears after drain");
}

static void provenance() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable identity test");
    record(recorder, key(65, true, 1, Channel::RawKeyboard, false, 100, 1000));
    check(recorder.take(snapshot), "carry identity on held row");
    record(recorder, key(65, true, 2, Channel::RawKeyboard, false, 200, 2000));
    check(recorder.take(snapshot), "take mixed held");
    auto row = rowFor(snapshot, key(65, true, 0, Channel::RawKeyboard));
    check(row.sourceMixed && row.deviceMixed && row.lastSourceId == 200 && row.lastDeviceId == 2000 &&
        row.repeatDownCount == 1 && !row.sourceUnknown && !row.deviceUnknown,
        "carried held merges identities without claiming owner");
    check(recorder.snapshot(snapshot), "snapshot mixed held without new activity");
    row = rowFor(snapshot, key(65, true, 0, Channel::RawKeyboard));
    check(row.sourceMixed && row.deviceMixed && row.eventCount == 0, "mixed flags survive held windows");
    record(recorder, key(65, false, 3, Channel::RawKeyboard, false, 200, 2000));
    check(recorder.take(snapshot), "drain release and prior identity");
    record(recorder, key(65, true, 4, Channel::RawKeyboard, false, 300, 3000));
    check(recorder.take(snapshot), "new episode starts identity afresh");
    row = rowFor(snapshot, key(65, true, 0, Channel::RawKeyboard));
    check(!row.sourceMixed && !row.deviceMixed && row.lastSourceId == 300 && row.lastDeviceId == 3000,
        "released/drained identity not assigned to new episode");
    record(recorder, key(66, true, 5, Channel::RawKeyboard, false, 0, 0));
    record(recorder, key(66, false, 6, Channel::RawKeyboard, false, 400, 4000));
    check(recorder.take(snapshot), "unknown identity take");
    row = rowFor(snapshot, key(66, true, 0, Channel::RawKeyboard));
    check(row.sourceUnknown && row.deviceUnknown && row.sourceMixed && row.deviceMixed &&
        row.lastSourceId == 400 && row.lastDeviceId == 4000, "unknown-to-known is explicitly mixed");
}

static void truncation() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable truncation test");
    for (std::uint16_t code = 70; code != 0; --code) {
        record(recorder, key(code, true)); record(recorder, key(code, false));
    }
    check(recorder.snapshot(snapshot) && snapshot.count == 64 && snapshot.totalRows == 70 &&
        snapshot.omittedRows == 6 && snapshot.omittedActiveRows == 6 && snapshot.omittedHeldRows == 0 &&
        snapshot.recordedEvents == 140 && snapshot.droppedOverflow == 0, "64-row cap does not overflow ledger");
    check(snapshot.rows[0].code == 1 && snapshot.rows[63].code == 64, "snapshot order is fixed, not recording order");
    check(recorder.take(snapshot) && snapshot.omittedRows == 6, "take reports omitted released activity");
    check(recorder.snapshot(snapshot) && snapshot.totalRows == 0, "take consumes omitted released rows too");
    for (std::uint16_t code = 1; code <= 70; ++code) record(recorder, key(code, true));
    check(recorder.take(snapshot) && snapshot.omittedHeldRows == 6 && snapshot.omittedActiveRows == 6,
        "held and activity omissions both reported");
    check(recorder.snapshot(snapshot) && snapshot.totalRows == 70 && snapshot.omittedHeldRows == 6 &&
        snapshot.omittedActiveRows == 0 && snapshot.recordedEvents == 0, "all held rows survive truncating drain");
    record(recorder, key(70, false));
    check(recorder.take(snapshot) && snapshot.totalRows == 70 && snapshot.omittedActiveRows == 1 &&
        snapshot.omittedHeldRows == 5, "release of omitted held row remains current-window activity");
    check(recorder.snapshot(snapshot) && snapshot.totalRows == 69, "omitted release clears only its held state");
}

static void completeLedger() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable full ledger");
    for (auto channel : {Channel::LowLevelKeyboard, Channel::RawKeyboard,
        Channel::SelfSendInputRequest, Channel::SelfSendInputReturnedPrefix}) {
        for (std::uint16_t code = 0; code < 256; ++code)
            for (bool extended : {false, true}) record(recorder, key(code, true, 1, channel, extended));
    }
    for (auto channel : {Channel::MouseHook, Channel::SelfSendInputRequest, Channel::SelfSendInputReturnedPrefix}) {
        for (std::uint16_t button = 1; button <= 5; ++button)
            record(recorder, mouse(Kind::MouseButton, button, true, 0, channel));
        for (auto kind : {Kind::WheelVertical, Kind::WheelHorizontal, Kind::MouseMove})
            record(recorder, mouse(kind, 0, false, 0, channel));
    }
    for (auto channel : {Channel::SelfSendInputRequest, Channel::SelfSendInputReturnedPrefix}) {
        for (std::uint16_t code = 0; code < 256; ++code) {
            for (bool extended : {false, true}) {
                auto event = key(code, true, 1, channel, extended);
                event.kind = Kind::KeyboardScanCode;
                record(recorder, event);
            }
        }
    }
    check(recorder.take(snapshot) && snapshot.totalRows == activity::ledgerCapacity &&
        snapshot.recordedEvents == activity::ledgerCapacity && snapshot.omittedRows == activity::ledgerCapacity - 64 &&
        snapshot.droppedOverflow == 0, "all 3096 valid slots coexist without collisions or eviction");
    check(recorder.snapshot(snapshot) && snapshot.totalRows == 256 * 2 * 6 + 5 * 3,
        "only wheel/move activity disappears after full-ledger drain");
    check(recorder.reset() && recorder.snapshot(snapshot) && snapshot.totalRows == 0 &&
        snapshot.discardedOnReset == 0 && snapshot.discardedHeldOnReset == 256 * 2 * 6 + 5 * 3,
        "reset counts carried held state separately from pending events");
}

static void invalidEvents() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable invalid-event test");
    auto badCode = key(256, true);
    auto badChannel = key(1, true); badChannel.channel = static_cast<Channel>(255);
    auto badKind = key(1, true); badKind.kind = static_cast<Kind>(255);
    auto badDelta = key(1, true); badDelta.delta = 1;
    auto badExtended = mouse(Kind::MouseButton, 1, true); badExtended.extended = true;
    const std::array<activity::Event, 13> bad{
        badCode, badChannel, badKind, badDelta, badExtended,
        key(1, true, 1, Channel::MouseHook),
        mouse(Kind::MouseButton, 1, true, 0, Channel::RawKeyboard),
        mouse(Kind::MouseButton, 0), mouse(Kind::MouseButton, 6),
        mouse(Kind::WheelVertical, 1), mouse(Kind::WheelHorizontal, 0, true),
        mouse(Kind::MouseMove, 0, false, 1), mouse(Kind::MouseMove, 0, true)};
    for (const auto& event : bad) check(recorder.record(event) == RecordResult::Invalid, "invalid tuple rejected");
    check(recorder.take(snapshot) && snapshot.totalRows == 0 && snapshot.rejectedInvalid == bad.size(),
        "invalid events accounted without aliasing valid rows");
    check(recorder.take(snapshot) && snapshot.rejectedInvalid == 0, "invalid count drains");
}

static void resetEpochs() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable reset test");
    record(recorder, key(65, true)); record(recorder, key(65, false));
    record(recorder, key(66, true));
    auto old = Access::state(recorder);
    check(recorder.reset() && recorder.enabled(), "reset preserves enabled gate");
    check(Access::resume(recorder, key(67, true), old) == RecordResult::DroppedReset,
        "pre-reset writer cannot enter new epoch");
    check(recorder.snapshot(snapshot) && snapshot.totalRows == 0 && snapshot.discardedOnReset == 4 &&
        snapshot.discardedHeldOnReset == 1 && snapshot.discardedStaleEpoch == 1 && snapshot.heldStateMayHaveGaps,
        "reset reports pending events, held state and stale writer discard");
    record(recorder, key(66, false));
    check(recorder.take(snapshot), "take new reset epoch");
    check(rowFor(snapshot, key(66, false)).orphanUpCount == 1, "reset does not retain old held truth");
    record(recorder, key(68, true));
    old = Access::state(recorder);
    check(recorder.enable(false) && !recorder.enabled(), "disable clears pending state");
    check(recorder.record(key(69, true)) == RecordResult::Disabled, "disabled gate rejects immediately");
    check(recorder.enable(true), "re-enable starts fresh epoch");
    check(Access::resume(recorder, key(70, true), old) == RecordResult::DroppedReset,
        "disable/re-enable cannot admit delayed prior writer");
    check(recorder.take(snapshot) && snapshot.totalRows == 0 && snapshot.discardedOnReset == 2 &&
        snapshot.discardedHeldOnReset == 1 && snapshot.discardedStaleEpoch == 1,
        "enable transitions preserve outstanding loss counters");
    check(recorder.take(snapshot) && snapshot.discardedOnReset == 0 && snapshot.discardedHeldOnReset == 0,
        "discard counters drain exactly once");
}

static void contentionAndNoAllocation() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    watchAllocations.store(true);
    check(recorder.enable(true), "enable allocation probe");
    record(recorder, key(65, true));
    check(Access::lock(recorder), "test holds recorder lock");
    snapshot.count = 123; snapshot.totalRows = 456; snapshot.recordedEvents = 789;
    for (int i = 0; i < 7; ++i)
        check(recorder.record(key(66, true)) == RecordResult::DroppedContention, "busy writer drops without waiting");
    check(!recorder.snapshot(snapshot) && !recorder.take(snapshot) && snapshot.count == 123 &&
        snapshot.totalRows == 456 && snapshot.recordedEvents == 789, "busy snapshot/take leave output untouched");
    check(!recorder.enable(false) && !recorder.reset() && recorder.enabled(), "busy controls do not mutate");
    Access::unlock(recorder);
    check(recorder.snapshot(snapshot) && snapshot.droppedContention == 7 && snapshot.recordedEvents == 1 &&
        snapshot.heldStateMayHaveGaps, "contention accounts only rejected records");
    check(recorder.take(snapshot) && snapshot.droppedContention == 7 && snapshot.count == 1,
        "failed take did not consume rows or loss counts");
    check(recorder.take(snapshot) && snapshot.droppedContention == 0 && snapshot.heldStateMayHaveGaps,
        "contention count drains but held uncertainty persists");
    check(recorder.reset() && recorder.enable(false), "allocation-free controls");
    check(Access::lock(recorder), "lock while disabled");
    check(recorder.record(key(1, true)) == RecordResult::Disabled, "disabled record bypasses busy lock");
    Access::unlock(recorder);
    check(recorder.take(snapshot) && snapshot.droppedContention == 0, "disabled drops are not contention");
    watchAllocations.store(false);
    check(allocationCount.load() == 0, "record/read/control paths allocate no heap memory");
}

static void overflowAccounting() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable overflow probe");
    auto event = key(65, true);
    record(recorder, event);
    Access::saturate(recorder, event);
    check(recorder.record(event) == RecordResult::CountOverflow, "counter saturation is reported");
    event.down = false; event.tick = 99;
    check(recorder.record(event) == RecordResult::CountOverflow, "overflow still processes release");
    check(recorder.take(snapshot) && snapshot.droppedOverflow == 2 && snapshot.countersSaturated &&
        snapshot.recordedEvents == 3, "overflow counts events once, not overflowing fields");
    const auto& row = rowFor(snapshot, event);
    check(row.eventCount == (std::numeric_limits<std::uint64_t>::max)() && !row.observedHeld &&
        row.lastEdgeTick == 99 && row.upCount == 1, "saturation does not wrap counts or freeze held state");
    auto wheel = mouse(Kind::WheelVertical, 0, false, 1);
    record(recorder, wheel);
    Access::deltaLimit(recorder, wheel, (std::numeric_limits<std::int64_t>::max)());
    check(recorder.record(wheel) == RecordResult::CountOverflow, "positive delta saturation");
    check(recorder.take(snapshot) && rowFor(snapshot, wheel).deltaSum ==
        (std::numeric_limits<std::int64_t>::max)(), "positive delta saturates safely");
    wheel.delta = -1; record(recorder, wheel);
    Access::deltaLimit(recorder, wheel, (std::numeric_limits<std::int64_t>::min)());
    check(recorder.record(wheel) == RecordResult::CountOverflow, "negative delta saturation");
    check(recorder.take(snapshot) && rowFor(snapshot, wheel).deltaSum ==
        (std::numeric_limits<std::int64_t>::min)(), "negative delta saturates safely");
    Access::contentionLimit(recorder);
    check(Access::lock(recorder), "lock contention wrap probe");
    check(recorder.record(key(1, true)) == RecordResult::DroppedContention, "bounded atomic contention rollover");
    Access::unlock(recorder);
    check(recorder.take(snapshot) && snapshot.contentionCounterWrapped && snapshot.droppedContention == 0,
        "contention rollover has explicit lifetime flag");
    check(recorder.take(snapshot) && snapshot.contentionCounterWrapped, "contention wrap flag remains visible");
}

static void parallelWriters() {
    activity::Recorder recorder;
    activity::Snapshot snapshot{};
    check(recorder.enable(true), "enable concurrent producer probe");
    constexpr std::size_t producers = 4;
    constexpr unsigned iterations = 20000;
    std::atomic<unsigned> ready{0};
    std::atomic<bool> start{false};
    std::array<std::uint64_t, producers> accepted{}, dropped{}, unexpected{};
    std::array<std::thread, producers> threads;
    for (std::size_t producer = 0; producer < producers; ++producer) {
        threads[producer] = std::thread([&, producer] {
            ready.fetch_add(1);
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            for (unsigned i = 0; i < iterations; ++i) {
                const auto result = recorder.record(key(static_cast<std::uint16_t>(80 + producer),
                    (i & 1) == 0, i, Channel::RawKeyboard, false, producer + 1, producer + 101));
                if (result == RecordResult::Recorded) ++accepted[producer];
                else if (result == RecordResult::DroppedContention) ++dropped[producer];
                else ++unexpected[producer];
            }
        });
    }
    while (ready.load() != producers) std::this_thread::yield();
    start.store(true, std::memory_order_release);
    for (auto& thread : threads) thread.join();
    std::uint64_t acceptedTotal = 0, droppedTotal = 0, unexpectedTotal = 0;
    for (std::size_t producer = 0; producer < producers; ++producer) {
        acceptedTotal += accepted[producer]; droppedTotal += dropped[producer]; unexpectedTotal += unexpected[producer];
    }
    check(recorder.take(snapshot) && unexpectedTotal == 0 &&
        acceptedTotal + droppedTotal == producers * iterations &&
        snapshot.recordedEvents == acceptedTotal && snapshot.droppedContention == droppedTotal,
        "parallel producers conserve all attempts as recorded or contention-dropped");
    std::uint64_t rowEvents = 0;
    for (std::size_t i = 0; i < snapshot.count; ++i) {
        const auto& row = snapshot.rows[i]; rowEvents += row.eventCount;
        check(row.downCount + row.upCount == row.eventCount && row.repeatDownCount <= row.downCount &&
            row.orphanUpCount <= row.upCount && !row.sourceMixed && !row.deviceMixed,
            "parallel rows preserve counts and independent source identity");
    }
    check(rowEvents == acceptedTotal, "parallel row totals equal accepted attempts");
    std::printf("parallel attempts=%u accepted=%llu contention=%llu\n", static_cast<unsigned>(producers * iterations),
        static_cast<unsigned long long>(acceptedTotal), static_cast<unsigned long long>(droppedTotal));
}

int main() {
    static_assert(noexcept(std::declval<activity::Recorder&>().record(std::declval<const activity::Event&>())));
    static_assert(noexcept(std::declval<activity::Recorder&>().snapshot(std::declval<activity::Snapshot&>())));
    static_assert(noexcept(std::declval<activity::Recorder&>().take(std::declval<activity::Snapshot&>())));
    static_assert(noexcept(std::declval<activity::Recorder&>().enable(true)));
    static_assert(noexcept(std::declval<activity::Recorder&>().reset()));
    static_assert(activity::capacity == 64 && activity::ledgerCapacity == 3096);
    shortPressAndDisabled(); heldRepeatOrphanRelease(); allKeysAndNamespaces(); prefixAssumption();
    scanCodeNamespaces(); convenienceMethods(); mouseActivity(); provenance();
    truncation(); completeLedger(); invalidEvents(); resetEpochs();
    contentionAndNoAllocation(); overflowAccounting(); parallelWriters();
    std::printf("input_signal_activity_evidence_selftest checks=%u failures=%u allocations=%u "
        "recorderBytes=%zu snapshotBytes=%zu ledgerSlots=%zu outputRows=%zu\n", checks, failures,
        allocationCount.load(), sizeof(activity::Recorder), sizeof(activity::Snapshot),
        activity::ledgerCapacity, activity::capacity);
    return failures ? 1 : 0;
}
