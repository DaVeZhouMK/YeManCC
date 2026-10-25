# STA bootstrap preparation sidecar

This sidecar prepares the **first positive** of the unchanged `sta-proof.mjs`.
It never spawns a producer, MTA, target, device call, STA thread or Dispatcher.
Only its parent may execute the original producer and business suite.

## Source-confirmed contract

- Generator mode: `sta-leaf-ports`, budget **30000ms**.
- Positive consumer: `sta-init`, budget **45000ms**, expected exit **0**.
- Exact source case set: **16**, checks **>=100**.
- Every case and staEvidence ID set must match the original list; every owned
  lifecycle thread must exit. All positive startGateHeld entries stay true.
- `sta-public-handshake-real-pump` must report createdThreads == 1 and
  readOnlyOsIdentityCalls == 1. NativeAttempts remains 0, Host Main false,
  hardwareIo false, and candidateHostSha256 must equal the generated pin.
- Later original faults are preserved as data: self-stop (1 failed ID),
  startup-stop (1), ended-owner (1), start-atomic (2), pending-work (1), each with
  its original error fragment. They are not generated or replayed here.
- Parent360000ms / tool3072 / original physical7+2+3 contracts and unresolved
  historical joined123/119 evidence are unchanged.

The basis contains exact case labels/line witnesses, complete signatures,
scoped typed operands, and the original negative contracts. The source case
list is independently extracted from `StaCases.cs` and agrees, in order, with
`sta-proof.mjs`. The actual original37 compiled StaCases/FactoryCases/Harness/
HarnessLoadContexts source bytes were compared with current G and match.

## Reusable prepare-only entry

The three required existing imports are `default-case-contract.mjs`,
`prepare-factory-initialization.mjs`, and `factory-initialization-basis.json`.
The integrated factory preparation itself reuses the unchanged staging helper.
Their exact G SHA pins are recorded in the input and patch manifest. This slice
adds only prepare-sta-bootstrap.mjs, sta-bootstrap.test.mjs, this document and
sta-bootstrap-basis.json; no existing source is changed.

```powershell
& $fixedNode "$moduleRoot/prepare-sta-bootstrap.mjs" `
  --mode prepare-only --inputs $inputsJson --sha256 $inputsSha `
  --out-dir $freshParentOwnedDirectory
```

SchemaVersion 1, kind `onex-sta-bootstrap-prepare`, requires exact pins:
factoryInputs (the reviewed original37/current2b599 factory input envelope),
factoryPrepareModule, factoryBasis, cleanRaw, rawReader and sources (staProof,
staCases, harness, harnessLoader, fixture, hostSource, factoryCases).
`STA-BOOTSTRAP-INPUTS.json` is the concrete delivery example.

The integrated factory prepare is used solely to reuse its original37/Default44
copy, source preflight, and fresh absent-Host staging. Its subordinate factory
plan is **not** this sidecar's executable endpoint and is never run. The new
STA-BOOTSTRAP-GENERATION-PLAN.json is the endpoint. Relocation retains the
subordinate preparation's owned directory names; names do not change the mode.

The STA generation policy preserves the exact normalized Default44 identity/path/SHA/request bindings, rejecting even a same-count replacement by an unknown library. It has only Default44: clean current Host2b599 and
Backend2a3ae are metadata, rawHC08a is metadata-only, syntheticHC6d7 alone is
available for runtime. The existing helper copies the 13 support files and
leaves the Cecil Host output absent; no sealed source attribute is cleared.

Exact producer args are:

`['sta-leaf-ports', cleanCurrentHost, freshOutputHost, freshPortsProof]`

The plan additionally names the original generator's intermediate outputs:
`freshOutputHost + '.five-leaves.dll'` and
`freshPortsProof + '.five-leaves.json'`. All four outputs plus the MTA receipt
and metadata-validate receipt start absent. The emitted MTA vectors are:

- `[exactMTA, 'run', exactPolicy, policySHA, freshReceipt, ...args]`
- `[exactMTA, 'validate', exactPolicy, policySHA, freshMetadataReceipt]`

These vectors are data only. Parent chooses its fresh directory, performs the
new policy metadata/preload checks, and owns process budgets and natural exit.

## What the original Cecil producer changes

InstrumentStaPorts first calls the original InstrumentFactoryLeaves, preserving
its separate five-leaf proof. It then changes the OS-ID body and inserts a
scheduling observer before the single original Thread.Start call.

| Method on RealHcBackend | Allowed change |
| --- | --- |
| ValidateFormalSessionCapabilityAcl | FactoryPorts.Check("validation.acl"); ret |
| ValidateDeploymentGenerationForHcLoad | FactoryPorts.Check("validation.payload"); ret |
| ValidateGamepadMotion | FactoryPorts.Check("assembly.native"); ret |
| ConfigureHcRuntime | FactoryPorts.Check("assembly.configure"); ret |
| ResolveDependency(ALC,AssemblyName) | original five-leaf observer body, scoped FactoryPorts.Resolve; ldnull; ret |
| static UInt32 ReadCurrentOsThreadId() | remove Host PInvoke/implementation flags; call scoped StaPorts.ReadOsThreadId; ret |
| EnsureStaDispatcher() | exactly dup; ldarg.0; call scoped StaPorts.BeforeThreadStart(Thread,Object) immediately before the retained original Thread.Start |

Original readiness, factory, handshake, work admission, pending work retirement,
thread ownership, disposal, and all other methods remain. WidenStaBranches
changes short branch encoding only. The original pinned producer removes the
three observer instructions in memory and checks FactoryMethodCanon equality,
including attributes, locals, InitLocals, EH ranges, and instructions; its final
proof must report exactly seven method changes and both originalStaInstructionsRetained
and branchEncodingNormalized true.

The new raw validator independently binds unique full signatures and scoped
typed operands. It checks six exact leaf bodies, no remaining Host OS-ID PInvoke,
exactly one observer before the original Start, and 19 selected unchanged bodies
among 26 Host MethodDefs. It converts branch/EH byte offsets to instruction
indices and ignores RID/operandToken numbering. It rejects branch targets into
the removed probe, altered owners, scopes, original Start/control flow,
duplicate signatures, or an eighth change. Raw reader does not expose all
attributes; their preservation is supplied by the unchanged original producer's
method-canon proof, not claimed from unavailable raw fields.

## Generated bytes -> raw PE request -> replay preparation

Generation is ready. Replay remains `BLOCKED_MISSING_GENERATED_METADATA` until
the parent supplies all original output pins and its generation MTA receipt.
No placeholder assembly or inferred SHA is accepted.

```js
import {
  prepareGeneratedStaRawRequest,
  prepareStaBootstrapReplay,
} from './prepare-sta-bootstrap.mjs';
// Pins are exact {path: absolutePath, sha256: fileSHA256}.
const pins = {
  generationPin, generatedPin, proofPin, intermediatePin, fiveLeafProofPin,
};
const request = prepareGeneratedStaRawRequest({
  mode: 'prepare-generated-raw-request', ...pins,
  outDir: freshRawDirectoryUnderGenerationOwnedRoot,
});
// Parent invokes only request.program + request.args with fixed reader3bd052.
const replay = prepareStaBootstrapReplay({
  mode: 'accept-generated-metadata-only', ...pins,
  rawRequestPin, rawReportPin, generationReceiptPin,
  outDir: freshReplayDirectoryUnderGenerationOwnedRoot,
});
```

Acceptance checks intermediate five-leaf proof and SHA, final seven-method
proof and SHA, exact output paths, original source pins and staged outputs,
exact generation producer/MTA managed bindings, and the raw report bound to its
request. Generation's extra CLR allowance uses the original five exact preload
rows and pre-existing netstandard metadata row, plus exact MTA identity. There
is no folder-prefix approval or unknown-name fallback. A new managed dependency
in generation remains a concrete refusal for parent review.

Replay preparation emits exactly:

`['sta-init', generatedHost, fixedLightE546, metadataOnlyRawHC08a, freshResult]`

with Default44, one Host+Backend custom context, and a separate Light context.
Context IDs use the existing canonical-uppercase-root convention. Its vectors
and 45000ms gate are prepared only; the plan explicitly retains OS boundary
review requirements and grants no target/fullHost execution approval.

## Real OS boundaries: do not confuse device counter with OS isolation

This suite intentionally tests real Windows threading and message pumps.
These actions remain original and are **not** synthesized by the port rewrite:

- Thread.SetApartmentState(STA), foreground Thread.Start, Thread.Join, owned
  task callers, synchronization events, waits and original timing windows.
- WindowsBase Dispatcher.CurrentDispatcher, Dispatcher.Run, BeginInvoke,
  DispatcherSynchronizationContext, shutdown and pending-work retirement.
  Windows dispatcher/STA COM initialization and native message handling remain
  framework/OS work; this slice does not recursively prove those internals.
- StaPorts.ReadOsThreadId records the actual thread, invokes the original owned
  IdentityHook, then calls its unchanged `kernel32.dll!GetCurrentThreadId`
  PInvoke. Rebinding the Host method moves the call to this observed port;
  it does **not** replace the OS endpoint with a synthetic numeric ID.
- The first selftest calls HostOptions.Parse(Array.Empty<string>()), which
  creates a random session token using framework RandomNumberGenerator. Its
  empty-argument branch does not read a session-token file. This OS-backed RNG
  is separate from hardware/registry IO.

Harness.Load installs the original native Deny on loaded Host/Light (and local
product dependencies), while synthetic HC is an existing Default export and is
not given that Host Deny. The unchanged StaPorts PInvoke can therefore reach its
real OS endpoint. The original NativeAttempts counter measures guarded product
native attempts; zero does not mean zero framework/kernel32/COM calls. No native
allowlist or Deny implementation is modified by this slice.

Static construction is finite: current RealHcBackend .cctor/.ctor are the same
pinned bytes as the prior bounded factory review; StaPorts .cctor creates its
owned dictionaries/queues; FactoryPorts and synthetic ROG/IDevice use the original
fixture. Most suite cases use uninitialized HostOptions populated by
NewFactoryBackend; the internal dispatcher selftest uses the real empty Parse
and constructor path. Neither path starts Host Main. Factory device stages end
at synthetic FactoryPorts/ROG methods. The suite does not call device Open/Close,
start sensor timers/mappings, or acquire a backend ASUS handle; Backend.Dispose
retains its real null guards, resolver removal, dispatcher shutdown and joining.

No selected named branch or synthetic port directly requests WinRT/SDK target
loading or activates a WinRT type. The policy grants neither raw HC nor SDK
managed pins. This is **not** a proof of all implicit framework/COM/WinRT internals,
native DLL byte resolution, or all CLR TPA behavior. Parent must review the real
OS actions and eventual managed/native receipts before deciding to run.

## Finite verification

`sta-bootstrap.test.mjs --inputs … --sha256 … --out-dir …` runs pure preparation,
source and JSON-model gates. It does not run a producer or target. It checks real
37/44 copying and fresh four generator outputs, mode/source drift, same-count unknown-policy refusal, consumed
output refusal, symbolic seven-method diff with token/branch re-encoding,
observer owner, wrong scoped OS port, remaining PInvoke, extra/duplicate/changed
methods, original16/100/identity/start gates, all five negative contracts,
thread cleanup, absent generated outputs and explicit real OS boundary records.
Modelled staEvidence/results are validator data, never business evidence.

The D prototype loader maps only the three exact existing imports to G; after
integration normal relative imports are used. Patch application is byte-checked
in an owned D repository with core.autocrlf=false and reverse-check, and the
manifest records actual G-before absence for all four new files.
