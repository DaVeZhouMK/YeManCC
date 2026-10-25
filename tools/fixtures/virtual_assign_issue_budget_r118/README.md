# R118 integrated virtual assignment issue budget

This bounded sidecar compiles actual production callers together with the actual
complete OpenXInput assignment wrapper. There is no fake AssignSlot wrapper and
no copied assignment or budgeting algorithm.

The runner extracts verbatim:

- The complete `ymccOpenXInputVirtualSlotGuard` function and its attempt limit.
- The entire actual handoff `case 4` body, including its local callbacks and
  unissued waiting behavior. A fixture function supplies the original `now`
  local and invokes that body directly.
- The entire shared intent budget implementation, constants, and source-key
  functions.
- The complete assignment wrapper, callback alias, readback and device-path
  helpers, and error constants.
- Actual plan declarations, queue logging, plan clearing, admission gates,
  target blocker, identity classifier, connection masks, and physical refresh
  outcome/helper.

All extracted statements remain unchanged. Cached identities, coarse flags,
intent/power/topology values, clock, raw proxy availability/Set/Get/GetDevicePath/
Refresh pointers, lifecycle logs, identity reprobes, physical cycle leaf, and
requeue recorder are process-local deterministic fixture seams. The callback
uses its actual production logic. Raw proxy seams record the shared budget and
virtual attempt counter at call entry, exposing reservation timing directly.
There are no product, Host, device, Steam, driver, service, or power actions, no
full build, and no packaging. Full scheduler execution and broad concurrency
proof are outside this sidecar's scope.

## Run

From `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC`:

```powershell
node tools/virtual_assign_issue_budget_r118_selftest.mjs --mode baseline
node tools/virtual_assign_issue_budget_r118_selftest.mjs --mode candidate
```

Default mode is `candidate`. Optional paths are `--source MAIN_CPP`,
`--baseline OLD_MAIN_CPP`, and `--out EVIDENCE_DIR`. Baseline defaults to:

`G:/YeManCC-Work/Build/Validation/VirtualInput-VirtualIssue-R118-20261016/before/native/main.cpp`

Evidence defaults to:

`G:/YeManCC-Work/Build/Validation/VirtualInput-VirtualIssue-R118-20261016/integrated-issue`

Each invocation builds only its generated fixture with MSVC `/W4 /WX` in Release
(`/O2 /DNDEBUG /MT`) and Checked (`/Od /RTC1 /D_DEBUG /D_ITERATOR_DEBUG_LEVEL=2 /MTd`).
`YMCC_VCVARS64` can override the `vcvars64.bat` path. Exit 0 means all assertions
passed; exit 1 means behavioral assertions failed, including the deliberately
red old baseline; exit 2 means a compilation/infrastructure failure.

`candidate/LATEST.json` and `baseline/LATEST.json` locate the individual runs.
Root `LATEST.json` identifies the latest completed invocation, so a baseline run
can intentionally leave it red. `COMPARISON.json` records the final paired
comparison. Run directories contain byte snapshots, individual actual extracts,
line ranges and SHA-256 bindings, the generated translation unit, JSON header,
build commands and logs, executable hashes, and all case observations/checks.
The runner also verifies that the actual wrapper/helpers and budget
implementation are unchanged from the saved baseline, and that tested source
ranges and the fixture remain stable during verification.

## Final results

| Source | Profile | Scenarios | Caller executions | Assertions | Failed assertions | Failed scenarios |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Original baseline | Release | 81 | 85 | 1457 | 332 | 71 |
| Original baseline | Checked | 81 | 85 | 1457 | 332 | 71 |
| Final candidate | Release | 81 | 128 | 1545 | 0 | 0 |
| Final candidate | Checked | 81 | 128 | 1545 | 0 | 0 |

Final baseline run:

`baseline/runs/2026-10-16T14-14-02-668Z-52480`

Final current-source run, including the noexcept catch and fresh
`GetTickCount64()` deadline initialization:

`candidate/runs/2026-10-16T14-14-21-842Z-52260`

Both profiles produced identical complete results within each source version.
The final paired runs used the same fixture, extractor, runner, and 81 scenario
names. Candidate execution accounting is:

- 69 single-call matrix scenarios: 69 caller executions.
- Seven unissued-window/recovery scenarios: 51 caller executions, including 41
  observations through the fixed four-second window.
- Four rollback scenarios: four caller executions, each potentially issuing both
  the real virtual assignment wrapper and real physical compensation wrapper.
- One shared-budget scenario spanning both callers: four caller executions.

Thus 128 counts actual calls to the extracted VirtualSlotGuard or case4 body,
not proxy operations and not 128 distinct scenarios. The old baseline has fewer
executions/assertions because it never retains the new stage4 unissued wait;
those conditional recovery/poll loops stop rather than driving its old stage3
as though it were stage4.

The candidate full source remained unchanged during and after verification:

`643492f977e20df7b0f33dc649bd9aa43b41f4aa60528666cc6bfc3e4dea1f67`

The actual complete assignment wrapper is unchanged from the saved baseline:

`4b1c14dfa3d82afc73c21c0cd1a91e6259ffdfe7f320dc65af4c6103d9187582`

No production patch API incompatibility or integrated boundary defect was found.
A fixture-only baseline compile warning for an unused coarse-flag seam was
resolved by referencing that function; production statements were untouched.

## Verified contracts

Wrapper entry availability runs before any reservation or virtual attempt
increment. Initial proxy unavailability, wrapper-entry proxy unavailability,
and cached-context rejection before the first Set spend zero. Successful first
admission reserves once immediately before the first raw Set. Every subsequent
same-attempt Refresh and retry Set sees the same single reservation and the same
virtual attempt count. Failed issued operations still consume that reservation.

The matrix cancels in wrapper availability before first Set, inside a failed
first Set before Refresh, and inside successful Refresh before retry Set. It
uses actual exit/publication/power/topology/source/plan/intent checks. Cancellation
prevents later mutating calls, preserves spent budget, and creates no automatic
retry/rearm. Raw GetDevicePath and Get counts separately establish that callback
rechecks add no device queries. Original preflight target proof is retained.

Initial proxy unavailability tests the original preflight return/cancel path at
zero cost. The new waiting path first passes original target proof and then
reports proxy unavailability at wrapper entry while the request remains current.
It retains the same stage4 plan, requeues at 100ms, fixes the original four-second
deadline once, emits one wait receipt, and expires with an explicit unissued
reason. Forty requeues and 41 observations issue no writes and create neither
confirmation nor exhaustion. Temporary unavailability can recover on the next
scheduled attempt without losing automatic retry. Cancellation while waiting
clears immediately. Once the first operation is issued, the wait state clears;
a failed issued operation retains its original stage3 and 1500ms retry. Advancing
the clock beyond the old unissued deadline inside an issued Set does not block
its original Refresh/retry or create a new I/O timeout.

A successfully issued Set still follows the actual wrapper's original readback,
ACK log, and target identity reprobe even when admission changes inside Set or
readback. Successful Refresh retains its own all-slots reprobe. Mismatching or
failed readback keeps `accepted` false. Caller confirmation additionally requires
current admission and its original readback rules. Supplied proxy replies do
not establish physical success.

Both callers retain the original shared two-operation limit. A guard failure can
consume operation one and a handoff failure operation two; neither caller can
issue a third. Temporary unknown identity and recovery of the same key preserve
spent/exhausted state. Compensation continues to use the frozen physical source
and slot through the real wrapper, with no second shared reservation. Rollback
verification still requires its original ACK/readback/presence checks. Physical
rollback verification and the final virtual slot0 readback remain independent:
a failed physical compensation may coexist with a supplied final virtual slot0
readback, exactly as in the original policy.

`COMPARISON.json` also compares 27 unchanged positive, terminal failure, shared
limit, and rollback controls. Their full raw arguments/results, ACKs, reprobes,
caller logs, rollback outcome, and final budget/confirmation/plan state match.
Only the intentionally changed budget/attempt observations at wrapper entry are
removed for that legacy comparison. They remain explicitly asserted in the
integrated matrix and are present in all raw evidence.

## Owned files and handoff

Only these four repository files were added:

- `tools/virtual_assign_issue_budget_r118_selftest.mjs`
- `tools/fixtures/virtual_assign_issue_budget_r118/fixture.cpp`
- `tools/fixtures/virtual_assign_issue_budget_r118/source-binding.mjs`
- `tools/fixtures/virtual_assign_issue_budget_r118/README.md`

Production files, R117 files, and existing tests were not edited. After the final
paired runs and evidence comparison, only this README was written. Verification
is complete; no additional runs or scope expansion are pending.
