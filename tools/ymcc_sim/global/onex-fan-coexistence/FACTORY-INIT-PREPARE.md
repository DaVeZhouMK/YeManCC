# Factory initialization: bounded prepare and metadata acceptance

This module prepares the first positive branch of `factory-proof.mjs`. It does
not spawn dotnet, run the Cecil producer, load a target, or execute factory-init.
The generation endpoint is ready. Replay remains
`BLOCKED_MISSING_GENERATED_METADATA` until the parent supplies its outputs.

## Original contracts

The source-confirmed suite contains exactly 24 distinct case IDs. The original
consumer compares the exact **set**, not execution order. Its positive gate is
PASS, no failed cases, checks >= 157, nativeAttempts == 0,
hostMainStarted == false, hardwareIo == false, and the generated Host SHA.
Generation and replay each retain 30000ms. The original parent budget remains
360000ms. No original assertion, clock, physical 7+2+3 port contract, 3072 gate,
or historical joined 123/119 failure evidence is changed here.

`factory-initialization-basis.json` records each source Run expression, line and
loop binding, the original consumer case list, and the exact original later
negative error fragments. `factoryInitializationContract('readiness')` returns
15 failed IDs; `factoryInitializationContract('continuation')` returns 7. Those
are separate mutation/replay contracts, not failures expected in the positive
suite. `assessFactoryInitializationResult` retains their exact failure sets and
error substrings. This slice does not generate either later mutation.

## Prepare the generation endpoint

Use the reviewed fixed Node executable and the integrated module:

```powershell
& $fixedNode "$moduleRoot/prepare-factory-initialization.mjs" `
  --mode prepare-only --inputs $inputsJson --sha256 $inputsSha `
  --out-dir $freshOwnedDirectory
```

The input is schemaVersion 1, kind `onex-factory-initialization-prepare`, with
exact SHA pins for basePlan, currentSourceBuild, cleanRaw, rawReader,
frameworkMetadata, basePreload, and sources (factoryProof, factoryCases,
harness, fixture, hostSource). The delivery's `FACTORY-INITIALIZATION-INPUTS-r2.json`
is the concrete example. The source-bound current Host is 2b59965f… and Backend
2a3ae2d1…; the original producer is fe96c6a5…, MTA 55bbc787…, synthetic HC
6d7bff05…, raw HC 08a38660… (metadata only), and Light e5460cfc….

Preparation verifies the source/compiled-source pins, source-bound build pin,
original 37 output bytes/layout and original Default44 policy. It relocates
those 37 outputs, copies the fixed Light, and invokes the existing
`stageFaultPayload` helper to stage 13 support files while leaving
`leaf-payload/YeManFanHost.dll` absent. Read-only source attributes are untouched.
All pins are checked again after preparation. Existing output directories or
changed sources are refused.

The generated `FACTORY-INITIALIZATION-GENERATION-PLAN.json` contains:

- `args = ['factory-leaf-ports', exactCleanHost, freshGeneratedHost, freshProof]`
- `dotnetArguments = [exactMTA, 'run', exactPolicy, policySHA, freshReceipt, ...args]`
- `metadataValidateArguments = [exactMTA, 'validate', exactPolicy, policySHA, freshMetadataReceipt]`
- budgetMs = replayBudgetMs = 30000; status = PREPARED_NOT_EXECUTED.

These vectors are data for the parent's separately approved process runner.
The generator policy has only Default44. It grants no runtime target context
for clean Host, Backend, raw HC, Light or SDK. The unchanged original producer
reads the Host using Cecil with the already audited metadata generator.

## Exactly five replaced bodies

Every signature belongs to `YeManFanHost.RealHcBackend`:

| Full signature suffix | Required replacement body |
| --- | --- |
| `System.Void …::ValidateFormalSessionCapabilityAcl()` | ldstr validation.acl; call synthetic FactoryPorts.Check(string); ret |
| `System.Void …::ValidateDeploymentGenerationForHcLoad()` | ldstr validation.payload; same Check; ret |
| `System.Void …::ValidateGamepadMotion()` | ldstr assembly.native; same Check; ret |
| `System.Void …::ConfigureHcRuntime()` | ldstr assembly.configure; same Check; ret |
| `System.Reflection.Assembly …::ResolveDependency(System.Runtime.Loader.AssemblyLoadContext,System.Reflection.AssemblyName)` | ldarg.0; ldarg.2; callvirt AssemblyName.get_Name; call synthetic FactoryPorts.Resolve(object,string); ldnull; ret |

The complete signatures and scoped typed operands are in the basis. The
resolver's original return-null terminal and four one-use expected-absent
probes remain intact. No general unknown-name fallback is introduced.

The pinned original `FactoryChangedMethods` proof compares all methods by full
signature, attributes, implementation attributes, locals, InitLocals, EH ranges,
and symbolic instructions. It must report exactly these five changes. The new
raw verifier additionally checks 37 selected Host MethodDefs: the five exact
new bodies and 32 unchanged constructor/control-flow/callback/disposal bodies.
It rejects duplicate or changed signatures, a sixth reported change, wrong
assembly scope/typed operands, changed fields/bases, or changed selected bodies.
It normalizes branch/EH offsets to instruction indices and ignores numerical
method/operand tokens. It does not assume Cecil preserves RID ordering. The raw
reader does not expose every method attribute; preservation of those attributes
is supplied by the original pinned generator proof, not invented from raw data.

## Parent generation -> raw metadata -> replay preparation

After the parent runs the exact generation plan once and captures the MTA
receipt and original ports proof, use these exported APIs:

```js
import {
  prepareGeneratedFactoryRawRequest,
  prepareFactoryInitializationReplay,
} from './prepare-factory-initialization.mjs';
// Each *Pin is {path: exactAbsolutePath, sha256: exactFileSHA256}.
const request = prepareGeneratedFactoryRawRequest({
  mode: 'prepare-generated-raw-request',
  generationPin, proofPin, generatedPin,
  outDir: freshDirectoryUnderGenerationOwnedRoot,
});
// Parent invokes request.program + request.args with its reviewed raw PE reader.
// This worker has not invoked that generated-output request.
const replay = prepareFactoryInitializationReplay({
  mode: 'accept-generated-metadata-only',
  generationPin, proofPin, generatedPin,
  rawRequestPin, rawReportPin, generationReceiptPin,
  outDir: anotherFreshDirectoryUnderGenerationOwnedRoot,
});
```

The request is pinned to reader 3bd052bb… and the generated DLL SHA. Acceptance
requires the original PASS proof, exact generation-receipt path, zero generation
failure, exact producer/MTA bindings, an exact managed-load allowance, and a
metadata-only raw report tied to the exact generated path/SHA. Framework
allowance uses five exact existing preload rows plus the exact netstandard row
from the pre-existing pinned framework-metadata file; it never trusts a folder
prefix. Unknown assemblies are refused. The parent still owns process budget,
natural-exit verification and any new MTA/default-preload execution receipts.

Replay preparation creates exactly:

`['factory-init', generatedHost, fixedLight, metadataOnlyRawHC, freshResult]`

It registers Host+Backend in one `Harness+ProductContext` and Light in another,
using the existing canonical-uppercase-root context IDs. Default44 continues to
resolve only the synthetic HC fixture. The replay remains PREPARED_NOT_EXECUTED;
its MTA run and validate vectors are emitted for parent review. Missing generated
DLL/proof/receipt/raw output is an explicit block, never a fabricated pin or
prepare-pass claimed as business-pass.

## Finite synchronous boundary

The original FactoryCases and Harness sources match those compiled into the
original 37. NewFactoryBackend allocates HostOptions without its constructor,
sets only authorization/synthetic-HC fields, and optionally selects the synthetic
ROGAlly; FanProvider and Diagnostic stay null. The actual default/diagnostic
factory routes therefore use synthetic IDevice.GetCurrent or synthetic ROGAlly
construction. Motherboard Collect, PullSensors, Initialize(false,false), Version
and ReadMsiFanStateReadOnly resolve to the original fixture's FactoryPorts
methods. Raw queries found their exact signatures; no reflection mismatch was
observed. FactoryPorts.OnStage synchronously performs only the suite's original
failure, disposal, reentry or owner-replacement callbacks.

This named mode returns RunFactoryCases before the default suite and never
calls device Open/Close or public STA wrappers. Backend Dispose preserves real
resolver subscription/removal; it does not synthesize device.Close. This fresh
branch never starts temperature/watchdog timers, a monitor mapping, a dispatcher,
or a backend ASUS session. Their fields remain null; the existing null guards
prevent the corresponding Dispose/ACPI/dispatcher terminals. Diagnostic is null,
so diagnostic exception formatting callbacks are skipped. Real factory state,
readiness, ownership, retry, handshake and disposal code remains in place.

Remaining ordinary file IO is explicit and owned: the harness authorization
text, invalid-hc-image text for its original BadImageFormatException probe,
result/log outputs, and reads of exact pinned images. No physical device,
registry or external-process terminal is approved. The conditional software-only
conclusion depends on accepting the generated five-leaf diff and parent review;
it is not proof of all CLR static dependencies, SDK/native guard policy, the
whole Host, or full-parent acceptance.

## Tests and evidence

Run `factory-initialization.test.mjs --inputs … --sha256 … --out-dir …` with
fixed Node. In this D overlay only, the sealed checks/dependency-loader.mjs maps
the two unchanged relative helper imports to their exact G files. Integrated
sources use normal relative imports and need no loader.

14 finite gates pass: actual 37/44 preparation, explicit mode, consumed output,
generator context/mode refusal, token-renumber acceptance, wrong typed scope,
sixth change, altered preserved method, duplicate signature, original 24/157 and
15/7 negative result contracts, one-statement count-assertion source drift,
read-only own-file staging, and missing generated inputs. JSON-model diff/results
are validator data only. No product fault DLL or business target was generated
or run by this slice. The initial extraction's over-strict order comparison FAIL
is preserved; the source expansion and original consumer agree on the exact set.

## Reflected runtime implementation scope

The original Cecil generator imports typeof(AssemblyName).GetProperty(Name).GetMethod. In the exact pinned generation runtime this method is declared by System.Private.CoreLib, not the System.Runtime facade. The basis now records the exact scoped CoreLib signature, tied to the existing locked CoreLib file hash and actual generation load receipt. Wrong facade/assembly scopes remain rejected. This correction changes one expected operand only; the five generated bodies, other32 methods, source proof, original budgets and24-case contract remain unchanged. The failed initial acceptance is preserved and its already generated DLL is reused.
