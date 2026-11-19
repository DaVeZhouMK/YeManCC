#pragma once
#include "../deps/json/json.hpp"
#include <functional>
#include <cstdint>
#include <memory>
namespace ymcc::steamnative {class ReadScope;}
namespace ymcc::steamcommand {class Intent;class AvailabilityEvent;}
namespace ymcc::steamgamefacts {class ProcessGroupLease;class RequestCancellation;}
// Synchronous event job. Call handle on a dedicated YMCC worker, never the UI
// thread. All Steam debugger APIs stay on that exact calling OS thread.
namespace ymcc::steamgamefacts { class StartupHandoffEvidence; class HistoricalStartupHandoffEvidence; }
namespace ymcc::steamOriginalRecovery {
using Json = nlohmann::json;
using Emit = std::function<void(const Json&)>;
using Current = std::function<bool()>;
using WakeSignal = std::uintptr_t; // Borrowed host event; signals grant observation, never mutation.
// Required: parent/game/steam {pid,birth:string,path,session}, account64:string,
// dataRoot/logRoot:absolute, trigger {kind,generation,id,rulesEpoch},
// recoveryAllowed:bool. game also has gameId:string and appid:uint32.
// cef may be supplied explicitly; otherwise only Steam-owned CEF is resolved.
// dataRoot holds persistent references/consumption/config/images; evidenceRoot
// optionally separates clearable receipts. Production always supplies both.
// Optional recoveryRoute: "legacy-dynamic" or "semi-static" (default).
// The host schedules routes. Legacy uses exact known disk pairs, original raw
// typed loaded guards and a fresh getter-context capture. Semi-static keeps
// content/loaded contracts, relocated reads and the warm/cold source selection.
// Both share the existing durable original-instance publication rights.
// Optional seed: {baseline:<saved healthy observation>,context:<receipt>}.
// Duplicate rights are checked before source capture. Actual publication rights
// are durably consumed by a synchronous Current beforeMutation callback inside
// the driver, after its own read-only qualification and before its first remote
// allocation. Returning false/throwing rejects dispatch. Clean source/driver
// preflight failures do not consume; any failure after commit never refunds.
// New records use consumption.v2 with unchanged keys; old keys/disabled remain
// authoritative. Only an explicit, fully pinned original-instance audit of the
// known R7 loaded-contract failure may authorize one legacy transfer; all old
// ledger/disabled bytes stay intact, and unknown or written attempts cannot transfer.
// Healthy checks do not consume. Wake requires trigger.powerEventFileTime:string
// within 60 seconds: legacy accepts the host's current PBT resume/boundary
// witness; the host owns the independent semi-static wake admission.
// Startup permits one actual attempt per original Steam+game lifetime. Other kinds
// observe only. Missing healthy reference while lost is an explicit refusal.
Json handle(const Json& event, Emit emit = {}, Current current = {},
    Current observationCurrent = {}, WakeSignal wakeSignal = 0,
    WakeSignal cancelSignal = 0, WakeSignal identityCancelSignal = 0,
    std::shared_ptr<ymcc::steamnative::ReadScope> publicationScope = {},
    std::shared_ptr<ymcc::steamgamefacts::ProcessGroupLease> processGroup = {},
    std::shared_ptr<ymcc::steamgamefacts::RequestCancellation> businessRequestCancellation = {},
    std::shared_ptr<ymcc::steamcommand::Intent> mutationIntent = {},
    std::shared_ptr<ymcc::steamcommand::AvailabilityEvent> mutationAvailability = {},
    std::shared_ptr<const ymcc::steamgamefacts::StartupHandoffEvidence> startupHandoff = {},
    Current startupHandoffCurrent = {},
    std::shared_ptr<const ymcc::steamgamefacts::HistoricalStartupHandoffEvidence> historicalStartupHandoff = {},
    Current historicalStartupHandoffCurrent = {}); // Borrowed public Native scope events.
// mutationIntent coordinates only this original Steam instance. Startup arrives
// already promoted by the host BEFORE route admission; it never waits here.
// Wake remains passive throughout its existing observation, then waits only in
// the original physical-wake remainder before QueryActor/debugger/source work.
// The host keeps the intent and dedicated availability resource until handle
// returns through actual cleanup. A signal is not identity/admission authority.
// processGroup is an in-process public-owner lease; JSON cannot mint or replace
// it. A supplied lease must match the selected game, Steam and complete type2
// identity. It is a topology candidate, not a native role decision. The first
// existing typed sample freezes strict-single when P=F=selected; that path uses
// the original selected+Steam currentness, reference v1 and disabled group wire.
// Only a non-exact qualified two-member tuple enables full group currentness,
// the role contract and reference v2. Later samples cannot change the mode.
// The caller's original Current excludes unused helper lifetime; the engine
// applies full group guards only for its frozen group mode. Other counts retain
// the original exact-role path without silently manufacturing a group alias.
// The original account/power/native pins, both routes and durable rights remain
// mandatory. The private typed host call requires matching driver_port and
// real ABI6 DLL/embedded assets; final composition/qualification remains OPEN.
// startupHandoff is a separate historical launch source. A nonempty proof
// requires its public owner-current callback and exact current Steam/child tuple.
// Both remain retained through Finish. No native role or healthy reference is
// synthesized; first-lost/no-reference and dead-launcher/no-record still refuse.
// historicalStartupHandoff is a distinct owner-issued historical-parent source.
// It requires its own current callback and the exact Steam/child/full type2 ID.
// Supplying both handoff kinds is ambiguous and refuses; neither supplies
// native roles, a healthy reference or mutation authority. Both arguments
// remain retained through Finish with all original publication gates intact.
// Read-only cached state. Safe from the native host/UI/shutdown thread.
Json current_status();
bool active() noexcept;
// Emit callbacks must return promptly. Their exceptions never escape into the
// debugger owner. An unsupported restoration/DR/detach boundary emits
// {event:"unsupported-owner-held"}; handle then does not return while Steam
// lives. Shutdown must preserve this worker and delay process termination.
// No child process, anchor, helper exe, Node or UI operation is used.
}

// A group request additionally requires nonempty original Current and a retained
// request cancellation signal. Its consumer must synchronously cancel at its
// own target/rules/config/OFF/exit/request invalidation points, before replacing
// that scope. Neither metadata nor the group graph can supply this authority.
