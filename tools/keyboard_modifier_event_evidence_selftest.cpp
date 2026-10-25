#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>
#include "keyboard_modifier_event_evidence.h"

namespace evidence = ymcc::keyboardEvents;
static std::atomic<bool> countAllocations{false};
static std::atomic<unsigned> allocations{0};
void* operator new(std::size_t size) {
    if (countAllocations.load(std::memory_order_relaxed)) ++allocations;
    if (void* value = std::malloc(size ? size : 1)) return value;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* value) noexcept { std::free(value); }
void operator delete[](void* value) noexcept { std::free(value); }
void operator delete(void* value, std::size_t) noexcept { std::free(value); }
void operator delete[](void* value, std::size_t) noexcept { std::free(value); }

namespace ymcc::keyboardEvents::detail {
struct RecorderTestAccess {
    static std::unique_lock<std::mutex> hold(Recorder& recorder) {
        return std::unique_lock<std::mutex>(recorder.mutex_);
    }
    static void resumeCapturedWriter(Recorder& recorder, const Event& event, std::uint64_t epoch) {
        recorder.appendAtEpoch(event, epoch);
    }
};
}

static unsigned checks = 0, failures = 0;
static void check(bool passed, const char* name) {
    ++checks;
    if (!passed) { ++failures; std::fprintf(stderr, "FAILED %s\n", name); }
}
static evidence::Event event(evidence::Kind kind, DWORD vk, ULONGLONG tick = 101) {
    evidence::Event result{};
    result.kind = kind; result.vk = vk; result.tick = tick;
    result.threadId = 17; result.powerGeneration = 901; result.deviceGeneration = 77;
    result.scan = 0x1d; result.flags = 0x90; result.message = WM_KEYUP;
    result.extraInfo = static_cast<std::uintptr_t>(0xfedcba9876543210ULL);
    result.device = static_cast<std::uintptr_t>(0xabcdef0123456789ULL);
    result.requestedInputs = 4; result.insertedInputs = 1; result.win32ErrorHint = 5;
    result.sourceId = evidence::SourceId::Other;
    return result;
}
static bool same(const evidence::Event& a, const evidence::Event& b) {
    return a.kind == b.kind && a.tick == b.tick && a.threadId == b.threadId &&
        a.powerGeneration == b.powerGeneration && a.deviceGeneration == b.deviceGeneration &&
        a.vk == b.vk && a.scan == b.scan && a.flags == b.flags && a.message == b.message &&
        a.extraInfo == b.extraInfo && a.device == b.device &&
        a.requestedInputs == b.requestedInputs && a.insertedInputs == b.insertedInputs &&
        a.win32ErrorHint == b.win32ErrorHint && a.sourceId == b.sourceId &&
        a.attemptedModifierDownMask == b.attemptedModifierDownMask &&
        a.attemptedModifierUpMask == b.attemptedModifierUpMask &&
        a.insertedModifierDownMask == b.insertedModifierDownMask &&
        a.insertedModifierUpMask == b.insertedModifierUpMask;
}

int main() {
    static_assert(noexcept(std::declval<evidence::Recorder&>().record(std::declval<const evidence::Event&>())));
    static_assert(noexcept(std::declval<evidence::Recorder&>().take(0)));
    static_assert(std::is_trivial_v<evidence::Event> && std::is_trivial_v<evidence::Snapshot>);
    static_assert(evidence::capacity == 64);
    {
        evidence::Recorder recorder;
        recorder.record(event(evidence::Kind::LowLevel, VK_LCONTROL));
        const auto disabled = recorder.take(100);
        check(!disabled.enabled && disabled.drained && !disabled.complete && disabled.count == 0 &&
            disabled.capacity == 64 && disabled.configEpoch == 0 && disabled.ignoredNonModifiers == 0,
            "atomic gate defaults off and ignores records");
        recorder.enable(true, 100);
        recorder.enable(true, 999); // Idempotent calls preserve opening time.
        auto empty = recorder.take(110);
        check(empty.enabled && empty.complete && empty.drained && empty.count == 0 &&
            empty.configEpoch == 1 && empty.enabledSinceTick == 100 &&
            empty.windowBeginTick == 100 && empty.windowEndTick == 110,
            "enabled empty snapshot is a drained window, never absence proof");
        recorder.enable(false, 120);
        recorder.enable(false, 130);
        recorder.record(event(evidence::Kind::RawInput, VK_CONTROL));
        const auto off = recorder.take(140);
        check(!off.enabled && !off.complete && off.count == 0 && off.configEpoch == 2 &&
            off.enabledSinceTick == 100, "disable is idempotent and records nothing");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 100);
        unsigned ignored = 0;
        for (int vk = 0; vk < 256; ++vk) {
            const int index = evidence::modifierIndex(vk);
            const auto bit = evidence::bit(vk);
            check((index >= 0) == (bit != 0), "public index/bit agree");
            if (index < 0) ++ignored;
            recorder.record(event(evidence::Kind::LowLevel, static_cast<DWORD>(vk)));
        }
        const auto result = recorder.take(120);
        check(result.count == 11 && result.ignoredNonModifiers == ignored &&
            result.droppedOverflow == 0 && result.complete, "only 11 modifier VKs survive; typing is not retained");
        for (std::size_t i = 0; i < evidence::modifierKeys.size(); ++i) {
            check(evidence::modifierIndex(static_cast<int>(evidence::modifierKeys[i])) == static_cast<int>(i) &&
                evidence::bit(static_cast<int>(evidence::modifierKeys[i])) == (1u << i), "stable 8-side plus 3-generic mask order");
        }
        check(evidence::modifierIndex(-1) == -1 && evidence::modifierIndex(100000) == -1,
            "out-of-range values are not modifiers");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 100);
        for (const auto kind : {evidence::Kind::LowLevel, evidence::Kind::RawInput}) {
            for (const auto vk : evidence::modifierKeys) {
                const auto input = event(kind, vk);
                recorder.record(input);
                const auto result = recorder.take(120);
                check(result.count == 1 && same(result.events[0], input), "LL/raw source kind and payload are not rewritten");
            }
        }
        for (const auto source : {evidence::SourceId::NativeKeyboardShortcut, evidence::SourceId::OemKeyboardReplay,
            evidence::SourceId::KeyboardGamebar, evidence::SourceId::Other}) {
            auto input = event(evidence::Kind::SelfSendInput, VK_LCONTROL);
            input.sourceId = source;
            input.attemptedModifierDownMask = evidence::allModifierMask;
            input.attemptedModifierUpMask = evidence::allModifierMask;
            input.insertedModifierDownMask = evidence::bit(VK_LCONTROL);
            input.insertedModifierUpMask = 0;
            recorder.record(input);
            const auto result = recorder.take(120);
            check(result.count == 1 && same(result.events[0], input), "self source/result and inserted prefix masks are preserved");
        }
        auto shiftedTyping = event(evidence::Kind::SelfSendInput, 'A');
        shiftedTyping.scan = 0x1e;
        shiftedTyping.attemptedModifierDownMask = evidence::bit(VK_LSHIFT);
        shiftedTyping.attemptedModifierUpMask = evidence::bit(VK_LSHIFT);
        shiftedTyping.insertedModifierDownMask = evidence::bit(VK_LSHIFT);
        recorder.record(shiftedTyping);
        const auto privacy = recorder.take(120);
        shiftedTyping.vk = shiftedTyping.scan = 0;
        check(privacy.count == 1 && same(privacy.events[0], shiftedTyping),
            "requiresShift result retained without ordinary key or scan");
        auto gamebar = event(evidence::Kind::SelfSendInput, 0);
        gamebar.attemptedModifierDownMask = evidence::bit(VK_LWIN);
        gamebar.insertedModifierUpMask = evidence::bit(VK_LWIN);
        recorder.record(gamebar);
        check(recorder.take(120).count == 1, "mask-only SelfSendInput is accepted");
        auto typing = event(evidence::Kind::SelfSendInput, 'B');
        recorder.record(typing);
        typing.attemptedModifierDownMask = 0x8000; // Unknown high bits cannot make typing eligible.
        recorder.record(typing);
        const auto ignored = recorder.take(130);
        check(ignored.count == 0 && ignored.ignoredNonModifiers == 2, "self ordinary-key-only events are ignored");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 500);
        for (unsigned i = 0; i < 70; ++i) recorder.record(event(evidence::Kind::RawInput, VK_RCONTROL, 501 + i));
        const auto full = recorder.take(600);
        check(full.count == 64 && full.capacity == 64 && full.droppedOverflow == 6 && !full.complete && full.drained,
            "real helper retains first 64 and reports overflow");
        for (std::size_t i = 0; i < full.count; ++i)
            check(full.events[i].tick == 501 + i, "insertion order and overflow payload preserved");
        const auto cleared = recorder.take(610);
        check(cleared.count == 0 && cleared.droppedOverflow == 0 && cleared.complete &&
            cleared.windowBeginTick == 600 && cleared.windowEndTick == 610, "take clears rows/counters and advances window");
        recorder.record(event(evidence::Kind::RawInput, VK_LMENU));
        recorder.enable(false, 620);
        const auto discarded = recorder.take(630);
        check(!discarded.enabled && discarded.count == 0 && discarded.discardedOnConfigChange == 1 &&
            !discarded.complete && discarded.configEpoch == 2, "configuration transition accounts for pending rows discarded");
        recorder.enable(true, 640);
        const auto fresh = recorder.take(650);
        check(fresh.enabled && fresh.configEpoch == 3 && fresh.enabledSinceTick == 640 &&
            fresh.discardedOnConfigChange == 0, "re-enable has a fresh epoch/opening time");
        check(!recorder.take(649).complete, "backwards window tick cannot claim completeness");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 100);
        recorder.record(event(evidence::Kind::RawInput, VK_LCONTROL));
        auto held = evidence::detail::RecorderTestAccess::hold(recorder);
        evidence::Snapshot contended{};
        std::thread writer([&] {
            recorder.record(event(evidence::Kind::LowLevel, VK_RCONTROL));
            contended = recorder.take(150);
        });
        writer.join(); // Both production APIs must finish while the mutex remains held.
        held.unlock();
        check(!contended.drained && !contended.complete && contended.count == 0,
            "writer and take never wait for another lock owner");
        const auto actual = recorder.take(200);
        check(actual.drained && actual.count == 1 && actual.droppedContention == 1 &&
            actual.windowBeginTick == 100 && !actual.complete, "failed take does not consume queue/counters/window");
        check(recorder.take(210).droppedContention == 0, "contention counter is cleared only by a successful drain");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 100);
        std::atomic<unsigned> finished{0};
        constexpr unsigned writers = 4, each = 1000;
        std::vector<std::thread> threads;
        for (unsigned thread = 0; thread < writers; ++thread) threads.emplace_back([&, thread] {
            for (unsigned i = 0; i < each; ++i) {
                auto input = event(evidence::Kind::RawInput, VK_LCONTROL, 101 + i);
                input.threadId = thread + 1; input.deviceGeneration = thread + 900;
                recorder.record(input);
            }
            finished.fetch_add(1, std::memory_order_release);
        });
        std::uint64_t retained = 0, overflow = 0, contention = 0;
        unsigned snapshots = 0;
        do {
            const auto result = recorder.take(200 + snapshots++);
            if (result.drained) {
                retained += result.count; overflow += result.droppedOverflow; contention += result.droppedContention;
                for (std::size_t i = 0; i < result.count; ++i)
                    check(result.events[i].deviceGeneration == result.events[i].threadId + 899,
                        "cross-thread payload is coherent without rewriting device generation");
            }
            std::this_thread::yield();
        } while (finished.load(std::memory_order_acquire) != writers);
        for (auto& thread : threads) thread.join();
        const auto tail = recorder.take(5000);
        retained += tail.count; overflow += tail.droppedOverflow; contention += tail.droppedContention;
        check(retained + overflow + contention == writers * each, "all concurrent API calls accounted for by rows/overflow/contention");
    }
    {
        evidence::Recorder recorder; recorder.enable(true, 100);
        const auto oldEpoch = recorder.take(110).configEpoch;
        const auto oldInput = event(evidence::Kind::RawInput, VK_LCONTROL, 115);
        recorder.enable(false, 120);
        recorder.enable(true, 130);
        // Resume the actual internal append path with the epoch captured before
        // disable/re-enable. No fake mutex, API override or input submission.
        evidence::detail::RecorderTestAccess::resumeCapturedWriter(recorder, oldInput, oldEpoch);
        auto current = event(evidence::Kind::RawInput, VK_RCONTROL, 135);
        recorder.record(current);
        const auto fresh = recorder.take(140);
        check(fresh.configEpoch == oldEpoch + 2 && fresh.count == 1 && same(fresh.events[0], current) &&
            fresh.discardedOnConfigChange == 1 && !fresh.complete,
            "old epoch writer cannot enter a new configuration; explicit gap is retained");
        recorder.enable(false, 150);
        evidence::detail::RecorderTestAccess::resumeCapturedWriter(recorder, current, fresh.configEpoch);
        const auto disabled = recorder.take(160);
        check(!disabled.enabled && disabled.count == 0 && disabled.discardedOnConfigChange == 1,
            "writer resumed after disable is explicitly discarded");
    }
    {
        evidence::Recorder recorder;
        const auto input = event(evidence::Kind::LowLevel, VK_LCONTROL);
        allocations.store(0);
        countAllocations.store(true);
        recorder.enable(true, 100);
        for (unsigned i = 0; i < 100; ++i) recorder.record(input);
        const auto result = recorder.take(200);
        recorder.enable(false, 210);
        const auto off = recorder.take(220);
        countAllocations.store(false);
        check(result.count == 64 && !off.enabled && allocations.load() == 0,
            "enable/record/take perform no C++ heap allocations");
    }
    std::printf("{\"passed\":%s,\"checks\":%u,\"failed\":%u,\"capacity\":64,\"heapAllocationsDuringApiChecks\":%u,"
        "\"scope\":\"Actual helper production APIs, synthetic event data, memory-only concurrency; no real input APIs, products, devices or services.\"}\n",
        failures ? "false" : "true", checks, failures, allocations.load());
    return failures ? 1 : 0;
}
