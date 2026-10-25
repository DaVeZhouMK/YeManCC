# Background consumer gate

The `display.background-signals` descriptor is exported by `getBackgroundAdapters()` from `adapter.mjs` or `index.mjs`. The parent owns global registration; this directory never modifies the shared registry or CLI.

```js
import {runBackgroundRegressions,getBackgroundAdapters} from './background-signals/index.mjs';
const result=await runBackgroundRegressions({repoRoot,evidenceDir,timeoutMs:90000});
// Accept only status === 'PASS'. evidenceDir must be a new directory outside repoRoot.
```

```powershell
node tools/ymcc_sim/global/background-signals/index.mjs --evidence-dir <fresh-directory>
node --test tools/ymcc_sim/global/background-signals/index.test.mjs
```

Optional CLI arguments: `--repo-root`, `--timeout-ms` (500..300000), `--pwsh-path`. Exit 0 is PASS, exit 1 is a failed contract, exit 2 is BLOCKED. Missing sources/compiler/PS7, missing or invalid receipts, a process without feedback, timeout, source changes or reused evidence cannot pass. Only the gated owned Windows Job contains test descendants; no user/App/Host process is inspected or terminated.

The gate bundles unmodified `tools/dynamic_background_selftest.ts` with actual formal product imports. Only IPC, settings, filesystem/registry API and Steam HTTP boundaries are replaced by in-memory or rejecting implementations. It does not replace `SharedGameMedia`, the revision fence, title policy or dynamic/fixed background consumers with models. Actual esbuild read inputs/metafiles and bundle hashes are recorded, compared with initial approved inputs and rehashed after execution.

`tools/default_background_selftest.mjs` is reused through a reviewed copy in the fresh evidence directory. The original currently fails before native execution because its SettingsView default MP4 wording no longer exists. The wrapper substitutes the exact current backgroundGet/loadBackgroundState consumer binding as a **source-contract** check and invokes the installed MSVC/SDK explicitly because vcvars64 omits the installed UCRT headers here. Its production C++ extraction, isolated cwd paths, memory settings, assertions and execution remain unchanged. Original/adapted script hashes, exact native read-source hashes, extracted function regions, generated C++/EXE hashes and compiler identity are recorded. Source text checks do not prove UI rendering or native IPC.

The 20 frontend cases execute production `background.ts`, `backgroundPolicy.ts`, `dynamicBackground.ts`, `sharedGameMedia.ts`, `gameMediaRevisionFence.ts` and `steamMediaSource.ts`. They include hysteresis, malformed battery samples, video intent/actual reconciliation, actual bridge request construction, clamped settings/event feedback, settings/native IO failure, ready media, disabled/no IO, shared inflight requests, PID reuse/missing creation identity, disabling during a pending read, deleted/invalid media, a revision invalidated during IO, native failure, missing receipt and an unresolved transport. The unresolved request is explicitly recorded as incomplete, not a successful media response; the outer runner has its own real timeout test.

The native fixture executes 12 checks covering absent directory/file, directory/empty/invalid MP4, valid default media, custom precedence, persistent clear, missing custom media, custom re-enable, legacy config and malformed legacy JSON. Files stay inside the new native fixture directory; the actual desktop wallpaper, default video path and user settings are never touched.

`result.json` exposes `sourceChanges`, `sourceHashes`, `actualCompiledInputs`, `nativeSourceInputs`, `nativeArtifacts`, `executedNativeRegions`, `executedProductModules`, `inputConsumerStateFeedback`, `sourceVerification`, `checks`, `remainingGaps` and explicit safety claims. `worker-result.json` retains the complete raw contract used by the parent runner. Corrupt or missing acknowledgment is validated independently of case PASS labels.

The production changes preserve low-battery state for unknown/nonfinite/wrongly typed battery values, validate dynamic cache receipts at runtime, and recheck the enabled setting and current revision after awaited process admission. The fixed-background bridge is exercised unchanged. Full App.vue rendering/media element behavior, installed IPC, actual process identification, OS sleep and physical display output remain separate acceptance work.
