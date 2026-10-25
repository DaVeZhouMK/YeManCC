#pragma once

#include <cstdint>
#include <limits>
#include <string>

namespace ymcc::steamlink {

enum class IdentityStatus { Same, Exited, Changed, Unknown };
enum class AccountStatus { Same, Changed, Unknown };
enum class TrackerStatus { Exact, Absent, Unknown };
enum class PrimaryStatus { Exact, Different, Unknown };
enum class ValidatorStatus { Accepted, Rejected, Unknown };
enum class UiStatus { RunningAndListed, RunningMissingList, NotRunning, Unknown };
enum class CandidateStatus { Same, Different, None, Unknown };

// Each observation describes this capture's original identities and account.
// The caller owns sampling and synchronization; this model performs no I/O.
struct Evidence {
    std::uint64_t sequence = 0, tick = 0;
    bool enabled = true;
    std::uint64_t configEpoch = 0;
    IdentityStatus game = IdentityStatus::Unknown, steam = IdentityStatus::Unknown;
    AccountStatus account = AccountStatus::Unknown;
    TrackerStatus tracker = TrackerStatus::Unknown;
    PrimaryStatus primary = PrimaryStatus::Unknown;
    ValidatorStatus validator = ValidatorStatus::Unknown;
    UiStatus ui = UiStatus::Unknown;
    CandidateStatus sharedCandidate = CandidateStatus::Unknown;
    bool transportAvailable = false;
};

struct Snapshot {
    std::string phase = "idle", reason = "not-captured";
    std::uint64_t serial = 0, configEpoch = 0, lastSequence = 0, lastTick = 0, dueTick = 0;
    // Consecutive accepted, nonterminal samples with unavailable transport.
    unsigned readFailures = 0;
    bool active = false, linked = false, contactConnected = false, transportConnected = false, terminal = false;
    bool actualGameInputVerified = false, continueButtonVerified = false;
    CandidateStatus sharedCandidate = CandidateStatus::Unknown;
};

class State {
public:
    // A terminal lifecycle may be explicitly replaced with a fresh capture.
    // An active capture cannot be replaced, even with the same serial/epoch.
    bool begin(std::uint64_t serial, std::uint64_t epoch, std::uint64_t tick) {
        if (!serial || !epoch || state_.active) return false;
        state_ = Snapshot{};
        state_.phase = "waiting";
        state_.reason = "awaiting-evidence";
        state_.serial = serial;
        state_.configEpoch = epoch;
        state_.lastTick = tick;
        state_.dueTick = tick; // First observation is immediately due; zero is a valid clock.
        state_.active = true;
        everLinked_ = false;
        return true;
    }

    // Sequence zero is reserved for an unsampled capture. Equal ticks with a
    // strictly increasing sequence are valid, including at a saturated clock.
    // dueTick is advisory: a fresh event may be observed before it is due.
    bool observe(const Evidence& evidence) {
        if (!state_.active || evidence.sequence <= state_.lastSequence ||
            evidence.tick < state_.lastTick) return false;

        state_.lastSequence = evidence.sequence;
        state_.lastTick = evidence.tick;
        state_.sharedCandidate = evidence.sharedCandidate; // Diagnostic only; never retargets.
        state_.linked = false;
        state_.contactConnected = false;

        // Explicit cancellation and confirmed identity events outrank missing
        // transport/UI evidence. Native overview NotRunning is not an OS exit.
        if (!evidence.enabled) return seal("cancelled", "off");
        if (evidence.configEpoch != state_.configEpoch)
            return seal("cancelled", "config-epoch-changed");
        if (evidence.game == IdentityStatus::Exited) return seal("closed", "game-exited");
        if (evidence.steam == IdentityStatus::Exited) return seal("closed", "steam-exited");
        if (evidence.game == IdentityStatus::Changed)
            return seal("revoked", "game-identity-changed");
        if (evidence.steam == IdentityStatus::Changed)
            return seal("revoked", "steam-identity-changed");
        if (evidence.account == AccountStatus::Changed) return seal("revoked", "account-changed");

        if (!evidence.transportAvailable) {
            state_.transportConnected = false;
            if (state_.readFailures != (std::numeric_limits<unsigned>::max)())
                ++state_.readFailures;
            constexpr std::uint64_t retries[] = {1000, 3000, 8000, 20000, 30000};
            const unsigned index = state_.readFailures < 5 ? state_.readFailures - 1 : 4;
            return pending("transport-unavailable", retries[index]);
        }
        state_.readFailures = 0; // Reconnection supplies observations, never actions.
        // This is the narrow two-end transport fact: the same Steam process and
        // bound account answered a non-mutating request. It deliberately does
        // not require the game/UI association, so a broken Running list can be
        // diagnosed without being mislabeled as a transport disconnect.
        state_.transportConnected = evidence.steam == IdentityStatus::Same &&
            evidence.account == AccountStatus::Same;

        // No previous sample fills in a missing source in the current sample.
        // Unexpected enum values are also treated as unknown evidence.
        if (evidence.game != IdentityStatus::Same) return pending("game-identity-unknown", 2000);
        if (evidence.steam != IdentityStatus::Same) return pending("steam-identity-unknown", 2000);
        if (evidence.account != AccountStatus::Same) return pending("account-unknown", 2000);
        if (evidence.tracker != TrackerStatus::Exact && evidence.tracker != TrackerStatus::Absent)
            return pending("tracker-unknown", 2000);
        if (evidence.primary != PrimaryStatus::Exact && evidence.primary != PrimaryStatus::Different)
            return pending("native-primary-unknown", 2000);
        if (evidence.validator != ValidatorStatus::Accepted && evidence.validator != ValidatorStatus::Rejected)
            return pending("validator-unknown", 2000);
        if (evidence.ui != UiStatus::RunningAndListed && evidence.ui != UiStatus::RunningMissingList &&
            evidence.ui != UiStatus::NotRunning) return pending("ui-unknown", 2000);

        // Identity-level contact is deliberately separate from Steam's Running
        // list. A non-Steam game's overview can remain present while localRunning
        // and inRunningList are false; that is a broken Steam association layer,
        // not proof that the original OS process or the transport disappeared.
        // The contact still requires the complete same-instance evidence set,
        // including the shared candidate, so it cannot retarget another game.
        const bool exactContact = evidence.tracker == TrackerStatus::Exact && evidence.primary == PrimaryStatus::Exact &&
            evidence.validator == ValidatorStatus::Accepted && evidence.sharedCandidate == CandidateStatus::Same;
        if (evidence.ui == UiStatus::RunningMissingList) return exactContact ?
            degraded("same-original-contact-running-list-missing", true) : degraded("missing-running-list");
        if (evidence.ui == UiStatus::NotRunning) return exactContact ?
            degraded("same-original-contact-steam-not-running", true) : degraded("native-overview-not-running");
        state_.contactConnected = exactContact;
        if (evidence.tracker == TrackerStatus::Absent) return degraded("tracker-absent");
        if (evidence.primary == PrimaryStatus::Different) return degraded("native-primary-different");
        if (evidence.validator == ValidatorStatus::Rejected) return degraded("validator-rejected");

        state_.phase = "linked";
        state_.reason = "exact-original-linked";
        state_.contactConnected = exactContact;
        state_.linked = true;
        everLinked_ = true;
        state_.dueTick = addSaturated(evidence.tick, 10000);
        return true;
    }

    bool active() const noexcept { return state_.active; }
    Snapshot snapshot() const { return state_; }

private:
    Snapshot state_;
    bool everLinked_ = false;

    static std::uint64_t addSaturated(std::uint64_t tick, std::uint64_t delay) noexcept {
        constexpr auto maximum = (std::numeric_limits<std::uint64_t>::max)();
        return delay > maximum - tick ? maximum : tick + delay;
    }

    bool seal(const char* phase, const char* reason) {
        state_.transportConnected = false;
        state_.contactConnected = false;
        state_.phase = phase;
        state_.reason = reason;
        state_.active = false;
        state_.terminal = true;
        state_.dueTick = 0;
        return true;
    }

    bool pending(const char* reason, std::uint64_t delay) {
        state_.phase = everLinked_ ? "degraded" : "waiting";
        state_.reason = reason;
        state_.dueTick = addSaturated(state_.lastTick, delay);
        return true;
    }

    bool degraded(const char* reason, bool contact = false) {
        state_.contactConnected = contact;
        state_.phase = contact ? "contact-degraded" : "degraded";
        state_.reason = reason;
        state_.dueTick = addSaturated(state_.lastTick, 2000);
        return true;
    }
};

} // namespace ymcc::steamlink

