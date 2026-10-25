# Private WinMM timer retirement probe

Two scheduling classes only, each with ordinary TIME_PERIODIC and the candidate
TIME_PERIODIC|TIME_KILL_SYNCHRONOUS. Each configuration runs two repetitions.
Actual timeSetEvent/timeKillEvent, Win32 events and a separate cancellation thread
execute in the private helper. The timer period is 8 ms; no timeBeginPeriod call,
YMCC function, hardware/DLL/Host, service, settings, real sleep or network executes.

Lock-scope correction: the actual main callback declares unique_lock at function
scope. A successful BUS try_lock owns the mutex through explicit SetEvent and
explicit tail diagnostics. The probe never releases a successful callback BUS
lock early. Tail tests make try_lock fail while the controller owns BUS, then the
controller releases its lock before cancellation/drain. Pre-BUS tests stop the
callback before try_lock and keep stop=false. Their BUS operation is a counter;
no actual retired owner or DLL is accessed.

Manual expectations:

- Busy-skip tail, legacy: positively observe successful cancellation return,
  completed BUS drain and mock retirement while callback remains gated with no
  user-event signal/completion. Only then release tail; observe its actual later
  SetEvent/diagnostic completion. Record that closing the production-style user
  event at the earlier point would overlap unfinished callback work; do not close
  it there and do not attempt invalid-handle reuse.
- Before BUS, legacy: positively observe cancel return, BUS drain and mock owner
  retirement before releasing callback. The actual later try_lock succeeds with
  stop still false, and lateBusAdmissions becomes one. This is an isolated source
  interleaving witness, not a physical fault or actual DLL-unload experiment.
- Both synchronous variants: a bounded early wait has no cancel return, followed
  by an unconditional controller gate release. Require actual callback completion,
  actual successful timeKillEvent return, and actual mock retirement in that order.
  The negative wait alone is never success. No late BUS admission is allowed.

Every gate wait has an upper bound. The main controller releases regardless of an
observation failure. The cancel thread must exit and join, kill must succeed, the
callback gate wait must have been released (not timed out), and callbacks must be
inactive. Final event closure holds a separate lifetime gate; static context keeps
any late dispatch from touching closed events. Exceptional bounded failure exits
the private process without closing handles a running callback could use; it is
never accepted as a passing observation. Windows owns internal WinMM threads;
all helper cancellation thread handles, timer registrations and events are retired.

The source receipt freezes the whole callback, WorkerFault and worker plus timer
creation, four kill sites, three kill/drain/Shutdown paths and event close. These
are source-only. WorkerFault has no direct stop store; indirect logger behavior is
not executed or proven. The successful-lock explicit tail remains protected by
BUS. There is no attribution to the supplied device logs, and no production fix
or candidate deployment is performed. This probe does not audit every producer
call site for lock ordering or callback-self-cancellation deadlock safety.


## 2026-10-22 actual production consumer

The original `winmm_probe.cpp` remains a private OS-leaf test; it does not execute production functions. `actual_worker_fixture.cpp.in`, driven by `tools/input_capture_worker_retirement_selftest.py`, additionally compiles the complete frozen current production callback, WorkerFault and worker with the actual Recorder. Only OS/init/shutdown/BUS/Host leaves are substituted. Its owned test threads always join before success; event storage remains alive to observe a stale signal safely without an invalid real handle access.

The current source contract requires periodic scheduling with synchronous cancellation and terminal stop publication at both outer worker catch entries before WorkerFault/cancellation. This does not alter the 8ms normal cadence. The fixture executes the current worker verbatim, then removes these exact changes in frozen negative sources. Six schedules per build combine callback-before-BUS / busy-skip-tail with normal / standard / nonstandard worker exit. The legacy source must reproduce the closed-event signal and must fail the current safety contract; removing only the fault-stop stores must fail the specified stop/late-BUS checks.

The real private WinMM probe and the actual-source memory consumer prove separate boundaries. Neither accepts real capture shutdown resources, hung-callback bounded destruction, ignored capture-thread wait results, cancellation failure, physical sleep or game consumption. These remain outside the measured observations and must not be fabricated as persistent lifecycle acceptance. See `native/virtual_controller_runtime.md` and the unique task/evidence record.
