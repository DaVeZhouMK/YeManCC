# R117 OpenXInput assignment guard sidecar

This sidecar compiles and executes the complete actual `ymccOpenXInputAssignSlot`
wrapper from the saved baseline and the current production source in separate
C++ namespaces. It also extracts the complete actual readback helper, the three
OpenXInput error constants, and the candidate `OpenXInputAssignGuard` alias.
The extracted statements are inserted verbatim, with no body substitutions.

All availability checks, raw Set/Get/Refresh pointers, lifecycle logging, identity
reprobe requests, and guard contexts are deterministic inert fixture seams. The
fixture includes no Windows SDK headers and never loads a proxy DLL. It performs
no product, Host, Steam, device, service, or power operations. The outer physical
slot guard is outside this test's scope.

## Run and evidence

From `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC`:

```powershell
node tools/openxinput_assign_guard_r117_selftest.mjs
```

Optional arguments are `--source MAIN_CPP` (also `--current`),
`--baseline OLD_MAIN_CPP`, and `--out EVIDENCE_DIR`. Compilation uses MSVC
`vcvars64.bat`; set `YMCC_VCVARS64` to override its location. Only the fixture is
compiled, with `/W4 /WX`, in Release (`/O2 /DNDEBUG /MT`) and Checked
(`/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd`).

The actual saved baseline is:

`G:/YeManCC-Work/Build/Validation/VirtualInput-PhysicalGuard-R117-20261016/before/native/main.cpp`

The default evidence root is:

`G:/YeManCC-Work/Build/Validation/VirtualInput-PhysicalGuard-R117-20261016/assign-issue`

`LATEST.json` points to the completed run. Each run contains full byte snapshots,
individual verbatim extracts with line ranges and SHA-256 bindings, the generated
translation unit, the exact JSON dependency, build commands/logs, executables,
and the complete per-scenario observations and assertions. The runner checks that
all extracted declarations and functions remain unchanged after both builds;
unrelated edits in the shared source are recorded separately. It writes no
production files or existing tests.

Verified run:

`runs/2026-10-16T13-47-07-157Z-52636`

Release and Checked each passed **99 scenarios, 480 wrapper executions, 1498
assertions, 0 failed checks**. Their complete observable results are identical.
Both full source snapshots and all extracted statements remained unchanged
during this run. No assignment-wrapper boundary defect was found. The initial
fixture-only compile warning was a global context variable shadowing a name in
`json.hpp`; renaming that fixture variable resolved it without production edits.

## Scenario and execution accounting

The 99 scenarios comprise 63 legacy-equivalence cases and 36 guard-boundary
cases. A scenario is an input/expectation pair; each invocation mode or baseline
replay is a separate wrapper execution, not another scenario. Per build profile:

| Group | Scenarios | Executions | What is compared |
| --- | ---: | ---: | --- |
| Original three-argument baseline | 63 | 63 | Independent expected calls, results, logs, and reprobes |
| Candidate with default arguments, explicit four-argument null guard, and null guard plus nonnull context | 63 | 189 | Each of three modes matches the original baseline exactly |
| Candidate with an always-true guard, using explicit context and default null context | 61 | 122 | Both modes match the full baseline external trace; guard calls and their exact positions/context are checked separately |
| Candidate cancellation/observation boundaries, using both context modes | 36 | 72 | Exact return, accept, callback count, callback/write adjacency, and all remaining effects |
| Baseline replays for already-issued successful operations | 17 | 34 | One replay for each of the two guarded context modes; observation path must remain exactly the legacy path |
| **Total** | **99 distinct scenarios** | **480** | **1498 assertions** |

The two additional null-only equivalence cases start with revoked admission,
covering first-success and retry-success paths. A null callback must continue to
ignore that opaque fixture state. These two cases are excluded from the
always-true group because the same admission callback would correctly reject
that state. The 61 always-true cases hold admission true throughout execution.

External-trace equivalence compares every availability check, Set/Get/Refresh
argument and result, lifecycle event with its complete detail, reprobe mask and
reason, return value, and `accept`. Guarded full-trace checks additionally require
one successful callback immediately before each issued Set or Refresh, without
extra checks after an issued operation. Comparing the external trace does not
hide extra callbacks: their full trace and count are independently asserted.

Legacy paths cover slots 0 through 3; first and retry success; matching,
mismatching, unknown, failed, and unavailable readback; disconnected first Set;
missing Refresh; failed Refresh; and successful Refresh followed by successful,
unavailable, generic-failed, or disconnected retry. Invalid path/slot/proxy
preconditions retain their original early-return behavior. All Set calls retain
`powerDownOnChange = FALSE`.

## Cancellation and already-issued operations

A rejecting callback returns false and clears `accept`, including when `accept`
was initially true. Rejection before the first Set issues no Set; revocation
inside a failed first Set blocks Refresh; revocation inside a successful Refresh
keeps its original all-slots reprobe and blocks retry Set. Failed preconditions
invoke no callback. Failed/disconnected operations preserve their specified
terminal observation and reprobe behavior.

Revocation inside a successfully issued first or retry Set does not retroactively
change the proxy's return code. The wrapper follows its original readback, log,
and success-reprobe statements. Exactly one target-slot success reprobe is
requested for that successful Set. A preceding successful Refresh has its own
single all-slots reprobe, so a successful retry has two total requests with
different original purposes. This is also checked when readback disagrees,
fails, or loses proxy availability, and when admission changes during readback.

The wrapper's `true` return acknowledges the supplied Set-success result;
`accept` is true only when the actual readback helper reports the target slot.
A failed or mismatching readback still leaves `accept` false. These fixtures
preserve those original acknowledgement facts and do not establish physical
mapping success, convergence, or hardware acceptance.

## Owned files

- `tools/openxinput_assign_guard_r117_selftest.mjs`
- `tools/fixtures/openxinput_assign_guard_r117/fixture.cpp`
- `tools/fixtures/openxinput_assign_guard_r117/source-binding.mjs`
- `tools/fixtures/openxinput_assign_guard_r117/README.md`

After the verified run, only this README was added. No additional runs or scope
expansion are required for this handoff.
