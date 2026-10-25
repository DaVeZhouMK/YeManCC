#pragma once
#include "../deps/json/json.hpp"
#include <functional>
#include <cstdint>
// Synchronous event job. Call handle on a dedicated YMCC worker, never the UI
// thread. All Steam debugger APIs stay on that exact calling OS thread.
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
    Current observationCurrent = {}, WakeSignal wakeSignal = 0);
// Read-only cached state. Safe from the native host/UI/shutdown thread.
Json current_status();
bool active() noexcept;
// Emit callbacks must return promptly. Their exceptions never escape into the
// debugger owner. An unsupported restoration/DR/detach boundary emits
// {event:"unsupported-owner-held"}; handle then does not return while Steam
// lives. Shutdown must preserve this worker and delay process termination.
// No child process, anchor, helper exe, Node or UI operation is used.
}
