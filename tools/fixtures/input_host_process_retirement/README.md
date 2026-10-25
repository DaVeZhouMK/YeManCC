# Checked process retirement: bounded local repair, parent still INCOMPLETE

Run from the mainline repository with a fresh evidence directory:

```powershell
python tools/input_host_process_retirement_selftest.py --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-HostRetire23/next
```

The current reviewed run is `VC-HostRetire23/r7/result.json`. It pins the parent
main to `daa89a5e9d4c6e849eaf773c209a217af90349f166ea1cd64ed37e5b75dcde10`.
Use `--expected-main-sha256 <sha>` to require an explicit parent identity.
The optional historical comparison requires both `--before-quarantine <file>`
and `--expected-before-sha256 <approved-sha>`; it never promotes that baseline.
The historical quarantine source is the parent's `main.before-quarantine.cpp`, SHA
`d7d3884307855dc7a12ad1296df4e4e37b3b69be55fabd1e8a2d8da807f5feca`.
Use `--before-start <file>` and `--expected-before-start-sha256 <approved-sha>`
together for the original Start negative comparison. The fixed original snapshot
is `VC-HostRetire23/start.before-checked-close.h`, SHA
`5541514afc0f5c0bbc0dedc3b49bf23ac6fbfebc2e98d164d587c712e9c24a57`.
The original r5 failures and r6 pre-quarantine source/receipt are retained.

The fixture compiles the full actual StopLocked, ClearLocked, Owned/Disabled
predicates, quarantine probe and lifecycle clear; it also executes exact Start
old-session and post-Stop admission fragments. Source snapshots, function
bindings, all consumed source hashes, MSVC/vcvars identity, SDK version, build
arguments, imports and individual results are retained. Release and Checked use
`/W4 /WX` with only unreferenced fixture leaves suppressed. The runner reuses
`tools/input_timer_evidence_adapter_selftest.py` for the declared compiler path,
clean compiler environment, process execution and forbidden import check.

Only private kernel child processes, threads and events execute. A private
child waits on its inherited release event and naturally exits; a private thread
can release it. Protected handles produce a real CloseHandle failure (Windows
reported error 6 here). A real process handle without SYNCHRONIZE produces
WAIT_FAILED/error 5. Explicit OS-memory ports inject ERROR_NOT_ENOUGH_MEMORY (8)
for wait and close. Protocol, MSI/PS, endpoint facts, visibility/physical return,
return-ledger and lifecycle clock/ticket dependencies are named memory ports.
There is no fake Host/SDK, driver/device/service operation, installation,
Steam, sleep, deployment or whole-product build/acceptance.

Each r7 profile has 34 successful test assertions. The original 23 assertions
are retained, including three observations of historical consumer failures.
Historical consumers remain `pass:false` in their own collection. Current Start
requires a positive exit observation and successful checked close before Clear.
WAIT_FAILED or unexpected waits retain the owner/tuple, revoke publication and
set the existing quarantine/start gates. A failed wake-held close does the same.
The existing quarantine collector can close the exact same process handle; the
wake/endpoint/physical gates still govern the later tuple clear. Repeated Start
while quarantined does not wait or close again. A successful wake-held close
still follows the original Clear/admission gates without physical restore.
There is no new retry, timer, wait budget, mode or thread.

The unconditional-null helper mutation rejects eight assertions per profile;
all six original required helper/Stop/quarantine negatives remain required.
The missing-quarantine Start mutation rejects seven assertions, including both
WaitFailed and Close failure. Both controls compile and exit nonzero. Real
protected-close and restricted-process wait failures, explicit memory failures,
unexpected wait states and pre-existing quarantine are covered.

`Receipt.exitConfirmed` and `Receipt.handleClosed` have separate meanings.
`retire(owner, 0)` waits once, attempts close only on WAIT_OBJECT_0, retains the
same owner after wait/close failure, and clears it only after successful close.
A null owner produces no exit proof and no close attempt. No receipt supplies a
whole-Host completion bool. Stop keeps its original wait/initial-startup-orphan
policy and existing physical/OEM return rules. Close failure sets the existing
quarantine/start gate after independent endpoint/visibility decisions. Clear
then retains the old tuple; actual parent quarantine can retry the same handle
and the existing lifecycle clear still requires independent return gates.

`inputHostOwnedCleanupProvenLocked` means **owned session cleanup incl physical
duty**. It preserves the MSI ReturnOwed, endpoint, durable history and visibility
conditions and includes the retained server-pipe and explicit
`inputHostPipeRetirementProvenLocked()` conditions. It does not inspect
saved player Enabled. Disabled remains `!Enabled && Owned`. These are business
completion predicates, not a local-handle-only or bounded app-exit await gate.
MSI physical/durable work may require existing observers whose exit admission is
already revoked. Parent must additionally integrate the transport owner's
pending retirement proof; null server pipe alone is not that proof.

Remaining: main pipe transfer/retirement and Clear pending/settled integration
(separate parent owner; current main still predates it); launch
`pi.hThread` close ownership (outside the repaired old-Host branch, unchanged); ProtocolSelfTest and native fixture teardown as
separate test consumers, with fake-event cleanup distinct from process exit.
Full Start, startup-orphan termination, full pipe transport, real Host/SDK,
physical/OEM return and the complete parent exit lifecycle are not accepted.
The old `inputhost_failed_exit_lifecycle_selftest.mjs` was inspected, not changed;
its historical incomplete result is not promoted by this focused fixture.


Primary-thread dependency maintenance (VC-PrimaryThread23): the runner now binds
actual process/primary/server HANDLE and run/epoch global declarations from main,
and compiles the complete actual primary checked-close and pipe-only admission
helpers before the existing Clear/Stop/Start/quarantine consumers. The primary
owner remains nullptr in every original scene; no primary Close/Wait/creation
case is added and no original expectation or count changes. This run only
maintains the original 34-case regression under the new production dependencies.
New primary positive/negative acceptance belongs to the separate parent fixture.
The process fixture's pipe-retired/protocol/physical facts remain explicit memory
ports; this does not execute the real transport Runtime. The owned-cleanup
composition consumer separately compiles the actual pipe Runtime and wrapper.
