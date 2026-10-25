# Legion families: actual consumer / registration / selection composition

Run from the YeManCC source root after the main owner confirms stable sources:

    node tools/legion_motion_consumer_selftest.mjs

The default fresh evidence root is
Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/ConsumerTests.
Use --root consumer for the previous approved LegionConsumer/Tests root; an explicit
--out must be a fresh direct child of either selected approved root. No existing
source, protocol header, test outside this fixture, UI or deployment is changed.

extract_main.mjs copies complete exact main function definitions and structures,
with UTF-8 byte offsets, line ranges and SHA256; the complete main byte snapshot
retains its original line endings. The executed methods include actual PID checks,
four-model route/hardware/collection/identity gates, configured and selected sensor
index, revision-fenced side selection, stable physical identity, source retirement,
registration, raw report front door, publish/snapshot, final atomic predicate,
structured calibration, optional Session consumer and final issue/request guards.
The actual gamepadRegisterRawInput function is also executed to prove that the
original five generic registrations, keyboard gate, original error receipt and
return value remain independent of the separate vendor collection registration.

Real session, processor, lease, frame decision, source-window, message-clock and Legion logical
protocol headers are compiled read-only. The protocol is not reimplemented here.
family-topology.json independently pins the approved four exact model names,
VID/PID sets, rejected names/devices, strict settings input values and a small
set of raw front-door packets with hand-pinned axis numerators. This supplements
the independently owned protocol tests; it does not repeat their full matrix.

The harness controls ambient globals, clock and complete frame Current seam.
Calibration lookup counts actual path/open attempts against isolated JSON files.
GamepadMotion callbacks are memory effects with unique controller handles and an
independent forbidden Windows handle sentinel. RegisterRawInputDevices, GetLastError,
SetTimer and lifecycle logging are memory sinks. No Win32 input registration or
timer is actually issued. Calling the real existing registration function lets the
fixture check that its original timer invocation is preserved; it adds no timer.

The new cases cover:
- Four exact model/VID/PID/base-container identity topologies; cross-family VID/PID,
  wrong usage, unknown/suffixed names and other platform families are refused.
- ConfiguredSensorIndex accepts JSON integer 0/1 only for non-S Tablets. Null,
  bool, string, object, array, negative, 2, fractions (including 1.0) and huge values
  fall back to 0; Go S is always 0. Parsing does not mutate live index/revision/epoch.
- SelectSensorIndex returns whether the side changed: same-side newer revision
  updates revision but returns false without retirement. Invalid index or older
  revision is rejected. Real side change stores the index before epoch/revoke.
- Repeated Tablet/Tablet2 0->1->0 transitions require fresh epochs and separate
  structured calibration records; late wrong-side and old left tokens are rejected.
- Go S remains sensor 0 even with a residual Tablet-right atomic setting, cannot
  consume the Tablet hardware identity, and uses its own calibration/session.
- Final real issue/request guards reject a frame assembled before a side change;
  returning to the same side cannot revive it; full neutral release still works.
- Non-target registration makes zero Win calls. Target registration retires its
  lease before FFA0/1 INPUTSINK|DEVNOTIFY registration. Failure closes publish,
  snapshot and fast submission; recovery requires a new actual sample and epoch.
- Base five-item registration and vendor one-item registration remain independent
  across all four success/failure combinations, including the saved base error.
- Actual raw DecodeReport plus the real pure decoder flows into publication and
  consumption. One zero-ID normalization, actual lengths/capacity, Tablet left/right,
  Go S single sensor and model/usage rejection are checked with independent pins.

The earlier consumer cases remain: actual sample/calibration/handle separation,
repeat tick reuse, A-B-A, Ready/power/raw retirement, expiry/future/absent recovery,
create failure, six nonfinite axes, finite stationary zero-gradient quaternion,
player/world reads, neutral/non-Legion submission and revoke-before-unload.

Full RawInput capture, settings persistence/events, onGamepad, Host, real selected
setters, resolver and shutdown functions are source-only anchors. Resolver default
allowLegionMotionCollection=false, separate same-type cache namespace, shared-lock
invalidation, and unchanged non-motion hide/observe call scopes are static evidence;
no full Win32 resolver execution or cache concurrency is claimed. Real report
selection and bounds are also saved as source-only anchors. The getter RPC and UI
are outside the executed fixture scope.

Release and Checked compile real copied main bodies with actual header bytes,
then run explicit NDEBUG-safe checks. Finite semantic mutants must compile and fail
named witnesses in both builds; compile errors, exceptions, crashes and timeouts
cannot count as kills. Exact inputs, methods, anchors, headers, executable hashes,
JSON files, registration effects and callback traces are retained in fresh evidence.
Failed historical runs are preserved. Passing isolated composition does not prove
physical HID input, real PBT/OS sleep delivery, real locking/scheduling, complete
main/Host/Steam integration, physical endpoint support or hardware acceptance.

Queue-time and epoch-cutover extension:
- Execute the exact main legionMotionMessageReceiptTick wrapper using counted,
  controlled memory GetMessageTime/GetTickCount64 stubs. Non-target routes call
  neither clock; each exact Legion route calls each once, in production order.
- Exercise the actual production age window, equality, expiry, future/pre-boot
  refusal, optional tick zero and downstream zero refusal, signed low32 bits, wrap.
- Execute real revoke, side selection and registration cutover stores; verify
  current-epoch samples queued at or before cutover are refused; only strictly
  later times pass publish/snapshot/submission and final issue/request guards.
- A controlled cutover-only interleave keeps epoch/context/lease current so each
  timestamp predicate is independently witnessed. This does not simulate threads.
- Wrapper optional-to-Sample transfer is fixture orchestration. Whole WM_INPUT
  and physicalHidCaptureRawInput remain source-only: exact frozen bodies and
  ordered call/signature/null/timestamp checks. The actual normalization/decoder
  is still executed separately. No complete RawInput front-door run is claimed.
- Windows sentinel and canonical null-controller frame paths remain covered.
- New finite semantic mutants omit each of publish/snapshot/submission cutover
  predicates, bypass wrapper route gating or substitute processing time for queue
  time. They must compile and fail named witnesses in Release and Checked.

Message time is OS queue uptime, not physical sensor sample time. Full 32-bit-ring
aliases cannot be uniquely recovered; large-window future ambiguity and same-ms
cutover ordering remain helper limitations (equal queue ticks are refused; later ticks can resume). Tests use the
actual bounded production age window and do not establish real clock, PBT, HID,
firmware, scheduling or hardware guarantees. The stable helper is read-only.
