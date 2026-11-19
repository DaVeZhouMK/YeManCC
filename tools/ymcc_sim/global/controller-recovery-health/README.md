# Native recovery-health consumer adapter

Export: `getControllerRecoveryHealthAdapters(repo = REPO)` from `adapter.mjs`.
Case id: `controller.recovery-health`. Registration is intentionally left to the parent; no extra-adapters file is changed.

Run via the existing global producer after obtaining this adapter, or directly:

```powershell
node tools/ymcc_sim/global/controller-recovery-health/run.mjs --evidence-dir G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/recovery-adapter/new-case
```

The evidence directory must be fresh, absolute, inside the workspace Build directory and outside production checkout. Reusing a prior directory is refused.

The adapter freezes the existing original runner, original fixture, native source and helper headers. It executes the real health projection, producer continuity, request/ACK/suppression branches, maintenance and Window using the original inert fixture: 47 original case names / 80 assertions (original68 preserved;12 current-route/wake checks added) for diagnostics off and on. It never calls product Main, HostSession, HID SDK, real pipe, reader, device, sleep or recovery actions. It does not claim motion or CPU acceptance.

A private copy of `native/input_host_recovery_health.h` changes only the observation-time argument in `fresh()`: it clamps now to the cached read completion. The original runner's `--baseline-header` and explicit `--expect-semantic-failure` run the same assertions against this actual helper. Both builds must succeed, both executables must finish with exit1 and both original JSON reports must contain the four exact semantic failures listed in spec.json. A compile error, missing report, wrong case count or generic nonzero exit cannot pass.

The minimal change to the original runner adds the expected-semantic-failure mode and saves original reports before expectation validation. All68 original fixture assertions remain, with12 additional assertions inside three existing cases. The wrapper additionally checks exact case names, individual actual/expected assertions, positive/negative reports, source bindings, original stdout, compiler outputs and actual include/library ledgers.

Input closure includes this module, original runner/fixture, all extracted product headers, JSON header and calibration compiler setup helper. toolchain-inputs.json pins 301 installed tool/script/header/library artifacts from the original C++ consumer probe; all are verified before/after. Every compile emits MSVC sourceDependencies and link verbose-library output; external compiler inputs outside the declared set are rejected. Normal OS loader/kernel remains an explicit platform boundary. No SDK or installed app identity is claimed by these compiler pins.

The nested result.json supplies sourceHashes and affirmative status for the existing global producer. Actual global execution and receipt-audit must remain successful before registration; no independent model's PASS substitutes for the original consumer assertions. Negative results remain visibly failed inside the expected-rejection subreport.

Current frame context includes feedback SourceToken and game-motor Sample, so their real production headers are frozen and included. The newly reached downstream serial-feedback revocation is an explicit inert terminal, outside this health-only scope. No serial output, vibration or new feedback forwarding is executed or accepted; existing health assertions remain unchanged.

Current-source maintenance (2026-11-09): snapshot physicalReaderPid, MSI mode generation, actual owned kept/recreated wake helper and inactive lifecycle queue terminals are explicit. Health request slices exclude unrelated gyro-ready staging while retaining both actual health assignments. Raw/medium ROG source, medium PID replacement, MSI mode replacement and retained/recreated wake proofs are exercised in existing cases. Other recovery consumers and episode diagnostic writer remain inert terminals; no Stop/reopen/target-return action is accepted. The current291-member tool closure preserves the old removed-file/changed-byte diff separately and still checks all four actual compiler/link ledgers.
