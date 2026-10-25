# InputHost managed lifecycle integration (2026-10-23)

Exclusive tools: `tools/input_host_managed_lifecycle_selftest.mjs`, `tools/input_host_managed_lifecycle_legacy_replay.mjs`, `tools/input_host_managed_lifecycle_evidence.mjs` and this fixture directory. No production InputHost/native/global registry files are edited.

Run from mainline with fresh absolute output directories:

```powershell
node tools/input_host_managed_lifecycle_selftest.mjs --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-current
node tools/input_host_managed_lifecycle_selftest.mjs --fault accept-late-output --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-negative-late
node tools/input_host_managed_lifecycle_selftest.mjs --fault skip-output-stop --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-negative-output
node tools/input_host_managed_lifecycle_selftest.mjs --fault skip-idle-stop --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-negative-idle
node tools/input_host_managed_lifecycle_selftest.mjs --fault ignore-parent-cancellation --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-negative-parent
node tools/input_host_managed_lifecycle_legacy_replay.mjs --inputs G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-current/sources --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostManaged23/new-legacy
```

The evidence audit script seals the specifically named delivered runs (`current-005`, four `negative-*-001`, `legacy-adapted-001` and retained `existing-original-001`); it is deliberately a dated delivery check, not a global adapter. `node tools/input_host_managed_lifecycle_evidence.mjs <evidence-root>` reads those receipts, recompares original source/tool hashes, and refuses missing semantic negatives or erased legacy failures. Keep all original failed build rounds.

The current runner imports the existing `createOfflineHostFixture`, preserving exact production parser/dispatcher/prepare/release/wait/response functions. It removes only that generator's terminal fake SDK class definitions and replaces them with the existing host-sdk-stop BCL scaffold plus hash-locked SDK 2.0 Dispose/StopThreads/StopWorkers/CloseOutputCts/OutputPollLoop and clock Stop/Wake/Close methods. `ManagedCases.cs` is assertions, fixtures and accessors, never a replacement state machine. The two actual production stop/receipt helper files compile unchanged. The only call-target substitutions route fake assembly metadata through their existing IRuntime seams; production metadata admission is independently tested to reject fake assembly identity.

Real operations: owned BCL threads, CTS and wait handles, AutoResetEvent, cancellation lock contention, join, named pipes, OpenProcess+RegisteredWaitHandle on owned helper parent processes. Each row captures resource state before fixture cleanup; cleanup then unblocks/joins only fixture-owned workers and exits owned helper parents by closing stdin. Product Main/installed applications/driver APIs are never executed. Native timer handle stays zero and native clock calls throw if reached.

Explicit terminals: fake SDK constructor/context callback, neutral/frame backend, complete PnP census snapshots, fake wait clock, report-chain close recorder, HidHide commands, optional diagnostics, test-only USB maintenance lease and virtual signal reset observer. They prove dispatch/refusal/order, not real USB mutex/service, driver, endpoint removal, report-chain mapping destruction or physical acceptance. The original lifecycle entry currently fails compilation because the last two terminal dependencies were added to production without bindings in that fixture. Its failure is retained. The independent legacy replay adds those terminal declarations only to its generated evidence project and keeps the original test assertions.

Current integrated result contains 18 assertions per build configuration: 16 healthy/refusal/ordering cases plus 2 known failing resource contracts reproduced successfully. An exit 0 from the fixture means assertion/reproduction success; it does not grant lifecycle acceptance. `resourceContractPass:false` and `knownFailureReproduced:true` record the original parent-loss blocked-output/blocked-clock gaps. DELIVERY.json always keeps `lifecycleAcceptance:INCOMPLETE` and `fullParentGoalComplete:false` for this delivery.
