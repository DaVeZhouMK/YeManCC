#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace ymcc::steamOriginal {

enum class Trigger { Startup, TargetChanged, SteamChanged, SuspendCapture, Wake };

struct Identity {
  std::uint32_t pid = 0, session = 0;
  std::uint64_t birth = 0;
  std::wstring path;

  bool operator==(const Identity&) const = default;
  bool valid() const { return pid && birth && !path.empty(); }
};

struct Scope {
  Identity parent, game, steam;
  std::uint64_t rulesEpoch = 0;

  bool operator==(const Scope&) const = default;
  bool valid() const {
    return parent.valid() && game.valid() && steam.valid() &&
           parent.session == game.session && parent.session == steam.session;
  }
};

struct Event {
  Trigger trigger = Trigger::Startup;
  std::uint64_t powerGeneration = 0, sequence = 0;
};

// accepted reports event eligibility. Only admit consumes the dispatch/
// observation budget; preview never reserves it. For Startup/Wake,
// recoveryAllowed permits the native engine to check recovery eligibility in
// that same job. It does not assert unhealthy state, authorize a mutation, or
// report a repair. The engine owns the exact Running/full-original-identity
// read, healthy baseline and all guards immediately before any mutation.
struct Decision {
  bool accepted = false;
  const char* reason = "not-armed";
  bool recoveryAllowed = false;
};

// Both methods are called under the host queue lock. Identities come only from
// the existing YMCC game valve. Keep this Admission alive across rule/scope
// invalidations. Valve touches, rule epochs and sequences never refund budgets.
class Admission {
 public:
  static constexpr std::size_t ledgerCapacity = 256;

  // Cheap read-only filter before expensive private-identity qualification.
  // This is not a reservation: after qualification, call admit under the lock
  // to recheck against any intervening consumption or power-generation advance.
  Decision preview(const Scope& scope, Event event, bool enabled) const {
    return evaluate(scope, event, enabled).decision;
  }

  // No health preflight or native reads. A normal wake gets one engine job;
  // the engine checks health and mutation eligibility within that same job.
  Decision admit(const Scope& scope, Event event, bool enabled) {
    const auto checked = evaluate(scope, event, enabled);
    // Preserve the monotonic fence for valid power events even when full.
    // evaluate/preview themselves never advance this fence.
    if (checked.generation) latestPower_ = checked.generation;
    if (!checked.decision.accepted) return checked.decision;

    // Append only. A cancelled/failed job or failed engine guard gives no refund.
    // If copying an identity throws, consumed_ is not incremented.
    ledger_[consumed_] = {scope.parent, scope.game, scope.steam,
                          event.trigger, checked.generation};
    ++consumed_;
    return checked.decision;
  }

  // Invalidating a transient host binding never invalidates event consumption
  // or the power-generation fence. This API intentionally has no reset path.
  void invalidate() {}

 private:
  struct ConsumedEvent {
    Identity parent, game, steam;
    Trigger trigger = Trigger::Startup;
    std::uint64_t powerGeneration = 0;
  };
  struct Evaluation {
    Decision decision;
    // Nonzero only for a supported, enabled, valid, non-stale power event.
    std::uint64_t generation = 0;
  };

  // Single source of validation, key matching, capacity and decision reasons.
  // No identity copies, ledger writes, reservations or power-fence updates.
  Evaluation evaluate(const Scope& scope, Event event, bool enabled) const {
    if (!enabled) return {{false, "disabled", false}};
    if (!scope.valid())
      return {{false, "unique-game-or-steam-unavailable", false}};

    const char* replayReason = nullptr;
    Decision allowed{true, "not-armed", false};
    bool powerEvent = false;
    switch (event.trigger) {
      case Trigger::Startup:
        replayReason = "startup-already-observed";
        allowed = {true, "startup-bound-original-check", true};
        break;
      case Trigger::TargetChanged:
        replayReason = "same-unique-target";
        allowed.reason = "healthy-reference-observation";
        break;
      case Trigger::SteamChanged:
        replayReason = "same-steam-session";
        allowed.reason = "steam-session-observation";
        break;
      case Trigger::SuspendCapture:
        replayReason = "suspend-already-observed";
        allowed.reason = "suspend-reference-check";
        powerEvent = true;
        break;
      case Trigger::Wake:
        replayReason = "wake-already-admitted";
        allowed = {true, "wake-bound-original-check", true};
        powerEvent = true;
        break;
      default: return {{false, "unsupported-trigger", false}};
    }

    if (powerEvent) {
      if (!event.powerGeneration)
        return {{false, "power-generation-unavailable", false}};
      if (event.powerGeneration < latestPower_)
        return {{false, "stale-power-generation", false}};
    }
    // Non-power events cannot acquire a fresh budget via arbitrary generations.
    const auto generation = powerEvent ? event.powerGeneration : 0;
    for (std::size_t i = 0; i < consumed_; ++i) {
      const auto& entry = ledger_[i];
      if (entry.trigger != event.trigger ||
          entry.powerGeneration != generation ||
          !(entry.steam == scope.steam) || !(entry.game == scope.game))
        continue;
      // Startup belongs to the Steam+game pair even if its parent changes.
      if (event.trigger == Trigger::Startup || entry.parent == scope.parent)
        return {{false, replayReason, false}, generation};
    }
    if (consumed_ == ledgerCapacity)
      return {{false, "event-ledger-exhausted", false}, generation};
    return {allowed, generation};
  }

  std::array<ConsumedEvent, ledgerCapacity> ledger_{};
  std::size_t consumed_ = 0;
  std::uint64_t latestPower_ = 0;
};

inline const char* name(Trigger trigger) {
  switch (trigger) {
    case Trigger::Startup: return "startup";
    case Trigger::TargetChanged: return "target-changed";
    case Trigger::SteamChanged: return "steam-changed";
    case Trigger::SuspendCapture: return "suspend-capture";
    case Trigger::Wake: return "wake";
  }
  return "unknown";
}

}  // namespace ymcc::steamOriginal
