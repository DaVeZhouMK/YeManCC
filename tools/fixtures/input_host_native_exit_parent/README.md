# Current native exit local-owner regressions

Current execution lives under `Testing/YMCC/Evidence/VC-NativeExitOwned23`.
The original executed parent failure at `VC-NativeExitParent23/r4` is immutable
historical FAIL, including original source/fixture/tool/receipt snapshots. New
scoped success never promotes it or the complete virtual-controller lifecycle.

The parent runner compiles complete actual exitCleanupThreadProc, public Stop,
StopLocked, Clear, primary checked-close, actual pipe wrapper, complete
inputCaptureEnsureExitOwner, capture AwaitStop and predicate, actual current
input_host_exit_retirement.h and input_host_exit_owner_runtime.h. Early identity
and forceSpent globals are extracted from main, respecting production declaration
order. RetireStep, end Await and LocalGetter are the actual header, never a fake
getter. The actual pipe Runtime is compiled with the existing memory API's real
private event/original/duplicate owners. No real SDK/Host/device/driver/service,
Steam instance, sleep or deployment is executed.

Run with a fresh output and explicitly captured main identity:

```powershell
python tools/input_host_native_exit_parent_selftest.py --out G:/YeManCC-Work/Testing/YMCC/Evidence/VC-NativeExitOwned23/parent/next --expected-main-sha256 <sha256>
```

Release/Checked each run three fresh private executable instances:

- Healthy ordinary: explicit successful protocol shutdown releases the child's
  inherited event; actual process/primary exit and production closes are observed
  without force. The protocol is a labeled memory leaf, not managed Host proof.
- Unprotected uncooperative: protocol returns false and the real child remains
  alive through the actual 5000ms Stop wait. Current production performs one
  identity-checked force; WinAPI process and creation-thread signals/exit code
  prove destruction before actual product owner/pipe retirement and READY.
  No fixture kill/event-release backstop makes this case succeed.
- Protected primary/pipe: child is actually ended by production force, but a real
  protected primary Close and explicit Runtime original-handle Close fault remain.
  Observe worker still running at input-local-return, zero READY, actual getter
  false, same primary/pipe/tuple retained. Fixture removes only its own protection
  and Close fault. Same actual worker/Runtime closes the original owners before
  READY/Ensure return. This proves conditional progress, not permanent OS-fault
  destruction. The 30s observation is not awaited or reclassified as success.

Every scenario keeps independent process/primary observation duplicates, joins
both real kernel objects and records exit code. Product closes original process,
primary and pipe owners; Runtime snapshot and independent memory handle oracle
must agree. Only afterward does fixture checked-close observation duplicates and
its test event. Complete capture Stop/worker/resources, business target/physical/
durable return and non-controller features are explicit memory ports. Whole
wWinMain and WM_APP_EXIT_READY branch execution belongs to Sagan's separate
consumers and is not accepted by this runner.

Dedicated helper runner `tools/input_host_exit_retirement_selftest.py` directly
executes the actual header/private child for one real force (capped 2000ms), all
four missing authority gates, PID/creation/unknown identity, real WAIT_FAILED,
unexpected wait, accepted-force-without-exit, failed force, and real protected
process-close failure/retry. Later forceSpent calls have zero new wait budget.
Explicit non-exit/force-failure ports are labeled; their fixture event cleanup is
not product success. Primary/pipe composition is covered by the full parent
scenarios, not inferred from this process-only helper.

All known private owners are joined/closed before test exit; this is not a claim
that every runtime resource or permanently blocked OS operation is destroyed.
Permanent OS Close/pending-IO remains UNKNOWN. Existing 34/95 tools, global and
native production remain read-only in this task.
