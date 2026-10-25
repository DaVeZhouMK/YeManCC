# Virtual controller signal diagnostics in the existing global simulator



`controller.signal-evidence` calls the established `tools/msi_external_raw_receipts_selftest.mjs` entry in a case-owned output directory. It executes the actual raw helper, descriptor decoder, bounded semantic witness, timer Recorder, extracted production resume hook, actual timer callback/counter/export consumers, and complete actual worker/callback retirement. It does not introduce a product model or an additional device reader.



The consumer verifies every child result, specified failing mutants, current source hashes, actual reported compiler/SDK identity, and the declared compiler/SDK/Python tools before accepting diagnostic PASS. The existing reviewed MSVC/SDK declaration is reused only for tool identity; no Decky consumer is invoked. Supplementary HID/Python identities are in `tool-inputs.json`. Imported Python module identities are an observed import closure, not exhaustive IO tracing.



Input, timer, Host, service and device leaves are memory fixtures. Stop additionally measures actual Windows Wait/Close on private test-owned threads and events; a controller join handle preserves proof even when the legacy product handle is closed. No installed YMCC, Steam, physical controller, real input timer, service, driver, OEM setting or sleep is operated. The callback Recorder has no control authority. In particular its negative cases do not prove real timer callbacks have drained.



The parent virtual input feature is **persistent**: it owns periodic timers, subscriptions, a capture worker and kernel handles, and a Host session across power changes. This scenario reports no measured lifecycle observations. Its diagnostic PASS therefore leaves the normal global lifecycle gate **INCOMPLETE**, with a nonzero global result; it cannot authorize a release or prove suppression/game consumption. No lifecycle measurement is inferred from return codes or fabricated for the gate.



Use the existing CLI (absolute fresh evidence directory):



```powershell

node tools/ymcc_sim/global/cli.mjs --mode plan --profile full --test controller.signal-evidence --out-dir <evidence-directory>

node tools/ymcc_sim/global/cli.mjs --mode run --profile full --test controller.signal-evidence --out-dir <evidence-directory>

```



The registered runner executes `node --test tools/ymcc_sim/global/controller-signal-evidence/receipts.test.mjs` against its own fresh consumers, with absolute `YMCC_SIGNAL_EVIDENCE_RECEIPTS` and `YMCC_SIGNAL_EVIDENCE_SOURCE_FILE` inputs. Standalone runs require those explicit inputs; they have no old-result default. Those checks copy existing actual child receipts into private temporary fixtures and mutate only that copy. They cannot substitute for executing the registered scenario.

The consumer writes `diagnostic-result.json`; `result.json` belongs to the global runner. Its original diagnostic result and SHA must remain intact for later audit. The child environment contains only required OS locations plus runner-owned output/tool values; compiler/Node/Python/fixture overrides are not inherited.



The worker retirement group executes the whole current production worker/callback with explicit memory OS/init/shutdown/BUS/Host leaves, then reproduces the legacy cancellation gap and removes only the fault-stop guards as a specified negative. It does not execute the real capture shutdown body or prove bounded destruction when a callback stalls. Private real-WinMM leaf evidence is kept separately and is not launched by this global diagnostic scenario.



The Stop group separately verifies wait status, handle closure and retained records, including actual private kernel timeout/signaled cases. A second Stop after the fixture controller releases/joins is only a fixture continuation; the current product caller does not provide that retry. Retained ownership is not bounded destruction, and the parent lifecycle gate remains incomplete. Cancellation-failure conditional models are preserved separately as unaccepted evidence; unsupported error ports are not claimed as actual OS/product failures.



The startup groups add the production StartupWait header unit and complete current Start/worker/Stop functions. They reproduce the historical 2500ms unconditional delay versus the real 2000ms Stop wait, then verify cooperative stop before/during that delay and preserve normal 2500ms expiry. Only test-owned Windows threads/handles are used; COM is explicitly rejected before capture/timer/Host admission. Header negatives delete notify or the predicate and must fail while still explicitly joining their threads. Existing worker-retirement and Stop-only fixtures use clearly marked fake startup/stop-publication leaves; they do not claim to exercise the real condition-variable wait. The parent persistent lifecycle, arbitrary stalled destruction, physical signal suppression and game consumption remain unaccepted. Failed fixture settlement must retain ownership and fail; process exit is never accepted as thread cleanup.


The terminal-fence group executes the full current Raise/Reset/getter/cleanup predicate/admission predicate. Fresh-process static state supplies the open fence; delayed capture initialization may not clear a raised terminal fence. It covers five call orders including joined concurrency, retains the historical clear defect and restores that one clear as a specified negative. All queue and log leaves are memory-only. The separately replayed actual WM_DESTROY exit path and main-finally still expose an unaccepted held-worker continuation gap (`tools/input_capture_exit_continuation_selftest.py`, production acceptance false/nonzero). No controller-release, physical or parent persistent lifecycle observation is inferred from the fence group.


The fence consumer also extracts and executes the identical local fixture-restoration lambdas from the two existing native selftests. Their eight setup/restore sites have explicit test-only authority; production initialization remains unable to clear exit. This additional scenario verifies test restoration followed by production late-init refusal. It is not a claim that either complete native CLI selftest was executed. The positive receipt count follows declared child receipts; each production child still has its explicit schema, source and negative checks.


The partial-init group executes the complete production Shutdown and the original contiguous Init prefix through DSU start on explicit memory leaves. It checks actual resource counts when ready was never set, repeated cleanup, failed sensor shutdown retaining the same sink, terminal Init admission, and late same-owner DSU acquisition followed by final Shutdown. Full Init, the real capture worker, DSU sockets and sensor/Host shutdown are not executed. A preserved frozen source and four specified resource/admission mutants must fail their resource assertions with exit 2; these failures never become production acceptance. The normal global entry runs both profiles for all six routes and verifies current source/tool identities and the negative counters, rather than accepting a declared pass.

The runner budget is 480 seconds to include the added twelve compiler invocations on top of the existing private-kernel suite; this does not change the product's 30-second R3 transaction budget or any product wait. The parent persistent lifecycle remains INCOMPLETE.


## Registration update (current source freeze)

The registered scenario now has 18 groups. The original 12 groups remain, followed by `stop-receipt`, `exit-continuation`, `dsu-owner`, `sensor-owners`, `x2-owner`, and `gamebar-owner`. Current partial-init output is schema-driven: the current source has 57 cases per configuration, including six Init-prefix cases, 50 resource-settling cases, and the isolated GM-without-DLL case that retains Motion debt and remains UNKNOWN for destruction. The 14 negative variants run in both Release and Checked, for 28 specified failing builds; the exact historical baseline is linked to its independent closure and old receipt.

The formal command remains:

```powershell
node tools/ymcc_sim/global/cli.mjs --mode run --profile full --test controller.signal-evidence --out-dir <fresh-absolute-evidence-directory>
```

Before a formal run, the bounded registration gate can be run without launching consumers:

```powershell
node --test tools/ymcc_sim/global/controller-signal-evidence/registration.test.mjs
```

The registration runner uses four independent output batches and inspects every settled result. Each consumer has a 900-second timeout and the registered scenario has a 1800-second timeout. Timeout, signal termination, tool or source drift, missing receipt, or an expected negative that exits cleanly is a failure; it is never converted to PASS.

DSU is bound to `VC-DsuOwner23/FINAL-AUDIT.json` and its final receipt. Sensor owners use the 20-profile receipt. X2 uses `VC-X2Owner23/RESULT.json` and its `verified/source-bindings.json`; Gamebar uses `FINAL-AUDIT.json` and its `finalResult`. These leaf receipts retain their memory/private-kernel boundaries. The parent virtual input lifecycle remains persistent and incomplete; Host completion, arbitrary blocked-parent destruction, hardware input suppression/game consumption, installed application, service, driver, and sleep acceptance remain unproven.


## Execution tools and short owned paths (2026-10-23)

`tool-inputs.json` now declares the primary Node and Git executables alongside
Python, MSVC compiler/linker/dumpbin, vcvars and the command processor. The
allowlisted child PATH contains only these declared tool directories and the
required Windows directories. DSU and Sensor invoke primary Git/CL/Node by
absolute path, check process results before reading output, and record primary
identities before and after execution. Ambient tool overrides remain excluded.

Each invocation creates a unique owned tree under
`Testing/YMCC/Evidence/VC-CaptureReceipt23/t/r-*/jNN/`. `owner.json` and `job.json`
record its repository, formal output, tools/source identities and role; their
hashes are verified during execution. Each job uses its own short `t/` directory
for child TEMP/TMP. Partial-init also builds under the same job's short `o/`
directory because long formal case nesting can exceed MSVC's include-path
limit. The canonical formal receipt is an unchanged byte copy; the execution
record and aggregate retain the actual receipt path and SHA256. No HOME,
machine/user TEMP setting, drive mapping or production code is changed.

Run the bounded execution probe in a fresh absolute output directory:

```powershell
node tools/ymcc_sim/global/controller-signal-evidence/execution-probe.mjs <fresh-absolute-probe-directory>
```

It calls the real DSU/Sensor runner entry points with `--tool-probe`, executing
Git, SDK setup, CL compile/link and a binary that creates/deletes one file in the
owned TEMP. It also compares actual header paths of 248/262/280 characters and
TEMP paths of 211/280 characters. Probe receipts are named `TOOL-PROBE.json` and
carry `productionAcceptance=false`; they cannot replace a normal owner receipt.
Normal DSU/Sensor commands continue to execute their complete original cases
and negatives. The 503 previous broad evidence inputs are all retained, with
three helper/probe sources added (506 evidence inputs, 507 including run.mjs).

R2 partial-current already passed. Its header path was 248 characters, while
long mutant snapshot headers reached 262 and 280; the inspected R2 build logs
contain C1083 despite those files existing. The minimal compiler comparison
reproduced that boundary. The existing 211-character TEMP compiled; a separate
280-character TEMP reproduced D8018. These are distinct from the previously
fixed missing-TEMP-directory issue and are not evidence of a shared-TEMP race.
Parent lifecycle, arbitrary blocked destruction and physical/installed scope
remain unchanged and unaccepted.

## Primary creation-thread ownership (group 21)

`primary-thread/RESULT.json` runs the actual adoption fragment and complete primary retirement, Clear, BUS/lifecycle quarantine, Owned/Disabled and base-log functions through `tools/input_host_primary_thread_selftest.py`. The original two 31-check positives and twelve historical/mutation negatives are unchanged. All fourteen complete generated units, compiler commands, dependencies, objects, executables, logs and runtime resource witnesses are pinned. Each route joins five real private threads and returns handles 50 to 50 before exit; positives require zero oracle salvage.

The producer adds only optional toolchain, runtime closure and explicit approved identity bindings. The raw main, complete raw functions, actual Start and historical Start remain pinned separately from CRLF-to-LF-only function hashes. Thirteen actual source inputs include the real transport/process/Start/Stop headers. The historical Start, BEFORE.json and run-final2 evidence remain immutable.

The full runner uses the parent sensorEnsure approval authority; this registration does not refresh that approval. `primary-thread-registration.mjs` accepts an explicit manifest path and SHA for a bounded independent group21 run. It uses the existing short owned workspace, all 731 reviewed tools and the complete 5772-file frozen-toolchain before/after closure, without flattening or reducing it. `primary-thread.test.mjs` supplies new registration and rejection gates alongside all previous gates. A standalone group PASS leaves parent lifecycle acceptance INCOMPLETE and does not execute the other twenty groups.

An independent source snapshot may reside three levels below the work root, preserving the producer’s fixed historical evidence location. Its explicit approval binds `sourceRepository` and `toolchainAuthorityRepository`; the latter permits reusing the original captured toolchain authority without changing any frozen dependency. Such a receipt proves only its immutable snapshot, and cannot be passed to the normal full validator as the shared current repository. Original failed live-source attempts remain preserved.

## Bounded current-source integration (2026-10-23)

The signal scenario still has twenty-one children. `exitFenceCurrent` and its old source contract remain immutable; `exitFenceCurrentV2` selects the separately reviewed complete startup-policy dependency. Its sole substantive addition publishes `startupObservedBoot`. The full pure native policy suite executes 37 cases in Release/Checked, and the original fence suite preserves all 148-check primary/control routes and unknown-source/clear negatives. The former current receipt is tested only against its actual historical snapshot and explicit original registration; it cannot sign the new dependency.

`native-exit-source-closure.json` binds both new exit headers and Gauss's actual producer/fixture bytes and preserved delivery receipts. It adds source coverage without registering or claiming execution of a native exit group.

`managedTerminalCurrent` is a versioned **pending** signal integration, with `registeredSignalGroup=false`. Its independent actual run has 40 cases per configuration and all five semantic mutants, retains the 19-case legacy and old zero/false failures, and binds explicit current source identity plus runtime closure. The helper and standalone entry are saved as `managed-terminal-receipts.mjs` and `managed-terminal-registration.mjs`; no twenty-second child or full-run job is enabled. `bounded-current-evidence.json` pins the evidence actually consumed by registration gates. A first managed attempt rejected a legitimate concurrent parent-fixture source change; the original FAIL is preserved, and the subsequent source identity is separately versioned against the actual five-case parent-port receipt.

The separate existing `controller.host-parent-lifetime` entry now includes actual `HostTerminalCleanup.cs` after the parent tool's LoopResult port. Its original five cases and `ignore-parent-cancellation` negative remain; its own `host-run.mjs` passes both configurations and rejects exactly the original three cancellation cases. This is real BCL pipe/parent cancellation and actual terminal policy on recorder owners, not actual Host Main or SDK release acceptance. The forty-case managed consumer covers its separately declared real private worker/barrier scope.

The parent whole-main approval, original 5772-file closure and 731 reviewed tools remain unchanged. No complete signal scenario or full global run is performed by this bounded integration.

## Managed terminal group22 (2026-10-23)

The default signal queue now executes managed-terminal/RESULT.json after preserving all original21 receipt slots. Both the full queue and managed-terminal-registration.mjs call the same runManagedTerminalGroup production test executor. Its six real child invocations retain two current 40-case profiles, ten specified negative builds, exact generated functions, private worker/parent resources, complete571 .NET tool pins, original legacy19 and old zero+false failures. The two basis receipts are declared artifacts as well as runtime pins. Group registration does not prove whole Host/driver/physical sleep acceptance.

Fresh wrapper/resource rejection tests are managed-terminal.test.mjs; full runs supply their new canonical receipt, while a scoped run supplies YMCC_MANAGED_TERMINAL_EXECUTION explicitly. A bare registration run never uses an old runtime default.
