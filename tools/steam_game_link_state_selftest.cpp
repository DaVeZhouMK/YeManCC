#include "../native/steam_game_link_state.h"

#include <iostream>
#include <stdexcept>
#include <type_traits>

using namespace ymcc::steamlink;

static_assert(std::is_aggregate<Evidence>::value, "Evidence must remain plain public data");
static_assert(std::is_aggregate<Snapshot>::value, "Snapshot must remain plain public data");
static_assert(std::is_same<decltype(Snapshot{}.phase), std::string>::value, "Stable phase type");
static_assert(std::is_same<decltype(Snapshot{}.reason), std::string>::value, "Stable reason type");
static_assert(std::is_same<decltype(Snapshot{}.serial), std::uint64_t>::value, "Stable serial type");
static_assert(std::is_same<decltype(Snapshot{}.readFailures), unsigned>::value, "Stable counter type");

namespace {
unsigned tests = 0;
std::uint64_t assertions = 0, combinations = 0;
constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();

void expect(bool value, const char* message) {
    ++assertions;
    if (!value) throw std::runtime_error(message);
}

template<class Test>
void run(const char* name, Test test) {
    try {
        test();
        ++tests;
        std::cout << "PASS " << name << '\n';
    } catch (...) {
        std::cerr << "FAIL " << name << '\n';
        throw;
    }
}

bool equal(const Snapshot& a, const Snapshot& b) {
    return a.phase == b.phase && a.reason == b.reason && a.serial == b.serial &&
        a.configEpoch == b.configEpoch && a.lastSequence == b.lastSequence &&
        a.lastTick == b.lastTick && a.dueTick == b.dueTick && a.readFailures == b.readFailures &&
        a.active == b.active && a.linked == b.linked && a.contactConnected == b.contactConnected && a.transportConnected == b.transportConnected && a.terminal == b.terminal &&
        a.actualGameInputVerified == b.actualGameInputVerified &&
        a.continueButtonVerified == b.continueButtonVerified && a.sharedCandidate == b.sharedCandidate;
}

Evidence healthy(std::uint64_t sequence, std::uint64_t tick, std::uint64_t epoch = 7) {
    Evidence e;
    e.sequence = sequence;
    e.tick = tick;
    e.configEpoch = epoch;
    e.game = e.steam = IdentityStatus::Same;
    e.account = AccountStatus::Same;
    e.tracker = TrackerStatus::Exact;
    e.primary = PrimaryStatus::Exact;
    e.validator = ValidatorStatus::Accepted;
    e.ui = UiStatus::RunningAndListed;
    e.sharedCandidate = CandidateStatus::Same;
    e.transportAvailable = true;
    return e;
}

State capture(std::uint64_t tick = 0) {
    State state;
    expect(state.begin(42, 7, tick), "valid capture must begin");
    return state;
}

void accept(State& state, const Evidence& evidence) {
    expect(state.observe(evidence), "fresh evidence must be accepted, including terminal evidence");
    const auto s = state.snapshot();
    expect(state.active() == s.active, "active accessor must match snapshot");
    expect(!s.actualGameInputVerified && !s.continueButtonVerified,
        "observations never verify actual game input or Continue");
}

void openAs(const State& state, const char* phase, const char* reason, std::uint64_t due) {
    const auto s = state.snapshot();
    expect(s.active && !s.terminal, "evidence loss must preserve the original active capture");
    expect(s.phase == phase && s.reason == reason, "phase and reason must describe current evidence");
    expect(s.linked == (s.phase == "linked"), "only the linked phase verifies the current association");
    expect(s.dueTick == due, "next observation must use the required cadence");
    expect(s.serial == 42 && s.configEpoch == 7, "observations cannot replace original capture identity");
}

struct Mutation {
    const char* name;
    const char* reason;
    void (*apply)(Evidence&);
};
} // namespace

int main() {
    try {
        run("plain-defaults-and-no-capture", [] {
            State state;
            const auto before = state.snapshot();
            expect(before.phase == "idle" && before.reason == "not-captured", "uncaptured state is idle");
            expect(!before.active && !before.linked && !before.terminal && !state.active(), "idle is not terminal");
            expect(before.serial == 0 && before.configEpoch == 0 && before.lastSequence == 0 &&
                before.lastTick == 0 && before.dueTick == 0 && before.readFailures == 0, "idle counters are zero");
            expect(!before.actualGameInputVerified && !before.continueButtonVerified && !before.contactConnected && !before.transportConnected &&
                before.sharedCandidate == CandidateStatus::Unknown, "idle verification remains unknown");
            expect(!state.observe(healthy(1, 0)) && equal(before, state.snapshot()), "observe cannot invent capture");
            const Evidence defaults;
            expect(defaults.sequence == 0 && defaults.tick == 0 && defaults.enabled && defaults.configEpoch == 0 &&
                defaults.game == IdentityStatus::Unknown && defaults.steam == IdentityStatus::Unknown &&
                defaults.account == AccountStatus::Unknown && defaults.tracker == TrackerStatus::Unknown &&
                defaults.primary == PrimaryStatus::Unknown && defaults.validator == ValidatorStatus::Unknown &&
                defaults.ui == UiStatus::Unknown && defaults.sharedCandidate == CandidateStatus::Unknown &&
                !defaults.transportAvailable, "Evidence defaults match the stable interface");
        });
        run("nonzero-serial-and-epoch-required", [] {
            State state;
            const auto before = state.snapshot();
            expect(!state.begin(0, 7, 0) && !state.begin(42, 0, 0) && !state.begin(0, 0, 0), "invalid begin is rejected");
            expect(equal(before, state.snapshot()), "invalid begin leaves all fields unchanged");
        });
        run("zero-clock-begin-is-immediately-due", [] {
            auto state = capture();
            openAs(state, "waiting", "awaiting-evidence", 0);
            expect(state.snapshot().lastTick == 0 && state.snapshot().lastSequence == 0, "no sentinel clock assumption");
        });
        run("active-waiting-capture-cannot-be-replaced", [] {
            auto state = capture(123);
            const auto before = state.snapshot();
            expect(!state.begin(99, 8, 999) && !state.begin(42, 7, 123), "new and repeated begin cannot overwrite active");
            expect(!state.begin(0, 0, 0) && equal(before, state.snapshot()), "all rejected begins are immutable");
        });
        run("active-linked-capture-cannot-be-replaced", [] {
            auto state = capture();
            accept(state, healthy(1, 100));
            const auto before = state.snapshot();
            expect(!state.begin(99, 8, 0) && equal(before, state.snapshot()), "linked original cannot be retargeted");
            auto e = healthy(2, 101);
            e.ui = UiStatus::NotRunning;
            accept(state, e);
            const auto degraded = state.snapshot();
            expect(!state.begin(99, 8, 102) && equal(degraded, state.snapshot()), "degraded original cannot be replaced either");
        });
        run("healthy-ten-second-sampling", [] {
            auto state = capture();
            for (std::uint64_t i = 0; i < 8; ++i) {
                accept(state, healthy(i + 1, i * 10000));
                openAs(state, "linked", "exact-original-linked", (i + 1) * 10000);
                expect(state.snapshot().readFailures == 0, "healthy samples have no transport failures");
            }
        });
        run("original-remains-linked-at-35s-48s-and-hours", [] {
            auto state = capture();
            std::uint64_t sequence = 0;
            for (auto tick : {0ULL, 35000ULL, 48000ULL, 60000ULL, 61000ULL, 300000ULL, 36000000ULL}) {
                accept(state, healthy(++sequence, tick));
                openAs(state, "linked", "exact-original-linked", tick + 10000);
            }
        });
        run("unknown-original-has-no-30-or-60-second-deadline", [] {
            auto state = capture();
            std::uint64_t sequence = 0;
            for (auto tick : {0ULL, 35000ULL, 48000ULL, 60000ULL, 300000ULL, 36000000ULL}) {
                auto e = healthy(++sequence, tick);
                e.game = IdentityStatus::Unknown;
                accept(state, e);
                openAs(state, "waiting", "game-identity-unknown", tick + 2000);
            }
        });
        run("native-overview-not-running-keeps-same-original-contact", [] {
            auto state = capture();
            accept(state, healthy(1, 0));
            std::uint64_t sequence = 1;
            for (auto tick : {35000ULL, 48000ULL, 60000ULL, 300000ULL, 900000ULL}) {
                auto e = healthy(++sequence, tick);
                e.ui = UiStatus::NotRunning;
                accept(state, e);
                openAs(state, "contact-degraded", "same-original-contact-steam-not-running", tick + 2000);
            }
            accept(state, healthy(++sequence, 905000));
            openAs(state, "linked", "exact-original-linked", 915000);
        });
        run("missing-running-list-degrades-and-can-recover", [] {
            auto state = capture();
            auto e = healthy(1, 0);
            e.ui = UiStatus::RunningMissingList;
            accept(state, e);
            openAs(state, "contact-degraded", "same-original-contact-running-list-missing", 2000);
            accept(state, healthy(2, 48000));
            openAs(state, "linked", "exact-original-linked", 58000);
        });
        run("known-unlinked-evidence-is-reversible", [] {
            const Mutation losses[] = {
                {"tracker-absent", "tracker-absent", [](Evidence& e) { e.tracker = TrackerStatus::Absent; }},
                {"primary-different", "native-primary-different", [](Evidence& e) { e.primary = PrimaryStatus::Different; }},
                {"validator-rejected", "validator-rejected", [](Evidence& e) { e.validator = ValidatorStatus::Rejected; }}
            };
            for (const auto& loss : losses) {
                auto state = capture();
                auto e = healthy(1, 0);
                loss.apply(e);
                accept(state, e);
                openAs(state, "degraded", loss.reason, 2000);
                accept(state, healthy(2, 600000));
                openAs(state, "linked", "exact-original-linked", 610000);
            }
        });

        const Mutation unknowns[] = {
            {"unknown-game-identity", "game-identity-unknown", [](Evidence& e) { e.game = IdentityStatus::Unknown; }},
            {"unknown-steam-identity", "steam-identity-unknown", [](Evidence& e) { e.steam = IdentityStatus::Unknown; }},
            {"unknown-account", "account-unknown", [](Evidence& e) { e.account = AccountStatus::Unknown; }},
            {"unknown-tracker", "tracker-unknown", [](Evidence& e) { e.tracker = TrackerStatus::Unknown; }},
            {"unknown-native-primary", "native-primary-unknown", [](Evidence& e) { e.primary = PrimaryStatus::Unknown; }},
            {"unknown-validator", "validator-unknown", [](Evidence& e) { e.validator = ValidatorStatus::Unknown; }},
            {"unknown-ui", "ui-unknown", [](Evidence& e) { e.ui = UiStatus::Unknown; }}
        };
        for (const auto& unknown : unknowns) run(unknown.name, [&] {
            auto state = capture();
            auto e = healthy(1, 0);
            unknown.apply(e);
            accept(state, e);
            openAs(state, "waiting", unknown.reason, 2000);
            accept(state, healthy(2, 2000));
            openAs(state, "linked", "exact-original-linked", 12000);
            e = healthy(3, 7000);
            unknown.apply(e);
            accept(state, e);
            openAs(state, "degraded", unknown.reason, 9000);
            accept(state, healthy(4, 48000));
            openAs(state, "linked", "exact-original-linked", 58000);
        });

        run("unknown-sources-outrank-known-ui-loss", [] {
            auto state = capture();
            auto e = healthy(1, 0);
            e.ui = UiStatus::NotRunning;
            e.tracker = TrackerStatus::Unknown;
            accept(state, e);
            openAs(state, "waiting", "tracker-unknown", 2000);
            e.sequence = 2;
            e.tracker = TrackerStatus::Absent;
            accept(state, e);
            openAs(state, "degraded", "native-overview-not-running", 2000);
        });
        run("transport-backoff-persists-beyond-one-minute", [] {
            auto state = capture();
            const std::uint64_t delays[] = {1000, 3000, 8000, 20000, 30000, 30000, 30000, 30000};
            std::uint64_t tick = 0, sequence = 0;
            for (const auto delay : delays) {
                auto e = healthy(++sequence, tick);
                e.transportAvailable = false;
                accept(state, e);
                openAs(state, "waiting", "transport-unavailable", tick + delay);
                expect(state.snapshot().readFailures == sequence, "only accepted transport failures advance backoff");
                tick = state.snapshot().dueTick;
            }
            expect(tick > 60000, "regression covers persistent retry after the former deadline");
        });
        run("late-transport-observation-schedules-once-from-current-tick", [] {
            auto state = capture();
            accept(state, healthy(1, 0));
            auto e = healthy(2, 35000);
            e.transportAvailable = false;
            accept(state, e);
            openAs(state, "degraded", "transport-unavailable", 36000);
            e.sequence = 3;
            e.tick = 48000;
            accept(state, e);
            openAs(state, "degraded", "transport-unavailable", 51000);
            e.sequence = 4;
            e.tick = 600000;
            accept(state, e);
            openAs(state, "degraded", "transport-unavailable", 608000);
            expect(state.snapshot().readFailures == 3, "elapsed time does not fabricate additional failed reads");
        });
        run("transport-reconnect-revalidates-all-current-original-evidence", [] {
            auto state = capture();
            accept(state, healthy(1, 0));
            auto e = healthy(2, 5000);
            e.transportAvailable = false;
            accept(state, e);
            e = healthy(3, 6000);
            e.primary = PrimaryStatus::Unknown;
            accept(state, e);
            openAs(state, "degraded", "native-primary-unknown", 8000);
            expect(state.snapshot().readFailures == 0, "available transport resets consecutive transport failures");
            e = healthy(4, 8000);
            e.transportAvailable = false;
            accept(state, e);
            openAs(state, "degraded", "transport-unavailable", 9000);
            expect(state.snapshot().readFailures == 1, "new outage starts with the one-second delay");
            accept(state, healthy(5, 9000));
            openAs(state, "linked", "exact-original-linked", 19000);
        });
        run("fresh-events-may-arrive-before-due-and-snapshot-is-stable", [] {
            auto state = capture();
            accept(state, healthy(1, 100));
            const auto before = state.snapshot();
            expect(equal(before, state.snapshot()), "reading snapshots does not advance deadlines");
            auto e = healthy(2, 101);
            e.ui = UiStatus::RunningMissingList;
            accept(state, e);
            openAs(state, "contact-degraded", "same-original-contact-running-list-missing", 2101);
        });

        const Mutation terminals[] = {
            {"off-seals-cancelled", "off", [](Evidence& e) { e.enabled = false; }},
            {"epoch-change-seals-cancelled", "config-epoch-changed", [](Evidence& e) { e.configEpoch = 8; }},
            {"missing-epoch-also-cancels", "config-epoch-changed", [](Evidence& e) { e.configEpoch = 0; }},
            {"confirmed-game-exit-seals-closed", "game-exited", [](Evidence& e) { e.game = IdentityStatus::Exited; }},
            {"confirmed-steam-exit-seals-closed", "steam-exited", [](Evidence& e) { e.steam = IdentityStatus::Exited; }},
            {"original-game-pid-reuse-seals-revoked", "game-identity-changed", [](Evidence& e) { e.game = IdentityStatus::Changed; }},
            {"original-steam-pid-reuse-seals-revoked", "steam-identity-changed", [](Evidence& e) { e.steam = IdentityStatus::Changed; }},
            {"account-change-seals-revoked", "account-changed", [](Evidence& e) { e.account = AccountStatus::Changed; }}
        };
        for (const auto& terminal : terminals) run(terminal.name, [&] {
            auto state = capture();
            accept(state, healthy(1, 0));
            auto outage = healthy(2, 5000);
            outage.transportAvailable = false;
            accept(state, outage);
            auto e = healthy(3, 6000);
            e.transportAvailable = false;
            e.tracker = TrackerStatus::Unknown;
            e.primary = PrimaryStatus::Unknown;
            e.ui = UiStatus::NotRunning;
            e.sharedCandidate = CandidateStatus::Different;
            terminal.apply(e);
            accept(state, e);
            const auto sealed = state.snapshot();
            const std::string expectedPhase = !e.enabled || e.configEpoch != 7 ? "cancelled" :
                e.game == IdentityStatus::Exited || e.steam == IdentityStatus::Exited ? "closed" : "revoked";
            expect(sealed.phase == expectedPhase && sealed.reason == terminal.reason, "terminal event outranks missing reads");
            expect(sealed.terminal && !sealed.active && !sealed.linked && sealed.dueTick == 0, "terminal has no future work");
            expect(sealed.serial == 42 && sealed.configEpoch == 7 && sealed.lastSequence == 3 && sealed.lastTick == 6000,
                "terminal keeps the original capture and the terminating sample");
            expect(sealed.readFailures == 1 && sealed.sharedCandidate == CandidateStatus::Different,
                "terminal freezes prior failure count and retains current diagnostic candidate");
            expect(!state.observe(healthy(4, 7000)) && equal(sealed, state.snapshot()), "healthy reconnect cannot reopen terminal");
            expect(!state.begin(0, 8, 0) && !state.begin(99, 0, 0) && equal(sealed, state.snapshot()),
                "invalid new capture cannot unseal lifecycle");
            expect(state.begin(99, 8, 0), "explicit valid new capture resets sealed lifecycle");
            const auto fresh = state.snapshot();
            expect(fresh.active && !fresh.terminal && !fresh.linked && fresh.serial == 99 && fresh.configEpoch == 8 &&
                fresh.lastSequence == 0 && fresh.lastTick == 0 && fresh.dueTick == 0 && fresh.readFailures == 0 &&
                fresh.phase == "waiting" && fresh.reason == "awaiting-evidence" && fresh.sharedCandidate == CandidateStatus::Unknown,
                "new capture resets every lifecycle and scheduling field");
            e = healthy(1, 0, 8);
            e.tracker = TrackerStatus::Unknown;
            accept(state, e);
            expect(state.snapshot().phase == "waiting", "new capture forgets the previous linked history");
        });

        run("terminal-priority-off-epoch-exit-identity-account", [] {
            auto state = capture();
            auto e = healthy(1, 0);
            e.enabled = false;
            e.configEpoch = 8;
            e.game = IdentityStatus::Exited;
            e.steam = IdentityStatus::Changed;
            e.account = AccountStatus::Changed;
            accept(state, e);
            expect(state.snapshot().reason == "off", "Off outranks every other event");
            state = capture();
            e.enabled = true;
            accept(state, e);
            expect(state.snapshot().reason == "config-epoch-changed", "epoch cancellation outranks exits");
            state = capture();
            e.configEpoch = 7;
            e.game = IdentityStatus::Changed;
            e.steam = IdentityStatus::Exited;
            accept(state, e);
            expect(state.snapshot().phase == "closed" && state.snapshot().reason == "steam-exited", "confirmed exit outranks changes");
            state = capture();
            e.steam = IdentityStatus::Same;
            accept(state, e);
            expect(state.snapshot().reason == "game-identity-changed", "game identity change outranks account change");
            state = capture();
            e.game = IdentityStatus::Unknown;
            accept(state, e);
            expect(state.snapshot().reason == "account-changed", "confirmed account change outranks unknown identity");
        });
        run("stale-sequence-cannot-cancel-revoke-or-change-backoff", [] {
            auto state = capture();
            auto e = healthy(10, 500);
            e.transportAvailable = false;
            accept(state, e);
            const auto before = state.snapshot();
            e.enabled = false;
            e.configEpoch = 8;
            e.game = IdentityStatus::Exited;
            e.tick = 999999;
            expect(!state.observe(e) && equal(before, state.snapshot()), "equal sequence is wholly rejected");
            e.sequence = 9;
            expect(!state.observe(e) && equal(before, state.snapshot()), "reordered lower sequence is wholly rejected");
            accept(state, healthy(11, 500));
            openAs(state, "linked", "exact-original-linked", 10500);
        });
        run("backward-tick-is-rejected-even-with-new-sequence", [] {
            auto state = capture(100);
            auto e = healthy(1, 99);
            expect(!state.observe(e) && state.snapshot().lastSequence == 0, "begin tick is the clock floor");
            accept(state, healthy(1, 100));
            const auto before = state.snapshot();
            e = healthy(2, 99);
            e.enabled = false;
            expect(!state.observe(e) && equal(before, state.snapshot()), "backwards tick cannot cancel current capture");
            accept(state, healthy(2, 100));
            openAs(state, "linked", "exact-original-linked", 10100);
        });
        run("sequence-zero-is-reserved-and-sequence-does-not-wrap", [] {
            auto state = capture();
            const auto before = state.snapshot();
            expect(!state.observe(healthy(0, 0)) && equal(before, state.snapshot()), "zero sequence is not fresh evidence");
            accept(state, healthy(maximum, 0));
            const auto last = state.snapshot();
            expect(!state.observe(healthy(0, 1)) && !state.observe(healthy(maximum, 1)) && equal(last, state.snapshot()),
                "sequence wrap cannot create fresh observations");
        });
        run("all-transitions-work-at-zero-clock", [] {
            auto state = capture();
            accept(state, healthy(1, 0));
            openAs(state, "linked", "exact-original-linked", 10000);
            auto e = healthy(2, 0);
            e.account = AccountStatus::Unknown;
            accept(state, e);
            openAs(state, "degraded", "account-unknown", 2000);
            e = healthy(3, 0);
            e.transportAvailable = false;
            accept(state, e);
            openAs(state, "degraded", "transport-unavailable", 1000);
            e = healthy(4, 0);
            e.game = IdentityStatus::Exited;
            accept(state, e);
            expect(state.snapshot().terminal && state.snapshot().dueTick == 0, "zero tick still seals confirmed exit");
        });
        run("healthy-and-evidence-loss-deadlines-saturate", [] {
            auto state = capture(maximum - 10001);
            accept(state, healthy(1, maximum - 10001));
            openAs(state, "linked", "exact-original-linked", maximum - 1);
            accept(state, healthy(2, maximum - 10000));
            openAs(state, "linked", "exact-original-linked", maximum);
            accept(state, healthy(3, maximum - 9999));
            openAs(state, "linked", "exact-original-linked", maximum);
            auto e = healthy(4, maximum - 1999);
            e.ui = UiStatus::RunningMissingList;
            accept(state, e);
            openAs(state, "contact-degraded", "same-original-contact-running-list-missing", maximum);
            e = healthy(5, maximum);
            e.game = IdentityStatus::Unknown;
            accept(state, e);
            openAs(state, "degraded", "game-identity-unknown", maximum);
            accept(state, healthy(6, maximum));
            openAs(state, "linked", "exact-original-linked", maximum);
        });
        run("all-transport-backoff-deadlines-saturate", [] {
            auto state = capture(maximum - 500);
            for (std::uint64_t i = 0; i < 7; ++i) {
                auto e = healthy(i + 1, i ? maximum : maximum - 500);
                e.transportAvailable = false;
                accept(state, e);
                openAs(state, "waiting", "transport-unavailable", maximum);
                expect(state.snapshot().readFailures == i + 1, "same saturated tick can still carry fresh observations");
            }
        });
        run("maximum-serial-epoch-and-clock-are-valid", [] {
            State state;
            expect(state.begin(maximum, maximum, maximum), "all nonzero u64 identifiers remain valid");
            accept(state, healthy(1, maximum, maximum));
            const auto s = state.snapshot();
            expect(s.serial == maximum && s.configEpoch == maximum && s.lastTick == maximum && s.dueTick == maximum && s.linked,
                "identifiers and clock are not truncated or wrapped");
        });
        run("every-shared-candidate-is-diagnostic-only", [] {
            auto state = capture();
            std::uint64_t sequence = 0;
            for (auto candidate : {CandidateStatus::Same, CandidateStatus::Different, CandidateStatus::None, CandidateStatus::Unknown}) {
                ++sequence;
                auto e = healthy(sequence, sequence * 5000);
                e.sharedCandidate = candidate;
                accept(state, e);
                openAs(state, "linked", "exact-original-linked", e.tick + 10000);
                expect(state.snapshot().sharedCandidate == candidate, "snapshot publishes diagnostic divergence literally");
            }
            auto e = healthy(++sequence, 48000);
            e.game = IdentityStatus::Changed;
            e.sharedCandidate = CandidateStatus::Same;
            accept(state, e);
            expect(state.snapshot().terminal && state.snapshot().phase == "revoked", "candidate agreement cannot save changed original");
        });
        run("snapshot-copy-cannot-mutate-the-model", [] {
            auto state = capture();
            accept(state, healthy(1, 0));
            const auto before = state.snapshot();
            auto copy = state.snapshot();
            copy.phase = "revoked";
            copy.reason = "foreign";
            copy.serial = 99;
            copy.configEpoch = 8;
            copy.active = false;
            copy.actualGameInputVerified = copy.continueButtonVerified = true;
            copy.dueTick = 123;
            expect(equal(before, state.snapshot()), "plain snapshot is a detached value");
        });
        run("unexpected-status-values-remain-unknown", [] {
            const Mutation invalid[] = {
                {"game", "game-identity-unknown", [](Evidence& e) { e.game = static_cast<IdentityStatus>(99); }},
                {"steam", "steam-identity-unknown", [](Evidence& e) { e.steam = static_cast<IdentityStatus>(99); }},
                {"account", "account-unknown", [](Evidence& e) { e.account = static_cast<AccountStatus>(99); }},
                {"tracker", "tracker-unknown", [](Evidence& e) { e.tracker = static_cast<TrackerStatus>(99); }},
                {"primary", "native-primary-unknown", [](Evidence& e) { e.primary = static_cast<PrimaryStatus>(99); }},
                {"validator", "validator-unknown", [](Evidence& e) { e.validator = static_cast<ValidatorStatus>(99); }},
                {"ui", "ui-unknown", [](Evidence& e) { e.ui = static_cast<UiStatus>(99); }}
            };
            for (const auto& item : invalid) {
                auto state = capture();
                auto e = healthy(1, 0);
                item.apply(e);
                accept(state, e);
                openAs(state, "waiting", item.reason, 2000);
            }
        });
        run("exhaustive-status-matrix-never-overclaims-link-or-exit", [] {
            for (auto game : {IdentityStatus::Same, IdentityStatus::Exited, IdentityStatus::Changed, IdentityStatus::Unknown})
            for (auto steam : {IdentityStatus::Same, IdentityStatus::Exited, IdentityStatus::Changed, IdentityStatus::Unknown})
            for (auto account : {AccountStatus::Same, AccountStatus::Changed, AccountStatus::Unknown})
            for (auto tracker : {TrackerStatus::Exact, TrackerStatus::Absent, TrackerStatus::Unknown})
            for (auto primary : {PrimaryStatus::Exact, PrimaryStatus::Different, PrimaryStatus::Unknown})
            for (auto validator : {ValidatorStatus::Accepted, ValidatorStatus::Rejected, ValidatorStatus::Unknown})
            for (auto ui : {UiStatus::RunningAndListed, UiStatus::RunningMissingList, UiStatus::NotRunning, UiStatus::Unknown})
            for (auto candidate : {CandidateStatus::Same, CandidateStatus::Different, CandidateStatus::None, CandidateStatus::Unknown})
            for (bool transport : {false, true}) {
                auto state = capture(100);
                auto e = healthy(1, 100);
                e.game = game;
                e.steam = steam;
                e.account = account;
                e.tracker = tracker;
                e.primary = primary;
                e.validator = validator;
                e.ui = ui;
                e.sharedCandidate = candidate;
                e.transportAvailable = transport;
                accept(state, e);
                const auto s = state.snapshot();
                const bool exact = transport && game == IdentityStatus::Same && steam == IdentityStatus::Same &&
                    account == AccountStatus::Same && tracker == TrackerStatus::Exact && primary == PrimaryStatus::Exact &&
                    validator == ValidatorStatus::Accepted && ui == UiStatus::RunningAndListed;
                const bool contact = transport && game == IdentityStatus::Same && steam == IdentityStatus::Same &&
                    account == AccountStatus::Same && tracker == TrackerStatus::Exact && primary == PrimaryStatus::Exact &&
                    validator == ValidatorStatus::Accepted && ui != UiStatus::Unknown && candidate == CandidateStatus::Same;
                const bool ended = game == IdentityStatus::Exited || steam == IdentityStatus::Exited ||
                    game == IdentityStatus::Changed || steam == IdentityStatus::Changed || account == AccountStatus::Changed;
                expect(s.linked == exact, "linked requires every current original evidence source, independent of candidate");
                expect(s.contactConnected == contact, "identity contact requires same original evidence and shared candidate while allowing Running loss");
                expect(s.terminal == ended && s.active != ended, "only confirmed identity events terminate this matrix");
                expect(s.sharedCandidate == candidate && s.serial == 42 && s.configEpoch == 7, "matrix never retargets original");
                expect(s.dueTick == (ended ? 0 : 100 + (exact ? 10000 : transport ? 2000 : 1000)),
                    "matrix cadence distinguishes healthy, evidence loss, unavailable transport, and terminal");
                ++combinations;
            }
            expect(combinations == 41472, "every enum and transport combination was exercised");
        });

        std::cout << "STEAM_GAME_LINK_STATE_OK tests=" << tests << " assertions=" << assertions
            << " statusCombinations=" << combinations
            << " steamWrites=0 gameWrites=0 inputWrites=0 deviceWrites=0 actionWrites=0"
            << " actualGameInputVerified=false continueButtonVerified=false\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "STEAM_GAME_LINK_STATE_FAILED tests=" << tests << " assertions=" << assertions
            << " error=" << error.what() << '\n';
        return 1;
    }
}
