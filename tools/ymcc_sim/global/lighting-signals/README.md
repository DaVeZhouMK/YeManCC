# Lighting signals: production source and safe regression

This adapter executes actual LightSetter consumers and protocol code. It uses scratch-only compiled fixture pins and replaces only the HID/serial/device terminal. The shipping helper is compiled and its exact DLL is loaded by the existing LightSetterSelfTest artifact entry. Only pure parsing/action/receipt methods execute against memory devices; Main, HC factory, target dependency resolution and native calls cannot run. Native probes compile the actual route header and extracted native command/admission/receipt functions with memory stubs. The real Vue component runs with in-memory bridge/settings.

## Entry points

```powershell
node tools/ymcc_sim/global/lighting-signals/index.mjs --evidence-dir D:/path/to/fresh-evidence
```

```js
import { runLightingSignals, contract } from './tools/ymcc_sim/global/lighting-signals/index.mjs';
const result = await runLightingSignals({ evidenceDir: 'D:/path/to/fresh-evidence' });
```

`adapter.mjs` exports `lightingAdapter`, a descriptor for the parent-owned global registry. No shared CLI/registry is modified here. Each run uses its own evidence/build directory, builds sequentially with one MSBuild worker, and returns nonzero on any failed or stale-source gate. Do not reuse an evidence directory containing result.json.

Optional arguments map to camelCase options:

- `--before-directory`: snapshot containing the previous Program.cs and DeviceCoreConsumer.cs; execute the same cases against that source and compare failures. No fixture result is substituted for an unexecuted product consumer.
- `--hc-profile legacy|ymcc-device-core-v1`, `--hc-version`: candidate profile and exact AssemblyVersion label; no HC version is hardcoded in the simulator contract. Profiles and binary pins remain reviewed product source policy.
- `--runtime-root`: directory containing a candidate HandheldCompanion.dll and Shared.dll. Only static PE metadata is inspected here. Unauthorized hash/profile, missing assembly or requested version mismatch produces an explicit compatibility difference and fails this adapter.
- `--shared-runtime-root`: flat candidate directory containing the eight shared DLLs. Candidate bytes are compared with the production resolver's reviewed pins; manifest self-reports cannot authorize bytes.
- `--host-payload-root`: candidate light-setter directory, or its parent containing light-setter/. Supply `--expected-helper-sha256` with the reviewed candidate DLL pin. Missing payload/pin or differing bytes fails. The regular helper build has no output-path reproducibility contract, so its isolated-build hash is recorded as build evidence and is not used to infer a packaged DLL is stale.
- `--dotnet`, `--python`: runtime executable overrides. A bundled Python is detected when present. Node, .NET 10 Windows desktop reference/runtime, MSVC/Windows SDK, repository build dependencies and headless Edge are required.

The runtime-root/shared-root/payload checks are bounded static identity checks. `candidateRuntimeAcceptance` remains NOT_EXECUTED when no incompatibility was found; this is never promoted to full runtime acceptance. HC factory/manager initialization belongs to the separate runtime-connectivity gate.

## Stable protocol and evidence

Schema: `ymcc.lighting-signals.v1`. The independent component ABI is `YeManLightDeviceCore`, `ApiVersion=1`, `bool Supports(string,string,string)`, `string Execute(string,string)`. Production `DeviceCoreConsumer.ResolveApi(Type)` is executed against compatible and incompatible signatures/versions. Production `RuntimeDependencyContract.LoadPinned` rejects an unverified previously loaded image even when the requested disk hash matches.

Supported independent capability: exact X1 Mini class, solid/rainbow color and brightness. Other requests remain on explicit legacy HC selection or fail unsupported. A supported component transport failure cannot trigger fallback. Successful receipts must match requested device/action, ready/opened/released, exact attempted-operation count and known note. Physical verification is always false. Numeric clamping remains the existing product contract.

`result.json` contains `sourceChanges`, `sourceHashes`, `executedProductModules`, `inputConsumerStateFeedback`, `compatibility`, and `remainingGaps`. It also contains per-model source routes and PE reference/native import metadata. Inputs are labelled `source-derived-fixture`; no archived hardware success is invented. Top-level PASS is scoped to the offline cases, not installation or physical acceptance.

The source bin for the default HC derivative is a replacement overlay, not a complete installed runtime. Static Shared.dll metadata is read from the frozen base directory declared by the existing profile; it is not guessed missing because it is absent from the overlay.

`ui-feedback.mjs <evidenceDir> [--component-source <snapshot.vue>]` executes the actual component. It checks refusal, exception, absent acknowledgement and current success, in addition to the existing five model/preset/save/apply groups. The parent integrated the status-rendering patch; the patch artifact records the original target hash. Full engine navigation/release checks remain parent-owned.

## Scope still requiring separate evidence

Actual HID descriptors, physical write acknowledgement/LED observation, live native process creation/deadline/termination, an installed complete HC runtime and selected factory, pre-trim archived successful sender receipts, and any restoration contract are not proven here. A one-shot setter has no automatic restoration lease. Do not deploy, run the production helper, open real HID/EC/OEM drivers or trigger OS sleep from this adapter.

The artifact entry runs 177 mandatory checks against the exact compiled helper (and a supplied candidate helper when its reviewed pin matches). It rejects inherited IDevice no-op success, validates refusal before loading devices, preserves false results, and checks receipt identity/completion. The former installed 233f2c1c... image was executed by this same entry and rejected specifically for inherited no-op color success; current fixed artifacts pass. The browser fixture reserves a bounded high loopback port and records it, so a desktop low ephemeral-port range cannot select Edge-forbidden 1723.

The exact helper verifies Run calls the lighting-only Open boundary. The actual HC method-body gate checks ClawA1M parent flags/firmware, HID key 1 in both LED paths, and parent/child general-Open power/mode/ROM side effects. Only the three exact Open owners ClawA1M, ClawA2VM and MSI.ClawCG3EM with the audited base shape and UseOpenLib=false use lighting preparation: read the already bound HID's firmware, run the base Open nonvirtually, set managed Firmware/ClawOpen state. General MSI Open is never called for a lighting request. Missing/disconnected HID and failed base Open remain failed; other models retain virtual transport initialization. The corrected fixture models the real ClawA1M parent. It rejects the first incomplete prototype and reproduces four old virtual-Open power writes into memory terminals. No device power is written by these tests.

OneX X1-family legacy lighting prepares only its serial transport; Mini uses the HID already bound by IsReady. Run owns the narrow session in a finally scope. An owned serial port preserves HC baud/parity/data/stop settings, has a bounded write timeout, drains the existing SerialQueue transaction, and closes/disposes on success or failure. Missing, ambiguous, disabled or disconnected transports remain errors. Borrowed transports are not closed. No general OneX Open/Close or IMU service is started. Eight actual-PE checks bind these assumptions to pinned HC. The previous addb helper is rejected with one OLS initialization and three OEM-register writes into memory. Existing MSI checks remain mandatory. Transport cleanup is distinct from restoring LED state.

The one-shot X1 serial adapter invalidates only requested brightness/status cache before real HC action/preset dispatch. Default 0/false is not device readback: explicit zero, status off/on, full off, color+zero and preset+zero must emit their requested packets. Mini HID stays separate. The exact-DLL fixture covers four X1 models and every zero transaction packet failure/retry; three actual-PE checks verify the underlying HC default-cache skip conditions. Old105f first-zero negative records zero writes and fails. This does not restore LED state or claim physical light/power measurement.

The exact OneXFly and F1Pro classes use only their IsReady-bound HID. Their inherited general Open initializes OLS and changes F1/F2 OEM registers; neither LED V2 method needs it. Bound HID missing/disconnected, unknown lifecycle owner and write failure remain errors. Eight actual-PE checks bind transport assumptions and Apex inheritance/preset identity. The existing short-name preset guard already accepts Apex logical/native versus nested runtime naming; it is exercised without a speculative alias change. Old bdeb actual Open reproduces two OEM writes and one driver initialization into memory terminals.


## UI lifecycle regression (2026-10-20)

The existing global lighting.signal-consumer entry (which executes lighting-signals/index.mjs) now runs ui-lifecycle.mjs automatically. The standalone lighting.product-signals descriptor shares the same input declaration; it is not a second global registration. It reuses tools/controller_lighting_lifecycle_browser_selftest.mjs and the unchanged operations browser fixture, including the actual src/bridge/api.ts wrappers. At each of 1280×720 and 1280×800 it verifies queued lighting cancellation, late capabilities/settings refusal, second vibration pulse refusal after leaving, reactivation, and normal active updates. Only IPC/settings are memory ports.

Every normal lighting run also executes a private copy of the current component with only onDeactivated(deactivateFeedback) and onBeforeUnmount(deactivateFeedback) disconnected. Exactly 18 of the same 22 observations must fail, with the original business assertion and real exit codes. Compilation, launch or timeout failures cannot count as detection. Probes must differ only by the explicit component path and source/API/fixture hashes must remain stable. The aggregate includes the 22 actual positive cases and one proven fault-control case; raw negative failures remain in ui-lifecycle/fault.

The adapter consumes the same exported lightingSourceInputs used by the actual regression snapshot, so newly added test/source inputs cannot drift between execution and registered evidence. This does not re-sign the separately pinned historical ordering-consumer leaf or provide whole-page navigation/native ownership/physical acceptance. Already issued native commands remain under their existing native owner.

Direct focused use:

```powershell
node tools/ymcc_sim/global/lighting-signals/ui-lifecycle.mjs G:/YeManCC-Work/Build/Tasks/<fresh-lifecycle-evidence>
```
