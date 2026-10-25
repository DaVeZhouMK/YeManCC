// Pure in-memory policy tests. No platform, process, power, network or IO APIs.
#ifdef YMCC_STEAM_ORIGINAL_POLICY_LEGACY_HEADER
#include YMCC_STEAM_ORIGINAL_POLICY_LEGACY_HEADER
#else
#include "../native/steam_original_recovery_policy.h"
#endif

#include <cstdio>
#include <cstring>
#include <limits>

using namespace ymcc::steamOriginal;

namespace {
int checks = 0;
int failures = 0;
int cases = 0;

void check(bool condition, const char* message, int line) {
  ++checks;
  if (!condition) {
    ++failures;
    std::printf("  FAIL line %d: %s\n", line, message);
  }
}
#define CHECK(condition, message) check((condition), (message), __LINE__)

Scope sample() {
  return {{101, 7, 1001, L"C:\\YMCC\\YeManCC.exe"},
          {202, 7, 2002, L"D:\\Games\\game.exe"},
          {303, 7, 3003, L"C:\\Steam\\steam.exe"}, 1};
}

constexpr Trigger triggers[] = {Trigger::Startup, Trigger::TargetChanged,
                                Trigger::SteamChanged, Trigger::SuspendCapture,
                                Trigger::Wake};
constexpr Identity Scope::* roles[] = {&Scope::parent, &Scope::game, &Scope::steam};

void ruleEpochDoesNotRefund() {
  for (auto trigger : triggers) {
    Admission policy;
    auto scope = sample();
    CHECK(policy.admit(scope, {trigger, 9, 1}, true).accepted,
          "first event must be observed");
    for (auto epoch : {0ULL, 2ULL, 99ULL, 1ULL}) {
      scope.rulesEpoch = epoch;
      CHECK(!policy.admit(scope, {trigger, 9, epoch}, true).accepted,
            "rule epoch and sequence must not refund a consumed event");
    }
  }
}

void invalidateDoesNotRefund() {
  for (auto trigger : triggers) {
    Admission policy;
    const auto scope = sample();
    CHECK(policy.admit(scope, {trigger, 9, 1}, true).accepted,
          "first event must be observed");
    for (int i = 0; i != 3; ++i) {
      policy.invalidate();
      CHECK(!policy.admit(scope, {trigger, 9, 2}, true).accepted,
            "ordinary invalidate must preserve consumption");
    }
  }
}

void returningScopeDoesNotRefund() {
  for (auto trigger : triggers) {
    Admission policy;
    const auto original = sample();
    auto other = original;
    ++other.game.birth;
    CHECK(policy.admit(original, {trigger, 9, 1}, true).accepted,
          "original scope must be observed");
    CHECK(policy.admit(other, {trigger, 9, 2}, true).accepted,
          "new game process must be independently observed");
    CHECK(!policy.admit(original, {trigger, 9, 3}, true).accepted,
          "A to B to A must not replay A");
    CHECK(!policy.admit(other, {trigger, 9, 4}, true).accepted,
          "A to B to A must not replay B");
  }
}

void repeatedTargetTouches() {
  Admission policy;
  auto scope = sample();
  int accepted = 0;
  for (std::uint64_t i = 0; i != 1000; ++i) {
    scope.rulesEpoch = i;
    if (i % 3 == 0) policy.invalidate();
    const auto d = policy.admit(scope, {Trigger::TargetChanged, i, i}, true);
    accepted += d.accepted ? 1 : 0;
    CHECK(!d.recoveryAllowed, "target touches are observation only");
  }
  CHECK(accepted == 1, "1000 identical target touches must accept exactly once");
}

void startupBelongsToSteamGamePair() {
  Admission policy;
  const auto original = sample();
  CHECK(policy.admit(original, {Trigger::Startup}, true).accepted,
        "first Steam plus game startup must be admitted");
  auto changed = original;
  ++changed.parent.pid;
  ++changed.parent.birth;
  changed.parent.path += L".new";
  ++changed.rulesEpoch;
  policy.invalidate();
  CHECK(!policy.admit(changed, {Trigger::Startup, 55, 55}, true).accepted,
        "a new parent must not renew startup for the same Steam plus game");
  changed = original;
  ++changed.game.birth;
  CHECK(policy.admit(changed, {Trigger::Startup}, true).accepted,
        "a new game process may have its own startup");
  changed = original;
  ++changed.steam.birth;
  CHECK(policy.admit(changed, {Trigger::Startup}, true).accepted,
        "a new Steam process may have its own startup");
  CHECK(!policy.admit(original, {Trigger::Startup}, true).accepted,
        "returning to the original Steam plus game must remain consumed");
}

void powerFenceSurvivesScopeAndInvalidation() {
  Admission policy;
  auto scope = sample();
  CHECK(policy.admit(scope, {Trigger::SuspendCapture, 10}, true).accepted,
        "capture may establish the current power generation");
  CHECK(policy.admit(scope, {Trigger::Wake, 10}, true).accepted,
        "capture and wake are distinct events within one generation");
  CHECK(!policy.admit(scope, {Trigger::Wake, 9}, true).accepted,
        "an older wake must be rejected");
  CHECK(policy.admit(scope, {Trigger::Wake, 11}, true).accepted,
        "a newer wake may be observed");
  ++scope.game.birth;
  policy.invalidate();
  CHECK(!policy.admit(scope, {Trigger::SuspendCapture, 10}, true).accepted,
        "scope changes must preserve the global power fence");
  CHECK(!policy.admit(scope, {Trigger::Wake, 10}, true).accepted,
        "invalidate must preserve the global power fence");
  CHECK(policy.admit(scope, {Trigger::SuspendCapture, 11}, true).accepted,
        "the current generation may observe a new process identity");
}

void disabledAndInvalidDoNotConsume() {
  for (auto trigger : triggers) {
    Admission disabled;
    const auto scope = sample();
    const auto d = disabled.admit(scope, {trigger, 500, 1}, false);
    CHECK(!d.accepted && !d.recoveryAllowed,
          "disabled policy must not create any action");
    CHECK(disabled.admit(scope, {trigger, 5, 2}, true).accepted,
          "disabled requests must not consume or advance the power fence");

    for (auto role : roles) {
      for (int field = 0; field != 4; ++field) {
        Admission invalid;
        auto bad = scope;
        auto& identity = bad.*role;
        if (field == 0) identity.pid = 0;
        if (field == 1) identity.birth = 0;
        if (field == 2) identity.path.clear();
        if (field == 3) ++identity.session;
        CHECK(!bad.valid(), "test fixture must be invalid");
        const auto denied = invalid.admit(bad, {trigger, 500, 1}, true);
        CHECK(!denied.accepted && !denied.recoveryAllowed,
              "invalid identity must not create any action");
        CHECK(invalid.admit(scope, {trigger, 5, 2}, true).accepted,
              "invalid requests must not consume or advance the power fence");
      }
    }
  }
}

void triggerAndPowerKeyAreIndependent() {
  Admission policy;
  const auto scope = sample();
  for (auto trigger : triggers) {
    CHECK(policy.admit(scope, {trigger, 1, 1}, true).accepted,
          "each trigger has independent consumption");
    CHECK(!policy.admit(scope, {trigger, 1, 999}, true).accepted,
          "sequence changes must not replay any trigger");
  }
  for (auto trigger : {Trigger::Startup, Trigger::TargetChanged,
                       Trigger::SteamChanged})
    CHECK(!policy.admit(scope, {trigger, 99, 1000}, true).accepted,
          "non-power events must ignore fabricated generations");
  for (auto trigger : {Trigger::SuspendCapture, Trigger::Wake}) {
    CHECK(policy.admit(scope, {trigger, 2, 2}, true).accepted,
          "power events have an independent budget per new generation");
    CHECK(!policy.admit(scope, {trigger, 2, 3}, true).accepted,
          "each new power budget is still consumable only once");
  }
}

#ifndef YMCC_STEAM_ORIGINAL_POLICY_LEGACY_HEADER
void expect(const Decision& d, bool accepted, bool recovery,
            const char* reason, int line) {
  check(d.accepted == accepted, "unexpected admission result", line);
  check(d.recoveryAllowed == recovery, "unexpected recovery permission", line);
  check(std::strcmp(d.reason, reason) == 0, "unexpected decision reason", line);
}
#define EXPECT(d, accepted, recovery, reason) \
  expect((d), (accepted), (recovery), (reason), __LINE__)

void legacyApiAndNames() {
  Admission policy;
  const auto scope = sample();
  EXPECT(policy.admit(scope, {Trigger::Startup}, true), true, true,
         "startup-bound-original-check");
  EXPECT(policy.admit(scope, {Trigger::Wake, 1}, true), true, true,
         "wake-bound-original-check");
  EXPECT(policy.admit(scope, {Trigger::SuspendCapture, 1}, true), true, false,
         "suspend-reference-check");
  EXPECT(policy.admit(scope, {Trigger::SteamChanged}, true), true, false,
         "steam-session-observation");
  EXPECT(policy.admit(scope, {Trigger::TargetChanged}, true), true, false,
         "healthy-reference-observation");
  const char* expected[] = {"startup", "target-changed", "steam-changed",
                            "suspend-capture", "wake"};
  for (std::size_t i = 0; i != 5; ++i)
    CHECK(std::strcmp(name(triggers[i]), expected[i]) == 0,
          "existing trigger names must remain compatible");
  CHECK(std::strcmp(name(static_cast<Trigger>(99)), "unknown") == 0,
        "unknown trigger name must remain safe");
}

void identityComponentsCreateNewObservations() {
  for (auto trigger : {Trigger::TargetChanged, Trigger::Wake}) {
    Admission policy;
    const auto original = sample();
    CHECK(policy.admit(original, {trigger, 7}, true).accepted,
          "original identity must observe once");
    for (auto role : roles) {
      for (int field = 0; field != 3; ++field) {
        auto changed = original;
        auto& identity = changed.*role;
        if (field == 0) ++identity.pid;
        if (field == 1) ++identity.birth;
        if (field == 2) identity.path += L".new";
        CHECK(changed.valid(), "changed identity must still be valid");
        CHECK(policy.admit(changed, {trigger, 7}, true).accepted,
              "PID, birth and path of each process are part of identity");
        CHECK(!policy.admit(changed, {trigger, 7}, true).accepted,
              "a newly observed identity must also become consumed");
        CHECK(!policy.admit(original, {trigger, 7}, true).accepted,
              "new identity must not refund the original");
      }
    }
    auto changedSession = original;
    for (auto role : roles) ++(changedSession.*role).session;
    CHECK(changedSession.valid(), "consistent session changes must be valid");
    CHECK(policy.admit(changedSession, {trigger, 7}, true).accepted,
          "session is part of process identity");
    CHECK(!policy.admit(changedSession, {trigger, 7}, true).accepted,
          "the new session is consumable only once");
  }
}

void startupAndWakeDispatchWithoutHostPreflight() {
  for (auto trigger : {Trigger::Startup, Trigger::Wake}) {
    Admission policy;
    const auto scope = sample();
    const auto reason = trigger == Trigger::Startup ?
        "startup-bound-original-check" : "wake-bound-original-check";
    EXPECT(policy.admit(scope, {trigger, 10}, true), true, true, reason);
    const auto duplicate = policy.admit(scope, {trigger, 10, 999}, true);
    CHECK(!duplicate.accepted && !duplicate.recoveryAllowed,
          "a repeated dispatch must not schedule another engine check");
  }
}

// This sink supplies simulated engine outcomes, not a second health policy or
// an engine implementation. Counters are in memory; no native reads/writes run.
// These cases verify that dispatch is independent of a later mutation decision
// and that every completed/cancelled/failed job keeps its dispatch consumed.
enum class JobOutcome {
  Healthy, UnknownHealth, IdentityMismatch, MissingBaseline, GuardRevoked,
  Cancelled, Failed, MutationEligible
};
struct OfflineJobSink {
  int dispatches = 0, qualificationChecks = 0, mutationAttempts = 0;
  void dispatch(const Decision& d, JobOutcome outcome) {
    if (!d.accepted || !d.recoveryAllowed) return;
    ++dispatches;
    ++qualificationChecks;
    if (outcome == JobOutcome::MutationEligible) ++mutationAttempts;
  }
};

void engineJobOutcomeCannotRefundDispatch() {
  for (auto trigger : {Trigger::Startup, Trigger::Wake}) {
    for (auto outcome : {JobOutcome::Healthy, JobOutcome::UnknownHealth,
                         JobOutcome::IdentityMismatch, JobOutcome::MissingBaseline,
                         JobOutcome::GuardRevoked, JobOutcome::Cancelled,
                         JobOutcome::Failed, JobOutcome::MutationEligible}) {
      Admission policy;
      OfflineJobSink engine;
      auto scope = sample();
      const auto first = policy.admit(scope, {trigger, 20}, true);
      CHECK(first.accepted && first.recoveryAllowed,
            "startup/wake must dispatch without host health evidence");
      CHECK(engine.qualificationChecks == 0 && engine.mutationAttempts == 0,
            "admission itself does not perform an engine check or mutation");
      engine.dispatch(first, outcome);
      CHECK(engine.dispatches == 1 && engine.qualificationChecks == 1,
            "one admitted event provides one engine job/check");
      CHECK(engine.mutationAttempts == (outcome == JobOutcome::MutationEligible ? 1 : 0),
            "dispatch permission is distinct from the engine mutation outcome");
      for (std::uint64_t i = 0; i != 1000; ++i) {
        ++scope.rulesEpoch;
        policy.invalidate();
        const auto duplicate = policy.admit(scope, {trigger, 20, i + 1}, true);
        CHECK(!duplicate.accepted && !duplicate.recoveryAllowed,
              "a later engine outcome cannot refund the consumed dispatch");
        engine.dispatch(duplicate, JobOutcome::MutationEligible);
      }
      CHECK(engine.dispatches == 1 && engine.qualificationChecks == 1,
            "1000 duplicate events cause no extra engine jobs or reads");
      CHECK(engine.mutationAttempts == (outcome == JobOutcome::MutationEligible ? 1 : 0),
            "repeated events cannot upgrade a previous no-mutation outcome");
    }
  }
}
void consumedDispatchCannotBeRefunded() {
  Admission policy;
  const auto original = sample();
  EXPECT(policy.admit(original, {Trigger::Wake, 20}, true),
         true, true, "wake-bound-original-check");
  auto scope = original;
  ++scope.rulesEpoch;
  policy.invalidate();
  EXPECT(policy.admit(scope, {Trigger::Wake, 20, 2}, true),
         false, false, "wake-already-admitted");
  ++scope.game.birth;
  EXPECT(policy.admit(scope, {Trigger::Wake, 20}, true),
         true, true, "wake-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::Wake, 20}, true),
         false, false, "wake-already-admitted");
  EXPECT(policy.admit(original, {Trigger::Wake, 20}, false),
         false, false, "disabled");
  EXPECT(policy.admit(original, {Trigger::Wake, 20}, true),
         false, false, "wake-already-admitted");
}

void invalidAndDisabledCannotDispatch() {
  Admission policy;
  const auto scope = sample();
  EXPECT(policy.admit(scope, {Trigger::Wake, 1000}, false),
         false, false, "disabled");
  auto bad = scope;
  bad.parent.birth = 0;
  EXPECT(policy.admit(bad, {Trigger::Wake, 1000}, true),
         false, false, "unique-game-or-steam-unavailable");
  EXPECT(policy.admit(scope, {Trigger::Wake, 1}, true),
         true, true, "wake-bound-original-check");
}

void invalidEventsDoNotAdvanceFence() {
  Admission policy;
  const auto scope = sample();
  for (auto trigger : {Trigger::SuspendCapture, Trigger::Wake})
    EXPECT(policy.admit(scope, {trigger, 0}, true), false, false,
           "power-generation-unavailable");
  EXPECT(policy.admit(scope, {static_cast<Trigger>(99), 9999}, true),
         false, false, "unsupported-trigger");
  EXPECT(policy.admit(scope, {Trigger::Wake, 5}, true),
         true, true, "wake-bound-original-check");
  EXPECT(policy.admit(scope, {Trigger::Wake, 4}, true),
         false, false, "stale-power-generation");
}

void maximumGenerationCannotWrap() {
  Admission policy;
  const auto scope = sample();
  const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
  EXPECT(policy.admit(scope, {Trigger::Wake, maximum}, true),
         true, true, "wake-bound-original-check");
  policy.invalidate();
  EXPECT(policy.admit(scope, {Trigger::Wake, maximum}, true),
         false, false, "wake-already-admitted");
  EXPECT(policy.admit(scope, {Trigger::Wake, 1}, true),
         false, false, "stale-power-generation");
  EXPECT(policy.admit(scope, {Trigger::Wake, 0}, true),
         false, false, "power-generation-unavailable");
}

void ledgerExhaustionFailsClosedWithoutEviction() {
  Admission policy;
  const auto original = sample();
  EXPECT(policy.admit(original, {Trigger::Startup}, true),
         true, true, "startup-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::Wake, 40}, true),
         true, true, "wake-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::SuspendCapture, 40}, true),
         true, false, "suspend-reference-check");
  for (std::size_t i = 3; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i;
    CHECK(policy.admit(scope, {Trigger::TargetChanged}, true).accepted,
          "all remaining ledger slots should be available");
  }
  for (int i = 0; i != 3; ++i) {
    policy.invalidate();
    auto scope = original;
    scope.rulesEpoch += static_cast<std::uint64_t>(i);
    ++scope.parent.birth;
    EXPECT(policy.admit(scope, {Trigger::Startup, 123, 123}, true),
           false, false, "startup-already-observed");
    EXPECT(policy.admit(original, {Trigger::Wake, 40}, true),
           false, false, "wake-already-admitted");
    EXPECT(policy.admit(original, {Trigger::SuspendCapture, 40}, true),
           false, false, "suspend-already-observed");
  }
  auto newScope = original;
  newScope.game.birth += Admission::ledgerCapacity + 1;
  for (auto trigger : triggers) {
    EXPECT(policy.admit(newScope, {trigger, 40}, true),
           false, false, "event-ledger-exhausted");
  }
  for (std::size_t i = 3; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i;
    ++scope.rulesEpoch;
    EXPECT(policy.admit(scope, {Trigger::TargetChanged, 999, 999}, true),
           false, false, "same-unique-target");
  }
  EXPECT(policy.admit(newScope, {Trigger::Wake, 41}, true),
         false, false, "event-ledger-exhausted");
  policy.invalidate();
  EXPECT(policy.admit(newScope, {Trigger::Wake, 40}, true),
         false, false, "stale-power-generation");
}

void duplicateTouchesDoNotSpendExtraLedgerSlots() {
  Admission policy;
  const auto original = sample();
  CHECK(policy.admit(original, {Trigger::TargetChanged}, true).accepted,
        "first target must consume one slot");
  for (std::uint64_t i = 0; i != 1000; ++i) {
    auto scope = original;
    scope.rulesEpoch = i;
    policy.invalidate();
    CHECK(!policy.admit(scope, {Trigger::TargetChanged, i, i}, true).accepted,
          "a repeated target never spends another slot");
  }
  for (std::size_t i = 1; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i;
    CHECK(policy.admit(scope, {Trigger::TargetChanged}, true).accepted,
          "duplicates must leave all other slots available");
  }
  auto scope = original;
  scope.game.birth += Admission::ledgerCapacity;
  EXPECT(policy.admit(scope, {Trigger::TargetChanged}, true),
         false, false, "event-ledger-exhausted");
}

void previewRepeatedlyDoesNotConsumeBudget() {
  const char* reasons[] = {"startup-bound-original-check",
      "healthy-reference-observation", "steam-session-observation",
      "suspend-reference-check", "wake-bound-original-check"};
  for (std::size_t index = 0; index != 5; ++index) {
    Admission policy;
    const Admission& readOnly = policy;  // preview must work on a const object.
    auto scope = sample();
    const auto trigger = triggers[index];
    const bool mayCheckRecovery = trigger == Trigger::Startup || trigger == Trigger::Wake;
    for (std::uint64_t i = 0; i != 1000; ++i) {
      scope.rulesEpoch = i;
      EXPECT(readOnly.preview(scope, {trigger, 9, i}, true),
             true, mayCheckRecovery, reasons[index]);
    }
    int accepted = 0;
    for (std::uint64_t i = 0; i != 1000; ++i) {
      scope.rulesEpoch = i;
      const auto d = policy.admit(scope, {trigger, 9, i}, true);
      accepted += d.accepted ? 1 : 0;
      CHECK(d.recoveryAllowed == (d.accepted && mayCheckRecovery),
            "only the one admitted startup/wake may dispatch eligibility checks");
    }
    CHECK(accepted == 1,
          "1000 previews spend no budget; 1000 admits still accept exactly once");
    CHECK(!readOnly.preview(scope, {trigger, 9, 1001}, true).accepted,
          "preview must reject the event after its one actual consumption");
    for (std::size_t i = 1; i < Admission::ledgerCapacity; ++i) {
      auto fresh = sample();
      fresh.game.birth += i;
      CHECK(policy.admit(fresh, {Trigger::TargetChanged}, true).accepted,
            "1000 previews must leave every other ledger slot available");
    }
    auto overflow = sample();
    overflow.game.birth += Admission::ledgerCapacity;
    EXPECT(policy.admit(overflow, {Trigger::TargetChanged}, true),
           false, false, "event-ledger-exhausted");
  }
}

void previewNeverAdvancesPowerFence() {
  for (auto trigger : {Trigger::SuspendCapture, Trigger::Wake}) {
    Admission policy;
    const Admission& readOnly = policy;
    const auto scope = sample();
    const auto reason = trigger == Trigger::Wake ?
        "wake-bound-original-check" : "suspend-reference-check";
    const bool mayCheckRecovery = trigger == Trigger::Wake;
    const auto maximum = (std::numeric_limits<std::uint64_t>::max)();
    EXPECT(readOnly.preview(scope, {trigger, maximum}, true),
           true, mayCheckRecovery, reason);
    EXPECT(readOnly.preview(scope, {trigger, 5}, true),
           true, mayCheckRecovery, reason);
    EXPECT(policy.admit(scope, {trigger, 5}, true),
           true, mayCheckRecovery, reason);
    EXPECT(readOnly.preview(scope, {trigger, 4}, true),
           false, false, "stale-power-generation");
    EXPECT(readOnly.preview(scope, {trigger, maximum}, true),
           true, mayCheckRecovery, reason);
    EXPECT(readOnly.preview(scope, {trigger, 6}, true),
           true, mayCheckRecovery, reason);
    EXPECT(policy.admit(scope, {trigger, 6}, true),
           true, mayCheckRecovery, reason);
  }
}

void previewRejectsInvalidWithoutMutation() {
  for (auto trigger : triggers) {
    for (auto role : roles) {
      for (int field = 0; field != 4; ++field) {
        Admission policy;
        const Admission& readOnly = policy;
        const auto scope = sample();
        auto bad = scope;
        auto& identity = bad.*role;
        if (field == 0) identity.pid = 0;
        if (field == 1) identity.birth = 0;
        if (field == 2) identity.path.clear();
        if (field == 3) ++identity.session;
        EXPECT(readOnly.preview(scope, {trigger, 9999}, false),
               false, false, "disabled");
        EXPECT(readOnly.preview(bad, {trigger, 9999}, true),
               false, false, "unique-game-or-steam-unavailable");
        CHECK(readOnly.preview(scope, {trigger, 1}, true).accepted,
              "rejected previews must not consume or advance the power fence");
        CHECK(policy.admit(scope, {trigger, 1}, true).accepted,
              "admission after rejected previews must retain its whole budget");
      }
    }
  }
  Admission policy;
  const Admission& readOnly = policy;
  const auto scope = sample();
  for (auto trigger : {Trigger::SuspendCapture, Trigger::Wake})
    EXPECT(readOnly.preview(scope, {trigger, 0}, true),
           false, false, "power-generation-unavailable");
  EXPECT(readOnly.preview(scope, {static_cast<Trigger>(99), 9999}, true),
         false, false, "unsupported-trigger");
  EXPECT(policy.admit(scope, {Trigger::Wake, 1}, true),
         true, true, "wake-bound-original-check");
}

void previewUsesTheSameConsumptionKeys() {
  const char* reasons[] = {"startup-already-observed", "same-unique-target",
      "same-steam-session", "suspend-already-observed", "wake-already-admitted"};
  for (std::size_t index = 0; index != 5; ++index) {
    Admission policy;
    const Admission& readOnly = policy;
    const auto original = sample();
    const auto trigger = triggers[index];
    CHECK(policy.admit(original, {trigger, 9}, true).accepted,
          "fixture must consume the original event");
    auto changed = original;
    ++changed.rulesEpoch;
    policy.invalidate();
    EXPECT(readOnly.preview(changed, {trigger, 9, 999}, true),
           false, false, reasons[index]);
    EXPECT(policy.admit(changed, {trigger, 9, 999}, true),
           false, false, reasons[index]);
    ++changed.parent.birth;
    if (trigger == Trigger::Startup) {
      EXPECT(readOnly.preview(changed, {trigger, 9}, true),
             false, false, "startup-already-observed");
    } else {
      CHECK(readOnly.preview(changed, {trigger, 9}, true).accepted,
            "preview must allow a new parent identity for non-startup events");
      CHECK(policy.admit(changed, {trigger, 9}, true).accepted,
            "admit must agree with the unconsumed new identity preview");
      EXPECT(readOnly.preview(changed, {trigger, 9}, true),
             false, false, reasons[index]);
    }
    changed = original;
    ++changed.game.birth;
    CHECK(readOnly.preview(changed, {trigger, 9}, true).accepted,
          "a new game process is independently eligible at preview");
    CHECK(policy.admit(changed, {trigger, 9}, true).accepted,
          "admit must agree with the new game identity preview");
    EXPECT(readOnly.preview(original, {trigger, 9}, true),
           false, false, reasons[index]);
  }
}

void previewFullLedgerNeverMutates() {
  Admission policy;
  const Admission& readOnly = policy;
  const auto original = sample();
  EXPECT(policy.admit(original, {Trigger::Wake, 9}, true),
         true, true, "wake-bound-original-check");
  for (std::size_t i = 1; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i;
    CHECK(policy.admit(scope, {Trigger::TargetChanged}, true).accepted,
          "fixture must fill every remaining ledger slot");
  }
  auto newScope = original;
  newScope.game.birth += Admission::ledgerCapacity + 1;
  for (std::uint64_t i = 0; i != 1000; ++i)
    EXPECT(readOnly.preview(newScope, {Trigger::Wake, 10000 + i}, true),
           false, false, "event-ledger-exhausted");
  EXPECT(readOnly.preview(original, {Trigger::Wake, 9}, true),
         false, false, "wake-already-admitted");
  EXPECT(readOnly.preview(newScope, {Trigger::Wake, 8}, true),
         false, false, "stale-power-generation");
  for (auto trigger : {Trigger::Startup, Trigger::TargetChanged, Trigger::SteamChanged})
    EXPECT(readOnly.preview(newScope, {trigger}, true),
           false, false, "event-ledger-exhausted");
  for (std::size_t i = 1; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i;
    EXPECT(readOnly.preview(scope, {Trigger::TargetChanged}, true),
           false, false, "same-unique-target");
  }
  // A rejected real admit still observes the valid power generation, matching
  // the original monotonic fence semantics. A rejected preview never does.
  EXPECT(policy.admit(newScope, {Trigger::Wake, 10}, true),
         false, false, "event-ledger-exhausted");
  EXPECT(readOnly.preview(original, {Trigger::Wake, 9}, true),
         false, false, "stale-power-generation");
}

void previewDoesNotReserveOrSkipRecheck() {
  Admission policy;
  const auto original = sample();
  auto other = original;
  ++other.game.birth;
  EXPECT(policy.preview(original, {Trigger::Wake, 10}, true),
         true, true, "wake-bound-original-check");
  EXPECT(policy.admit(other, {Trigger::Wake, 11}, true),
         true, true, "wake-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::Wake, 10}, true),
         false, false, "stale-power-generation");
  EXPECT(policy.preview(original, {Trigger::Startup}, true),
         true, true, "startup-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::Startup}, true),
         true, true, "startup-bound-original-check");
  EXPECT(policy.admit(original, {Trigger::Startup}, true),
         false, false, "startup-already-observed");

  Admission nearlyFull;
  for (std::size_t i = 1; i < Admission::ledgerCapacity; ++i) {
    auto scope = original;
    scope.game.birth += i + 1;
    CHECK(nearlyFull.admit(scope, {Trigger::TargetChanged}, true).accepted,
          "fixture must leave exactly one free ledger slot");
  }
  CHECK(nearlyFull.preview(original, {Trigger::TargetChanged}, true).accepted,
        "preview must not reserve the last slot");
  CHECK(nearlyFull.preview(other, {Trigger::TargetChanged}, true).accepted,
        "a second candidate may preview the same unreserved last slot");
  CHECK(nearlyFull.admit(original, {Trigger::TargetChanged}, true).accepted,
        "first qualified candidate consumes the final slot");
  EXPECT(nearlyFull.admit(other, {Trigger::TargetChanged}, true),
         false, false, "event-ledger-exhausted");
}
#endif

void run(const char* title, void (*test)()) {
  ++cases;
  const int before = failures;
  test();
  std::printf("%s %s\n", failures == before ? "PASS" : "FAIL", title);
}
}  // namespace

int main() {
  run("rule epoch never refunds consumption", ruleEpochDoesNotRefund);
  run("ordinary invalidate never refunds consumption", invalidateDoesNotRefund);
  run("returning scope never refunds consumption", returningScopeDoesNotRefund);
  run("1000 identical target touches accept once", repeatedTargetTouches);
  run("startup belongs to the Steam plus game pair", startupBelongsToSteamGamePair);
  run("latest power survives scope and invalidation", powerFenceSurvivesScopeAndInvalidation);
  run("disabled and invalid requests consume nothing", disabledAndInvalidDoNotConsume);
  run("trigger and generation keys are independent", triggerAndPowerKeyAreIndependent);
#ifndef YMCC_STEAM_ORIGINAL_POLICY_LEGACY_HEADER
  run("existing API and trigger names", legacyApiAndNames);
  run("full process identity creates new observations", identityComponentsCreateNewObservations);
  run("startup and wake dispatch without host preflight", startupAndWakeDispatchWithoutHostPreflight);
  run("engine outcomes never refund dispatch or repeat reads", engineJobOutcomeCannotRefundDispatch);
  run("admitted engine dispatches cannot be refunded", consumedDispatchCannotBeRefunded);
  run("invalid or disabled cannot dispatch", invalidAndDisabledCannotDispatch);
  run("invalid events cannot advance the power fence", invalidEventsDoNotAdvanceFence);
  run("maximum generation cannot wrap", maximumGenerationCannotWrap);
  run("full ledger fails closed and never evicts", ledgerExhaustionFailsClosedWithoutEviction);
  run("duplicate touches consume only one ledger slot", duplicateTouchesDoNotSpendExtraLedgerSlots);
  run("1000 const previews spend no admission budget", previewRepeatedlyDoesNotConsumeBudget);
  run("preview never advances the power-generation fence", previewNeverAdvancesPowerFence);
  run("invalid and disabled previews have no side effects", previewRejectsInvalidWithoutMutation);
  run("preview uses the same consumption keys and reasons", previewUsesTheSameConsumptionKeys);
  run("full-ledger preview never advances or evicts", previewFullLedgerNeverMutates);
  run("admit rechecks unreserved previews after intervening events", previewDoesNotReserveOrSkipRecheck);
#endif
  std::printf("%d cases, %d checks, %d failures\n", cases, checks, failures);
  return failures == 0 ? 0 : 1;
}
