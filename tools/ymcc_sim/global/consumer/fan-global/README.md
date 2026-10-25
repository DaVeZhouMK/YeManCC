# Original fan regressions for the global simulator

This entry runs the existing five TypeScript test suites without changing their source or assertions. It uses the repository's esbuild dependency and the original ESM-bundle → Node execution method, with repository cwd and independent case directories:

```text
node tools/ymcc_sim/global/consumer/fan-global/run.mjs --evidence-root <fresh absolute directory> --output <result.json>
```

Optional `--repo-root` identifies the current checkout; the default is this containing repository. Optional `--test startup-self-heal,control-self-heal,lifecycle,wake-progress,display-evidence` selects only those reviewed original suites. The exported API is `runFanGlobalRegressions({repoRoot,evidenceRoot,output,timeoutMs,tests})`.

| Suite | Original entry | Meaning |
| --- | --- | --- |
| startup-self-heal | tools/fan_startup_cleanup_self_heal_selftest.ts | Actual retained ON, bounded cleanup and exact-exit/single-successor recovery without physical OEM evidence |
| control-self-heal | tools/fan_control_self_heal_selftest.ts | Actual bounded recovery for OEM/resource errors, unknown writes, finite feedback observation, OFF and new-owner cancellation |
| lifecycle | tools/fan_host_lifecycle_selftest.ts | Actual FanHostLifecycle queue/admission/lease/control/restore/suspend/resume/close plus original static contracts |
| wake-progress | tools/fan_wake_open_progress_selftest.ts | Actual wake → Open/OpenEvents progression, supersession and write admission using the existing NoIoHost |
| display-evidence | tools/fan_control_display_evidence_selftest.ts | Actual bridge generation-resolution/display publication with an inert subscriber, including stale reads and failed delivery |

Current getAdapters/getExtraAdapters contain no direct execution owner for these five original entries. Other fan adapters invoke their own regressions, rather than these complete original suites. A package script or an evidence-input association alone is not execution and is not treated as duplicate behavioral coverage. The runner emits this review as `duplicationReview`; the parent adds only one aggregate adapter.

The lifecycle deployment-record section creates fake manifests under os.tmpdir and injects its own runInstaller function. It does not invoke an installer. The child TEMP/TMP/TMPDIR values are redirected to that case's private tmp directory, keeping the original code intact. Literal HC runtime IDs there are fixture strings; no HC assembly or installed payload is read or loaded.

The child preload `runtime-guard.mjs` denies network clients/listeners, child processes, workers, native addon loading, links, and writes outside the owned case. Only reviewed static sources and actual compiler inputs are readable outside the case. Attempted forbidden IO invalidates the case even if the caller catches it and exits zero. The guard changes process-local boundary APIs, never a product expression or a simulated fan/recovery implementation.

Each esbuild onLoad returns the exact original source bytes and records their SHA-256. The actual metafile graph must match captured inputs. Results retain compiler version, bundle hash/metafile, exact input hashes, separate static-source hashes, original structured output and exit code, boundary receipt, and source stability. Aggregate `sourceHashes` is compatible with the existing global structured-evidence checker; contradictory snapshots or end-of-run drift fail acceptance. Static .vue/.cpp/.cs/MD reads remain labelled static; their presence does not prove runtime or hardware execution.

PASS requires every requested original entry to execute, exit zero, return its original ok=true/nonempty-check report, preserve all source hashes and produce a passing runtime boundary receipt. Missing dependency/source/report/consumer input, failed original assertions, forbidden IO or a timeout cannot pass.

The display suite covers real bridge callback publication and original source contracts, not rendering a FanView in a browser. No real Host Main, Host process, install operation, EC/OEM/HID/driver or OS sleep is exercised. Hardware/physical OEM restore, browser/physical UI and HC/runtime construction remain separate gates. Scope is original tests with inert boundaries; it is not full product acceptance.

The separate runner tests consume an explicit actual result and an owned unit root:

```powershell
$env:YMCC_FAN_GLOBAL_RESULT = '<actual result.json>'
$env:YMCC_FAN_GLOBAL_UNIT_ROOT = '<owned work directory>'
node --test tools/ymcc_sim/global/consumer/fan-global/runner.test.mjs
```

These validate failed/missing evidence and launch only tiny owned Node guard probes. No Host or network is used. `adapter-suggestion.json` supplies one reviewed adapter object for the parent; this directory does not edit global CLI/registry or sleep entries.
