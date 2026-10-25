# Input signal consumer

This extends the existing global simulator. It executes the unchanged product WebView IPC, controller shortcut runtime, serialized actions, shared action lock, API wrappers, settings repository, shortcut feedback channel, monitor notice host/state and press feedback. `native-mailbox.cpp` is only a JSON-line adapter around the production `native/shortcut_action_protocol.h`; it does not implement a substitute mailbox.

The owned helper has no Windows/device/network/real-settings API. The WebView boundary returns memory-only lifecycle, fake settings, task-view/window/app-launch and notice-surface results. It cannot contact an installed YMCC instance. VirtualClock is reused from the existing simulator; MSVC/SDK discovery is reused from the existing reviewed-calibration toolchain. No HC upgrade, installation, runtime replacement, device write or real sleep is performed.

## Stable entry points

```js
import {
  replayInputSignals, runInputSignalRegressions,
  createInputSignalHarness, evaluateInputCompatibility, INPUT_SIGNAL_CONTRACT,
} from './input-signal-consumer.mjs';

const observed = await replayInputSignals({repoRoot, events: plan.events, sourceArchive: plan.sourceArchive});
const regressions = await runInputSignalRegressions({repoRoot, workDir});
```

Results expose `schemaVersion`, standard `status` (PASS/INCOMPLETE/BLOCKED/FAIL), detailed `verdict`, `sourceChanges`, `sourceHashes`, `executedProductModules`, `inputConsumerStateFeedback`, `compatibility` and `remainingGaps`. Each evidence row has input/consumer/state/feedback. Source hashes are recomputed and a cached bundle is invalidated if a product input changes. Bundled module initialization and the specifically asserted action branches are distinguished in execution labels/evidence; listing a module does not imply all its branches ran.

MSI `msi-oem-suppression-observation` records retain their reviewed top-level bool/int/phase/origin fields as `archive-literal`. They never trigger an action. `actionDispatchAccepted=true` does not prove `defaultActionSuppressionVerified` or a hardware write. No record is joined with another file or used to reconstruct an unlogged request.

An additional **separate** fixture must be explicit:

```js
{event:'fixture.shortcutAction', provenance:'source-derived-fixture',
 actionId:'app.launchQuickApp', params:{appPath:'Z:\\YMCC-MEMORY-ONLY\\fixture.exe'}}
```

That request is issued by the real native mailbox, injected into the real WebView consumer and completed through the native mailbox before entering the real feedback channel. A raw `input.shortcutAction` without a corresponding native mailbox entry fails claim; the adapter does not forge one. Successful `os.switchProgram` feedback is intentionally hidden by the actual product presentation rule, whereas failed outcomes remain readable.

## Commands

From the repository, provide a task-owned scratch directory:

```text
node tools/ymcc_sim/global/input-signals/input-signal-consumer.mjs --work-dir <work-directory> --output <regressions.json>
node tools/ymcc_sim/global/input-signals/input-signal-consumer.mjs --events <log-replay-plan.json> --output <archive-result.json>
node --test tools/ymcc_sim/global/input-signals/input-signal-consumer.test.mjs
```

Set `YMCC_INPUT_WORK_DIR` for the test runner if the default `work/ymcc-input-signals` is unsuitable. Windows uses an existing complete MSVC/x64 SDK; other systems use an existing `c++`. Build failure is reported, never replaced with a model-only success.

## Compatibility inputs

`compatibility` accepts `expectedContract` (schemaVersion/requestSchemaVersion/feedbackSchemaVersion) and caller-supplied `baseline`/`candidate` objects (hcVersion/runtimeRoot/sharedRuntimeRoot/hostPayloadRoot/payloadIdentity). Any unequal or missing side of a supplied identity is a concrete difference and blocks acceptance. CLI accepts `--compatibility <json-file>`. Versions are not hardcoded. Matching identities prove only this schema check; HC ABI/COM/manager/host validation remains the responsibility of the runtime-connectivity gate.

## Production repair and limits

`controllerShortcutRuntime.ts` bounds claim/complete IPC waits at 3000ms. `controllerShortcutActions.ts` prevents completion transport failure from retrying an already applied action as a false failed result. The original sources and the repaired sources are both identified by SHA256 in the result/patch evidence. No `native/main.cpp` change is required.

The simulator does not execute actual WMI/HID capture or the native serial dispatcher. `native/shortcut_action_runtime.h` is source-traced; `shortcut_action_protocol.h` really compiles and executes. It does not establish OEM suppression, physical display, vibration, haptic/light output, or actual OS restore. Existing controller feedback capabilities intentionally stay UNENCLOSED. Invalidation/stop checks prevent later actions after pending IO returns; this task does not provide universal cancellation/timeout for every unrelated system/settings call or physical rollback of an action already issued.

## OEM diagnostic/keyboard follow-up gate

`input-followup.mjs` exports `runInputFollowupRegressions({repoRoot?,workDir?})`,
`getInputFollowupAdapter()` and `followupSourceInputs()`. The parent owns global
registration. The adapter descriptor uses the existing global runner, with id
`controller.input-diagnostics`, node/controller/behavioral-mock, a 120s timeout,
`--work-dir {outDir}/input-followup-work --output {outDir}/input-followup-result.json`,
and resultFiles `['input-followup-result.json']`. `evidenceInputs` are provided by
`followupSourceInputs()`; the actual global runner computes their sourceInputs hashes.

The gate source-extracts **unchanged current** C++ declarations, complete dispatch
function, click log status expressions, summary digest/log block, edge/emit
conditions, keyboard/desktop mapping and chord admission functions. It compiles
these with the installed SDK and runs only memory boundary adapters. No normal
YMCC App, Host, settings document, physical input or hardware port is opened.
An import audit rejects user32/win32u, OS input, shell and OLE imports. A passing
source-excerpt test does not claim that all of native/main.cpp executed.

The original main.cpp diagnostic expressions omitted uiAction. Four identity
lines plus summary uiAction digest invalidation were applied by the parent.
The new regression covers ownAction/inputAction/uiAction/priority, every log site,
accepted/failed/waiting/unconsumed and edge/release/repeat branches. Diagnostic
acceptance never becomes default-action suppression proof.

The old 22-key numeric assertion predates the production expansion to 51:
10 Digit codes, F1–F24, 10 Numpad digits and 7 Numpad/NumLock controls. The independent
keyboard contract checks all 137 identities across five groups, native virtual
keys, Shift and extended flags (including NumpadEnter vs Enter), defaults,
validator, reload and invalid codes. It also found the real symbol-default bug:
`Tilde` is not admitted by either the option list or the native consumer;
`Backquote` is the canonical supported code. Existing saved invalid `Tilde`
values remain rejected; this task does not silently rewrite user documents.

Run the gate with `node tools/ymcc_sim/global/input-signals/input-followup.mjs
--work-dir <scratch> --output <result.json>`. Run its 9 contract/tool checks with
`node --test tools/ymcc_sim/global/input-signals/input-followup.test.mjs` (set
YMCC_INPUT_FOLLOWUP_WORK_DIR for explicit scratch placement). Every result carries
source changes/hashes, exact excerpt hashes, import/fixture/binary identities,
input/consumer/state/feedback, compatibility and remaining physical gaps.
