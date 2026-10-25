# Process identity leaf (task 107)

The actual production header `native/virtual_target_return_process.h` uses the existing record `ProcessIdentity` (`pid`, canonical nonzero uint64 decimal `creationTime`). It is Windows-only, synchronous, and observes exactly one recorded process. It does not scan, retry, wait beyond a zero-time status read, schedule work, write records, terminate processes, or touch controllers, services or drivers. The coordinator owns record/session/runtime integration.

## Interface

Namespace: `ymcc::targetReturn::process`.

- `capture(HANDLE borrowedProcess, DWORD* error = nullptr) -> std::optional<ProcessIdentity>`. The caller retains the handle and must keep it open throughout the call. Queries actual PID and process creation FILETIME; rejects non-process handles, query failure, and zero birth. An existing process pseudo handle is valid. Success clears the optional error; failures preserve the Win32 error or use `ERROR_INVALID_DATA` for zero birth.
- `probeRecordedOwner(const ProcessIdentity&) -> Probe { State state; Reason reason; DWORD win32Error; }`. States are `Alive`, `Dead`, `Unknown`. Stable `stateName`/`reasonName` helpers support caller diagnostics.

A malformed recorded identity is Unknown without opening anything. The probe opens only the recorded PID with `PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE`, noninheritable, and closes its own handle on every return. It verifies both PID and actual birth before a nonblocking `WaitForSingleObject(..., 0)`:

| Observation | State | Reason |
| --- | --- | --- |
| Matching identity, timeout (still running) | Alive | matching-running |
| Matching identity, signaled (terminated) | Dead | matching-exited |
| Same PID, different nonzero birth | Dead for original recorded identity | pid-reused |
| Valid nonzero recorded PID, OpenProcess error 87 | Dead | pid-not-found |
| Access denied or any other open/query failure | Unknown | distinct failure reason |
| Zero returned birth, mismatched PID, failed/unexpected/abandoned wait | Unknown | distinct failure reason |

Alive may become stale immediately. Dead applies only to that PID/birth pair and grants no device, target, descendant-process, CLI-completion, or unhide authority. Parent/Host death says nothing about another owner. Successful capture alone is also not liveness proof. A persisted PID/birth must already have trustworthy machine/boot/origin context from its caller; this leaf does not authenticate records or replace session ownership.

## Direct regression

Use explicit absolute paths and a fresh private output directory outside source/toolchain directories:

```powershell
node G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/virtual_target_return_process_selftest.mjs --out ABS_FRESH_PRIVATE_DIRECTORY --toolchain ABS_CAPTURED_TOOLCHAIN_DIRECTORY
```

Task 107 reuses the captured environment at `Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/production-link100`. The tool never discovers a new compiler or overwrites prior evidence. It freezes the current process header, record header, JSON dependency, fixture, runner and this README; verifies tools against the captured manifest; compiles the actual frozen headers; binds compiler includes and verbose linker library inputs; inspects PE imports; then binds the executed binaries and verifies source/toolchain freshness after execution.

Release is `/O2 /MT /W4 /WX`; Checked is `/Od /RTC1 /MT /W4 /WX`. Checked is an MSVC runtime-check configuration, not an arithmetic-overflow sanitizer. Both run real private native child processes, a real child that outlives its private parent, capture of owned/borrowed and restricted handles, real event rejection, terminal identity, repeated real probes and actual OS handle counts. Children execute only this fixture and use private events with bounded safety timeouts. Fixture-only cleanup may terminate its own still-running children. No product, InputHost, Steam, service or driver is started or controlled.

PID reuse, open denial/not-found/other errors, query failures, zero/mismatched identity, and uncertain waits are explicitly labelled controlled Win32 leaf faults. They intercept only the compiled production leaf's API calls; normal calls and all private process management use real Win32. An actual synchronize-only borrowed handle also exercises a real access-denied capture. No controlled case claims OS PID reuse or real device evidence.

Five separately compiled semantic faults must fail original assertions: classify access denial as Dead; classify reused PID as Alive; classify exited process as Alive; classify failed wait as Dead; omit probe-handle close. These are negative controls, not alternate production implementations. The fixture reclaims leak-mutant handles after measuring the fault. Historical failures remain in their own fresh output directories. This regression does not integrate recovery or establish real hardware/sleep/installation acceptance.
