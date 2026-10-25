# Clear / pipe retirement composition

This isolated consumer extracts the complete current production functions:
`inputHostPipeCloseOwned`, `inputHostPipeRetirementProvenLocked`,
`inputHostClearLocked`, `inputHostReconcileQuarantine`,
`inputHostReconcileQuarantineOnLifecycleThread`, and Owned/Disabled predicates.
It executes them with the actual pipe Runtime and private Win32 HANDLE leaves.
No production function body is replaced or shortened in the current variant.

Eight scenarios cover the pre-adoption raw slot before first Runtime, failed
original and terminal-event Close, pending I/O with retained issuing duplicate,
terminal cache, unavailable/busy snapshot with the borrowed pointer already
absent, wake hold, endpoint absence, physical restore failure, PS durable
commit/readback failure, and an explicit rearm request. Clear must retain the
old run/epoch/HELLO/PREPARE/creation history and quarantine/start gate until the
same owner is actually retired. The actual BUS consumer queues the source-only
continuation; its external queue is a counted port and the fixture calls the
actual lifecycle consumer afterward. Physical restore/rearm cannot precede pipe
retirement. A repeated lifecycle callback must be inert.

Host process, SDK, physical endpoint and OEM evidence are explicit memory ports.
Read/write/query/cancel/disconnect leaves reuse the controlled memory ports from
the existing pipe fixture; successful event/duplicate/Close leaves use real
private Win32 HANDLEs. Current positives require no owner-external salvage and
all runs compare the independent live-handle oracle and process HANDLE counts
before exit. This does not execute a real named pipe, Host or SDK, prove physical
return, or establish bounded destruction under arbitrary OS/driver blocking.

Use the existing bundled Python and captured toolchain; the runner creates fresh
evidence below `Testing/YMCC/Evidence/VC-ClearRetire23` by default:

```powershell
& 'C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' -B tools/input_host_clear_retirement_selftest.py
```

`--out` selects a fresh absolute private directory outside source/toolchain;
`--toolchain` accepts the existing captured MSVC environment/manifest. Actual
compiler inputs and searched linker libraries must match that capture, tools
are hash checked before/after, and live source drift prevents a current PASS.
No frozen pins or earlier receipts are rewritten.

Release/Checked each execute current plus three compiled semantic controls:
remove both Clear pipe gates; remove the lifecycle early pipe gate; move Clear
after rearm checks. Each control must compile and hit its specified original
assertion. Build errors and unrelated failures cannot satisfy a control.
Global registration and its strict validator remain owned by the parent task.
No product, Steam, driver, service, device or real sleep operation is performed.
