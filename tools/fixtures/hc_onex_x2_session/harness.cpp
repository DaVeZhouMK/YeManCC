#include "onex_x2_session.h"
#include "expected.generated.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

namespace p = ymcc::onex_x2;
using Time = std::uint64_t;
static std::uint64_t checks = 0;
static std::string scenario;
static void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) throw std::runtime_error(scenario + ": " + message);
}
template<class A, class B> static void eq(A actual, B expected, const std::string& message) {
    ++checks;
    bool equal;
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        using Common = std::common_type_t<A, B>;
        equal = static_cast<Common>(actual) == static_cast<Common>(expected);
    } else equal = actual == expected;
    if (!equal) {
        std::ostringstream text;
        text << scenario << ": " << message;
        if constexpr ((std::is_arithmetic_v<A> || std::is_enum_v<A>) &&
                      (std::is_arithmetic_v<B> || std::is_enum_v<B>))
            text << " actual=" << static_cast<unsigned long long>(actual)
                 << " expected=" << static_cast<unsigned long long>(expected);
        throw std::runtime_error(text.str());
    }
}
static const p::Context baseContext{7, 19};
static p::Model modelOf(const oracle::Model& m) { return static_cast<p::Model>(m.tag); }
static p::TransportContract contract(p::InputLayout layout = p::InputLayout::LogicalFrame,
                                     std::size_t in = 64, std::size_t out = 64) {
    return {layout, in, out};
}
static void attach(p::Session& s, const oracle::Model& m, p::TransportContract c = contract(), Time at = 0) {
    s.request(modelOf(m), baseContext, at);
    require(s.canBind(at), "supported selection can bind");
    require(s.bind(c, at), "verified contract binds");
    const auto snap = s.snapshot();
    eq(snap.phase, p::Phase::Live, "bound live");
    eq(snap.requested, modelOf(m), "request preserved");
    eq(snap.bound, modelOf(m), "bound exact model");
    eq(snap.context, baseContext, "bound context");
    eq(snap.generation, 1, "first bind creates generation one");
    eq(snap.publicationRevision, 1, "first bind publication revision one");
    eq(snap.validFrames, 0, "initial bind has no valid samples");
    eq(snap.invalidFrames, 0, "initial bind has no invalid samples");
    eq(snap.inputLayout, c.inputLayout, "snapshot reports explicit input layout");
    eq(snap.inputBytes, c.inputBytes, "snapshot input capacity");
    eq(snap.outputBytes, c.outputBytes, "snapshot output capacity");
}
static std::vector<std::uint8_t> encoded(const oracle::Init& step, std::size_t size) {
    // Independently builds the fixed C# writer frame; never calls encodeReport.
    std::vector<std::uint8_t> bytes(size, 0);
    const std::size_t offset = size == 65 ? 1u : 0u;
    bytes[offset] = static_cast<std::uint8_t>(step.command);
    bytes[offset + oracle::markerIndex] = static_cast<std::uint8_t>(oracle::marker);
    bytes[offset + 2] = 1;
    std::copy(step.payload.begin(), step.payload.end(), bytes.begin() +
              static_cast<std::ptrdiff_t>(offset + oracle::payloadIndex));
    bytes[offset + 62] = static_cast<std::uint8_t>(oracle::marker);
    bytes[offset + 63] = static_cast<std::uint8_t>(step.command);
    return bytes;
}
static void expectWrite(const p::Session& s, const oracle::Init& step, std::size_t size) {
    const auto expected = encoded(step, size);
    eq(s.writeSize(), size, "wire write length");
    require(std::equal(expected.begin(), expected.end(), s.writeBuffer()), "full fixed-source page bytes");
}
static std::vector<std::uint8_t> frame(p::TransportContract c, unsigned command,
                                      unsigned id, unsigned state) {
    std::vector<std::uint8_t> bytes(c.inputBytes, 0);
    const std::size_t offset = c.inputLayout == p::InputLayout::LogicalFrame ? 0u : 1u;
    require(bytes.size() >= offset + 14, "fixture frame has logical minimum");
    bytes[offset + oracle::commandIndex] = static_cast<std::uint8_t>(command);
    bytes[offset + oracle::markerIndex] = static_cast<std::uint8_t>(oracle::marker);
    bytes[bytes.size() - oracle::markerFromEnd] = static_cast<std::uint8_t>(oracle::marker);
    if (command == oracle::statusCommand) bytes[offset + oracle::statusIndex] = static_cast<std::uint8_t>(state);
    else {
        bytes[offset + oracle::buttonIndex] = static_cast<std::uint8_t>(id);
        bytes[offset + oracle::pressedIndex] = static_cast<std::uint8_t>(state);
    }
    return bytes;
}
static p::Operation beginFrame(p::Session& s, const std::vector<std::uint8_t>& bytes) {
    const auto op = s.beginRead();
    require(op.has_value(), "read ticket acquired");
    require(bytes.size() <= 512, "fixture stays within input storage");
    std::copy(bytes.begin(), bytes.end(), s.readBuffer());
    return *op;
}
static void deliver(p::Session& s, p::TransportContract c, unsigned id, unsigned state, Time now) {
    const auto op = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), id, state));
    require(s.completeRead(op, true, c.inputBytes, now), "input completion accepted");
}
static void status(p::Session& s, p::TransportContract c, unsigned value, Time now) {
    const auto op = beginFrame(s, frame(c, static_cast<unsigned>(oracle::statusCommand), 0, value));
    require(s.completeRead(op, true, c.inputBytes, now), "status completion accepted");
}
static unsigned idFor(const oracle::Model& m, unsigned role) {
    for (unsigned id = 0; id < 256; ++id) if (m.roles[id] == role) return id;
    throw std::runtime_error("fixed-source role missing");
}
static std::string fingerprint(const p::Snapshot& x) {
    std::ostringstream out;
    out << unsigned(x.requested) << ',' << unsigned(x.bound) << ',' << x.context.power << ',' << x.context.topology
        << ',' << unsigned(x.phase) << ',' << unsigned(x.reason) << ',' << x.generation << ',' << x.publicationRevision
        << ',' << x.retryAt << ',' << x.readPending << ',' << x.writePending << ',' << x.retainedNotice
        << ',' << x.initialized << ',' << x.initIndex << ',' << x.queuedEvents << ',' << x.reinitializations
        << ',' << unsigned(x.rearBits) << ',' << x.validFrames << ',' << x.invalidFrames
        << ',' << x.reportIdObserved << ',' << unsigned(x.lastReportId)
        << ',' << unsigned(x.inputLayout) << ',' << x.inputBytes << ',' << x.outputBytes;
    return out.str();
}
static void eventEquals(const p::Event& e, p::EventKind kind, unsigned button, unsigned rear,
                         Time now, unsigned vendorId, Time generation = 1, p::Context context = baseContext) {
    eq(e.kind, kind, "event kind");
    eq(static_cast<unsigned>(e.button), button, "independent source role");
    eq(static_cast<unsigned>(e.rearBits), rear, "semantic rear state");
    eq(e.generation, generation, "event generation from independent expected binding");
    eq(e.context, context, "event source context");
    eq(e.capturedAt, now, "event timestamp");
    eq(static_cast<unsigned>(e.vendorButtonId), vendorId, "event preserves exact vendor button ID");
}
static Time runPages(p::Session& s, const oracle::Model& m, std::size_t size, Time firstDue) {
    Time now = firstDue;
    for (std::size_t i = 0; i < m.init.size(); ++i) {
        const auto op = s.beginWrite(now);
        require(op.has_value(), "each expected page starts at its due time");
        expectWrite(s, m.init[i], size);
        require(!s.beginWrite(now), "only one pending write");
        require(!s.snapshot().initialized, "pending submission cannot initialize");
        const Time terminal = now + 7; // Real completion, not mere submission.
        require(s.completeWrite(*op, true, size, terminal), "exact terminal write accepted");
        eq(s.snapshot().initIndex, i + 1, "terminal advances exactly one page");
        require(!s.snapshot().initialized, "completion needs terminal gate tick");
        now = terminal + m.init[i].delay;
        if (m.init[i].delay) {
            s.advance(now - 1);
            require(!s.snapshot().initialized, "last page delay also honored");
            require(!s.beginWrite(now - 1), "no page before source delay");
        }
    }
    s.advance(now);
    require(s.snapshot().initialized, "only all successful terminal pages initialize");
    require(!s.beginWrite(now), "completed initialization has no excess page");
    return now;
}
static void constants() {
    scenario = "independent policy constants";
    eq(p::Session::insertionDelayMs, policy::insertionDelayMs, "4000ms insertion");
    eq(p::Session::writeTimeoutMs, policy::writeTimeoutMs, "250ms write timeout");
    eq(p::Session::retainedNoticeMs, policy::retainedNoticeMs, "1000ms retained notice");
    eq(p::Session::bindRetryMs, policy::bindRetryMs, "2000ms bind backoff");
    eq(p::Session::statusDelayMs, policy::statusDelayMs, "200ms status delay");
    eq(p::Session::reinitIntervalMs, policy::reinitIntervalMs, "2000ms reinit minimum");
    eq(p::Session::maxReinitializations, policy::maxReinitializations, "four retries");
    eq(p::Session::eventCapacity, policy::eventCapacity, "128-event bounded queue");
    eq(p::Session::maxInputBytes, policy::maxInputBytes, "512-byte input cap");
}
static void initialization() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u}) {
        scenario = std::string(m.cls) + " output=" + std::to_string(out);
        p::Session s;
        const auto c = contract(p::InputLayout::LogicalFrame, 64, out);
        constexpr Time boundAt = 123;
        attach(s, m, c, boundAt);
        for (const Time offset : {0ull, 1ull, 3998ull, 3999ull}) {
            require(!s.beginWrite(boundAt + offset), "initial 4000ms wait");
            require(!s.snapshot().initialized, "waiting cannot initialize");
        }
        const auto before = fingerprint(s.snapshot());
        s.request(modelOf(m), baseContext, boundAt + 2000);
        eq(fingerprint(s.snapshot()), before, "same request preserves initial deadline");
        runPages(s, m, out, boundAt + 4000);
        require(!s.popEvent(), "initialization emits no synthetic input");
    }
}
static void framing() {
    for (const auto& m : oracle::models) {
        for (const std::size_t out : {64u, 65u}) for (const std::size_t in : {0u, 13u, 14u, 15u, 64u, 65u, 512u, 513u}) {
            for (const auto layout : {p::InputLayout::Unverified, p::InputLayout::LogicalFrame,
                                      p::InputLayout::ZeroReportIdThenFrame, p::InputLayout::ReportIdThenFrame, static_cast<p::InputLayout>(255)}) {
                scenario = std::string(m.cls) + " layout=" + std::to_string(unsigned(layout)) +
                    " in=" + std::to_string(in) + " out=" + std::to_string(out);
                p::Session s;
                s.request(modelOf(m), baseContext, 0);
                const auto before = fingerprint(s.snapshot());
                const bool valid = in <= 512 && ((layout == p::InputLayout::LogicalFrame && in >= 14) ||
                    ((layout == p::InputLayout::ZeroReportIdThenFrame || layout == p::InputLayout::ReportIdThenFrame) && in >= 15));
                eq(s.bind(contract(layout, in, out), 0), valid, "explicit input admission independent of output");
                if (!valid) eq(fingerprint(s.snapshot()), before, "rejected contract has no state changes");
                else eq(s.readSize(), in, "read exact agreed input length");
            }
        }
        for (const std::size_t out : {0u, 1u, 14u, 63u, 66u, 512u}) {
            p::Session s;
            s.request(modelOf(m), baseContext, 0);
            require(!s.bind(contract(p::InputLayout::LogicalFrame, 64, out), 0), "invalid output rejected");
        }
        for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ZeroReportIdThenFrame, p::InputLayout::ReportIdThenFrame})
            for (const std::size_t in : {64u, 65u, 512u}) for (const std::size_t out : {64u, 65u}) {
                const auto c = contract(layout, in, out);
                p::Session s;
                attach(s, m, c);
                deliver(s, c, idFor(m, 4), 1, 10);
                auto e = s.popEvent();
                require(e.has_value(), "declared layout decodes rear press");
                eventEquals(*e, p::EventKind::RearChanged, 4, 1, 10, idFor(m, 4));
                // Deliberately encode the opposite layout at the same length.
                auto wrong = c;
                wrong.inputLayout = layout == p::InputLayout::LogicalFrame ?
                    p::InputLayout::ZeroReportIdThenFrame : p::InputLayout::LogicalFrame;
                const auto invalidBefore = s.snapshot().invalidFrames;
                const auto op = beginFrame(s, frame(wrong, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 5), 1));
                require(s.completeRead(op, true, in, 11), "wrong-layout completion is observed");
                eq(s.snapshot().invalidFrames, invalidBefore + 1, "opposite layout not guessed");
                require(!s.popEvent(), "wrong layout has no publication");
                eq(s.snapshot().rearBits, 1, "wrong layout leaves rear state");
            }
    }
}
static void inputSemantics() {
    for (const auto& m : oracle::models) for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ZeroReportIdThenFrame, p::InputLayout::ReportIdThenFrame}) {
        scenario = std::string(m.cls) + " all roles layout=" + std::to_string(unsigned(layout));
        const auto c = contract(layout, 65, 64);
        p::Session s;
        attach(s, m, c);
        Time at = 10;
        unsigned expectedRear = 0; // Independent reference state, never seeded from candidate snapshot.
        for (unsigned id = 0; id < 256; ++id) {
            const unsigned role = m.roles[id];
            const bool click = std::find(oracle::clickRoles.begin(), oracle::clickRoles.end(), role) != oracle::clickRoles.end();
            for (const unsigned state : {1u, 1u, 0u, 0u, 2u, 255u}) {
                const unsigned oldRear = expectedRear;
                const unsigned bit = role == 4 ? 1u : 2u;
                const unsigned nextRear = role == 4 || role == 5 ?
                    (state == 1 ? oldRear | bit : oldRear & ~bit) : oldRear;
                deliver(s, c, id, state, at);
                const bool isRear = role == 4 || role == 5;
                const bool changed = oldRear != nextRear;
                const bool expectedEvent = click ? state == 1 : isRear && (changed || state != 1);
                const auto expectedKind = click ? p::EventKind::Click : changed ? p::EventKind::RearChanged : p::EventKind::RearObserved;
                const auto event = s.popEvent();
                eq(event.has_value(), expectedEvent, "all IDs/press values event cardinality");
                if (event) eventEquals(*event, expectedKind, role, nextRear, at, id);
                require(!s.popEvent(), "one input at most one event");
                eq(s.snapshot().rearBits, nextRear, "held/released rear mask");
                expectedRear = nextRear;
                ++at;
            }
        }
        deliver(s, c, idFor(m, 4), 1, at++);
        deliver(s, c, idFor(m, 5), 1, at++);
        eq(s.snapshot().rearBits, 3, "two independent rear bits");
        deliver(s, c, idFor(m, 4), 0, at++);
        eq(s.snapshot().rearBits, 2, "left release retains right");
        deliver(s, c, idFor(m, 5), 0, at++);
        eq(s.snapshot().rearBits, 0, "both rear released");
        while (s.popEvent()) {}
        const auto invalidBefore = s.snapshot().invalidFrames;
        for (unsigned bad = 0; bad < 6; ++bad) {
            auto bytes = frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 1), 1);
            const std::size_t offset = layout == p::InputLayout::LogicalFrame ? 0u : 1u;
            std::size_t completed = c.inputBytes;
            switch (bad) {
            case 0: --completed; break;
            case 1: ++completed; break;
            case 2: bytes[offset] = 0; break;
            case 3: bytes[offset + oracle::markerIndex] ^= 1; break;
            case 4: bytes[bytes.size() - oracle::markerFromEnd] ^= 1; break;
            default: completed = 513; break;
            }
            const auto op = beginFrame(s, bytes);
            require(s.completeRead(op, true, completed, at++), "invalid payload completes ticket");
            require(!s.popEvent(), "invalid payload no event");
        }
        eq(s.snapshot().invalidFrames, invalidBefore + 6, "invalid frame observations");
        eq(s.snapshot().phase, p::Phase::Live, "malformed observation does not detach good transport");
        if (layout == p::InputLayout::ZeroReportIdThenFrame) {
            auto bytes = frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 1), 1);
            bytes[0] = 1;
            const auto op = beginFrame(s, bytes);
            require(s.completeRead(op, true, c.inputBytes, at), "nonzero report ID observed");
            require(!s.popEvent(), "nonzero report ID not accepted as verified zero prefix");
            eq(s.snapshot().invalidFrames, invalidBefore + 7, "nonzero report ID invalid count");
        }
    }
}
static void queueCapacity() {
    for (const auto& m : oracle::models) for (const bool rear : {false, true}) {
        scenario = std::string(m.cls) + (rear ? " rear queue" : " click queue");
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const auto origin = s.snapshot();
        const unsigned id = idFor(m, rear ? 4u : 1u);
        for (unsigned i = 0; i < 128; ++i) {
            deliver(s, c, id, rear ? (i % 2 == 0 ? 0u : 1u) : 1u, 10 + i);
            eq(s.snapshot().queuedEvents, i + 1, "all 128 queue entries retained");
            eq(s.snapshot().phase, p::Phase::Live, "capacity itself not overflow");
        }
        // Duplicate DOWN is idempotent; redundant actual UP now requires RearObserved.
        if (rear) {
            deliver(s, c, id, 1, 139);
            eq(s.snapshot().queuedEvents, 128, "full-queue duplicate rear DOWN idempotent");
            eq(s.snapshot().phase, p::Phase::Live, "duplicate does not trigger overflow");
        }
        deliver(s, c, id, rear ? 0u : 1u, 140);
        const auto dead = s.snapshot();
        eq(dead.reason, p::StopReason::QueueFull, "129th required event revokes source");
        eq(dead.phase, p::Phase::Detached, "no outstanding I/O can detach immediately");
        eq(dead.queuedEvents, 0, "overflow drops untrusted publication backlog");
        eq(dead.rearBits, 0, "overflow revokes held rear bits");
        eq(dead.generation, origin.generation + 1, "overflow changes generation");
        eq(dead.publicationRevision, origin.publicationRevision + 1, "overflow publishes revocation");
        require(!s.popEvent(), "overflow cannot leak old event");
        require(!s.canBind(2139), "overflow backoff before 2s");
        require(s.canBind(2140), "overflow rebind at 2s");
    }
    // Ring wraps repeatedly without coalescing continuous front clicks.
    for (const auto& m : oracle::models) {
        scenario = std::string(m.cls) + " FIFO ring wrap";
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        struct ExpectedClick { unsigned role, vendorId; Time at; };
        std::vector<ExpectedClick> expected;
        std::vector<unsigned> clickIds;
        for (unsigned id = 0; id < 256; ++id)
            if (std::find(oracle::clickRoles.begin(), oracle::clickRoles.end(), m.roles[id]) != oracle::clickRoles.end())
                clickIds.push_back(id);
        require(!clickIds.empty(), "fixed-source model has front click inputs");
        std::size_t consumed = 0;
        for (unsigned round = 0; round < 12; ++round) {
            for (unsigned i = 0; i < 80; ++i) {
                const unsigned id = clickIds[i % clickIds.size()];
                const unsigned role = m.roles[id];
                const Time at = 100 + round * 80 + i;
                deliver(s, c, id, 1, at);
                expected.push_back({role, id, at});
            }
            for (unsigned i = 0; i < 80; ++i) {
                const auto e = s.popEvent();
                require(e.has_value(), "front clicks retained without coalescing");
                eventEquals(*e, p::EventKind::Click, expected[consumed].role, 0, expected[consumed].at, expected[consumed].vendorId);
                ++consumed;
            }
            require(!s.popEvent(), "ring drained exactly");
            eq(s.snapshot().phase, p::Phase::Live, "wrap not overflow");
        }
    }
}
static void invalidTickets() {
    for (const auto& m : oracle::models) {
        scenario = std::string(m.cls) + " invalid and duplicate tokens";
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const auto read = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 4), 1));
        const auto write = s.beginWrite(4000);
        require(write.has_value(), "pending initialization writer");
        for (const auto op : {p::Operation{}, p::Operation{read.generation + 1, read.sequence},
                              p::Operation{read.generation, read.sequence + 9}, *write}) {
            const auto before = fingerprint(s.snapshot());
            require(!s.completeRead(op, true, 64, 4001), "invalid or write ticket cannot complete read");
            eq(fingerprint(s.snapshot()), before, "bad read ticket leaves full state unchanged");
        }
        for (const auto op : {p::Operation{}, p::Operation{write->generation + 1, write->sequence},
                              p::Operation{write->generation, write->sequence + 9}, read}) {
            const auto before = fingerprint(s.snapshot());
            require(!s.completeWrite(op, true, 64, 4001), "invalid or read ticket cannot complete write");
            eq(fingerprint(s.snapshot()), before, "bad write ticket leaves full state unchanged");
        }
        require(s.completeRead(read, true, 64, 4001), "real read terminal accepted once");
        auto before = fingerprint(s.snapshot());
        require(!s.completeRead(read, true, 64, 4002), "duplicate read ignored");
        eq(fingerprint(s.snapshot()), before, "duplicate read leaves queue and counters");
        require(s.completeWrite(*write, true, 64, 4002), "real write terminal accepted once");
        before = fingerprint(s.snapshot());
        require(!s.completeWrite(*write, true, 64, 4003), "duplicate write ignored");
        eq(fingerprint(s.snapshot()), before, "duplicate write cannot advance init index");
        const auto nextRead = s.beginRead();
        const auto nextWrite = s.beginWrite(4052);
        require(nextRead.has_value() && nextWrite.has_value(), "new tickets after terminals");
        require(!(read == *nextRead) && !(*write == *nextWrite), "tickets not reused inside generation");
        before = fingerprint(s.snapshot());
        require(!s.completeRead(read, false, 0, 4053), "old read cannot cancel new read");
        require(!s.completeWrite(*write, false, 0, 4053), "old write cannot cancel new write");
        eq(fingerprint(s.snapshot()), before, "late duplicates leave new operations untouched");
    }
}
static void retirementMatrix() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u})
        for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ReportIdThenFrame})
        for (unsigned change = 0; change < 4; ++change) for (unsigned order = 0; order < 2; ++order)
            for (unsigned readResult = 0; readResult < 3; ++readResult) for (unsigned writeResult = 0; writeResult < 3; ++writeResult) {
                scenario = std::string(m.cls) + " retire=" + std::to_string(change) + " order=" +
                    std::to_string(order) + " read=" + std::to_string(readResult) + " write=" + std::to_string(writeResult);
                p::Session s;
                const auto c = contract(layout, 64, out);
                attach(s, m, c);
                deliver(s, c, idFor(m, 4), 1, 3998);
                const auto before = s.snapshot();
                const auto read = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 5), 1));
                const auto write = s.beginWrite(4000);
                require(write.has_value(), "both read/write outstanding");
                const auto* readStorage = s.readBuffer();
                const auto* writeStorage = s.writeBuffer();
                const std::vector<std::uint8_t> writeBytes(writeStorage, writeStorage + out);
                auto nextContext = baseContext;
                if (change == 0) s.stop(p::StopReason::ObservationFailed, 4002);
                else if (change == 1) s.request(p::Model::Unsupported, nextContext, 4002);
                else if (change == 2) { ++nextContext.power; s.request(modelOf(m), nextContext, 4002); }
                else { ++nextContext.topology; s.request(modelOf(m), nextContext, 4002); }
                const auto retired = s.snapshot();
                eq(retired.phase, p::Phase::Retaining, "outstanding I/O must retain ownership");
                require(retired.readPending && retired.writePending, "retire keeps both tickets");
                eq(retired.generation, before.generation + 1, "retire revokes old generation");
                eq(retired.publicationRevision, before.publicationRevision + 1, "revocation revision");
                eq(retired.rearBits, 0, "revoke held source");
                eq(retired.queuedEvents, 0, "drop old queued events");
                eq(retired.context, nextContext, "requested new context visible");
                require(!s.popEvent() && !s.beginRead() && !s.beginWrite(4003), "retained source cannot submit/publish");
                auto cancel = s.takeCancellation();
                require(cancel.read && cancel.write, "one cancellation per outstanding direction");
                cancel = s.takeCancellation();
                require(!cancel.read && !cancel.write, "cancellation is not repeated");
                s.advance(5001);
                require(!s.snapshot().retainedNotice, "notice not before 1000ms");
                s.advance(5002);
                require(s.snapshot().retainedNotice, "1000ms is a diagnostic notice");
                s.advance(9000);
                eq(s.snapshot().phase, p::Phase::Retaining, "elapsed time alone never frees pending operations");
                require(!s.canBind(9000), "cannot rebind over retained storage");
                eq(s.readBuffer(), readStorage, "read storage remains valid");
                eq(s.writeBuffer(), writeStorage, "write storage remains valid");
                require(std::equal(writeBytes.begin(), writeBytes.end(), s.writeBuffer()), "retained output buffer unchanged");
                const auto oldIndex = s.snapshot().initIndex;
                const auto oldValid = s.snapshot().validFrames;
                const auto oldInvalid = s.snapshot().invalidFrames;
                for (unsigned terminal = 0; terminal < 2; ++terminal) {
                    const bool isRead = (terminal == 0) == (order == 0);
                    if (isRead) require(s.completeRead(read, readResult != 1, readResult == 2 ? 63 : 64, 9001 + terminal), "old read terminal retires only");
                    else require(s.completeWrite(*write, writeResult != 1, writeResult == 2 ? out - 1 : out, 9001 + terminal), "old write terminal retires only");
                    require(!s.popEvent(), "stale completion cannot publish input");
                    eq(s.snapshot().initIndex, oldIndex, "old successful write cannot advance initialization");
                    eq(s.snapshot().validFrames, oldValid, "old data not counted as current frame");
                    eq(s.snapshot().reportIdObserved, retired.reportIdObserved, "old raw callback cannot update report observation");
                    eq(s.snapshot().lastReportId, retired.lastReportId, "old raw callback cannot update observed report ID");
                    eq(s.snapshot().invalidFrames, oldInvalid, "old transport errors not attributed to new context");
                    eq(s.snapshot().phase, terminal == 0 ? p::Phase::Retaining : p::Phase::Detached, "release only after both actual terminals");
                    cancel = s.takeCancellation();
                    require(!cancel.read && !cancel.write, "terminal order never reissues cancellation");
                }
                if (change == 1) {
                    require(!s.canBind(9003), "unsupported request blocks new session");
                    s.request(modelOf(m), baseContext, 9003);
                }
                require(s.canBind(9003), "completed retirement with elapsed backoff allows bind");
                require(s.bind(c, 9003), "new binding after actual completions");
                eq(s.snapshot().initIndex, 0, "new session starts first page");
                require(!s.snapshot().initialized, "new session not initialized by old write");
                eq(s.snapshot().validFrames, 0, "fresh binding has no new sample evidence");
                eq(s.snapshot().invalidFrames, 0, "fresh binding has no prior invalid counts");
                require(!s.snapshot().reportIdObserved, "fresh binding report ID unobserved");
                const auto newRead = s.beginRead();
                const auto newWrite = s.beginWrite(13003);
                require(newRead.has_value() && newWrite.has_value(), "new generation owns new operations");
                require(!(read == *newRead) && !(*write == *newWrite), "ticket values not reused across generations");
                const auto fresh = fingerprint(s.snapshot());
                require(!s.completeRead(read, true, 64, 13004), "very late old read ignored");
                require(!s.completeWrite(*write, true, out, 13004), "very late successful old write ignored");
                eq(fingerprint(s.snapshot()), fresh, "old callbacks cannot complete new initialization");
            }
}static void repeatedRetirement() {
    for (const auto& m : oracle::models) for (unsigned order = 0; order < 2; ++order) {
        scenario = std::string(m.cls) + " long retained repeated stop order=" + std::to_string(order);
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const auto read = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 4), 1));
        const auto write = s.beginWrite(4000);
        require(write.has_value(), "writer before retained sequence");
        s.stop(p::StopReason::ObservationFailed, 4002);
        const auto initial = s.snapshot();
        const auto firstCancellation = s.takeCancellation();
        require(firstCancellation.read && firstCancellation.write, "initial cancellations consumed");
        const auto newModel = modelOf(oracle::models[(m.tag % 3)]);
        p::Context desiredContext = baseContext;
        for (const Time now : {4003ull, 4100ull, 4999ull, 5001ull, 5002ull, 8000ull, 20000ull}) {
            s.stop(p::StopReason::ObservationFailed, now);
            s.stop(p::StopReason::ReadFailed, now);
            s.stop(p::StopReason::WriteFailed, now);
            ++desiredContext.power;
            ++desiredContext.topology;
            s.request(newModel, desiredContext, now);
            s.advance(now);
            const auto snap = s.snapshot();
            eq(snap.phase, p::Phase::Retaining, "repeated observation failure keeps ownership");
            eq(snap.generation, initial.generation, "retaining stop/request cannot bump generation repeatedly");
            eq(snap.publicationRevision, initial.publicationRevision, "one revocation, not repeated publication");
            eq(snap.retryAt, 6002, "first retirement backoff never postponed");
            eq(snap.reason, initial.reason, "first retirement reason retained");
            eq(snap.retainedNotice, now >= 5002, "notice uses original retirement deadline");
            eq(snap.requested, newModel, "latest desired model updates while retaining");
            eq(snap.context, desiredContext, "latest desired context updates while retaining");
            eq(snap.bound, modelOf(m), "old transport stays bound until terminals");
            require(snap.readPending && snap.writePending, "same two operations remain pending");
            require(!s.canBind(now) && !s.bind(c, now), "long retention never replaced by a new bind");
            const auto cancel = s.takeCancellation();
            require(!cancel.read && !cancel.write, "repeated stop/request never duplicates cancellation");
        }
        for (unsigned i = 0; i < 2; ++i) {
            if ((i == 0) == (order == 0)) require(s.completeRead(read, true, 64, 20001 + i), "original read ticket still accepted as terminal");
            else require(s.completeWrite(*write, true, 64, 20001 + i), "original write ticket still accepted as terminal");
            eq(s.snapshot().phase, i == 0 ? p::Phase::Retaining : p::Phase::Detached, "two actual completions release ownership");
            require(!s.popEvent(), "old callbacks never publish new-context input");
        }
        require(s.canBind(20003) && s.bind(c, 20003), "long-retained desired session binds without renewed backoff");
        eq(s.snapshot().bound, newModel, "latest requested model selected on rebind");
        eq(s.snapshot().context, desiredContext, "latest context carried into new session");
        require(!s.snapshot().initialized, "old completion does not initialize new model");
    }
}
static void writerTimeout() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u}) for (const bool readFirst : {false, true}) {
        scenario = std::string(m.cls) + " timeout out=" + std::to_string(out) + " readFirst=" + std::to_string(readFirst);
        p::Session s;
        const auto c = contract(p::InputLayout::LogicalFrame, 64, out);
        attach(s, m, c);
        const auto read = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 4), 1));
        const auto write = s.beginWrite(4000);
        require(write.has_value(), "timeout starts from real writer submission");
        const auto* storage = s.writeBuffer();
        const std::vector<std::uint8_t> bytes(storage, storage + out);
        s.advance(4249);
        eq(s.snapshot().phase, p::Phase::Live, "249ms writer still live");
        require(!s.snapshot().initialized, "pending write cannot initialize");
        s.advance(4250);
        eq(s.snapshot().phase, p::Phase::Retaining, "250ms timeout retains storage");
        eq(s.snapshot().reason, p::StopReason::WriteTimeout, "writer timeout reason");
        require(s.snapshot().writePending && s.snapshot().readPending, "timeout keeps exact pending tickets");
        auto cancel = s.takeCancellation();
        require(cancel.read && cancel.write, "timeout cancels both once");
        for (const Time now : {5249ull, 5250ull, 6250ull, 999999ull}) {
            s.advance(now);
            eq(s.snapshot().phase, p::Phase::Retaining, "no timeout based free of pending storage");
            eq(s.snapshot().retainedNotice, now >= 5250, "notice boundary");
            require(!s.canBind(now) && !s.beginRead() && !s.beginWrite(now), "no ticket reuse during retention");
            require(std::equal(bytes.begin(), bytes.end(), s.writeBuffer()), "writer memory retained after timeout");
            eq(s.writeBuffer(), storage, "same outstanding writer allocation");
            cancel = s.takeCancellation();
            require(!cancel.read && !cancel.write, "no repeated timeout cancellation");
        }
        for (unsigned i = 0; i < 2; ++i) {
            if ((i == 0) == readFirst) require(s.completeRead(read, true, 64, 1000000 + i), "timeout read terminal");
            else require(s.completeWrite(*write, true, out, 1000000 + i), "timeout late successful write terminal");
            eq(s.snapshot().phase, i == 0 ? p::Phase::Retaining : p::Phase::Detached, "all actual terminals needed");
            eq(s.snapshot().initIndex, 0, "late writer success after timeout cannot advance init");
            require(!s.popEvent(), "timeout old data cannot publish");
        }
        require(s.bind(c, 1000002), "new bind after timeout terminals");
        const auto newer = s.beginWrite(1004002);
        require(newer.has_value() && !(*newer == *write), "timeout ticket not reused");
        const auto before = fingerprint(s.snapshot());
        require(!s.completeWrite(*write, true, out, 1004003), "old timeout success cannot finish new first page");
        eq(fingerprint(s.snapshot()), before, "new first page remains pending");
    }
}
static void failedWritesAndBackoff() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u})
        for (unsigned mode = 0; mode < 5; ++mode) {
            scenario = std::string(m.cls) + " short/failed writer mode=" + std::to_string(mode) + " out=" + std::to_string(out);
            p::Session s;
            const auto c = contract(p::InputLayout::LogicalFrame, 64, out);
            attach(s, m, c);
            const auto op = s.beginWrite(4000);
            require(op.has_value(), "writer ready");
            const auto transferred = mode == 0 ? 0 : mode == 1 ? 1 : mode == 2 ? out - 1 : mode == 3 ? out + 1 : out;
            require(s.completeWrite(*op, mode != 4, transferred, 4001), "failed/short/oversized terminal observed");
            const auto dead = s.snapshot();
            eq(dead.phase, p::Phase::Detached, "terminal writer failure retires without retained I/O");
            eq(dead.reason, p::StopReason::WriteFailed, "short/failed write reason");
            eq(dead.initIndex, 0, "short write cannot advance page");
            require(!dead.initialized, "short write cannot initialize");
            eq(dead.retryAt, 6001, "retry starts at failure plus 2s");
            require(!s.canBind(6000) && !s.bind(c, 6000), "no early rebind");
            require(s.canBind(6001) && s.bind(c, 6001), "rebind at exact 2s deadline");
            require(!s.beginWrite(10000), "rebind insertion delay starts anew");
            const auto next = s.beginWrite(10001);
            require(next.has_value() && !(*next == *op), "fresh initialization owns fresh writer ticket");
            expectWrite(s, m.init.front(), out);
        }
    for (const auto& m : oracle::models) {
        scenario = std::string(m.cls) + " read failure pending writer";
        p::Session s;
        attach(s, m);
        const auto read = s.beginRead();
        const auto write = s.beginWrite(4000);
        require(read.has_value() && write.has_value(), "two operations active");
        require(s.completeRead(*read, false, 0, 4001), "read terminal failure observed");
        eq(s.snapshot().reason, p::StopReason::ReadFailed, "read failure retires");
        eq(s.snapshot().phase, p::Phase::Retaining, "read failure still retains writer");
        const auto cancel = s.takeCancellation();
        require(!cancel.read && cancel.write, "only unfinished direction cancelled");
        require(s.completeWrite(*write, true, 64, 4002), "other writer terminal");
        eq(s.snapshot().phase, p::Phase::Detached, "remaining actual terminal releases");
    }
}
static void statusReinitialization() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u}) {
        scenario = std::string(m.cls) + " status 0x20 retries out=" + std::to_string(out);
        p::Session s;
        const auto c = contract(p::InputLayout::LogicalFrame, 64, out);
        attach(s, m, c);
        status(s, c, 0x20, 100);
        require(!s.beginWrite(3999), "status before init cannot skip insertion wait");
        eq(s.snapshot().reinitializations, 0, "status before init not counted");
        Time now = runPages(s, m, out, 4000);
        for (unsigned value : {0x00u, 0x01u, 0x1fu, 0x21u, 0xffu}) {
            status(s, c, value, ++now);
            now += 500;
            s.advance(now);
            eq(s.snapshot().reinitializations, 0, "other statuses do not request reinit");
        }
        now += 501;
        Time lastReinit = 0;
        for (unsigned retry = 1; retry <= 4; ++retry) {
            const Time received = now + 1;
            status(s, c, 0x20, received);
            // The task contract is a 200ms debounce after the 2s minimum gate.
            const Time expectedStart = (retry == 1 ? received : std::max(received, lastReinit + 2000)) + 200;
            status(s, c, 0x20, received + 1); // A burst must not postpone existing due work.
            s.advance(received + 199);
            eq(s.snapshot().reinitializations, retry - 1, "no status reinit before 200ms");
            s.advance(expectedStart - 1);
            eq(s.snapshot().reinitializations, retry - 1, "2s min plus debounce not early");
            require(!s.beginWrite(expectedStart - 1), "no writer before retry deadline");
            s.advance(expectedStart);
            eq(s.snapshot().reinitializations, retry, "due status retry once");
            require(!s.snapshot().initialized, "retry revokes init completion until all pages return");
            eq(s.snapshot().initIndex, 0, "retry restarts from first page");
            lastReinit = expectedStart;
            now = runPages(s, m, out, expectedStart);
            require(!s.popEvent(), "status never produces fake button input");
        }
        for (unsigned i = 0; i < 8; ++i) {
            now += 3000;
            status(s, c, 0x20, now);
            s.advance(now + 2200);
            eq(s.snapshot().reinitializations, 4, "bounded four reinitializations");
            require(s.snapshot().initialized && !s.beginWrite(now + 2200), "fifth status cannot create write");
        }
        s.stop(p::StopReason::SelectionChanged, now + 2201);
        require(s.bind(c, now + 4201), "new binding allowed after cooldown");
        eq(s.snapshot().reinitializations, 0, "new session resets bounded retry budget");
        require(!s.beginWrite(now + 8200), "new binding still waits 4s");
    }
}
static void unknownModels() {
    scenario = "unknown models never request I/O";
    for (unsigned tag = 0; tag <= 255; ++tag) {
        bool known = false;
        for (const auto& m : oracle::models) if (m.tag == tag) known = true;
        if (known) continue;
        p::Session s;
        s.request(static_cast<p::Model>(tag), {5, 9}, 0);
        for (const Time now : {0ull, 4000ull, 100000ull, std::numeric_limits<Time>::max()}) {
            require(!s.canBind(now) && !s.bind(contract(), now), "unsupported selection cannot bind");
            require(!s.beginRead() && !s.beginWrite(now), "unsupported emits zero I/O requests");
            require(!s.popEvent(), "unsupported cannot publish");
            const auto snap = s.snapshot();
            eq(snap.requested, p::Model::Unsupported, "invalid tag normalized");
            eq(snap.generation, 0, "unknown no session generation");
        }
    }
}
static void rawReportIds() {
    for (const auto& m : oracle::models) for (const auto layout : {p::InputLayout::ReportIdThenFrame, p::InputLayout::ZeroReportIdThenFrame})
        for (const std::size_t rawSize : {64u, 65u}) for (const std::size_t out : {64u, 65u}) {
            scenario = std::string(m.cls) + " raw=" + std::to_string(rawSize) + " out=" + std::to_string(out) +
                " layout=" + std::to_string(unsigned(layout));
            const auto c = contract(layout, rawSize, out);
            p::Session s;
            attach(s, m, c);
            const auto origin = s.snapshot();
            require(!origin.reportIdObserved, "report ID absent before first sample");
            eq(origin.validFrames, 0, "binding has zero valid frames");
            eq(origin.invalidFrames, 0, "binding has zero invalid frames");
            std::uint64_t valid = 0, invalid = 0;
            for (unsigned reportId = 0; reportId <= 255; ++reportId) {
                auto bytes = frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 1), 1);
                bytes[0] = static_cast<std::uint8_t>(reportId);
                // raw64 -> logical63; raw65 -> logical64. Output remains independent.
                const auto op = beginFrame(s, bytes);
                require(s.completeRead(op, true, rawSize, reportId), "full raw terminal observed");
                const bool accepted = layout == p::InputLayout::ReportIdThenFrame || reportId == 0;
                if (accepted) ++valid; else ++invalid;
                const auto snap = s.snapshot();
                require(snap.reportIdObserved, "observed raw report ID retained");
                eq(static_cast<unsigned>(snap.lastReportId), reportId, "all 256 report IDs observed unchanged");
                eq(snap.validFrames, valid, "raw normalization valid count");
                eq(snap.invalidFrames, invalid, "strict-zero invalid count");
                const auto e = s.popEvent();
                eq(e.has_value(), accepted, "exactly one stripped prefix with no fallback/filter guesses");
                if (e) eventEquals(*e, p::EventKind::Click, 1, 0, reportId, idFor(m, 1));
            }
            runPages(s, m, out, 4000);
            // Actual invalid observations exist before rebind, even in any-ID mode.
            const auto badRead = s.beginRead();
            require(badRead.has_value(), "short raw read acquired");
            require(s.completeRead(*badRead, true, rawSize - 1, 4300), "short raw terminal observed");
            eq(s.snapshot().invalidFrames, invalid + 1, "short raw count before rebind");
            s.stop(p::StopReason::ObservationFailed, 4301);
            require(s.bind(c, 6301), "fresh raw binding after failure backoff");
            const auto fresh = s.snapshot();
            eq(fresh.validFrames, 0, "fresh binding cannot reuse previous sample evidence");
            eq(fresh.invalidFrames, 0, "invalid frame count is binding local");
            require(!fresh.reportIdObserved, "fresh raw binding clears observation flag");
            eq(fresh.lastReportId, 0, "fresh raw binding clears old report ID");
            const auto before = fingerprint(fresh);
            require(!s.completeRead(*badRead, true, rawSize, 6302), "old raw callback rejected after fresh bind");
            eq(fingerprint(s.snapshot()), before, "stale completion cannot establish fresh readiness");
        }
    // Minimum raw length admission uses capacity >=15, not a hardcoded 65.
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u}) {
        scenario = std::string(m.cls) + " raw minimum capacity";
        for (std::size_t size = 0; size < 15; ++size) {
            p::Session rejected;
            rejected.request(modelOf(m), baseContext, 0);
            require(!rejected.bind(contract(p::InputLayout::ReportIdThenFrame, size, out), 0), "raw capacity below logical minimum rejected");
        }
        p::Session s;
        const auto c = contract(p::InputLayout::ReportIdThenFrame, 15, out);
        attach(s, m, c);
        auto bytes = frame(c, static_cast<unsigned>(oracle::statusCommand), 0, 0x21);
        bytes[0] = 0xe7;
        const auto read = beginFrame(s, bytes);
        require(s.completeRead(read, true, 15, 1), "15-byte raw full status accepted");
        eq(s.snapshot().validFrames, 1, "minimum length status decoded");
        eq(s.snapshot().lastReportId, 0xe7, "minimum raw report ID retained");
        require(!s.popEvent(), "status at minimum length not a button");
    }
}
static void statusFinalPageBoundary() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u})
        for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ZeroReportIdThenFrame, p::InputLayout::ReportIdThenFrame})
            for (const std::size_t in : {64u, 65u}) for (const int relative : {-1, 0, 1}) {
                scenario = std::string(m.cls) + " status-final-page-boundary relative=" + std::to_string(relative) +
                    " layout=" + std::to_string(unsigned(layout)) + " in=" + std::to_string(in) + " out=" + std::to_string(out);
                p::Session s;
                const auto c = contract(layout, in, out);
                attach(s, m, c);
                // Independent due time from pinned HC page delays, not candidate state.
                Time due = 4000;
                for (const auto& page : m.init) due += page.delay;
                eq(due, 4150, "fixed source's final initialization due time");
                const bool zeroLastDelay = m.init.back().delay == 0;
                // For Mini due-1 must precede the zero-delay final terminal.
                // Do not complete a write at due and then send a time-reversed read.
                const bool holdFinalPage = zeroLastDelay && relative < 0;
                const std::size_t terminals = m.init.size() - (holdFinalPage ? 1u : 0u);
                Time writeAt = 4000;
                for (std::size_t i = 0; i < terminals; ++i) {
                    const auto write = s.beginWrite(writeAt);
                    require(write.has_value(), "source-timed initialization page submitted");
                    expectWrite(s, m.init[i], out);
                    require(s.completeWrite(*write, true, out, writeAt), "source page exact terminal completion");
                    eq(s.snapshot().initIndex, i + 1, "only actual completion advances prefix");
                    require(!s.snapshot().initialized, "no settlement tick after terminal");
                    writeAt += m.init[i].delay;
                }
                const Time received = relative < 0 ? due - 1 : due + static_cast<Time>(relative);
                // No advance/beginWrite between final terminal and this status read.
                // beginRead/completeRead are deliberately first at the boundary tick.
                status(s, c, 0x20, received);
                eq(s.snapshot().initialized, relative >= 0, "status settles initialization iff final terminal and due reached");
                eq(s.snapshot().reinitializations, 0, "status only schedules, never runs reinit immediately");
                eq(s.snapshot().initIndex, terminals, "status cannot manufacture a terminal write");
                require(!s.snapshot().writePending, "boundary status leaves no spurious writer");
                require(!s.popEvent(), "boundary status cannot emit button input");
                if (relative < 0) {
                    if (holdFinalPage) {
                        eq(writeAt, due, "Mini zero-delay page first becomes due at final boundary");
                        const auto finalWrite = s.beginWrite(due);
                        require(finalWrite.has_value(), "Mini final page is still required at due");
                        expectWrite(s, m.init.back(), out);
                        require(s.completeWrite(*finalWrite, true, out, due), "Mini final zero-delay terminal");
                        require(!s.snapshot().initialized, "zero delay still requires settlement after completion");
                    }
                    s.advance(due);
                    require(s.snapshot().initialized, "ordinary settlement after early status");
                    s.advance(received + 199);
                    require(!s.beginWrite(received + 199), "early status does not queue a delayed retry");
                    s.advance(received + 200);
                    eq(s.snapshot().reinitializations, 0, "due-1 status is not replayed after ready");
                    require(s.snapshot().initialized && !s.beginWrite(received + 200), "due-1 status leaves initialized session idle");
                    eq(s.snapshot().initIndex, m.init.size(), "ignored early status preserves all final terminals");
                } else {
                    const Time scheduled = received + 200;
                    s.advance(scheduled - 1);
                    eq(s.snapshot().reinitializations, 0, "due/due+1 request waits full 200ms");
                    require(s.snapshot().initialized, "init remains ready before scheduled retry");
                    require(!s.beginWrite(scheduled - 1), "no early retry writer at 199ms");
                    s.advance(scheduled);
                    eq(s.snapshot().reinitializations, 1, "first boundary status retained for exactly +200ms retry");
                    require(!s.snapshot().initialized, "scheduled retry requires fresh page terminals");
                    eq(s.snapshot().initIndex, 0, "boundary retry restarts page zero");
                    const auto retryWrite = s.beginWrite(scheduled);
                    require(retryWrite.has_value(), "scheduled first page available exactly at +200ms");
                    expectWrite(s, m.init.front(), out);
                    require(s.completeWrite(*retryWrite, true, out, scheduled), "retry first page terminal");
                    eq(s.snapshot().initIndex, 1, "retry terminal counted once");
                }
            }
    // A status completion is not a substitute for advance: it must not execute
    // the writer watchdog while a genuine initialization write is still pending.
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u}) {
        scenario = std::string(m.cls) + " boundary status does not move writer watchdog out=" + std::to_string(out);
        p::Session s;
        const auto c = contract(p::InputLayout::ReportIdThenFrame, 65, out);
        attach(s, m, c);
        const auto writer = s.beginWrite(4000);
        require(writer.has_value(), "real writer before watchdog boundary");
        status(s, c, 0x20, 4250);
        eq(s.snapshot().phase, p::Phase::Live, "status does not run the 250ms watchdog");
        require(s.snapshot().writePending && !s.snapshot().initialized, "pending writer prevents status settlement");
        eq(s.snapshot().initIndex, 0, "pending writer has no terminal evidence");
        eq(s.snapshot().reinitializations, 0, "pending init status not a retry");
        s.advance(4250);
        eq(s.snapshot().phase, p::Phase::Retaining, "owner advance still executes watchdog");
        eq(s.snapshot().reason, p::StopReason::WriteTimeout, "watchdog keeps original timeout meaning");
        const auto cancellation = s.takeCancellation();
        require(!cancellation.read && cancellation.write, "watchdog cancels only actual pending direction");
        require(s.completeWrite(*writer, true, out, 4251), "late timeout terminal accepted for retirement only");
        eq(s.snapshot().phase, p::Phase::Detached, "watchdog storage releases after actual terminal");
        eq(s.snapshot().initIndex, 0, "status and late write cannot fake initialization");
    }
}
static void rearObservation() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u})
        for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ZeroReportIdThenFrame, p::InputLayout::ReportIdThenFrame})
            for (const std::size_t in : {64u, 65u}) {
                scenario = std::string(m.cls) + " actual redundant UP layout=" + std::to_string(unsigned(layout)) +
                    " in=" + std::to_string(in) + " out=" + std::to_string(out);
                p::Session s;
                const auto c = contract(layout, in, out);
                attach(s, m, c);
                const unsigned leftId = idFor(m, 4), rightId = idFor(m, 5);
                // Session produces evidence for the application fence, not the fence itself.
                deliver(s, c, leftId, 1, 10);
                auto event = s.popEvent();
                require(event.has_value(), "old-generation held DOWN has changed evidence");
                eventEquals(*event, p::EventKind::RearChanged, 4, 1, 10, leftId);
                const auto staleUp = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), leftId, 0));
                p::Context desired{8, 20};
                s.request(modelOf(m), desired, 20);
                require(!s.popEvent(), "context change cannot invent physical UP observation");
                require(s.completeRead(staleUp, true, in, 21), "old actual UP completes retirement only");
                require(!s.popEvent(), "old callback cannot unlock a new-context hold fence");
                require(s.bind(c, 2020), "new context binds after old terminal and backoff");
                eq(s.snapshot().generation, 3, "new observation generation independently expected");
                eq(s.snapshot().validFrames, 0, "new binding has no UP sample yet");
                require(!s.popEvent(), "binding reset itself cannot be physical UP evidence");
                // Both states are zero; only this new, successful read proves actual UP.
                deliver(s, c, leftId, 0, 2021);
                event = s.popEvent();
                require(event.has_value(), "actual redundant UP produces fence-unlock evidence");
                eventEquals(*event, p::EventKind::RearObserved, 4, 0, 2021, leftId, 3, desired);
                require(!s.popEvent(), "one actual UP produces exactly one observation");
                auto before = fingerprint(s.snapshot());
                require(!s.completeRead(staleUp, true, in, 2022), "duplicate stale callback remains ignored");
                eq(fingerprint(s.snapshot()), before, "duplicate stale UP has no state effect");
                // Observation must preserve the other held rear bit.
                deliver(s, c, rightId, 1, 2023);
                event = s.popEvent();
                require(event.has_value(), "other rear changes");
                eventEquals(*event, p::EventKind::RearChanged, 5, 2, 2023, rightId, 3, desired);
                deliver(s, c, leftId, 0, 2024);
                event = s.popEvent();
                require(event.has_value(), "redundant left UP observed while right held");
                eventEquals(*event, p::EventKind::RearObserved, 4, 2, 2024, leftId, 3, desired);
                deliver(s, c, rightId, 1, 2025);
                require(!s.popEvent(), "redundant DOWN emits neither changed nor observed");
                eq(s.snapshot().rearBits, 2, "redundant DOWN/other UP preserve rear state");
                deliver(s, c, rightId, 0, 2026);
                event = s.popEvent();
                require(event.has_value(), "held rear actual release is changed, not observed");
                eventEquals(*event, p::EventKind::RearChanged, 5, 0, 2026, rightId, 3, desired);
                deliver(s, c, rightId, 0, 2027);
                event = s.popEvent();
                require(event.has_value(), "following actual release stays observable");
                eventEquals(*event, p::EventKind::RearObserved, 5, 0, 2027, rightId, 3, desired);
                // A short physical read must not be upgraded to actual UP evidence.
                const auto shortUp = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), leftId, 0));
                require(s.completeRead(shortUp, true, in - 1, 2028), "short UP terminal observed as invalid");
                require(!s.popEvent(), "short UP cannot unlock fence");
                status(s, c, 0x20, 2029);
                require(!s.popEvent(), "status has no physical rear release evidence");
                deliver(s, c, idFor(m, 1), 0, 2030);
                require(!s.popEvent(), "front release is not a rear observation");
            }
    for (const auto& m : oracle::models) {
        scenario = std::string(m.cls) + " redundant UP bounded queue";
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const unsigned id = idFor(m, 4);
        for (unsigned i = 0; i < 128; ++i) {
            deliver(s, c, id, 0, 10 + i);
            eq(s.snapshot().queuedEvents, i + 1, "all actual redundant UP observations are retained");
            eq(s.snapshot().rearBits, 0, "observations do not manufacture held state");
        }
        // Drain one to check the kind, then refill: wrap must preserve observations.
        const auto first = s.popEvent();
        require(first.has_value(), "first redundant UP observable");
        eventEquals(*first, p::EventKind::RearObserved, 4, 0, 10, id);
        deliver(s, c, id, 0, 138);
        eq(s.snapshot().queuedEvents, 128, "observed event participates in ordinary queue accounting");
        deliver(s, c, id, 0, 139);
        eq(s.snapshot().reason, p::StopReason::QueueFull, "redundant UP overflow revokes source");
        eq(s.snapshot().queuedEvents, 0, "overflow clears all observation evidence");
        require(!s.popEvent(), "overflowed observations cannot leak");
    }
}
static void discardPreparedWrite() {
    for (const auto& m : oracle::models) for (const std::size_t out : {64u, 65u})
        for (const auto layout : {p::InputLayout::LogicalFrame, p::InputLayout::ReportIdThenFrame}) {
            scenario = std::string(m.cls) + " discard prepared pages layout=" + std::to_string(unsigned(layout)) + " out=" + std::to_string(out);
            p::Session s;
            const auto c = contract(layout, 65, out);
            attach(s, m, c);
            deliver(s, c, idFor(m, 4), 1, 3990);
            const auto bytes = frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 5), 1);
            const auto read = beginFrame(s, bytes);
            const auto* readStorage = s.readBuffer();
            Time writeAt = 4000;
            for (std::size_t i = 0; i < m.init.size(); ++i) {
                const auto prepared = s.beginWrite(writeAt);
                require(prepared.has_value(), "transport prepares current page without kernel submission");
                expectWrite(s, m.init[i], out);
                for (const auto bad : {p::Operation{}, read, p::Operation{prepared->generation + 1, prepared->sequence},
                                       p::Operation{prepared->generation, prepared->sequence + 100}}) {
                    const auto before = fingerprint(s.snapshot());
                    require(!s.discardPreparedWrite(bad), "invalid/wrong direction ticket cannot discard prepared write");
                    eq(fingerprint(s.snapshot()), before, "invalid discard leaves every snapshot field");
                }
                auto expectedAfter = s.snapshot();
                expectedAfter.writePending = false; // The only permitted observable change.
                require(s.discardPreparedWrite(*prepared), "matching Live/current prepared ticket withdrawn");
                eq(fingerprint(s.snapshot()), fingerprint(expectedAfter), "discard only clears prepared writer; no advance or read stop");
                eq(s.snapshot().initIndex, i, "discard cannot advance initialization page");
                eq(s.snapshot().rearBits, 1, "discard retains held rear");
                eq(s.snapshot().queuedEvents, 1, "discard retains queued input");
                require(s.snapshot().readPending, "read remains active throughout prepared withdrawal");
                eq(s.readBuffer(), readStorage, "discard does not replace read storage");
                require(std::equal(bytes.begin(), bytes.end(), s.readBuffer()), "discard does not alter pending read bytes");
                expectWrite(s, m.init[i], out);
                auto cancellation = s.takeCancellation();
                require(!cancellation.read && !cancellation.write, "prepared discard is not retirement/cancellation");
                auto before = fingerprint(s.snapshot());
                require(!s.discardPreparedWrite(*prepared), "withdrawn ticket cannot be discarded twice");
                require(!s.completeWrite(*prepared, true, out, writeAt), "withdrawn preparation cannot later complete a page");
                eq(fingerprint(s.snapshot()), before, "withdrawn duplicate callbacks cannot progress state");
                const auto replacement = s.beginWrite(writeAt);
                require(replacement.has_value(), "withdrawal does not add page delay or bind backoff");
                require(!(*replacement == *prepared), "replacement preparation gets a distinct ticket");
                expectWrite(s, m.init[i], out);
                before = fingerprint(s.snapshot());
                require(!s.discardPreparedWrite(*prepared), "old preparation cannot discard replacement writer");
                eq(fingerprint(s.snapshot()), before, "replacement writer remains owned");
                require(s.completeWrite(*replacement, true, out, writeAt + 3), "only replacement actual terminal advances page");
                eq(s.snapshot().initIndex, i + 1, "one terminal per source page");
                before = fingerprint(s.snapshot());
                require(!s.discardPreparedWrite(*replacement), "completed operation is no longer a preparation");
                eq(fingerprint(s.snapshot()), before, "discard after terminal has no effect");
                writeAt += 3 + m.init[i].delay;
            }
            s.advance(writeAt);
            require(s.snapshot().initialized && s.snapshot().readPending, "all actual pages initialize with original read still pending");
            require(s.completeRead(read, true, c.inputBytes, writeAt), "original read ticket remains valid after all withdrawals");
            const auto left = s.popEvent(), right = s.popEvent();
            require(left.has_value() && right.has_value(), "old queued event and later real input both preserved");
            eventEquals(*left, p::EventKind::RearChanged, 4, 1, 3990, idFor(m, 4));
            eventEquals(*right, p::EventKind::RearChanged, 5, 3, writeAt, idFor(m, 5));
            require(!s.popEvent(), "withdrawal emits no artificial event");
        }
    for (const auto& m : oracle::models) for (unsigned cause = 0; cause < 4; ++cause) {
        scenario = std::string(m.cls) + " reject retained write discard cause=" + std::to_string(cause);
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const auto read = beginFrame(s, frame(c, static_cast<unsigned>(oracle::buttonCommand), idFor(m, 4), 0));
        const auto write = s.beginWrite(4000);
        require(write.has_value(), "prepared writer before retirement");
        if (cause == 0) s.stop(p::StopReason::ObservationFailed, 4001);
        else if (cause == 1) s.request(modelOf(m), {8, 19}, 4001);
        else if (cause == 2) s.request(modelOf(m), {7, 20}, 4001);
        else s.request(modelOf(oracle::models[m.tag % 3]), baseContext, 4001);
        const auto before = fingerprint(s.snapshot());
        require(!s.discardPreparedWrite(*write), "retained old matching writer cannot be withdrawn");
        require(!s.discardPreparedWrite({s.snapshot().generation, write->sequence}), "forged current-generation old writer denied");
        eq(fingerprint(s.snapshot()), before, "retained ownership, notice and deadlines untouched");
        const auto cancellation = s.takeCancellation();
        require(cancellation.read && cancellation.write, "denied discard keeps both cancellation debts");
        require(s.completeRead(read, true, c.inputBytes, 4002), "old read actual terminal");
        eq(s.snapshot().phase, p::Phase::Retaining, "writer still needs real terminal after denied discard");
        require(s.completeWrite(*write, true, c.outputBytes, 4003), "old writer actual terminal");
        eq(s.snapshot().phase, p::Phase::Detached, "retained writer releases only at terminal");
        require(!s.discardPreparedWrite(*write), "Detached phase has no discardable preparation");
        require(s.bind(c, 6001), "fresh generation after retirement");
        const auto freshRead = s.beginRead();
        const auto freshWrite = s.beginWrite(10001);
        require(freshRead.has_value() && freshWrite.has_value(), "fresh operations exist");
        const auto fresh = fingerprint(s.snapshot());
        require(!s.discardPreparedWrite(*write), "old generation cannot withdraw new preparation");
        eq(fingerprint(s.snapshot()), fresh, "new generation prepared write untouched by stale discard");
    }
    for (const auto& m : oracle::models) {
        scenario = std::string(m.cls) + " withdrawal preserves watchdog ownership boundary";
        p::Session s;
        const auto c = contract();
        attach(s, m, c);
        const auto read = s.beginRead();
        const auto old = s.beginWrite(4000);
        require(read.has_value() && old.has_value(), "two outstanding tickets before withdrawal");
        require(s.discardPreparedWrite(*old), "unsubmitted writer withdrawn without advancing time");
        s.advance(4250);
        eq(s.snapshot().phase, p::Phase::Live, "withdrawn preparation cannot time out");
        require(s.snapshot().readPending, "owner tick retains unaffected original read");
        const auto replacement = s.beginWrite(4250);
        require(replacement.has_value(), "new preparation can start at old timeout tick");
        s.advance(4499);
        eq(s.snapshot().phase, p::Phase::Live, "replacement watchdog uses replacement start time");
        s.advance(4500);
        eq(s.snapshot().reason, p::StopReason::WriteTimeout, "replacement still has normal 250ms watchdog");
        require(!s.discardPreparedWrite(*replacement), "timeout-retained writer cannot be withdrawn");
        require(s.completeRead(*read, false, 0, 4501), "original read actual cancellation terminal");
        require(s.completeWrite(*replacement, false, 0, 4502), "replacement actual cancellation terminal");
        eq(s.snapshot().phase, p::Phase::Detached, "actual terminals retire timeout debt");
    }
}
struct Outcome { std::string name; bool passed; std::uint64_t checks; std::string error; };
static std::string json(const std::string& text) {
    std::ostringstream out;
    out << '"';
    for (const unsigned char c : text) {
        switch (c) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: if (c < 32) out << '?'; else out << static_cast<char>(c); break;
        }
    }
    return out.str() + '"';
}
int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"policy-constants", constants}, {"initialization", initialization}, {"explicit-framing", framing},
        {"rear-observation", rearObservation}, {"discard-prepared-write", discardPreparedWrite}, {"raw-report-ids", rawReportIds}, {"input-semantics", inputSemantics}, {"queue-capacity", queueCapacity}, {"invalid-tickets", invalidTickets},
        {"retirement-matrix", retirementMatrix}, {"repeated-retirement", repeatedRetirement},
        {"writer-timeout", writerTimeout}, {"write-failure-backoff", failedWritesAndBackoff},
        {"status-reinitialization", statusReinitialization}, {"status-final-page-boundary", statusFinalPageBoundary}, {"unknown-models", unknownModels},
    };
    std::vector<Outcome> outcomes;
    unsigned failed = 0;
    for (const auto& test : tests) {
        const auto before = checks;
        try { test.second(); outcomes.push_back({test.first, true, checks - before, ""}); }
        catch (const std::exception& error) { ++failed; outcomes.push_back({test.first, false, checks - before, error.what()}); }
    }
    std::cout << "{\"status\":" << json(failed ? "fail" : "pass") << ",\"checks\":" << checks
              << ",\"groups\":" << tests.size() << ",\"failedGroups\":" << failed << ",\"results\":[";
    for (std::size_t i = 0; i < outcomes.size(); ++i) {
        if (i) std::cout << ',';
        const auto& x = outcomes[i];
        std::cout << "{\"name\":" << json(x.name) << ",\"status\":" << json(x.passed ? "pass" : "fail")
                  << ",\"checks\":" << x.checks << ",\"error\":" << json(x.error) << '}';
    }
    std::cout << "]}\n";
    return failed ? 1 : 0;
}