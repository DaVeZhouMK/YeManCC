# Automatic mode and manual transaction consumer proof

The new harness reuses the existing legion_motion_consumer controlled globals,
callback sinks, identity and private JSON/Win32 file boundary without editing it.
Only the new scenario array runs. Real main functions and registry/types are
extracted verbatim, and real Session/Owner/CalibrationTransaction/Lease headers
execute. The base g_realStickTest.autoCalibrate field is a std::atomic<bool>& bound to
g_controllerAutoCalibrationRequested, default true. Existing scenario assignments
publish to that exact atomic; there is no second flag or ordinary bool shadow.
Actual Controller steady/owner functions acquire-load this published preference.

Independent effect expectations:

- All four exact Legion models and the six valid model/side combinations run.
  Tablet sides are separate; SZ1/SZ2 use side 0. Windows sentinel is never used.
- Owner selects in Manual 0 with existing offset; automatic true requests mode 3,
  false requests 0. Repeated steady ticks do not reset calibration or rewrite an
  offset, reopen a calibration record, recreate a handle or persist any JSON.
  Session mode cache records callback-completed intent, not native readback.
- Manual prepare alone resets and selects mode 3. Automatic setting changes while
  busy cannot reconcile over the transaction. Duplicate receipts cannot train.
  Exact 0.99 confidence does not fetch/store offsets; finite >0.99 may persist,
  then commit offsets and return Manual 0. Windows/unrelated records survive.
- Success/persistence failure/timeout/request replacement return no motion on the
  terminal frame. Next fresh idle frame applies the current auto setting once.
  Withdrawal's !request steady branch and Abort cache invalidation are separate
  paths. Persistence failure restores baseline offsets. Abort cleanup can roll
  back a still-live borrowed owner before destruction; no dead handle callbacks,
  writes through an old key to a replacement, or Windows handle use are allowed.
- Retired power/raw/physical/epoch/side cannot process an old sample. A new epoch
  creates a new owner initialized according to the current setting. Native and
  read callback replacement rebuild the owner; a transaction cannot transfer its
  borrowed handle or callback authorization to that replacement. Required missing
  mode/process/read/manual callbacks fail closed, then a new epoch may recover.
- Steady checks source before mode reconciliation and again before ProcessMotion.
  A controlled nonthrowing, nonreentrant callback can model atomic source retirement
  between these checks; no production callback exception guarantee is invented.

No DLL/hardware/raw reader, Windows/Serial pipeline, live settings IPC, full Host,
virtual output, PBT or actual Win32 atomic replacement executes. Calibration JSON
reads/merge/serialization run on evidence-private files. Log exception policy is
unchanged and no added exception-safety claim is made. Product auto=true default
and internal atomic default=true are bound as static anchors. The existing
refresh publication is source-only: readBool(motion,"autoCalibrate",true) feeds a
local automaticCalibration, the unchanged Windows field assignment, then the
atomic release store. Both Controller steady and owner evidence acquire loads are
bound, and ordinary bool reads in these two Controller functions are rejected.
The complete settings refresh, UI/IPC execution and cross-thread stress are not
executed; this fixture does not claim a concurrency stress or full settings proof.

The additional lagging-request-flag scenario supplies HasRequest=true while the
locked registry snapshot is empty and Active is absent. In both steady modes it
processes repeated receipts and new receipts without repeated mode writes, resets,
offset writes or calibration file IO. It also checks that no registry/transaction
is created and the stale mirror remains untouched. Cache invalidation belongs
only to an actual snapshotted request or Active transaction, after the snapshot.
This is a controlled legal interleaving state, not execution of a concurrent
cancel thread or a claim that the registry itself has been repaired.
