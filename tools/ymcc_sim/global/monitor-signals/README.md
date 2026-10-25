# Monitor consumer signals

This independently registered adapter reuses three reviewed repository tests and executes real monitoring product functions with synthetic inputs. It does not change monitoring product source or use a native App/Host. Current product modules are compiled in isolated Node workers, with a strict source whitelist and before/after source hashes.

```powershell
node tools/ymcc_sim/global/monitor-signals/index.mjs --evidence-dir D:/path/to/fresh-monitor-evidence
```

```js
import {runMonitorSignals} from './tools/ymcc_sim/global/monitor-signals/index.mjs';
import {getMonitorAdapters} from './tools/ymcc_sim/global/monitor-signals/adapter.mjs';
const result=await runMonitorSignals({evidenceDir:'D:/path/to/fresh-monitor-evidence'});
```

The parent owns global registration. Spread `getMonitorAdapters()` once into its adapter list. The descriptor uses the existing sensor domain and `sensor.monitor-consumer-signals` ID; the global result is `monitor/result.json`. No global CLI/registry is modified here.

The runner calls the current getAdapters/getExtraAdapters exports before execution. An original test that has already acquired a direct adapter path is not executed twice; the evidence marks that slice `ALREADY_DIRECTLY_REGISTERED` and keeps this run blocked until the parent removes the duplicate selection. An evidenceInputs reference is only a source association and does not count as direct execution. The initial audit found no direct or evidence-only registration for the three targets.

## Retained tests and adaptations

- `tools/monitor_consumer_lifecycle_selftest.mjs`: real monitor hub/display feeds/shared source/freshness owner. All 57 original assertion calls remain. Adds records at existing checkpoints and exposes those same compiled exports for five extra command-receipt failure scenarios.
- `tools/monitor_clock_demand_selftest.mjs`: real model/sample source/top projection/preview owner/HUD coordination/hidden relay. All 97 original assertion/check calls remain. Its retained before snapshot must match the 15 files in clock-baseline.json. The exact original bytes are vendored under `fixtures/clock-before/` and copied from this repository-relative root into fresh evidence. The historical Build path is provenance only and is never required by a clean checkout. Missing or differing baseline files block this slice. `--clock-before-root <root>` may relocate the exact snapshot; it cannot authorize other bytes.
- `tools/monitor_bounded_identity_selftest.mjs`: real boundedHudData ownership, stale override and isolation. All 5 original calls remain, including the common assert wrapper; four identity checks execute.

Scripts are copied into evidence before adaptation; originals are not edited. `adaptations.json` records each exact old/new anchor and assertion-preservation receipt. New notification, game-policy and placement/snapshot authority imports in current HUD code receive explicit inert/failing ports. No model, sample source, hub, preview or relay implementation is replaced. The original clock formatter instrumentation remains and only counts original Intl calls. Host coordination TS is tested with its clock/carrier/IPC/notice/settings ports injected; this does not start any native Host.

`source-execution.json` records exact compiled source files and injected ports. `executionBoundaries` identifies named functions exercised by assertions; other compiled dependencies and referenceInputs are not promoted to full behavior coverage. Inputs are labelled source-derived-fixture, including the old source snapshot. Synthetic PID/lifetime/game strings come from the reviewed fixture and never from user sessions or running games.

## Failure gates

Each slice has a bounded Node worker, output limit and mandatory structured receipt. Nonzero exit, timeout, missing/not-PASS receipt, changed reviewed test, source drift or unknown source import fails or blocks the run. Review-lock.json pins the three audited test files. All source hashes, including the runner itself, are checked again before reporting success. New source imports need review.

Five extra product hub cases check missing start acknowledgement, refusal, thrown timeout, missing stop acknowledgement and a start acknowledgement arriving after release. Unknown state remains pending; retries retain the actual product budget; release emits the idempotent stop. A pending stop is not treated as a confirmed physical stop.

The five runner controls are separate from product coverage. They verify an actual timed-out disposable Node child is reaped, exit 0 without a receipt fails, failed receipts fail, a changed evidence-only source is rejected by the same source guard, and truncated output cannot pass. They never mutate product source.

## Scope

Only source/package/compiler reads and fresh owned evidence files use the filesystem. Sensor/IPC/window/carrier/timer/preference ports are injected. No devices, native App, real Host, installed directory, user settings/session, live game identity, network, physical sampling or OS sleep is used. Clock demand operation counts are not CPU performance measurements. Monitor dragging/rendering and the full native publisher path remain unverified. A PASS covers the named exercised behaviors; it does not accept the entire monitoring module or YMCC.


## Static descriptor and missing inputs

The descriptor has a fixed reviewed list of 15 fixture source names. Importing it or calling getMonitorAdapters never reads the baseline manifest; missing or invalid fixture metadata cannot remove the case or break other capability selections. clock-baseline.json and each before source remain required evidenceInputs. The real global runner classifies missing files as BLOCKED. The runtime also validates the manifest schema/file list/hash shape before registration audit or any product loading, returning structured BLOCKED and exit 10 on invalid inputs. Original 15 source SHA pins remain unchanged.

The monitor descriptor declares blockedExitCodes: [10]. The parent-owned runner must honor this explicit opt-in for a Node BLOCKED receipt; the original runner only recognizes PowerShell exit 10. runner-blocked-exit.patch provides the exact minimal parent-owned change and its input hash. descriptor-selftest.mjs validates missing manifest, invalid manifest and missing before source using fresh checkout copies; mainline fixtures are never changed. It separately records the current runner behavior and the proposed mapping, without relabeling the original Node FAIL as BLOCKED.
