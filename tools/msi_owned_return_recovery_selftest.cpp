#include "../native/msi_owned_return_recovery.h"
#include "../native/msi_owned_return_recovery.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <initializer_list>
#include <type_traits>

using ymcc::msi::ownedReturn::Recovery;
using Tick = std::uint64_t;

static_assert(std::is_nothrow_default_constructible_v<Recovery>);
static_assert(noexcept(Recovery{}.pending()));
static_assert(noexcept(Recovery{}.working()));
static_assert(noexcept(Recovery{}.failed()));
static_assert(noexcept(Recovery{}.completed()));
static_assert(noexcept(Recovery{}.begin(0, 1, true)));
static_assert(noexcept(Recovery{}.cancel()));
static_assert(noexcept(Recovery{}.due(0, 1, 1)));
static_assert(noexcept(Recovery{}.observeMatch(0, 1)));
static_assert(noexcept(Recovery{}.miss()));
static_assert(noexcept(Recovery{}.claimRetry(1500, true, true, true)));
static_assert(Recovery::observationIntervalMs == 1000);
static_assert(Recovery::stableMatchMs == 300);
static_assert(Recovery::retryDelayMs == 1500);
static_assert(Recovery::maxObservations == 20);

struct Checks {
    unsigned checks = 0;
    unsigned failures = 0;
    unsigned tests = 0;

    void require(bool condition, const char* message) {
        ++checks;
        if (!condition) {
            ++failures;
            std::printf("FAIL: %s\n", message);
        }
    }

    void run(const char* name, void (*test)(Checks&)) {
        ++tests;
        const unsigned before = failures;
        test(*this);
        std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", name);
    }
};

void exhaust(Checks& c, Recovery& r, Tick start, Tick topology, Tick power) {
    for (unsigned i = 0; i < Recovery::maxObservations; ++i) {
        const Tick now = start + static_cast<Tick>(i) * 1000;
        c.require(r.due(now, topology, power), "each of 20 spaced observations is granted");
        c.require(r.observations() == i + 1, "one due consumes exactly one observation");
        c.require(!r.due(now, topology, power), "same tick never claims a second observation");
        r.miss();
    }
    c.require(r.pending() && r.failed() && !r.working() && !r.completed(),
        "exhaustion becomes dormant while retaining the responsibility");
}

void idleAndZero(Checks& c) {
    Recovery r;
    c.require(!r.pending() && !r.working() && !r.failed() && !r.completed(), "default is idle");
    c.require(r.episode() == 0 && r.observations() == 0 && !r.retryUsed(), "default counters are zero");
    c.require(!r.begin(0, 0, true), "zero episode cannot begin");
    for (Tick now = 0; now < 10000; now += 100) {
        c.require(!r.due(now, now, 1), "idle topology events do not invent responsibility");
        c.require(!r.observeMatch(now, now + 1), "idle samples cannot complete");
        c.require(!r.claimRetry(now, true, true, true), "idle cannot retry");
    }
    r.miss();
    r.cancel();
    r.cancel();
    c.require(!r.pending() && r.episode() == 0, "idle cancellation is idempotent");
}

void stableSamples(Checks& c) {
    Recovery r;
    c.require(r.begin(100, 11, false), "fresh episode begins");
    c.require(r.due(100, 5, 9), "first observation is immediate");
    c.require(!r.observeMatch(100, 8), "one exact fresh sample does not complete");
    c.require(!r.observeMatch(400, 8), "same sequence after 300ms does not complete");
    c.require(r.observeMatch(400, 9), "different sequence at 300ms completes");
    c.require(r.completed() && !r.pending() && !r.working() && !r.failed(), "completion clears pending");
    c.require(!r.observeMatch(700, 10), "completion returns true only once");
    c.require(!r.claimRetry(5000, true, true, true), "completed responsibility cannot retry");
    c.require(!r.due(5000, 6, 10), "events cannot revive completed debt");
}

void stableInterval(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    c.require(!r.observeMatch(0, 100), "zero time is a valid first sample");
    c.require(!r.observeMatch(299, 101), "299ms is too short even with a new sequence");
    c.require(!r.observeMatch(300, 101), "reusing the too-early sample is not fresh evidence");
    c.require(r.observeMatch(300, 102), "a third fresh sample at 300ms completes");
}

void distinctNotOrdered(Checks& c) {
    Recovery r;
    r.begin(100, 7, false);
    c.require(!r.observeMatch(100, 900), "sequence establishes stable start");
    c.require(r.observeMatch(400, 3), "distinct sample identities need not be numerically increasing");
}

void missAndUnknownSample(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.observeMatch(0, 1);
    r.miss();
    c.require(!r.observeMatch(500, 2), "miss clears the earlier stable start");
    c.require(!r.observeMatch(799, 3), "new stable start requires another full 300ms");
    c.require(!r.observeMatch(800, 0), "unknown sample sequence cannot complete and breaks stability");
    c.require(!r.observeMatch(1000, 4), "sample after unknown starts a new pair");
    c.require(r.observeMatch(1300, 5), "fresh pair after miss completes");
}

void topologyInvalidates(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.due(0, 10, 20);
    r.observeMatch(0, 1);
    c.require(!r.due(300, 11, 20), "topology refresh honors the global one-second limit");
    c.require(r.observations() == 0 && r.working(), "event opens a new empty window");
    c.require(!r.observeMatch(300, 2), "rate-limited topology event still invalidates old stable start");
    c.require(!r.observeMatch(599, 3), "post-event pair cannot complete at 299ms");
    c.require(r.observeMatch(600, 4), "post-event fresh pair completes at 300ms");
}

void powerInvalidates(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.due(0, 10, 20);
    r.observeMatch(0, 1);
    c.require(r.due(1000, 10, 21), "nonzero power change opens a window");
    c.require(!r.observeMatch(1000, 2), "power change invalidates pre-event stability");
    c.require(r.observeMatch(1300, 3), "new power needs its own stable pair");
}

void unknownPower(Checks& c) {
    Recovery r;
    r.begin(0, 1, false);
    c.require(!r.due(0, 0, 0) && r.observations() == 0, "unknown power grants no observation");
    c.require(r.due(0, 0, 7), "zero topology is allowed with a nonzero power generation");
    r.observeMatch(0, 1);
    c.require(!r.due(300, 0, 0), "unknown power cannot act as a generation event");
    c.require(!r.observeMatch(300, 2), "unknown power also invalidates old stability");
    c.require(r.observeMatch(600, 3), "parent-validated new pair can complete");

    Recovery dormant;
    dormant.begin(0, 2, false);
    exhaust(c, dormant, 0, 4, 7);
    c.require(!dormant.due(20000, 4, 0), "unknown power cannot revive dormant debt");
    c.require(!dormant.due(21000, 4, 7) && dormant.failed(), "same known power after unknown is not an event");
    c.require(!dormant.due(22000, 5, 0) && dormant.failed(), "topology with unknown power is deferred");
    c.require(dormant.due(23000, 5, 7), "deferred topology is observed when power is known");
}

void windowBudget(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    exhaust(c, r, 0, 1, 1);
    const Tick limit = (std::numeric_limits<Tick>::max)();
    for (Tick now : {Tick{20000}, Tick{1000000}, limit}) {
        c.require(!r.due(now, 1, 1), "elapsed time alone never rearms a dormant window");
        c.require(r.pending() && r.failed() && r.observations() == 20,
            "dormant status and budget persist indefinitely");
    }
}

void lateCaller(Checks& c) {
    Recovery r;
    r.begin(0, 1, false);
    c.require(r.due(0, 1, 1), "initial observation granted");
    c.require(r.due(1000000, 1, 1), "late caller receives just one new observation");
    for (unsigned i = 0; i < 100; ++i)
        c.require(!r.due(1000000, 1, 1), "no catch-up burst after a late tick");
    c.require(r.observations() == 2, "late tick does not consume missed timer slots");
    c.require(!r.due(1000999, 1, 1), "rate limit measured from actual claim");
    c.require(r.due(1001000, 1, 1), "actual claim plus one second is due");
}

void eventWindowsKeepRetry(Checks& c) {
    Recovery r;
    r.begin(0, 7, true);
    c.require(r.claimRetry(1500, true, true, true), "first retry consumed before simulated I/O");
    exhaust(c, r, 1500, 8, 9);
    c.require(!r.due(20500, 9, 9), "event at previous probe time cannot cause a burst");
    c.require(r.working() && !r.failed() && r.pending(), "event revives observation only");
    c.require(r.retryUsed() && !r.claimRetry(20500, true, true, true), "topology cannot renew retry");
    exhaust(c, r, 21500, 9, 9);
    c.require(r.due(41500, 9, 10), "power event revives dormant observation");
    c.require(r.observations() == 1 && r.retryUsed(), "power window has its own probe count, same retry");
    c.require(!r.claimRetry(41500, true, true, true), "power cannot renew retry");
}

void eventStormSpacing(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    unsigned granted = 0;
    for (Tick now = 0; now <= 10000; ++now) {
        const bool due = r.due(now, now + 1, now + 1);
        if (due) ++granted;
        c.require(due == (now % 1000 == 0), "event storms cannot bypass one-second spacing");
    }
    c.require(granted == 11, "only eleven claims across ten seconds despite changing tokens");
}

void repeatBeginActive(Checks& c) {
    Recovery r;
    r.begin(0, 50, false);
    r.due(0, 2, 3);
    r.observeMatch(0, 1);
    c.require(!r.begin(200, 50, true), "same episode begin is a no-op");
    c.require(r.observations() == 1 && !r.due(200, 2, 3), "reentry preserves count and spacing");
    c.require(r.observeMatch(300, 2), "reentry does not erase stable samples");
    c.require(!r.claimRetry(1500, true, true, true), "reentry cannot elevate stored retry permission");

    Recovery allowed;
    allowed.begin(0, 51, true);
    c.require(!allowed.begin(1000, 51, false), "reentry does not change original allowed decision");
    c.require(allowed.claimRetry(1500, true, true, true), "original 1500ms origin is preserved");
}

void repeatBeginDormant(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    exhaust(c, r, 0, 2, 3);
    c.require(!r.begin(100000, 1, true), "repeated dormant begin cannot renew a window");
    c.require(r.failed() && r.observations() == 20 && !r.due(100000, 2, 3), "same episode stays dormant");
}

void repeatBeginCompleted(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.observeMatch(0, 1);
    r.observeMatch(300, 2);
    c.require(!r.begin(5000, 1, true) && r.completed(), "completed episode retains its tombstone");
    c.require(!r.pending() && !r.due(5000, 99, 99), "completion cannot be revived by begin or tokens");
}

void newEpisodeOnlyByBegin(Checks& c) {
    Recovery r;
    r.begin(0, 71, true);
    r.claimRetry(1500, true, true, true);
    exhaust(c, r, 1500, 10, 20);
    c.require(r.due(21500, 72, 72) && r.episode() == 71 && r.retryUsed(),
        "generation tokens never replace the debt episode");
    c.require(!r.begin(22000, 0, true) && r.episode() == 71, "zero does not replace a live episode");
    c.require(r.begin(22000, 72, true), "explicit different nonzero episode resets policy");
    c.require(r.episode() == 72 && r.observations() == 0 && !r.retryUsed() && r.pending(),
        "new episode has new budgets");
    c.require(r.due(22000, 72, 72), "new episode starts immediate observation");
    c.require(!r.claimRetry(23499, true, true, true), "new episode has its own 1500ms delay");
    c.require(r.claimRetry(23500, true, true, true), "new explicit episode may claim its retry once");
}

void cancellation(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.claimRetry(1500, true, true, true);
    r.due(1500, 5, 7);
    r.cancel();
    r.cancel();
    c.require(!r.pending() && !r.working() && !r.failed() && !r.completed(), "cancelled is inactive");
    c.require(r.retryUsed() && r.episode() == 1, "cancellation retains the episode and consumed retry");
    c.require(!r.begin(10000, 1, true), "cancelled old episode cannot be rearmed");
    c.require(!r.due(10000, 6, 8) && !r.observeMatch(10000, 1), "cancelled events and samples do nothing");
    c.require(!r.claimRetry(10000, true, true, true), "cancelled cannot retry");
    c.require(r.begin(10000, 2, true) && !r.retryUsed(), "only explicit new episode starts after cancel");
}

void zeroBeginDoesNotCancel(Checks& c) {
    Recovery r;
    r.begin(0, 8, true);
    r.due(0, 5, 7);
    r.observeMatch(0, 1);
    c.require(!r.begin(100000, 0, false), "zero begin is rejected unchanged");
    c.require(r.pending() && r.episode() == 8 && r.observations() == 1, "zero begin preserves debt and count");
    c.require(r.observeMatch(300, 2), "zero begin did not alter time or stability");
}

void retryAdmissionMatrix(Checks& c) {
    for (unsigned mask = 0; mask < 16; ++mask) {
        Recovery r;
        const bool allowed = (mask & 1u) != 0;
        const bool safe = (mask & 2u) != 0;
        const bool sameDinput = (mask & 4u) != 0;
        const bool unambiguous = (mask & 8u) != 0;
        r.begin(100, 1, allowed);
        const bool expected = mask == 15;
        c.require(r.claimRetry(1600, safe, sameDinput, unambiguous) == expected,
            "retry requires all four parent-supplied admission facts");
        c.require(r.retryUsed() == expected && r.pending(), "blocked retry consumes nothing and never clears debt");
        c.require(!r.claimRetry(1600, safe, sameDinput, unambiguous),
            "serialized duplicate call cannot claim a second retry");
        c.require(r.claimRetry(1700, true, true, true) == (allowed && !expected),
            "temporary unsafe, mismatch, or ambiguous facts may clear without elevating begin permission");
    }
}

void retryDelayAndOnce(Checks& c) {
    Recovery r;
    r.begin(500, 1, true);
    c.require(!r.claimRetry(1999, true, true, true) && !r.retryUsed(), "1499ms cannot claim");
    c.require(r.claimRetry(2000, true, true, true) && r.retryUsed(), "1500ms consumes before returning true");
    for (Tick now = 2000; now < 12000; ++now)
        c.require(!r.claimRetry(now, true, true, true), "repeated serialized callers have exactly one winner");
    c.require(r.pending() && !r.completed(), "retry itself is no completion proof");
}

void retryIoOutcomesNeverRefund(Checks& c) {
    // Simulate all return values without calling an SDK or performing I/O.
    for (unsigned outcome = 0; outcome < 8; ++outcome) {
        Recovery r;
        r.begin(0, outcome + 1, true);
        c.require(r.claimRetry(1500, true, true, true), "retry was consumed before simulated I/O result");
        const bool simulatedIoSuccess = (outcome & 1u) != 0;
        if (simulatedIoSuccess) r.miss();
        c.require(r.retryUsed() && r.pending() && !r.completed(), "I/O success, failure, or no signal cannot settle debt");
        c.require(!r.claimRetry(2500, true, true, true), "I/O result never refunds consumed retry");
        c.require(!r.begin(2500, outcome + 1, true), "result-path reentry cannot renew budget");
        r.due(2500, 1, 1);
        r.due(3500, 2, 2);
        c.require(!r.claimRetry(3500, true, true, true), "post-result event does not refund budget");
    }
}

void retryResetsSignal(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.observeMatch(0, 1);
    c.require(r.claimRetry(1500, true, true, true), "retry claimed");
    c.require(!r.observeMatch(1500, 2), "pre-retry signal cannot confirm post-retry return");
    c.require(r.observeMatch(1800, 3), "fresh stable pair after retry confirms");
}

void dormantRetryDoesNotRearm(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    exhaust(c, r, 0, 1, 1);
    c.require(r.claimRetry(20000, true, true, true), "existing lifecycle may claim still-unused retry with fresh safe facts");
    c.require(r.failed() && r.pending() && !r.due(20000, 1, 1), "retry never reopens a dormant observation window");
    c.require(!r.observeMatch(20000, 1) && r.observeMatch(20300, 2),
        "existing lifecycle fresh XInput evidence may settle dormant responsibility");
}

void finalObservationCanComplete(Checks& c) {
    Recovery r;
    r.begin(0, 1, false);
    for (unsigned i = 0; i < 19; ++i) {
        const Tick now = static_cast<Tick>(i) * 1000;
        c.require(r.due(now, 1, 1), "first nineteen probes granted");
        if (i == 18) r.observeMatch(now, 100);
        else r.miss();
    }
    c.require(r.due(19000, 1, 1) && r.failed(), "twentieth claim exhausts observation budget");
    c.require(r.observeMatch(19000, 101), "twentieth probe can still supply the confirming sample");
    c.require(r.completed() && !r.pending() && !r.failed(), "final probe completion replaces dormant status");
}

void rollbackDue(Checks& c) {
    Recovery r;
    r.begin(1000, 1, true);
    c.require(!r.due(999, 1, 1) && r.observations() == 0, "rollback before first observation is rejected");
    c.require(r.due(1000, 1, 1), "clock catches up without losing first observation");
    c.require(!r.due(500, 2, 2), "rollback does not process a generation event");
    c.require(!r.due(1000, 2, 2) && r.observations() == 0, "caught-up event still honors old observation rate");
    c.require(r.due(2000, 2, 2), "caught-up clock permits one observation after full second");
    for (unsigned i = 0; i < 50; ++i)
        c.require(!r.due(0, 100 + i, 100 + i), "repeated rollback calls cannot cause probe bursts");
    c.require(r.observations() == 1 && r.episode() == 1, "rollback does not reset budgets or episode");
}

void rollbackStability(Checks& c) {
    Recovery r;
    r.begin(1000, 1, false);
    r.observeMatch(1000, 1);
    c.require(!r.observeMatch(900, 2), "rollback sample cannot confirm");
    c.require(!r.observeMatch(1300, 3), "rollback invalidates earlier stable start");
    c.require(r.observeMatch(1600, 4), "caught-up clock requires a new stable pair");
}

void rollbackRetry(Checks& c) {
    Recovery r;
    r.begin(1000, 1, true);
    r.due(3000, 1, 1);
    c.require(!r.claimRetry(2500, true, true, true) && !r.retryUsed(), "rollback cannot consume retry via elapsed underflow");
    c.require(r.claimRetry(3000, true, true, true), "clock catch-up permits single delayed retry");
    c.require(!r.claimRetry(100, true, true, true) && r.retryUsed(), "rollback cannot refund retry");
}

void integerLimits(Checks& c) {
    const Tick top = (std::numeric_limits<Tick>::max)();
    Recovery r;
    r.begin(top - 2000, top, true);
    c.require(r.due(top - 2000, top, top), "large unsigned timestamps and generations are valid");
    c.require(!r.due(top - 1001, top, top), "999ms rate boundary at uint64 limit");
    c.require(r.due(top - 1000, top, top), "1000ms boundary at uint64 limit");
    c.require(!r.claimRetry(top - 501, true, true, true), "1499ms retry near uint64 limit is denied");
    c.require(r.claimRetry(top - 500, true, true, true), "1500ms retry near uint64 limit is granted");
    r.observeMatch(top - 300, 1);
    c.require(r.due(top, top, top), "no overflowing nextDue arithmetic");
    c.require(r.observeMatch(top, 2), "300ms stable sample near uint64 limit completes");

    Recovery end;
    end.begin(top, 1, true);
    c.require(end.due(top, 1, 1), "episode can begin at maximum timestamp");
    c.require(!end.due(0, 2, 2) && !end.claimRetry(0, true, true, true), "wrapped clock cannot trigger work");
    c.require(!end.claimRetry(top, true, true, true), "maximum timestamp cannot fake an elapsed delay");
}

void highWaterAcrossCalls(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    r.due(0, 1, 1);
    r.observeMatch(100, 1);
    c.require(!r.claimRetry(1400, false, true, true), "unsafe call records time but does not consume");
    c.require(!r.observeMatch(400, 2), "shared clock high-water rejects an older sample timestamp");
    c.require(!r.observeMatch(1400, 3), "time rollback resets stability across entry points");
    c.require(r.observeMatch(1700, 4), "new ordered pair can complete after shared-clock rollback");
}

void manyWindowsOneWrite(Checks& c) {
    Recovery r;
    r.begin(0, 1, true);
    unsigned writes = 0;
    Tick now = 0;
    for (Tick generation = 1; generation <= 32; ++generation) {
        for (unsigned i = 0; i < 20; ++i) {
            c.require(r.due(now, generation, generation), "each event window admits twenty observations");
            if (r.claimRetry(now, true, true, true)) ++writes;
            r.miss();
            c.require(r.pending() && !r.completed(), "missing XInput preserves debt across every window");
            now += 1000;
        }
        c.require(r.failed() && !r.working(), "each complete window becomes dormant");
        c.require(!r.due(now, generation, generation), "unchanged tokens cannot reopen an exhausted window");
        c.require(!r.begin(now, 1, true), "repeated lifecycle begin cannot reopen window");
    }
    c.require(writes == 1 && r.retryUsed() && r.pending(), "32 observation windows still produce exactly one retry claim");
}

void physicalSourceEventWithoutTopology(Checks& c) {
    Recovery r;r.begin(0,1,true);
    c.require(r.claimRetry(1500,true,true,true),"consume the episode retry before dormancy");
    exhaust(c,r,2000,7,9);
    c.require(!r.due(22000,7,9,0),"same source event stays dormant");
    c.require(r.due(23000,7,9,1),"physical source arrival alone resumes observation");
    c.require(!r.failed()&&r.working()&&r.retryUsed(),"source event cannot refund retry");
    c.require(!r.observeMatch(23000,100),"source event requires fresh stability evidence");
    c.require(!r.due(23100,7,9,2),"source event still respects global one-second rate");
    c.require(!r.observeMatch(23400,101),"new source event invalidates preceding first sample");
    c.require(r.due(24000,7,9,2),"second source-event window remains observable");
    c.require(r.observeMatch(24000,102),"two fresh samples after source event complete return");
    c.require(!r.claimRetry(25000,true,true,true),"completed/source events never add retries");
}
void sameObligationAfterCancelledTakeover(Checks& c) {
    Recovery r;
    c.require(!r.resumeCancelled(0,1,true),"no prior debt cannot be resumed");
    r.begin(0,1,true);r.due(0,3,7);
    c.require(!r.resumeCancelled(1,1,true),"active debt is not a cancelled takeover");
    c.require(r.claimRetry(1500,true,true,true),"initial retry consumed before new takeover");r.cancel();
    c.require(!r.resumeCancelled(2000,0,true)&&!r.resumeCancelled(2000,2,true),"resume cannot renumber the physical obligation");
    c.require(!r.resumeCancelled(1400,1,true),"clock rollback cannot resume");
    c.require(r.resumeCancelled(2000,1,true)&&r.retryUsed()&&r.episode()==1,"explicit same-debt resume preserves spent retry");
    c.require(!r.claimRetry(4000,true,true,true),"resume cannot refund any consumed write budget");
    c.require(!r.resumeCancelled(4000,1,true),"repeated OFF cannot reset active observation");
    c.require(r.due(4000,3,7),"new explicit OFF can observe the original debt");
    c.require(!r.observeMatch(4000,10)&&r.observeMatch(4400,11),"two real source receipts settle resumed debt");
    c.require(!r.resumeCancelled(5000,1,true),"completed debt cannot be resurrected");
    Recovery untrusted;untrusted.begin(0,3,false);untrusted.cancel();
    c.require(untrusted.resumeCancelled(2000,3,true),"an observation-only duty can resume");
    c.require(!untrusted.claimRetry(4000,true,true,true),"resume cannot increase previously denied write authority");
    Recovery reduced;reduced.begin(0,4,true);reduced.due(0,3,7);reduced.cancel();
    c.require(reduced.resumeCancelled(500,4,false),"ambiguous takeover can reduce permission while resuming observation");
    c.require(!reduced.due(500,3,7),"resume preserves the existing global observation spacing");
    c.require(!reduced.claimRetry(2000,true,true,true),"reduced permission cannot write");
}
static void durableCompletionAcknowledgement(Checks& c) {
    Recovery r;
    c.require(r.begin(10000,1,true),"staged match begins one owned obligation");
    c.require(!r.completeMatch(),"missing stable evidence cannot complete a duty");
    c.require(r.due(10000,1,1),"first staged read is due");
    c.require(!r.observeMatch(10000,1,false),"first staged match alone is insufficient");
    c.require(r.due(11000,1,1),"second staged read is due");
    c.require(r.observeMatch(11000,2,false),"second fresh window is ready for durable settlement");
    c.require(r.pending() && !r.completed(),"stable evidence does not end an uncommitted duty");
    c.require(r.due(12000,1,1) && r.observeMatch(12000,3,false),"failed external commit may settle on a later fresh window");
    c.require(r.completeMatch() && r.completed() && !r.pending(),"only explicit durable acknowledgement completes staged match");
    c.require(!r.completeMatch(),"completion acknowledgement is idempotently rejected");
    c.require(!r.begin(13000,1,true),"commit cannot renew the old responsibility");
}

void retryPreparationIsReadOnly(Checks& c) {
    Recovery r;
    c.require(noexcept(r.retryDue(0)), "read-only preparation gate is noexcept");
    c.require(!r.retryDue(100000), "idle preparation cannot create a responsibility");
    r.begin(500, 1, true);
    c.require(!r.retryDue(1999) && r.retryDue(2000), "preparation uses the original exact 1500ms deadline");
    for (unsigned i=0; i<1000; ++i)
        c.require(r.retryDue(100000), "repeated future preparation leaves eligibility intact");
    c.require(!r.retryUsed() && r.observations()==0 && r.working(), "preparation never consumes retry or observation");
    c.require(r.due(500, 1, 1), "future preparation does not advance the shared clock");
    c.require(!r.observeMatch(600, 1), "first physical sample remains staged");
    c.require(r.retryDue(100000), "future preparation can coexist with signal staging");
    c.require(r.observeMatch(900, 2) && r.completed(), "preparation cannot invalidate fresh signal stability");
    c.require(!r.retryDue(100000), "completed physical duty cannot prepare cleanup");
}
void retryPreparationAuthority(Checks& c) {
    Recovery denied;denied.begin(0, 1, false);
    c.require(!denied.retryDue(100000), "observation-only duty cannot prepare source release");
    Recovery cancelled;cancelled.begin(0, 2, true);cancelled.cancel();
    c.require(!cancelled.retryDue(100000), "cancelled takeover cannot prepare source release");
    c.require(cancelled.resumeCancelled(100, 2, true) && cancelled.retryDue(1500), "explicit same-duty resume retains the original deadline");
    c.require(cancelled.claimRetry(1500, true, true, true), "preparation did not claim the sole retry");
    c.require(!cancelled.retryDue(100000), "spent retry cannot prepare more cleanup");
    cancelled.cancel();cancelled.resumeCancelled(2000, 2, true);
    c.require(!cancelled.retryDue(100000), "same-duty resume cannot refund preparation authority");
    c.require(cancelled.begin(3000, 3, true) && !cancelled.retryDue(4499) && cancelled.retryDue(4500), "only a distinct duty has a fresh deadline");
}
void retryPreparationDormantWindow(Checks& c) {
    Recovery r;r.begin(0, 1, true);exhaust(c, r, 0, 7, 9);
    c.require(r.retryDue(20000), "dormant owned duty retains an unused eligible retry");
    for (unsigned i=0; i<1000; ++i)
        c.require(r.retryDue(20000+i), "repeated preparation does not change dormant state");
    c.require(r.failed() && r.observations()==20 && !r.retryUsed(), "preparation cannot renew observation or consume retry");
    c.require(!r.due(22000, 7, 9, 0), "elapsed time and preparation do not rearm a dormant observer");
    c.require(r.due(23000, 7, 9, 1) && r.retryDue(23000), "new physical source event wakes observation with same retry budget");
    c.require(r.claimRetry(23000, true, true, true) && !r.retryDue(23000), "only final guarded claim consumes preparation eligibility");
}
void retryPreparationClockLimits(Checks& c) {
    Recovery r;r.begin(500, 1, true);r.due(2500, 1, 1);
    c.require(!r.retryDue(2499) && r.retryDue(2500), "preparation respects the shared time high-water");
    c.require(!r.retryDue(0) && r.retryDue(2500), "rollback neither underflows delay nor changes eligibility");
    const Tick top=(std::numeric_limits<Tick>::max)();
    Recovery edge;edge.begin(top-2000, 2, true);
    c.require(!edge.retryDue(top-501) && edge.retryDue(top-500), "preparation deadline is safe near uint64 limit");
    c.require(!edge.retryDue(0) && edge.retryDue(top), "wrapped time cannot prepare but does not mutate the original duty");
    c.require(edge.claimRetry(top-500, true, true, true) && !edge.retryDue(top), "future read-only preparation cannot block an earlier valid claim");
}

int main() {
    Checks c;
    c.run("idle and zero episode", idleAndZero);
    c.run("distinct fresh samples and completion", stableSamples);
    c.run("minimum stable interval and duplicate rejection", stableInterval);
    c.run("sequence identities are distinct, not ordered", distinctNotOrdered);
    c.run("miss and unknown sequence reset stability", missAndUnknownSample);
    c.run("topology refresh invalidates stability even when rate limited", topologyInvalidates);
    c.run("power refresh invalidates stability", powerInvalidates);
    c.run("unknown power never grants or renews observation", unknownPower);
    c.run("twenty-probe budget retains dormant debt", windowBudget);
    c.run("late callers cannot catch up in bursts", lateCaller);
    c.run("event windows preserve consumed retry", eventWindowsKeepRetry);
    c.run("physical identity event without Windows topology", physicalSourceEventWithoutTopology);
    c.run("same physical duty after failed takeover", sameObligationAfterCancelledTakeover);
    c.run("event storms preserve global observation spacing", eventStormSpacing);
    c.run("active begin reentry preserves all policy", repeatBeginActive);
    c.run("dormant begin reentry cannot renew window", repeatBeginDormant);
    c.run("completed begin reentry cannot resurrect", repeatBeginCompleted);
    c.run("only explicit new episode resets budgets", newEpisodeOnlyByBegin);
    c.run("explicit cancellation and cancelled episode tombstone", cancellation);
    c.run("zero begin cannot cancel or mutate responsibility", zeroBeginDoesNotCancel);
    c.run("all retry admission combinations", retryAdmissionMatrix);
    c.run("retry delay and serialized single claimant", retryDelayAndOnce);
    c.run("simulated I/O results never refund retry or complete", retryIoOutcomesNeverRefund);
    c.run("retry invalidates previous signal stability", retryResetsSignal);
    c.run("dormant retry cannot renew observation", dormantRetryDoesNotRearm);
    c.run("twentieth observation may confirm completion", finalObservationCanComplete);
    c.run("observation clock rollback is bounded", rollbackDue);
    c.run("sample clock rollback invalidates stability", rollbackStability);
    c.run("retry clock rollback cannot underflow", rollbackRetry);
    c.run("uint64 timestamp and token limits", integerLimits);
    c.run("entry points share a clock high-water", highWaterAcrossCalls);
    c.run("many windows still allow only one write claim", manyWindowsOneWrite);
    c.run("stable evidence waits for durable acknowledgement", durableCompletionAcknowledgement);
    c.run("retry preparation is read-only and deadline-bound", retryPreparationIsReadOnly);
    c.run("retry preparation preserves authority and spent budget", retryPreparationAuthority);
    c.run("retry preparation cannot rearm a dormant window", retryPreparationDormantWindow);
    c.run("retry preparation clock rollback and integer limits", retryPreparationClockLimits);
#ifdef NDEBUG
    const char* configuration = "Release";
#else
    const char* configuration = "Checked";
#endif
    std::printf("{\"schemaVersion\":1,\"configuration\":\"%s\",\"tests\":%u,\"checks\":%u,"
                "\"failures\":%u,\"realDeviceIo\":0,\"hostExecuted\":false,\"sdkExecuted\":false}\n",
        configuration, c.tests, c.checks, c.failures);
    return c.failures == 0 ? 0 : 1;
}
