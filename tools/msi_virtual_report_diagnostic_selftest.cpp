// Include first: proves the pure helper is self-contained and nlohmann-free.
#include "msi_virtual_report_diagnostic.h"

#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

// Deterministic allocation failures, not an OS/SDK/device mock. Only this local
// pure fake executable overrides allocation; production code is untouched.
namespace allocationFake {
long failAfter = -1;
void* Allocate(std::size_t size) {
    if (failAfter == 0) throw std::bad_alloc{};
    if (failAfter > 0) --failAfter;
    if (void* p = std::malloc(size == 0 ? 1 : size)) return p;
    throw std::bad_alloc{};
}
}
void* operator new(std::size_t size) { return allocationFake::Allocate(size); }
void* operator new[](std::size_t size) { return allocationFake::Allocate(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

namespace diag = ymcc::msi::virtualReportDiag;
using diag::Binding;
using diag::FrameMetadata;
using diag::Gates;
using diag::PrepareAuthorization;
using diag::Session;
using diag::SourceStamp;

// Exact signatures (including noexcept). The added FINAL-output key is the
// only agreed amendment; runtime calls also exercise default arguments.
static_assert(std::is_same_v<decltype(&Session::Request),
    bool (Session::*)(std::string, uint32_t, uint32_t) noexcept>);
static_assert(std::is_same_v<decltype(&Session::ObserveHello), void (Session::*)(unsigned) noexcept>);
static_assert(std::is_same_v<decltype(&Session::OfferPrepare),
    std::optional<PrepareAuthorization> (Session::*)(const Binding&, const Gates&, uint64_t) noexcept>);
static_assert(std::is_same_v<decltype(&Session::AcceptPrepare),
    bool (Session::*)(const Binding&, bool, const std::string&, uint64_t) noexcept>);
static_assert(std::is_same_v<decltype(&Session::SourceForFrame),
    std::optional<FrameMetadata> (Session::*)(const Binding&, const Gates&, const SourceStamp&, uint64_t) noexcept>);
static_assert(std::is_same_v<decltype(&Session::ShouldRecordSource),
    bool (Session::*)(const FrameMetadata&, bool, uint64_t, uint32_t) noexcept>);
static_assert(std::is_same_v<decltype(&Session::ClearHost), void (Session::*)() noexcept>);
static_assert(std::is_same_v<decltype(&Session::Close), void (Session::*)() noexcept>);
static_assert(std::is_same_v<decltype(&Session::active), bool (Session::*)() const noexcept>);
static_assert(std::is_same_v<decltype(&Session::version), unsigned (Session::*)() const noexcept>);
static_assert(std::is_nothrow_default_constructible_v<Session>);
static_assert(!std::is_copy_constructible_v<Session> && !std::is_move_constructible_v<Session>);
static_assert(!std::is_copy_assignable_v<Session> && !std::is_move_assignable_v<Session>);
static_assert(std::is_aggregate_v<SourceStamp> && std::is_aggregate_v<Binding> &&
    std::is_aggregate_v<Gates> && std::is_aggregate_v<PrepareAuthorization> &&
    std::is_aggregate_v<FrameMetadata>);
static_assert(std::is_nothrow_move_constructible_v<PrepareAuthorization> &&
    std::is_nothrow_move_constructible_v<FrameMetadata> && std::is_nothrow_move_assignable_v<Binding>);
static_assert(std::is_same_v<decltype(SourceStamp::buttons), uint16_t> &&
    std::is_same_v<decltype(SourceStamp::lx), int16_t> &&
    std::is_same_v<decltype(SourceStamp::leftTrigger), uint8_t>);

namespace {
unsigned assertions = 0;
void Check(bool condition, const char* expression, int line) {
    ++assertions;
    if (!condition) throw std::runtime_error(std::string("line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(...) Check(static_cast<bool>((__VA_ARGS__)), #__VA_ARGS__, __LINE__)

Binding Tuple() { return {712u, "pure-fake-run", 77u, 19u}; }
Gates AllGates() { return {true, true, true, false, true}; }
SourceStamp Source(uint64_t now = 1000) {
    // Verified-binding ordinal intentionally differs from the fake Host epoch.
    return {1, 3, now, 0x1234, 14, 31, -32768, 32767, -321, 999, true};
}
bool Equal(const SourceStamp& a, const SourceStamp& b) {
    return a.sequence == b.sequence && a.generation == b.generation && a.capturedTick == b.capturedTick &&
        a.buttons == b.buttons && a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger &&
        a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry && a.valid == b.valid;
}
bool Equal(const Binding& a, const Binding& b) {
    return a.nativePid == b.nativePid && a.runId == b.runId && a.epoch == b.epoch &&
        a.powerGeneration == b.powerGeneration;
}
void Arm(Session& session, const Binding& binding = Tuple(), uint32_t duration = 10000,
         uint32_t capacity = 128, uint64_t now = 1000, const std::string& id = "explicit-session") {
    CHECK(session.Request(id, duration, capacity));
    session.ObserveHello(1);
    const auto offer = session.OfferPrepare(binding, AllGates(), now);
    CHECK(offer && Equal(offer->binding, binding));
    CHECK(offer->sessionId == id && offer->durationMs == duration && offer->maxMeaningfulEdges == capacity);
    CHECK(!session.active());
    CHECK(session.AcceptPrepare(binding, true, id, now));
    CHECK(session.active());
}
FrameMetadata Frame(Session& session, const SourceStamp& source = Source(), uint64_t now = 1000) {
    auto frame = session.SourceForFrame(Tuple(), AllGates(), source, now);
    CHECK(frame);
    CHECK(Equal(frame->source, source));
    return std::move(*frame);
}

void DefaultOff() {
    Session session;
    CHECK(!session.active() && session.version() == 0);
    CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
    session.ObserveHello(1);
    for (unsigned i = 0; i < 10; ++i) {
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
        CHECK(!session.SourceForFrame(Tuple(), AllGates(), Source(), 1000));
        CHECK(!session.ShouldRecordSource({"no-intent", Source()}, false, 1000));
        CHECK(!session.active());
    }
    session.ClearHost(); session.ObserveHello(1);
    CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
    CHECK(!session.AcceptPrepare(Tuple(), true, "unsolicited", 1000));
    CHECK(!session.active());
    SourceStamp source;
    CHECK(!source.valid && source.sequence == 0 && source.generation == 0 && source.capturedTick == 0);
    CHECK(source.buttons == 0 && source.leftTrigger == 0 && source.rightTrigger == 0);
    CHECK(source.lx == 0 && source.ly == 0 && source.rx == 0 && source.ry == 0);
    Gates gates;
    CHECK(!gates.knownMsiBoard && !gates.visibilityDiagnosticBuild && !gates.virtualDetailLogEnabled &&
        !gates.exitFence && !gates.powerReady);
}

void RequestValidation() {
    const std::string invalid[] = {"", " ", "\t", "a b", "a\nb", "a\rb", "a/b", "a\\b", "a:b",
        "\"quoted\"", "'quoted'", "a;b", "a|b", "a=b", "a?b", "a#b", "a%b", "a+b", "a@b",
        std::string("a\0b", 3), std::string(1, '\x7f'), std::string(1, static_cast<char>(0x80)),
        std::string(1, static_cast<char>(0xff)), std::string(97, 'a')};
    for (const auto& id : invalid) {
        Session session;
        CHECK(!session.Request(id)); session.ObserveHello(1);
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
        CHECK(session.Request("valid-after-rejection"));
    }
    for (unsigned c = 0; c <= 255; ++c) {
        const bool expected = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.';
        Session session;
        CHECK(session.Request(std::string(1, static_cast<char>(c))) == expected);
    }
    for (uint32_t duration : {0u, 30001u, (std::numeric_limits<uint32_t>::max)()}) {
        Session session;
        CHECK(!session.Request("duration", duration));
        CHECK(session.Request("duration-valid"));
    }
    for (uint32_t capacity : {0u, 7u, 129u, (std::numeric_limits<uint32_t>::max)()}) {
        Session session;
        CHECK(!session.Request("capacity", 10000, capacity));
        CHECK(session.Request("capacity-valid"));
    }
    for (uint32_t duration : {1u, 30000u}) {
        for (uint32_t capacity : {8u, 128u}) {
            Session session;
            Arm(session, Tuple(), duration, capacity, 1000, std::string(96, 'Z'));
        }
    }
    Session defaults;
    CHECK(defaults.Request("Az09-_.")); defaults.ObserveHello(1);
    const auto offer = defaults.OfferPrepare(Tuple(), AllGates(), 1000);
    CHECK(offer && offer->durationMs == 10000 && offer->maxMeaningfulEdges == 128);
}

void CannotReplaceAndOneShot() {
    Session session;
    CHECK(session.Request("original", 30000, 8));
    CHECK(!session.Request("pending-replacement")); session.ObserveHello(1);
    const auto offer = session.OfferPrepare(Tuple(), AllGates(), 1000);
    CHECK(offer && offer->sessionId == "original" && offer->maxMeaningfulEdges == 8);
    CHECK(!session.Request("offered-replacement"));
    CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
    Binding retarget = Tuple(); ++retarget.epoch;
    CHECK(!session.OfferPrepare(retarget, AllGates(), 1000));
    CHECK(session.AcceptPrepare(Tuple(), true, "original", 1001));
    CHECK(!session.Request("active-replacement"));
    CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1001)); session.Close();
    CHECK(!session.active() && session.version() == 1);
    CHECK(!session.OfferPrepare(retarget, AllGates(), 1002));
    CHECK(session.Request("explicit-second"));
    CHECK(session.OfferPrepare(retarget, AllGates(), 1002));
    CHECK(session.AcceptPrepare(retarget, true, "explicit-second", 1002));
    CHECK(session.active());
}

void GateTruthTable() {
    for (unsigned mask = 0; mask < 32; ++mask) {
        Gates gates{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                    (mask & 8) != 0, (mask & 16) != 0};
        const bool eligible = mask == 23;
        Session session;
        CHECK(session.Request("truth-table")); session.ObserveHello(1);
        CHECK(static_cast<bool>(session.OfferPrepare(Tuple(), gates, 1000)) == eligible);
        if (!eligible) CHECK(session.OfferPrepare(Tuple(), AllGates(), 1001));
        CHECK(!session.active());
        CHECK(session.AcceptPrepare(Tuple(), true, "truth-table", 1001));
    }
    for (unsigned mask = 0; mask < 32; ++mask) {
        Session session; Arm(session);
        Gates gates{(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0,
                    (mask & 8) != 0, (mask & 16) != 0};
        const auto frame = session.SourceForFrame(Tuple(), gates, Source(), 1000);
        CHECK(static_cast<bool>(frame) == (mask == 23));
        CHECK(session.active() == (mask == 23));
        if (mask != 23) {
            CHECK(!session.SourceForFrame(Tuple(), AllGates(), Source(), 1000));
            CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
        }
    }
}

void CompleteTupleAndAck() {
    for (unsigned field = 0; field < 4; ++field) {
        Binding incomplete = Tuple();
        if (field == 0) incomplete.nativePid = 0;
        if (field == 1) incomplete.runId.clear();
        if (field == 2) incomplete.epoch = 0;
        if (field == 3) incomplete.powerGeneration = 0;
        Session session;
        CHECK(session.Request("complete")); session.ObserveHello(1);
        CHECK(!session.OfferPrepare(incomplete, AllGates(), 1000));
        CHECK(session.OfferPrepare(Tuple(), AllGates(), 1000));
    }
    for (unsigned field = 0; field < 4; ++field) {
        Binding wrong = Tuple();
        if (field == 0) ++wrong.nativePid;
        if (field == 1) wrong.runId += "-other";
        if (field == 2) ++wrong.epoch;
        if (field == 3) ++wrong.powerGeneration;
        Session session;
        CHECK(session.Request("tuple")); session.ObserveHello(1);
        CHECK(session.OfferPrepare(Tuple(), AllGates(), 1000));
        CHECK(!session.AcceptPrepare(wrong, true, "tuple", 1000));
        CHECK(!session.AcceptPrepare(Tuple(), true, "tuple", 1000));
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
        CHECK(!session.active());
        Session active; Arm(active);
        CHECK(!active.SourceForFrame(wrong, AllGates(), Source(), 1000));
        CHECK(!active.active());
        CHECK(!active.OfferPrepare(Tuple(), AllGates(), 1000));
    }
    for (unsigned fault = 0; fault < 3; ++fault) {
        Session session;
        CHECK(session.Request("ack-id")); session.ObserveHello(1);
        CHECK(session.OfferPrepare(Tuple(), AllGates(), 1000));
        const std::string id = fault == 0 ? "wrong" : fault == 1 ? "" : "ack-id";
        CHECK(!session.AcceptPrepare(Tuple(), fault != 2, id, 1000));
        CHECK(!session.active());
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
    }
    Session duplicate; Arm(duplicate);
    CHECK(!duplicate.AcceptPrepare(Tuple(), true, "explicit-session", 1000));
    CHECK(!duplicate.active());
}

void LateAckAndDuration() {
    for (uint64_t ack : {uint64_t{999}, uint64_t{1100}, uint64_t{100000}}) {
        Session session;
        CHECK(session.Request("late", 100)); session.ObserveHello(1);
        CHECK(session.OfferPrepare(Tuple(), AllGates(), 1000));
        CHECK(!session.AcceptPrepare(Tuple(), true, "late", ack));
        CHECK(!session.active());
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), ack));
    }
    Session delayed;
    CHECK(delayed.Request("delayed", 100)); delayed.ObserveHello(1);
    CHECK(delayed.OfferPrepare(Tuple(), AllGates(), 1000));
    CHECK(delayed.AcceptPrepare(Tuple(), true, "delayed", 1099));
    CHECK(delayed.SourceForFrame(Tuple(), AllGates(), Source(1198), 1198));
    CHECK(!delayed.SourceForFrame(Tuple(), AllGates(), Source(1199), 1199));
    CHECK(!delayed.active());
    Session recordDeadline; Arm(recordDeadline, Tuple(), 1);
    const auto frame = Frame(recordDeadline);
    CHECK(!recordDeadline.ShouldRecordSource(frame, false, 1001));
    CHECK(!recordDeadline.active());
    Session regressed; Arm(regressed);
    CHECK(!regressed.SourceForFrame(Tuple(), AllGates(), Source(999), 999));
    CHECK(!regressed.active());
    Session regressedRecord; Arm(regressedRecord);
    CHECK(!regressedRecord.ShouldRecordSource({"explicit-session", Source(999)}, false, 999));
    CHECK(!regressedRecord.active());
}

void OverflowSafe() {
    const uint64_t maximum = (std::numeric_limits<uint64_t>::max)();
    Session overflow;
    CHECK(overflow.Request("overflow", 100)); overflow.ObserveHello(1);
    CHECK(overflow.OfferPrepare(Tuple(), AllGates(), maximum - 99));
    CHECK(!overflow.AcceptPrepare(Tuple(), true, "overflow", maximum - 99));
    CHECK(!overflow.active());
    CHECK(!overflow.OfferPrepare(Tuple(), AllGates(), maximum));
    Session boundary;
    Arm(boundary, Tuple(), 100, 128, maximum - 100, "maximum-boundary");
    auto frame = boundary.SourceForFrame(Tuple(), AllGates(), Source(maximum - 1), maximum - 1);
    CHECK(frame && boundary.ShouldRecordSource(*frame, false, maximum - 1));
    CHECK(!boundary.SourceForFrame(Tuple(), AllGates(), Source(maximum), maximum));
    CHECK(!boundary.active());
    Session noWrappedAck;
    CHECK(noWrappedAck.Request("no-wrapped-ack", 100)); noWrappedAck.ObserveHello(1);
    CHECK(noWrappedAck.OfferPrepare(Tuple(), AllGates(), maximum - 10));
    CHECK(!noWrappedAck.AcceptPrepare(Tuple(), true, "no-wrapped-ack", 5));
}

void SourceFreshnessAndNullProvenance() {
    Session session; Arm(session, Tuple(), 30000, 128, 2000);
    for (unsigned invalid = 0; invalid < 5; ++invalid) {
        SourceStamp source = Source(2000);
        if (invalid == 0) source.valid = false;
        if (invalid == 1) source.sequence = 0;
        if (invalid == 2) source.generation = 0;
        if (invalid == 3) source.capturedTick = 2001;
        if (invalid == 4) source.capturedTick = 999;
        const auto before = source;
        CHECK(!session.SourceForFrame(Tuple(), AllGates(), source, 2000));
        CHECK(Equal(source, before));
        CHECK(session.active());
        CHECK(!session.ShouldRecordSource({"explicit-session", source}, false, 2000));
    }
    CHECK(!session.SourceForFrame(Tuple(), AllGates(), SourceStamp{}, 2000));
    CHECK(session.active());
    const auto oldest = session.SourceForFrame(Tuple(), AllGates(), Source(1000), 2000);
    CHECK(oldest && Equal(oldest->source, Source(1000)));
    CHECK(session.ShouldRecordSource(*oldest, false, 2000));
    CHECK(!session.ShouldRecordSource(*oldest, true, 2001));
    Session zero; Arm(zero, Tuple(), 30000, 128, 0);
    CHECK(zero.SourceForFrame(Tuple(), AllGates(), Source(0), 1000));
    CHECK(!zero.SourceForFrame(Tuple(), AllGates(), Source(0), 1001));
    auto frame = session.SourceForFrame(Tuple(), AllGates(), Source(2000), 2000);
    CHECK(frame && frame->source.generation == 3 && frame->source.generation != Tuple().epoch);
    CHECK(frame->source.sequence == 1);
}

void MeaningfulRawEdges() {
    Session session; Arm(session);
    auto source = Source();
    auto frame = Frame(session, source);
    CHECK(session.ShouldRecordSource(frame, false, 1000));
    CHECK(!session.ShouldRecordSource(frame, false, 1000));
    for (unsigned i = 0; i < 64; ++i) {
        ++source.sequence;
        ++source.lx; --source.ly; ++source.rx; --source.ry;
        source.capturedTick = 1001 + i;
        frame = Frame(session, source, source.capturedTick);
        CHECK(!session.ShouldRecordSource(frame, false, source.capturedTick));
    }
    ++source.generation; frame = Frame(session, source, 1064);
    CHECK(session.ShouldRecordSource(frame, false, 1064));
    source.buttons ^= 0x8000; frame = Frame(session, source, 1064);
    CHECK(session.ShouldRecordSource(frame, false, 1064));
    ++source.leftTrigger; frame = Frame(session, source, 1064);
    CHECK(session.ShouldRecordSource(frame, false, 1064));
    ++source.rightTrigger; frame = Frame(session, source, 1064);
    CHECK(session.ShouldRecordSource(frame, false, 1064));
    CHECK(session.ShouldRecordSource(frame, true, 1064));
    CHECK(!session.ShouldRecordSource(frame, true, 1064));
    CHECK(session.ShouldRecordSource(frame, false, 1064));
    CHECK(session.active());
    FrameMetadata wrong = frame; wrong.sessionId = "wrong-session";
    CHECK(!session.ShouldRecordSource(wrong, true, 1064));
    CHECK(!session.ShouldRecordSource(frame, false, 1064));
}

void FinalOutputEdgesAreNotPhysicalBits() {
    Session session; Arm(session);
    const uint32_t heldOutput = 0x80001234u;
    auto raw = Source();
    auto frame = Frame(session, raw);
    CHECK(session.ShouldRecordSource(frame, false, 1000, heldOutput));
    CHECK(!session.ShouldRecordSource(frame, false, 1000, heldOutput));
    // Release-barrier/UI-neutral changes output while raw remains held.
    // semanticNeutral is unchanged to isolate the FINAL-output edge key.
    ++raw.sequence; raw.capturedTick = 1001;
    frame = Frame(session, raw, 1001);
    CHECK(session.ShouldRecordSource(frame, false, 1001, 0));
    CHECK(frame.source.sequence == 2 && frame.source.buttons == 0x1234);
    CHECK(frame.source.leftTrigger == 14 && frame.source.rightTrigger == 31);
    CHECK(frame.source.lx == -32768 && frame.source.ly == 32767);
    ++raw.sequence; raw.capturedTick = 1002;
    frame = Frame(session, raw, 1002);
    CHECK(session.ShouldRecordSource(frame, false, 1002, heldOutput));
    CHECK(frame.source.sequence == 3 && frame.source.buttons == 0x1234);
    CHECK(session.ShouldRecordSource(frame, false, 1002, heldOutput ^ 0x80000000u));
    CHECK(session.ShouldRecordSource(frame, true, 1002, heldOutput ^ 0x80000000u));
    CHECK(!session.ShouldRecordSource(frame, true, 1002, heldOutput ^ 0x80000000u));
    CHECK(session.ShouldRecordSource(frame, true, 1002, 0));
    raw.buttons = 0; ++raw.sequence; frame = Frame(session, raw, 1002);
    CHECK(session.ShouldRecordSource(frame, true, 1002, 0));
    raw.leftTrigger = 0; raw.rightTrigger = 0; frame = Frame(session, raw, 1002);
    CHECK(session.ShouldRecordSource(frame, true, 1002, 0));
}

void RecordCapacity() {
    for (uint32_t capacity : {8u, 128u}) {
        Session session; Arm(session, Tuple(), 30000, capacity);
        auto source = Source();
        for (uint32_t i = 0; i < capacity; ++i) {
            auto frame = Frame(session, source);
            CHECK(session.ShouldRecordSource(frame, false, 1000, i));
            CHECK(session.active() == (i + 1 < capacity));
        }
        CHECK(!session.SourceForFrame(Tuple(), AllGates(), source, 1000));
        CHECK(!session.ShouldRecordSource({"explicit-session", source}, true, 1000, 999));
        session.ObserveHello(1);
        CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
        Arm(session, Tuple(), 30000, capacity, 1000, "new-explicit-budget");
        CHECK(session.ShouldRecordSource(Frame(session, source), false, 1000, 0));
    }
}

void NativeAttemptsAreAfterBackend() {
    Session session; Arm(session, Tuple(), 30000);
    for (unsigned i = 0; i < 5000; ++i) CHECK(session.SourceForFrame(Tuple(), AllGates(), Source(), 1000));
    unsigned fakeBackendCalls = 0, records = 0;
    for (unsigned i = 0; i < 4100; ++i) {
        const auto metadata = session.SourceForFrame(Tuple(), AllGates(), Source(), 1000);
        ++fakeBackendCalls;
        if (metadata && session.ShouldRecordSource(*metadata, false, 1000)) ++records;
        if (i == 4094) CHECK(session.active());
        if (i >= 4095) CHECK(!session.active());
    }
    CHECK(records == 1 && fakeBackendCalls == 4100);
    CHECK(!session.OfferPrepare(Tuple(), AllGates(), 1000));
    Session lastAllowedEdge; Arm(lastAllowedEdge, Tuple(), 30000);
    auto frame = Frame(lastAllowedEdge);
    CHECK(lastAllowedEdge.ShouldRecordSource(frame, false, 1000));
    for (unsigned i = 1; i < 4095; ++i) CHECK(!lastAllowedEdge.ShouldRecordSource(frame, false, 1000));
    CHECK(lastAllowedEdge.active());
    CHECK(lastAllowedEdge.ShouldRecordSource(frame, true, 1000));
    CHECK(!lastAllowedEdge.active());
    Session invalidAttempts; Arm(invalidAttempts, Tuple(), 30000);
    for (unsigned i = 0; i < 4096; ++i)
        CHECK(!invalidAttempts.ShouldRecordSource({"foreign", Source()}, false, 1000));
    CHECK(!invalidAttempts.active());
}

void ClearCapabilityAndNoAutomaticRearm() {
    Session pending;
    CHECK(pending.Request("survives-initial-clear")); pending.ObserveHello(1);
    pending.ClearHost();
    CHECK(pending.version() == 0 && !pending.active());
    CHECK(!pending.OfferPrepare(Tuple(), AllGates(), 1000));
    pending.ObserveHello(2);
    CHECK(pending.version() == 2 && !pending.OfferPrepare(Tuple(), AllGates(), 1000));
    pending.ObserveHello(1);
    CHECK(!pending.active());
    CHECK(pending.OfferPrepare(Tuple(), AllGates(), 1000));
    pending.ClearHost(); pending.ObserveHello(1);
    CHECK(!pending.OfferPrepare(Tuple(), AllGates(), 1000));
    CHECK(!pending.AcceptPrepare(Tuple(), true, "survives-initial-clear", 1000));
    Session active; Arm(active);
    const auto old = Frame(active);
    active.ClearHost(); active.ObserveHello(1);
    CHECK(!active.ShouldRecordSource(old, false, 1000));
    CHECK(!active.SourceForFrame(Tuple(), AllGates(), Source(), 1000));
    CHECK(!active.OfferPrepare(Tuple(), AllGates(), 1000));
    Arm(active, Tuple(), 10000, 128, 1001, "intentional-after-clear");
    CHECK(!active.ShouldRecordSource(old, false, 1001));
    CHECK(active.ShouldRecordSource(Frame(active, Source(1001), 1001), false, 1001));
    Session closedPending;
    CHECK(closedPending.Request("cancelled")); closedPending.Close(); closedPending.ObserveHello(1);
    CHECK(!closedPending.OfferPrepare(Tuple(), AllGates(), 1000));
    CHECK(closedPending.Request("explicit-after-cancel"));
    for (unsigned unsupported : {0u, 2u, 3u, (std::numeric_limits<unsigned>::max)()}) {
        Session offered;
        CHECK(offered.Request("offered-cap")); offered.ObserveHello(1);
        CHECK(offered.OfferPrepare(Tuple(), AllGates(), 1000));
        offered.ObserveHello(unsupported); offered.ObserveHello(1);
        CHECK(!offered.AcceptPrepare(Tuple(), true, "offered-cap", 1000));
        CHECK(!offered.OfferPrepare(Tuple(), AllGates(), 1000));
        Session armed; Arm(armed); armed.ObserveHello(unsupported);
        CHECK(armed.version() == unsupported && !armed.active());
        armed.ObserveHello(1);
        CHECK(!armed.OfferPrepare(Tuple(), AllGates(), 1000));
    }
    Session repeatedHello; Arm(repeatedHello); repeatedHello.ObserveHello(1);
    CHECK(repeatedHello.active());
    CHECK(repeatedHello.SourceForFrame(Tuple(), AllGates(), Source(), 1000));
}

void OwnedValuesAndNoexceptAllocationFailures() {
    Binding binding = Tuple(); binding.runId = std::string(120, 'R');
    const std::string id(96, 'S');
    std::optional<PrepareAuthorization> survivingOffer;
    std::optional<FrameMetadata> survivingFrame;
    {
        Session owner;
        CHECK(owner.Request(id)); owner.ObserveHello(1);
        survivingOffer = owner.OfferPrepare(binding, AllGates(), 1000);
        CHECK(survivingOffer && survivingOffer->binding.runId == binding.runId);
        CHECK(owner.AcceptPrepare(binding, true, id, 1000));
        auto source = Source();
        survivingFrame = owner.SourceForFrame(binding, AllGates(), source, 1000);
        source.buttons = 0; source.generation = 999;
        binding.runId = "caller-mutated";
        owner.Close();
    }
    CHECK(survivingOffer && survivingOffer->binding.runId == std::string(120, 'R'));
    CHECK(survivingFrame && survivingFrame->sessionId == id && Equal(survivingFrame->source, Source()));
    binding = Tuple(); binding.runId = std::string(120, 'R');
    unsigned allocationFailures = 0;
    bool sawSuccess = false;
    for (long fail = 0; fail < 8 && !sawSuccess; ++fail) {
        Session owner;
        CHECK(owner.Request(id)); owner.ObserveHello(1);
        allocationFake::failAfter = fail;
        auto offer = owner.OfferPrepare(binding, AllGates(), 1000);
        allocationFake::failAfter = -1;
        if (!offer) {
            ++allocationFailures;
            CHECK(!owner.active());
            CHECK(!owner.OfferPrepare(binding, AllGates(), 1000));
            CHECK(owner.Request("explicit-after-allocation-failure"));
        } else {
            sawSuccess = true;
            CHECK(owner.AcceptPrepare(binding, true, id, 1000));
        }
    }
    CHECK(sawSuccess && allocationFailures >= 3);
    Session frameFailure; Arm(frameFailure, binding, 10000, 128, 1000, id);
    allocationFake::failAfter = 0;
    auto missing = frameFailure.SourceForFrame(binding, AllGates(), Source(), 1000);
    allocationFake::failAfter = -1;
    CHECK(!missing && !frameFailure.active());
    CHECK(!frameFailure.OfferPrepare(binding, AllGates(), 1000));
    Session noAllocation;
    std::string moveId = id;
    allocationFake::failAfter = 0;
    const bool requested = noAllocation.Request(std::move(moveId));
    noAllocation.ObserveHello(1);
    allocationFake::failAfter = -1;
    CHECK(requested);
    CHECK(noAllocation.OfferPrepare(binding, AllGates(), 1000));
    allocationFake::failAfter = 0;
    const bool accepted = noAllocation.AcceptPrepare(binding, true, id, 1000);
    const bool recorded = noAllocation.ShouldRecordSource(*survivingFrame, false, 1000, 0x1234);
    noAllocation.Close(); noAllocation.ClearHost();
    allocationFake::failAfter = -1;
    CHECK(accepted && recorded && !noAllocation.active() && noAllocation.version() == 0);
}

struct FakeOutput {
    uint32_t buttons;
    int16_t lx, ly, rx, ry;
    uint8_t leftTrigger, rightTrigger;
};
bool Equal(const FakeOutput& a, const FakeOutput& b) {
    return a.buttons == b.buttons && a.lx == b.lx && a.ly == b.ly && a.rx == b.rx && a.ry == b.ry &&
        a.leftTrigger == b.leftTrigger && a.rightTrigger == b.rightTrigger;
}

// Backend/control/logger operations are plain local values/functions. The
// exception fence models main's responsibility, never edits main/Host code.
void FakeControlAndLoggerAreIndependent() {
    Session session; Arm(session, Tuple(), 30000, 8);
    unsigned backendCalls = 0, nativeRecords = 0, loggerFailures = 0;
    const auto fakeLogger = [] { throw std::runtime_error("fake logger failure"); };
    for (uint32_t i = 0; i < 20; ++i) {
        const FakeOutput output{i & 1u, 17, -41, 23, -12, 7, 8};
        const auto before = output;
        auto raw = Source(1000 + i); raw.sequence = i + 1;
        const auto metadata = session.SourceForFrame(Tuple(), AllGates(), raw, 1000 + i);
        const FakeOutput submitted = output; ++backendCalls;
        if (metadata && session.ShouldRecordSource(*metadata, false, 1000 + i, submitted.buttons)) {
            CHECK(Equal(metadata->source, raw));
            ++nativeRecords;
            try { fakeLogger(); } catch (...) { ++loggerFailures; }
        }
        CHECK(Equal(output, before) && Equal(submitted, before));
    }
    CHECK(backendCalls == 20 && nativeRecords == 8 && loggerFailures == 8 && !session.active());
    Session neutral; Arm(neutral);
    auto actual = Source();
    auto metadata = neutral.SourceForFrame(Tuple(), AllGates(), actual, 1000);
    CHECK(metadata && neutral.ShouldRecordSource(*metadata, true, 1000, 0));
    CHECK(metadata->source.buttons != 0 && metadata->source.leftTrigger != 0);
    const FakeOutput callerNeutral{0, 0, 0, 0, 0, 0, 0};
    const auto unknown = neutral.SourceForFrame(Tuple(), AllGates(), SourceStamp{}, 1000);
    const auto submittedNeutral = callerNeutral; ++backendCalls;
    CHECK(!unknown && Equal(callerNeutral, submittedNeutral));
    CHECK(neutral.active());
    Gates nonMsi = AllGates(); nonMsi.knownMsiBoard = false;
    CHECK(!neutral.SourceForFrame(Tuple(), nonMsi, actual, 1000));
    ++backendCalls;
    CHECK(backendCalls == 22);
}

struct Test { const char* name; void (*run)(); };
}

int main() {
    const Test tests[] = {
        {"default-OFF-no-lifecycle-intent", DefaultOff},
        {"safe-ASCII-ID-duration-capacity-boundaries", RequestValidation},
        {"cannot-replace-one-shot-explicit-new-intent", CannotReplaceAndOneShot},
        {"all-32-gate-combinations-and-active-drop-fences", GateTruthTable},
        {"complete-exact-tuple-wrong-unarmed-duplicate-ACK", CompleteTupleAndAck},
        {"late-ACK-ACK-started-TTL-and-clock-regression", LateAckAndDuration},
        {"uint64-deadline-overflow-and-exclusive-boundary", OverflowSafe},
        {"source-age-null-provenance-no-invented-identity", SourceFreshnessAndNullProvenance},
        {"raw-digital-triggers-generation-semantic-neutral-no-axes-edges", MeaningfulRawEdges},
        {"FINAL-output-edges-preserve-held-raw-and-precise-sequence", FinalOutputEdgesAreNotPhysicalBits},
        {"exact-8-and-128-record-budgets-close-diagnostic-only", RecordCapacity},
        {"4096-Native-attempts-only-after-fake-backend", NativeAttemptsAreAfterBackend},
        {"initial-clear-capability-unsupported-no-auto-rearm", ClearCapabilityAndNoAutomaticRearm},
        {"owning-values-single-owner-noexcept-allocation-fences", OwnedValuesAndNoexceptAllocationFailures},
        {"fake-control-neutral-logger-failures-remain-independent", FakeControlAndLoggerAreIndependent}
    };
    unsigned passed = 0;
    for (const auto& test : tests) {
        try {
            test.run(); ++passed;
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception& error) {
            allocationFake::failAfter = -1;
            std::cerr << "FAIL " << test.name << ": " << error.what() << '\n';
        } catch (...) {
            allocationFake::failAfter = -1;
            std::cerr << "FAIL " << test.name << ": unknown fake-test exception\n";
        }
    }
    constexpr auto count = sizeof(tests) / sizeof(tests[0]);
    std::cout << "PURE_FAKE_SUMMARY passed=" << passed << " total=" << count
        << " assertions=" << assertions << " hardwareAccess=false productRun=false SDK=false\n";
    return passed == count ? 0 : 1;
}
