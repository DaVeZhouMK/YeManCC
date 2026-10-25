# STA counterexamples: prepare, fault raw acceptance, ports, replay

This adds only four new files. It preserves every integrated STA/bootstrap/
factory source, original assertion, failure ID/message, timing and budget.
Worker execution is limited to Node preparation/tests and the fixed approved
raw PE reader. No producer, target, STA thread, Dispatcher, SDK, device or
installation process is started here.

## Original roles, order, and contracts

The unchanged `sta-proof.mjs` loops positive first and then these five roles.
This sidecar starts at those negative roles; it does not repeat positive.
Every role uses exactly **clean current Host -> fault -> ports -> sta-init**.
The parent's already ported positive Host is a reference only and is never used
as the fault input.

| Order / role | Original generator mode | Only mutated MethodDef | Expected failed cases / fragment |
| --- | --- | --- | --- |
| 1 self-stop | fault-sta-self-stop | RealHcBackend.Dispose() | sta-disposal-from-own-thread-stops-pump / Disposal on the lifecycle STA must stop its own message pump |
| 2 startup-stop | fault-sta-startup-stop | RealHcBackend.StaDispatcherLoop() | sta-disposal-during-startup-does-not-leak-pump / A disposed pre-publication STA must not enter an unowned message pump |
| 3 ended-owner | fault-sta-ended-owner | RealHcBackend.StaDispatcherLoop() | sta-ended-pump-cannot-be-reused / Expected ObjectDisposedException; got |
| 4 start-atomic | fault-sta-start-atomic | RealHcBackend.EnsureStaDispatcher() | sta-concurrent-cold-start-has-one-owner / The actual Thread.Start boundary must remain inside its published owner gate; sta-dispose-before-thread-start-does-not-join-unstarted / Concurrent disposal must not Join an unstarted thread |
| 5 pending-work | fault-sta-pending-work | RealHcBackend/StaWorkItem`1.RejectUnexecuted() | sta-pump-exit-rejects-pending-public-work / Pending public work must be rejected promptly by its ended dispatcher |

Full signatures are read from the pinned raw metadata, including the exact
nested generic signature, and stored in the basis; matching never uses RID.
Each fault generator is **30000ms**, each subsequent `sta-leaf-ports` generator
is **30000ms**, and each `sta-init` replay is **45000ms**, expected exit **2**.
The original source case and evidence sets contain all **16** IDs. Every case's
owned thread cleanup remains required; nativeAttempts=0, Host Main=false,
hardwareIo=false, and the exact generated candidate SHA remain required.

The positive minimum remains **100** (parent observed16/133 is separate parent
evidence). The original negative branch does not impose the positive check
minimum or require all startGateHeld values true: start-atomic intentionally
violates that gate. Its original two failed assertions must remain failures.
All other original failure sets/error substrings are preserved exactly. No
counts or failure expectations are adjusted to fit guard behavior.

## First executable generation endpoints

```powershell
& $fixedNode "$moduleRoot/prepare-sta-counterexamples.mjs" `
  --mode prepare-only --inputs $inputsJson --sha256 $inputsSha `
  --out-dir $freshParentOwnedDirectory
```

Input schemaVersion1 / kind `onex-sta-counterexamples-prepare` requires SHA pins
for staInputs, staPrepareModule, staBasis, cleanRaw, rawReader,
positiveReference, and sources (sta-proof.mjs, StaCases.cs, FactoryCases.cs,
Harness.cs, HarnessLoadContexts.cs). `STA-COUNTEREXAMPLES-INPUTS.json` is the
concrete input envelope. Existing dependencies and their exact hashes are in
PATCH-MANIFEST.json. The source/compiled original37 view still matches these
current sources byte-for-byte.

The existing integrated STA preparation is reused only for current source
preflight, original37/Default44 preparation and fixed input pins. Its subordinate
positive preparation plans are never executed by this sidecar. Only the five
new STA-FAULT-GENERATION-PLAN.json files are negative generation endpoints.
Original37 is copied once per preparation; each role receives 13 unchanged
support files through the existing staging helper. No source Host is copied to
any fault or ports output, and no read-only source attribute/ACL is cleared.

Every first generation policy contains the exact Default44 identity/path/SHA/
request bindings and metadata-only clean2b599 Host. There is no Host/Backend/
Light/rawHC/SDK runtime context at either generation phase. Synthetic6d7 alone
is the runtime fixture. Backend2a3ae, LightE546 and rawHC08a remain exact pins;
rawHC is metadata-only. Unknown Default members are rejected even if the list
still contains44 entries.

Per-role plan vectors are exact data:

- args = ['fault-sta-'+role, cleanHost, freshFaultHost, freshFaultProof]
- dotnetArguments = [exactMTA,'run',policy,policySHA,freshReceipt,...args]
- metadataValidateArguments = [exactMTA,'validate',policy,policySHA,freshMetadataReceipt]

MTA `run` here executes the original producer's Cecil metadata generator only;
it is not SDK-free `validate` and it is not the STA business mode. Parent owns
both process execution and natural-exit/budget verification. Worker runs none.

## Phase A: fault bytes -> typed raw guard -> ports plan

After parent executes one exact fault generation endpoint and records its MTA
receipt, use:

```js
import {
  prepareStaFaultRawRequest, acceptStaFaultAndPreparePorts,
  prepareStaFaultPortedRawRequest, acceptStaFaultAndPrepareReplay,
} from './prepare-sta-counterexamples.mjs';
const request = prepareStaFaultRawRequest({
  mode: 'prepare-fault-raw-request',
  generationPin, faultPin, proofPin,
  outDir: freshRawDirUnderThatRoleRoot,
});
// Parent invokes request.program + request.args (fixed reader3bd052).
const portsPlan = acceptStaFaultAndPreparePorts({
  mode: 'accept-fault-metadata-only',
  generationPin, faultPin, proofPin,
  rawRequestPin, rawReportPin, generationReceiptPin,
  outDir: freshPortsPlanDirUnderThatRoleRoot,
});
```

Every pin is {path: absolutePath, sha256: exactFileSHA}. Missing fault/proof/
receipt/raw bytes produce an explicit block. No DLL or SHA is fabricated.
Raw requests bind role, stage, exact input SHA, reader hash, dotnet hash and exact
argv. Metadata receipts must show target/constructor execution false, external
resolution disabled, and the expected identity with no module initializer.

The original `MutateStaOwnership` proof must say PASS, exactly one full changed
signature, the exact role and clean/fault SHAs, and hardwareIo=false. The new raw
guard checks the complete typed instruction/locals/EH delta for that method and
25 other selected unchanged methods. It models only the exact metadata edit:

- self-stop inserts CurrentThread/owner-field comparison before the retained
  BeginInvokeShutdown; self-thread drops its arguments and skips that call.
- startup-stop replaces exactly the stopping-field read and disposed Volatile
  read with pop/ldc.i4.0; other reads/guards/finally remain.
- ended-owner replaces only the two named dispatcher/stopping stores with
  pop/pop, including the original null-dispatcher anchor.
- pending-work replaces only the original set_Failure call with pop/pop;
  completion signaling remains.
- start-atomic adds one Thread local, exits staGate before retained Thread.Start,
  and adds the exact finally re-entering that same gate. Original ownership,
  failure handling, all other instructions/locals/EH remain.

The guard uses full signatures, typed member operands and their scopes. Branch
and EH byte offsets become instruction indices; short/long branch encoding and
Cecil token renumbering are accepted. The JSON-only comparison model emits no
PE. Only the original reflected Thread.get_CurrentThread and Monitor.Enter(Object)/
Exit(Object) imports use the exact pinned System.Private.CoreLib full identity.
The actual parent self-stop raw proves get_CurrentThread; the fixed CoreLib
metadata contains all three exact static method definitions used by the original
typeof(Thread)/typeof(ThreadMonitor) ImportReference source. Existing emitted
System.Runtime/System.Threading.Thread operands are preserved exactly; no scope
is normalized or ignored. Any mismatched imported scope fails closed with the
first typed canonical difference. This is a pending generated-byte check, not a claim that
unavailable bytes were already observed, and there is no scope/name fallback.
The unchanged original mutator additionally checks full method canon, including
attributes, implementation flags, InitLocals and all-method preservation; the
raw reader's absent attribute fields are not invented.

Only after this acceptance is the original second generator vector emitted:

`['sta-leaf-ports', exactAcceptedFault, freshPortedHost, freshPortsProof]`

Its policy is still Default44-only, and its final/intermediate Host plus both
proofs must be fresh. It inherits 30000ms, with explicit metadata/argv pins.

## Phase B: ported bytes -> retain the fault -> negative replay plan

After parent runs the original ports generator, supply final/intermediate DLL
pins and both original proof pins:

```js
const request = prepareStaFaultPortedRawRequest({
  mode: 'prepare-ported-raw-request', portsPlanPin,
  portedPin, proofPin, intermediatePin, fiveLeafProofPin,
  outDir: freshPortedRawDirUnderThatRoleRoot,
});
// Parent invokes its exact raw-reader vector.
const replay = acceptStaFaultAndPrepareReplay({
  mode: 'accept-ported-metadata-only', portsPlanPin,
  portedPin, proofPin, intermediatePin, fiveLeafProofPin,
  rawRequestPin, rawReportPin, generationReceiptPin,
  outDir: freshReplayDirUnderThatRoleRoot,
});
```

The new modes above belong to this new module. The existing positive bootstrap
API still uses `accept-generated-metadata-only`; no existing API is modified.

Port proof source SHA must be the accepted **fault** SHA, never the clean or
already-ported positive SHA. Intermediate proof must report the original five
leaves. Final proof must report the original seven methods and both retained-IL
flags. Six new leaf bodies are exact; removing only dup/ldarg.0/BeforeThreadStart
before the retained original Thread.Start must recover the accepted faulted
EnsureStaDispatcher canon. Thus start-atomic's changed gate/EH cannot disappear
when ports instrumentation is added. All 19 selected unaffected methods match
relative to the fault; this preserves the other four role mutations as well.

The prepared replay contains exactly:

`['sta-init', acceptedPortedHost, fixedLightE546, metadataOnlyRawHC08a, freshResult]`

with original45000ms/exit2 and that role's original failure contract. Policy has
Default44 plus the exact Host+Backend custom context and separate Light custom
context, using the existing canonical-uppercase-root IDs. `assessStaCounterexampleResult`
reuses the integrated source-bound STA assessor in negative mode; it rejects
case/evidence loss, incorrect errors, unrelated failures or leaked owned threads.

Generation receipt acceptance permits only exact policy members, exact MTA and
six previously recorded framework rows (netstandard from its original pinned
metadata row). Unknown managed loads are refused; no directory-prefix native,
SDK or managed approval is introduced.

## Scope and verification

Parent's positive raw/replay evidence is linked only for isolation/provenance.
No positive producer or STA run is repeated here. Real STA/thread/Dispatcher/
kernel32/possible framework COM actions remain the original suite behavior;
NativeAttempts0 is a guarded product/device counter, not zero OS native calls.
Parent's OS admission and eventual per-role lifecycle receipts govern any run.
This metadata sidecar grants no target/fullHost/real-device permission and does
not claim complete implicit CLR/native/WinRT policy coverage. UNKNOWN remains.
Original7+2+3 physical ports,360s,3072 and historical joined123/119 are retained.

Finite gates use actual five-role preparation plus JSON-only typed fault/ports
models and synthetic result data: clean/positive isolation, exact source/order/
budgets, stale outputs, same-count unknown Default pin, original source drift,
each of five mutations, source immutability, wrong import scope, unrelated method
changes, cross-role substitution, missing atomic finally, fault preservation
through ports, reversed positive/ports substitution, wrong leaf, original16case
negative contracts/cleanup and absent generated inputs. These are validator tests;
no modeled result is business evidence and no product fault DLL is generated.

## Reflected CoreLib correction evidence

The old facade-scope rejection is retained in the parent FAILURE.json. This
correction changes only frameworkImportScope and the resulting self-stop/
start-atomic expected canonical hashes; the other three role hashes are exact.
Original22 finite gates remain, plus5 bounded scope-regression gates (27 total).
The existing self-stop fault/receipt/raw are reused without producer execution.
Start-atomic has not been newly generated here; its ThreadMonitor.Enter/Exit
scope is proven by the original reflection-import expressions and exact fixed
CoreLib MethodDefs, and awaits parent actual generated-byte validation.

The original raw reader returns exit3/FAIL when selecting its own CoreLib, since
its name-only target sentinel also sees the framework already loaded before the
read. That original receipt is preserved. A separate exact before/after graph
check confirms the same pinned CoreLib path/identity/SHA and zero product targets;
it does not reinterpret that raw receipt as exit0. Only PE metadata was read;
none of the three reflected methods or any business/STA target was invoked.
