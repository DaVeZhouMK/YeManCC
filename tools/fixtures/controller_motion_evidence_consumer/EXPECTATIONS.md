# Manual expectations and test boundary

This fixture compiles seven unchanged production function bodies, the production
PowerLifecycle enum, and actual Lease inspect/current, source-age and queue-epoch
headers. It supplies only a prebound identity, controlled globals/clock, mutexes,
and a memory file writer. It does not execute discovery, Win32 persistence, IPC
registration, log ZIP packaging, a reader, a device or a GamepadMotion DLL.

The expected outcomes below are explicit assertions, not a second implementation
of the diagnostics algorithm:

- Four exact Legion device classes are implemented. Tablets expose indices 0/1;
  SZ1/SZ2 expose only 0. Suffix/unknown/ROG/MSI/GPD/Steam Deck are unsupported.
  Unsupported sampleObserved is null; it cannot claim Windows IMU signal absence.
- Every result is readOnly, has scope legion-controller-source-only, and reports
  physicalAcceptanceVerified=false. Host/game stay not-observed-by-this-query.
  All four production power phase names are checked for supported and unsupported
  models, along with the read-only current power generation/observation time.
- A zero stationary accepted sample is current when the exact live source is
  ready and fresh. No accepted sample, not ready, stop, unsubscribed, suspension
  or resumption never claims a current sample; historical observation is retained.
- Power/raw generation mismatch or zero, absent/different physical identity,
  capture epoch mismatch/zero and wrong/invalid sensor index reject currentness.
  The retained sample is historical; diagnostics do not consume or repair it.
- Future receipt is not current and has null age until it is in the past. Age
  1500 ms is accepted, 1501 ms is expired. A receipt at epochStart is ambiguous
  and rejected with queue-time-not-after-epoch; epochStart+1 is eligible.
  Repeated queries and failed snapshots leave the previous real Lease unchanged.
- Power/raw/identity+epoch/capture/side/epoch-start/ready/stop/subscription/phase
  retirement after the locked read rejects currentness. Identity replacement
  follows the caller invariant and advances captureEpoch. Clock regression and
  crossing the age bound during the query reject currentness too.
- Query/export do not change a subsequent Lease current or production snapshot.
  No settings APIs are linked into the controlled translation unit. The immutable
  configuration marker is a seam check, not execution of the full settings stack.
- Successful export returns only its newly saved path and restores GetLastError.
  False/throwing atomic writes and query/clock/timestamp/directory exceptions
  return an empty path, keep the old file from being presented as current, and
  restore GetLastError. Atomic persistence itself is a memory boundary here.

Semantic mutants must compile and finish the harness with a named assertion
failure; a crash, timeout or build error is not a kill. They target final context
stability, completion freshness, null signal scope, stale-path export, last-error
restoration, ambiguous equal-tick admission, and diagnostics consuming the Lease.

The extraction receipt separately binds IPC delegation, successful-current-export
selection/missing branch, and the manifest disclaimer as static source evidence.
It does not claim those complete handlers or a real log package were executed.
