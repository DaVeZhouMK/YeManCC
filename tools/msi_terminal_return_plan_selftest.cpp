#include "../native/msi_terminal_return_plan.h"

#include <array>
#include <cstdint>
#include <iostream>
#include <limits>

namespace {
using ymcc::msi::terminalReturn::Facts;
using ymcc::msi::terminalReturn::Plan;

struct Checks {
    unsigned count = 0;
    unsigned failures = 0;

    void require(bool condition, const char* scenario) {
        ++count;
        if (!condition) {
            ++failures;
            std::cerr << "FAIL " << scenario << '\n';
        }
    }
};

Facts stoppedMsi() {
    Facts facts;
    facts.knownBoard = true;
    facts.powerReady = true;
    facts.returnOwed = true;
    facts.currentPower = 17;
    facts.currentMode = 29;
    return facts;
}

void initialAndContext(Checks& check) {
    Plan empty;
    const auto good = stoppedMsi();
    check.require(!empty.active() && empty.probes() == 0 &&
        empty.nextProbeAt() == 0, "default plan is inactive");
    check.require(!empty.due(0) && !empty.expire(0) &&
        !empty.checkContext(good) && !empty.observeAttempt(false, true),
        "inactive entry points cannot create a plan");

    // Exhaust all combinations of the seven admission/context booleans.
    for (unsigned mask = 0; mask < 128; ++mask) {
        auto facts = good;
        facts.knownBoard = (mask & 1) != 0;
        facts.exitFence = (mask & 2) != 0;
        facts.powerReady = (mask & 4) != 0;
        facts.targetEnabled = (mask & 8) != 0;
        facts.routeArmed = (mask & 16) != 0;
        facts.returnOwed = (mask & 32) != 0;
        facts.hostPresent = (mask & 64) != 0;
        const bool expected = mask == (1u | 4u | 32u);
        const bool mayBegin = (mask & ~64u) == (1u | 4u | 32u);
        Plan admission;
        check.require(admission.begin(100, facts) == mayBegin,
            "begin checks all admission facts except not-yet-cleared host ownership");
        check.require(admission.active() == mayBegin,
            "rejected admission leaves no active plan");
        Plan running;
        check.require(running.begin(100, good), "valid context setup");
        check.require(running.checkContext(facts) == expected,
            "every disallowed live boolean context cancels");
        check.require(running.checkContext(good) == expected,
            "restored context never revives a canceled plan");
    }

    struct ContextCase { const char* name; Facts facts; };
    std::array<ContextCase, 9> cases{{
        {"non-MSI board", good}, {"exit fence", good},
        {"power not ready", good}, {"new target enabled", good},
        {"route rearmed", good}, {"return debt disappeared", good},
        {"real process/prepared host present", good},
        {"cross power generation", good}, {"cross mode generation", good}
    }};
    cases[0].facts.knownBoard = false;
    cases[1].facts.exitFence = true;
    cases[2].facts.powerReady = false;
    cases[3].facts.targetEnabled = true;
    cases[4].facts.routeArmed = true;
    cases[5].facts.returnOwed = false;
    cases[6].facts.hostPresent = true;
    ++cases[7].facts.currentPower;
    ++cases[8].facts.currentMode;
    for (const auto& scenario : cases) {
        Plan plan;
        check.require(plan.begin(100, good) && plan.due(600), scenario.name);
        check.require(!plan.checkContext(scenario.facts) && !plan.active() &&
            !plan.observeAttempt(false, true) && !plan.due(1100), scenario.name);
    }

    auto stoppingHost = good;
    stoppingHost.hostPresent = true;
    Plan beforeHostClear;
    check.require(beforeHostClear.begin(100, stoppingHost) && beforeHostClear.active() &&
        beforeHostClear.probes() == 0,
        "stop may create a plan before inputHostClearLocked without claiming I/O");
    check.require(beforeHostClear.checkContext(good) && !beforeHostClear.due(599) &&
        beforeHostClear.checkContext(good) && beforeHostClear.due(600),
        "host cleanup followed by fresh context allows first probe");
    Plan hostStillPresent;
    check.require(hostStillPresent.begin(100, stoppingHost) &&
        !hostStillPresent.checkContext(stoppingHost) && !hostStillPresent.due(600),
        "uncleared real host cancels before probe acquisition");
    Plan earlyCancel;
    auto newTarget = good;
    newTarget.targetEnabled = true;
    check.require(earlyCancel.begin(100, good) && !earlyCancel.due(200) &&
        !earlyCancel.checkContext(newTarget) && !earlyCancel.active(),
        "every tick checks context even when next probe is not yet due");
    Plan mirror;
    check.require(mirror.begin(100, good) && mirror.checkContext(good),
        "UI mirror is not part of Facts or a substitute for real host ownership");
    mirror.cancel();
    mirror.cancel();
    check.require(!mirror.active() && !mirror.due(600) && good.returnOwed,
        "idempotent cancel leaves caller debt unchanged");
    // Reject independently unknown power, mode, and both, even while stop is
    // permitted to create the plan before actual host ownership is cleared.
    for (unsigned unknown = 1; unknown < 4; ++unknown) {
        auto zeroTokens = good;
        if ((unknown & 1) != 0) zeroTokens.currentPower = 0;
        if ((unknown & 2) != 0) zeroTokens.currentMode = 0;
        for (unsigned host = 0; host < 2; ++host) {
            zeroTokens.hostPresent = host != 0;
            Plan tokens;
            check.require(!tokens.begin(100, zeroTokens) && !tokens.active() &&
                tokens.probes() == 0 && tokens.deadline() == 0 &&
                tokens.nextProbeAt() == 0 && !tokens.due(600),
                "any zero generation refuses begin with or without a pending host");
        }
        zeroTokens.hostPresent = false;
        Plan beforeProbe;
        check.require(beforeProbe.begin(100, good) &&
            !beforeProbe.checkContext(zeroTokens) && !beforeProbe.active() &&
            !beforeProbe.due(600) && !beforeProbe.checkContext(good),
            "unknown generation cancels before first probe and cannot revive");
        Plan afterProbe;
        check.require(afterProbe.begin(100, good) && afterProbe.due(600) &&
            afterProbe.observeAttempt(false, true) &&
            !afterProbe.checkContext(zeroTokens) && !afterProbe.active() &&
            !afterProbe.due(1100) && good.returnOwed,
            "unknown generation cancels a retained plan without clearing debt");
    }
    for (const auto generation : {std::uint64_t{1},
            (std::numeric_limits<std::uint64_t>::max)()}) {
        auto knownTokens = good;
        knownTokens.currentPower = knownTokens.currentMode = generation;
        Plan tokens;
        check.require(tokens.begin(0, knownTokens) && tokens.checkContext(knownTokens) &&
            tokens.power() == generation && tokens.mode() == generation,
            "nonzero generation boundaries remain valid opaque identities");
    }
}

void boundariesAndBudget(Checks& check) {
    const auto facts = stoppedMsi();
    Plan plan;
    check.require(plan.begin(100, facts), "begin timing plan");
    check.require(plan.startedAt() == 100 && plan.deadline() == 10100 &&
        plan.nextProbeAt() == 600 && plan.probes() == 0 &&
        plan.power() == facts.currentPower && plan.mode() == facts.currentMode,
        "begin stores immutable time and generation anchors");
    for (unsigned probe = 1; probe <= 20; ++probe) {
        const std::uint64_t now = 100 + std::uint64_t(probe) * 500;
        check.require(!plan.due(now - 1) && plan.probes() == probe - 1,
            "one millisecond before each probe is too early");
        check.require(plan.due(now) && plan.probes() == probe && plan.active(),
            "claim increments probe count exactly once");
        check.require(plan.deadline() == 10100 && plan.startedAt() == 100,
            "claims never extend original deadline");
        if (probe < 20) {
            check.require(plan.nextProbeAt() == now + 500 && !plan.due(now),
                "claim advances next and repeated same-tick call cannot claim");
            check.require(plan.observeAttempt(false, true),
                "strict missing transport retains remaining schedule");
        } else {
            check.require(plan.nextProbeAt() == 0 && !plan.expire(now),
                "20th probe is claimable exactly at inclusive deadline");
            check.require(!plan.observeAttempt(false, true) && !plan.active(),
                "20th missing probe exhausts budget");
        }
    }
    check.require(!plan.due(10100) && !plan.due(10600) && plan.probes() == 20,
        "no 21st probe or automatic restart");
    check.require(facts.returnOwed, "budget exhaustion does not clear debt");

    Plan expired;
    check.require(expired.begin(100, facts) && !expired.expire(10100),
        "expire keeps exact deadline eligible");
    check.require(expired.expire(10101) && !expired.active() &&
        !expired.expire(10102), "deadline plus one expires once");
    Plan tooLate;
    check.require(tooLate.begin(100, facts) && !tooLate.due(10101) &&
        !tooLate.active() && tooLate.probes() == 0,
        "first late poll cannot extend deadline or claim a probe");

    Plan finalOnly;
    check.require(finalOnly.begin(0, facts) && finalOnly.due(10000) &&
        finalOnly.probes() == 1 && !finalOnly.observeAttempt(false, true),
        "first poll at deadline claims one probe with no catch-up");
    Plan unreportedFinal;
    check.require(unreportedFinal.begin(0, facts) && unreportedFinal.due(10000) &&
        !unreportedFinal.due(10000) && !unreportedFinal.active(),
        "final reservation cannot be duplicated even before result handling");
}

void delayedPollingAndRestart(Checks& check) {
    const auto facts = stoppedMsi();
    Plan plan;
    check.require(plan.begin(0, facts) && plan.due(750),
        "delayed first poll may claim one probe");
    check.require(plan.nextProbeAt() == 1250 && plan.deadline() == 10000 &&
        plan.observeAttempt(false, true) && !plan.due(750) && !plan.due(1249),
        "delayed claim uses actual claim time and no burst");
    check.require(plan.due(1250) && plan.observeAttempt(false, true),
        "second delayed probe remains on bounded schedule");
    check.require(plan.due(9600) && plan.nextProbeAt() == 0 &&
        !plan.observeAttempt(false, true) && !plan.due(10000),
        "remaining 400ms cannot squeeze in a 500ms probe");

    Plan edge;
    check.require(edge.begin(0, facts) && edge.due(9500) &&
        edge.nextProbeAt() == 10000 && edge.observeAttempt(false, true) &&
        !edge.due(9999) && edge.due(10000),
        "exactly 500ms remaining permits one final probe");
    edge.observeAttempt(false, true);

    Plan noRestart;
    auto changed = facts;
    ++changed.currentPower;
    ++changed.currentMode;
    check.require(noRestart.begin(100, facts) && noRestart.due(600) &&
        noRestart.observeAttempt(false, true), "restart refusal setup");
    check.require(!noRestart.begin(9000, changed) && noRestart.active() &&
        noRestart.startedAt() == 100 && noRestart.deadline() == 10100 &&
        noRestart.nextProbeAt() == 1100 && noRestart.probes() == 1 &&
        noRestart.power() == facts.currentPower && noRestart.mode() == facts.currentMode,
        "begin while active refuses without changing any plan anchors");
    check.require(!noRestart.begin(20000, changed) && noRestart.active() &&
        noRestart.deadline() == 10100 && noRestart.expire(10101),
        "begin cannot silently renew even an elapsed active plan");
    check.require(noRestart.begin(20000, changed) && noRestart.probes() == 0 &&
        noRestart.deadline() == 30000 && noRestart.nextProbeAt() == 20500 &&
        noRestart.power() == changed.currentPower && noRestart.mode() == changed.currentMode,
        "fresh caller authorization after completion can start a new plan");
}

void outcomeCompletion(Checks& check) {
    const auto facts = stoppedMsi();
    struct OutcomeCase {
        const char* name;
        bool writeAttempted;
        bool missingTransport;
    };
    // Classification is supplied synthetically. No native Outcome or I/O parser
    // is linked; the parent tests its own strict native classification.
    const std::array<OutcomeCase, 9> terminal{{
        {"successful write completes", true, false},
        {"failed write completes", true, false},
        {"timed-out write completes", true, false},
        {"unknown write result completes", true, false},
        {"write attempt dominates inconsistent missing flag", true, true},
        {"no-write open failure is not missing transport", false, false},
        {"no-write vendor/device match is not missing transport", false, false},
        {"no-write error is not missing transport", false, false},
        {"no-write unknown result is not missing transport", false, false}
    }};
    for (const auto& outcome : terminal) {
        Plan plan;
        check.require(plan.begin(0, facts) && plan.due(500), outcome.name);
        check.require(!plan.observeAttempt(outcome.writeAttempted, outcome.missingTransport) &&
            !plan.active() && !plan.due(1000) && !plan.checkContext(facts) &&
            !plan.observeAttempt(false, true), outcome.name);
        check.require(facts.returnOwed, "completed attempt does not assert external recovery or erase debt");
    }
    for (unsigned write = 0; write < 2; ++write) {
        for (unsigned missing = 0; missing < 2; ++missing) {
            Plan plan;
            check.require(plan.begin(0, facts) && plan.due(500), "outcome truth table setup");
            const bool retained = write == 0 && missing == 1;
            check.require(plan.observeAttempt(write != 0, missing != 0) == retained &&
                plan.active() == retained, "only strict missing/no-write retains plan");
        }
    }
}

void clockSafety(Checks& check) {
    const auto facts = stoppedMsi();
    Plan backBeforeStart;
    check.require(backBeforeStart.begin(100, facts) && !backBeforeStart.due(99) &&
        !backBeforeStart.active(), "rollback before start cancels");
    Plan backAfterClaim;
    check.require(backAfterClaim.begin(100, facts) && backAfterClaim.due(600) &&
        backAfterClaim.observeAttempt(false, true) && !backAfterClaim.due(599) &&
        !backAfterClaim.active(), "rollback after claim cancels");
    Plan backAfterEarlyPoll;
    check.require(backAfterEarlyPoll.begin(100, facts) && !backAfterEarlyPoll.due(300) &&
        backAfterEarlyPoll.expire(299), "even a pre-due observation detects rollback");
    Plan backAfterExpireCheck;
    check.require(backAfterExpireCheck.begin(100, facts) && !backAfterExpireCheck.expire(900) &&
        !backAfterExpireCheck.due(800) && !backAfterExpireCheck.active(),
        "expire and due share a monotonic time fence");

    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    Plan overflow;
    check.require(!overflow.begin(maximum, facts) &&
        !overflow.begin(maximum - 9999, facts) && !overflow.active() &&
        overflow.deadline() == 0 && overflow.nextProbeAt() == 0,
        "unrepresentable full deadline refuses without wrapped state");
    Plan atMaximum;
    const auto start = maximum - 10000;
    check.require(atMaximum.begin(start, facts) && atMaximum.deadline() == maximum,
        "largest representable full deadline is valid");
    for (unsigned probe = 1; probe <= 20; ++probe) {
        const auto now = start + std::uint64_t(probe) * 500;
        check.require(!atMaximum.due(now - 1) && atMaximum.due(now),
            "all twenty near-maximum probe boundaries stay safe");
        check.require(atMaximum.observeAttempt(false, true) == (probe < 20),
            "near-maximum final probe completes without next-time overflow");
    }
    check.require(atMaximum.probes() == 20 && !atMaximum.active() &&
        !atMaximum.due(maximum), "maximum tick cannot duplicate final probe");
    Plan wraps;
    check.require(wraps.begin(start, facts) && wraps.due(maximum - 500) &&
        wraps.observeAttempt(false, true) && !wraps.due(0) && !wraps.active(),
        "wrapped clock cancels instead of treating wrap as forward progress");
}
} // namespace

int main() {
    Checks checks;
    initialAndContext(checks);
    boundariesAndBudget(checks);
    delayedPollingAndRestart(checks);
    outcomeCompletion(checks);
    clockSafety(checks);
    std::cout << (checks.failures == 0 ? "MSI_TERMINAL_RETURN_PLAN_PURE_OK" :
        "MSI_TERMINAL_RETURN_PLAN_PURE_FAIL") << " checks=" << checks.count
        << " failures=" << checks.failures
        << " fixtureOnly=1 productDeviceApiCalls=0\n";
    return checks.failures == 0 ? 0 : 1;
}
