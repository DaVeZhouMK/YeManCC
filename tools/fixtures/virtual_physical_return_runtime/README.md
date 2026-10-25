# Compact physical return + cross-native CLI writer regression (task 124)

This adapts the task111 compact regression after the real CLI writer bridge was
connected. Task111's delivered source and results remain historical, unchanged;
task124 archives its pre-edit tools under `cli-physical-tools124/original124`.
This test is not the CLI micro harness, the whole real-process CLI test, the
13-mode full pipe suite, or a normal product/Host execution.

The fixture compiles the complete physical target-return runtime, complete CLI
writer bridge, exact `hidHideCliReapPendingLocked` body extracted from main, and
unchanged `hidHideRefreshPhysicalHide`. Actual targetReturn Session/store, 119
writer Record/Session/store, transaction mutex, start protection, JobGuard class,
physical/path/process headers and selected main types/helpers are compiler inputs.
The writer bridge's open/clearSettled/settleOwned/quiescent and the physical
physicalWritersQuiescent/before/after algorithms remain verbatim. No whole gate,
record-open, settle or reaper function is faked.

The original five physical leaves stay explicitly controlled: private record
path, machine binding, current-parent/synthetic Host capture, modeled CM ancestry,
and exact old physical-writer identity observation. Five new CLI leaves are
explicitly adapted: private `cli-writer.json` path, `fixture-cli-machine` binding,
current-parent/synthetic token capture, registered old parent/child process
observations, and the exact registered job-name observation. Foreign unregistered
identities/job names throw and record forbidden I/O. No AppData, OpenJobObject,
foreign PID probe, real CLI, device, driver, service or installed settings are
reached. `record.json` and `cli-writer.json` are distinct files in the same owned
private case directory, with independent real owner sentinels and commits.

JobGuard is the actual class, kept unprepared in these compact cases. Active/Idle/
Unknown old job observations are a named explicit model at the permitted job
probe leaf; no job or child is created here. A private manual-reset Event is the
existing explicit old pending-root model, supplying actual Wait(0) status. The
actual reaper owns/closes the signaled Event; the fixture does not close it twice.
Its pending writer pointer and owner lease are real119 Session objects. A fresh
controlled clock starts at the actual monotonic tick so the unchanged reaper's
original absolute deadline remains meaningful; no clock leaf in the reaper is
rewritten and no new deadline is manufactured by a gate.

The original38 physical scenarios and original assertion bodies are retained:
reserve-before-driver, lost ACK/debt, unknown baseline/proof, source/Host/context
retirement, carried debt, exact readback, atomic replacement failure/retry, and
multi-path scope. Four scoped additions cover:

- an independent actual writer Session holding `.owner`: Before issues zero hide,
  and After preserves exact physical debt until the owner is released;
- a real persisted active old writer with child Alive/Unknown or job Active/Unknown
  keeps the physical gate closed and preserves the writer revision;
- exact old parent+child Dead and named job Idle drive actual clearSettled through
  real119 commit, then continue the original physical Before/After;
- same-native pending root initially unproven retains its real writer lease; once
  its explicit Event model is signaled, the exact main reaper settles and releases
  that lease before real quiescent reopens it. This detects held-lease self-lock.

The original three compiled semantic negatives remain: bypass BeforeHide, ignore
ancestry proof, consume without valid readback. They must hit their original case
assertions and fail normally, not fail compilation, crash or count setup errors.
Release and Checked retain the original captured100 compiler flags and libraries;
actual source/generated/compiler/link inputs, imports and binary hashes are bound.
Static imports are limited to kernel32.dll; this is not runtime module tracing.

```
node tools/virtual_physical_return_runtime_selftest.mjs --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY
```

`--out` must be fresh, absolute and outside product/toolchain roots. Evidence124
uses the existing `production-link100` toolchain and writes only its new evidence
root. Initial Apply, Reconnect, real-process CLI/Job lifetime tests, full package,
foreign-process cold recovery, real hardware and full lifecycle acceptance remain
with the coordinator and other explicitly assigned helpers.

Part A lives in the existing runtime source preparer: exactly six allowlisted
bridges yield seven raw contributors including main. The full-TU runner adds the
actual reaper as core55. Its five new CLI environment leaves always increment
forbiddenIo and throw, since the ordinary100 suite must remain on its explicit
old CLI boundary. Source-prep unit tests retain all existing mutation checks and
verify those throw guards plus unmodified real bridge algorithms/data/Job includes.
They prepare source only and never compile or run the full100/pipe suite.