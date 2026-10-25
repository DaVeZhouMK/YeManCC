# Enabled MSI SD rear consumer public port

This directory exposes exactly one adapter: `controller.msi-sd-rear-consumer`.
Only the existing enabled MSI SD rear consumer is admitted. The unchanged original
producers exercise extracted production consumers with their original memory APIs.
Native keyboard stays at its original default build lock; the secondary display
and keyboard Experiment configurations are never enabled.

This delivery changes only this directory. Parent owns registry integration and
the sole formal execution. No producer, native/compiler, global suite or audit was
run to prepare this port. Local contract tests read the parent's existing receipts
at `D:/SD27/current-enabled-positive` and `D:/SD27/current-enabled-faults`; they are
structural/static evidence, never a new public-port PASS.

## Parent adapter contract

Import `getMsiSdRearConsumerAdapters(repo)` from `adapter.mjs` and append its one
returned descriptor in the parent registry. Its fixed contract is:

- ID `controller.msi-sd-rear-consumer`, domain `controller`, kind `powershell`,
  scope `behavioral-mock`, profiles `desktop` and `full`.
- Entry `Run-MsiSdRear.ps1`, arguments `-EvidenceDir {outDir}/msi-sd -NodePath`
  followed by the approved manifest's exact Node executable.
- Outer timeout **480000 ms**. The shared runner automatically invokes the existing
  `global/host-process.ps1` gated kill-on-close Windows Job wrapper. All wrapper,
  producer, compiler, native probe and per-fault descendants inherit this Job.
  There is no second Job, detached child, breakaway or name-based cleanup here.
  Job membership is expected from that runner contract, not independently observed.
- Node has a combined 450000 ms deadline to leave time for outer receipts/cleanup.
  The original producers' **60000 ms** child budgets and **8 MiB** buffer limits
  are unchanged. Outer timeout, output/launch failure or missing terminal fails;
  the shared Job reclaims descendants when it closes. A forced outer termination
  may prevent a wrapper terminal; that remains a failure.
- Exactly one stdout completion line is emitted after the current result and
  `wrapper-terminal.json` have been written successfully:

```text
MSI_SD_REAR_CONSUMER_V1_COMPLETED positive=78/67093 faults=5/5 DefaultRelease
```

The descriptor supplies this literal as `completionContract.rules[0].line`.
The shared runner must see it exactly once, together with natural outer exit 0,
complete stdout/stderr and unchanged source/tool identities. Do not interpret an
embedded copy in JSON or a historical completion line as completion.

Do not invoke `run.mjs` as an independent unmanaged process for formal execution.
It requires wrapper-generated `--run-id`, `--powershell-path`, `--powershell-pid`
and a fresh absolute `--evidence-dir` outside the checkout. The evidence parent
must already exist. Existing evidence, reparse paths, missing inputs and drift
are refused without overwriting or retrying. No old result parameter is accepted.

## Exact original sequence

Both scripts run from the real repository, with their current original bytes
pinned to these SHA-256 values. Updating a producer requires explicit review of
this contract; this entry never copies, rewrites or regenerates either producer.

| Original producer | SHA-256 |
| --- | --- |
| `tools/hc_msi_rear_consumers_selftest.mjs` | `179b4939c8d2c14836166854d7c5065ef05b65d866da61a84356d2ddcb051399` |
| `tools/hc_msi_rear_consumers_source_fault_selftest.mjs` | `b3bcd6de44fed08487231502adf8e19eade38b6fbe3a4e103d5eca8cdf43f11e` |

1. Positive receives `--repo <repo> --reference <repo> --before
   <repo>/tools/fixtures/hc_msi_rear_consumers/before --out <own>/positive
   --configurations DefaultRelease`.
2. Only after the positive receipt and natural producer exit pass, the unchanged
   source-fault producer receives `--repo <repo> --reference <repo> --out
   <own>/faults`. Its own fixed filter selects the enabled five definitions, and
   it invokes its unchanged positive child with `DefaultRelease` for each overlay.

There is no caller-supplied configuration, script, fault list, reference tree,
source overlay, hardware permission or Experiment switch.

Positive requires one `DefaultRelease` configuration, compiled=true, accepted=true,
native natural exit 0/no signal, PASS, **78 cases / 67093 checks**,
`defaultNativeKeyboardWritesEnabled=false`, `fixtureExperimentCompiled=false`,
`allHardwareFunctionsLinkedToMemoryFakes=true`, and unchanged native production lock.

Faults require all five rows in the original order, each exactly once:

| ID | Exact original semantic error |
| --- | --- |
| `family-label-instead-of-exact-model` | `changed model admitted` |
| `drop-endpoint-size` | `bad physical tuple admitted` |
| `nearest-unknown-firmware` | `unknown firmware admitted by total library` |
| `wrong-report-route` | `total factory changed packet` |
| `sd-wrong-command-order` | `SD exact wire bytes differ` |

Each original child result must have one compiled/accepted `DefaultRelease`,
native exit **1**, signal=null, report FAIL and that exact error. The pinned
fault producer's `rejected` predicate proves its per-fault child natural exit
**0**/no signal, with a complete child result. That child's raw stdout must equal
its native report with the configuration field. The full fault producer itself
must naturally exit 0, and its five stdout rows must equal the saved summary.
Native/per-fault exit values originate in these unchanged producers; this port
does not claim an independent native PID or signal observer.

`native-missing-mode-fence` is explicitly **excluded because experimentOnly=true**.
It is not a required sixth fault and is not an omission. An extra directory or
summary row for it is refused. Compiler failures, missing results/executables,
wrong errors, signals, timeout, truncation, repeated IDs and missing positive/fault
receipts cannot satisfy this semantic-rejection contract.

## Sources and approved tools

`sourceFiles()` declares the complete fixed source list reproduced below, including
all six runtime files of this module; the two original producers; original probe;
BASELINE and both frozen source files; actual main and unchanged native keyboard
runtime; every SD header and both configuration catalog headers; JSON; policy,
generator and the relevant generator helper; HC lock/index and all six original
HC sources; extractor and its imported fixture helper; toolchain helper; imported
matrix helper closure; all six manifests; shared Job, runner and completion code.

Actual `native/main.cpp` is consumed for the enabled SD path and default-disabled
native boundary. Actual `native/msi_native_rear_keyboard_runtime.h` must match its
originalFiles baseline identity. No Experiment configuration is compiled. The
catalog policy must keep SD enabled, keyboard disabled, six exact HC source hashes
matching the lock index, and its original provider hash. Generator files are
identity inputs only; this entry does not execute the generator or its other domains.

Tools come solely from the unchanged
`motion-consumer-signals/matrix-evidence.mjs::matrixToolchainInputs(repo)`.
It validates the six-layer manifest chain and returns the entire approved
**582-file** set. Every declared tool must exist and match its approved SHA-256;
none is dropped when missing. There is no new manifest, new tool authorization or
weakened parent pin. Node/PowerShell paths and bootstrap hashes must match; cmd and
vcvars64 must also belong to this same set. Node injection and compiler environment
options are cleared in the child environment.

All declared source and tool identities are captured before, checked between
producers, and captured again after execution, including real path, byte size and
SHA-256. Any difference fails. The parent's main source is being changed in
parallel: **if it changes during execution, preserve that failure**. Do not relax
the identity comparison, revert parent changes, auto-retry or relabel old receipts.

The original `sourceDependencies` JSON and reported dependency list must agree
entry by entry. Each actual include must hash to the predeclared source/tool set,
or to the precisely checked current-run generated probe/one-fault overlay set.
The original input rows and all 15 source fragments are also checked against their
actual bytes. The raw compile command must exclude the keyboard experiment define.

**582 is a predeclared prerequisite count, not a count of actual links or loads.**
The original producers provide sourceDependencies and no independent complete link
trace. Checking actual includes does not establish every linker input, DLL loading,
whole Host execution, hardware acceptance or automatic restoration. These limits
remain explicit in `boundary` and `remainingGaps` on every result.

## Results and complete original pointers

The adapter requires these nine JSON files below its case directory:

```text
msi-sd/result.json
msi-sd/wrapper-terminal.json
msi-sd/positive/result.json
msi-sd/faults/result.json
msi-sd/faults/family-label-instead-of-exact-model/execution/result.json
msi-sd/faults/drop-endpoint-size/execution/result.json
msi-sd/faults/nearest-unknown-firmware/execution/result.json
msi-sd/faults/wrong-report-route/execution/result.json
msi-sd/faults/sd-wrong-command-order/execution/result.json
```

The public `result.json` has schemaVersion=1, the fixed ID, status/passed, fresh
runId/evidenceRoot, Node/PowerShell identities, start/end/duration,
`positive`, `faultSummary`, all five `faults`, two `processes`, declarations,
`inputsUnchanged`, `toolsUnchanged`, `excludedFault`, `boundary`, `remainingGaps`,
`inputConsumerStateReceipt`, and any errors. It retains all unaltered original
files under positive/faults, including source overlays, generated source, compile
commands, binaries, dependencies, source-fragments and every raw stdout/stderr.
`originalArtifacts` and `sourceHashes` contain exact absolute/relative pointers,
byte sizes and SHA-256 values. Both producers' outer streams are written directly
to new files, without a string buffer limit, and have separate terminal JSON with
PID, command, cwd, timestamps, exit/signal, timeout/launch/completeness flags and
raw stream hashes. Original nested streams retain the original scripts' limits;
those scripts reject buffer/compile/parse failure before a successful result.

`attempt.json` binds the wrapper nonce to this Node PID and PowerShell PID.
`inputs-before/after.json` and `tools-before/after.json` retain all identities.
`wrapper-terminal.json` is written with CreateNew, only for this attempt; it binds
its own/Node exits and PIDs, current result hash, source/tool status, positive and
five-fault pointers, producer terminal records, Job expectation, boundaries and
completion literal. The shared runner's stdout.log/stderr.log preserve the wrapper
output. Missing/failed terminals remain failure; no success completion is emitted.

## Limited local checks

From the repository root, only this static/read-only test command is needed:

```powershell
node --test tools/ymcc_sim/global/msi-sd-rear-consumer/contract.test.mjs
```

The default fixture inputs are the existing parent receipts listed above.
For another preserved copy, set `MSI_SD_POSITIVE_CONTRACT_RESULT` and
`MSI_SD_FAULT_CONTRACT_RESULT` to its two original result.json paths. The fault
children must remain at their original relative subpaths. Tests mutate in-memory
copies of those actual report structures solely to verify rejection contracts;
they never execute producers or treat fixtures as fresh public evidence.

Node syntax checks and the PowerShell language Parser are sufficient additional
local checks for this delivery. Do not execute this wrapper, producers, compiler,
native probes, global run/audit or hardware flows here. Parent performs registration
and the one formal run after review.

The delivery index, static reports and READY marker are saved at
`D:/Codex/2026-10-18/kai-s/work/global-simulator-20261018/phase27/msi-sd-public-port`.
READY means ready for parent integration, not formal native/global PASS.

## Exact declared source paths

```text
tools/ymcc_sim/global/msi-sd-rear-consumer/adapter.mjs
tools/ymcc_sim/global/msi-sd-rear-consumer/inputs.mjs
tools/ymcc_sim/global/msi-sd-rear-consumer/contracts.mjs
tools/ymcc_sim/global/msi-sd-rear-consumer/evidence.mjs
tools/ymcc_sim/global/msi-sd-rear-consumer/run.mjs
tools/ymcc_sim/global/msi-sd-rear-consumer/Run-MsiSdRear.ps1
tools/hc_msi_rear_consumers_selftest.mjs
tools/hc_msi_rear_consumers_source_fault_selftest.mjs
native/main.cpp
native/msi_native_rear_keyboard_runtime.h
native/msi_native_rear_keyboard.h
native/msi_rear_protocol.h
native/msi_rear_sd_preparation_receipt.h
native/msi_blocked_start_preparation.h
capabilities/HcControllerConfigurationCapabilityCatalog.h
capabilities/HcControllerConfigurationCapabilityCatalog.generated.h
tools/fixtures/hc_msi_rear_consumers/probe.cpp
tools/fixtures/hc_msi_rear_consumers/before/BASELINE.json
tools/fixtures/hc_msi_rear_consumers/before/native/main.cpp
tools/fixtures/hc_msi_rear_consumers/before/native/msi_native_rear_keyboard_runtime.h
deps/json/json.hpp
capabilities/hc-controller-configuration-policy.json
tools/hc_capability_catalog_generate.mjs
tools/lib/hc_controller_configuration_catalog.mjs
deps/handheldcompanion-runtime/HC-BASELINE.json
deps/handheldcompanion-runtime/source-files.sha256
deps/handheldcompanion-runtime/source/Devices/MSI/ClawA1M.cs
deps/handheldcompanion-runtime/source/Devices/MSI/ClawA2VM.cs
deps/handheldcompanion-runtime/source/Devices/MSI/ClawBZ2EM.cs
deps/handheldcompanion-runtime/source/Devices/MSI/ClawCG3EM.cs
deps/handheldcompanion-runtime/source/Devices/MSI/DeviceVersion.cs
deps/handheldcompanion-runtime/source/Devices/IDevice.cs
tools/fixtures/legion_motion_consumer/extract_main.mjs
tools/lib/hc_controller_motion_fixture.mjs
tools/fixtures/hc_reviewed_calibration/toolchain.mjs
tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.mjs
tools/ymcc_sim/global/motion-consumer-signals/evidence.mjs
tools/ymcc_sim/global/motion-consumer-signals/contracts.mjs
tools/fixtures/hc_remaining_model_capabilities/expectations.mjs
tools/lib/hc_motion_matrix_guard.mjs
tools/ymcc_sim/global/motion-consumer-signals/toolchain-additions.json
tools/ymcc_sim/global/marker-ownership-consumer/toolchain-inputs.json
tools/ymcc_sim/global/sleep-single-pid-freeze-consumer/toolchain-inputs.json
tools/ymcc_sim/global/sleep-game-pause-consumer/toolchain-inputs.json
tools/ymcc_sim/global/sleep-non-user-wake-consumer/toolchain-inputs.json
tools/ymcc_sim/global/sleep-native-recovery-consumer/toolchain-inputs.json
tools/ymcc_sim/global/host-process.ps1
tools/ymcc_sim/global/runner.mjs
tools/ymcc_sim/global/execution-completion/index.mjs
```
