# HidHide CLI writer record and file session

This module records a single historical CLI writer attempt. It does not contain
an executable path, command, Job handle, process lookup, wait, device operation,
target origin, Host identity or permission for cold cleanup. Parent/child PID and
creation-time pairs are supplied by the caller; valid data never proves liveness.
The caller must prove the exact writer exited before calling `settle`.

## API

Namespace: `ymcc::hidhideCliWriter`.

- `Record { unsigned schemaVersion=1; string machineBinding; uint64_t revision=1; optional<Attempt> active; }`
- `Attempt { string attemptId; targetReturn::ProcessIdentity parent; optional<ProcessIdentity> child; string jobName; Stage stage=Stage::Reserved; }`
- `Stage::Reserved` requires no child; `Stage::MayRun` requires a valid child.
- `jobName` must equal `Local\YMCC.HidHide.Cli.` followed by the exact `attemptId`.
  IDs use `targetReturn::detail::token`, with a stricter 100-byte maximum.
- `validRecord`, `encode`, `decode` are pure; failed encode/decode preserve output.
  Wire keys are exactly `schemaVersion`, `machineBinding`, `revision`, `active`.
  Active attempts require `attemptId`, `parent`, `child`, `jobName`, `stage`.
  Both process objects require `pid` and `creationTime`. Wire stages are
  `reserved` and `may-run`; a childless writer is represented by JSON null.
- Duplicate keys (including escaped-equivalent names), unknown/missing fields,
  coercions, floats, nonpositive/overflow integers, invalid UTF-8/text, invalid
  process births, excess nesting, trailing data, BOM/NUL and >64KiB are rejected.
- `auto session = Session::open(explicitAbsolutePath, machineBinding);` uses
  guaranteed C++17+ factory elision. Session is neither copyable nor movable.
- `ready()`, `error()`, `record()` and `openedFrom()` expose the result. Failed
  opens have no record. Missing is distinct from Invalid/RetryableIo: Missing
  opens an in-memory revision-1 empty record without immediately writing it.
  A persisted `active:null` record is Valid, not Missing.
- `reserve(expectedRevision, reservedAttempt)` requires empty active and a
  childless Reserved attempt. Any already active reservation is rejected.
- `markMayRun(expectedRevision, attemptId, child)` binds the child once and
  preserves the exact original parent and job name. Same attempt + exact child
  replay at the current revision succeeds without writing or bumping revision.
  A different PID or birth cannot replace a bound child.
- `settle(expectedRevision, attemptId)` clears only the exact active attempt.
  An empty record or a different attempt cannot be settled. No process/Job test
  is performed. Each changing transaction increments revision exactly once.
- Stale revision is rejected even for an otherwise idempotent replay. Revision
  overflow rejects changing transactions; exact no-op replay can still succeed.

`load` and `store` are nonexclusive leaves. Session owns a same-directory `.owner`
regular, single-link, zero-byte sentinel via share=0 until destruction. Existing
owner bytes are never truncated/deleted/replaced. Reuse of
`targetReturn::fileDetail::{normalize,parents,info,sameInfo,Handle}` supplies path
and file identity checks without constructing any targetReturn Record/Origin.
Relative/UNC/ADS/device-style/unsafe parents, reparse files, hardlinks, directories,
unknown types and failed I/O are rejected. The caller controls the directory and
serializes all access to a Session; the file API is not an adversarial-directory
sandbox or an independent cross-process CAS without the owner session.

A candidate is validated/encoded before same-directory exclusive CREATE_NEW temp,
complete WriteFile, FlushFileBuffers, close and MoveFileEx(REPLACE_EXISTING |
WRITE_THROUGH). Only committed replacement publishes in memory. Any failed stage
keeps the old record/file and returns an explicit error. There is no hidden retry,
background thread, repair or permission to run the recorded job.

## Isolated evidence run

```
node tools/hidhide_cli_writer_record_selftest.mjs --out G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/cli-writer-record119/run01 --toolchain G:/YeManCC-Work/Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/production-link100
```

Both arguments must be explicit absolute paths. Output must be new and outside
product source and the captured toolchain. The runner consumes the sealed100
build-environment/toolchain manifests; it does not invoke vcvars or rediscover an
SDK. It freezes exact sources, builds Release and Checked, then a small set of
compiled semantic mutants. It binds actual compiler dependencies, linker-searched
libraries, contributing libraries, executable hashes and static DLL imports.

All successful I/O is in newly owned private directories. Fault wrappers intercept
selected file API leaves; they do not replace record/session algorithms. Tests
cover strict decoding, exact revisions/child binding, real hardlinks and sharing
failures, injected reparse/unknown metadata, full-write/flush/close/replace faults,
no-op writes, real independent native process exclusion, historical identity
import, and abrupt child exit immediately after zero-byte owner creation with
both existing and missing records. Only the fixture's own executable is spawned.
No normal product, Host, real CLI, driver, device, Job, service or AppData is used.
Static imports do not constitute runtime module tracing or hardware acceptance.