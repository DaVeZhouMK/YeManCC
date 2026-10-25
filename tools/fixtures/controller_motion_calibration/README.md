# Borrowed controller calibration transaction

Run offline from the repository root:

    node tools/controller_motion_calibration_selftest.mjs

The runner writes only a fresh child of ControllerMotion/LegionCalibration/Transaction,
compiles actual C++17 headers in Release and Checked, executes independent explicit
callback-order/value fixtures, and requires semantic (not compiler/crash) mutant
failures in both configurations. All actual compiler dependency inputs are hashed.
Previous successful and failed evidence directories are retained.

API is in native/controller_motion_calibration.h, namespace ymcc::controller_motion.
CalibrationOps is complete only with reset, setMode, getConfidence, getOffset and
setOffset; equality compares all five pointers. CalibrationTransaction borrows a
Session handle and its live Key and never acquires, destroys, loads or unloads it.
The transaction cannot be copied/moved and destruction invokes no callbacks.
Caller serialization, nonthrowing/non-reentrant callbacks and library lifetime are
required, just as for Session. Exact key/handle/callback arguments are the caller's
current live selection, not saved transaction metadata.

begin returns bool and records valid Key/handle/ops/finite baseline only; no callback.
Rebegin while active fails without altering it. Zero window and age are literal.
prepare returns bool granting one strictly increasing, nonzero, nonfuture, fresh
receipt. Only the first valid grant resets and sets Stillness|SensorFusion (3).
Caller runs Session.process exactly once after a grant, then calls observe with the
matching receipt. Every prepare/observe consumes any earlier authorization, even
if its receipt is invalid. A receipt used for a grant can never be granted again.
An accepted observe returns true even when confidence is below the success gate;
check candidate() separately. Invalid/unmatched observations do not poll confidence.
Confidence must be finite in [0,1]; strict float >0.99f reads a finite offset, with
weight int(confidence*10). Exactly 0.99f does not qualify. All-zero data is legal.
A candidate freezes further training while external persistence proceeds.

Persist the candidate externally, recheck the current live source, then finish:
true + candidate applies it and Manual (0), reports Succeeded and returns true;
true without candidate returns false and stays active; false restores baseline
and Manual, reports PersistFailed and returns false. Successful candidate remains
readable; failed/retired candidates clear. Caller must make the live final check;
finish acquires no clock. cancel returns void and restores baseline/Manual for a
matching live borrow (default outcome Aborted). Timeout (elapsed>=window), clock
regression, ProcessFailed and invalid confidence/offset also restore baseline.
Every source/key/handle/callback mismatch ends SourceRetired without touching any
old or replacement handle. Inactive methods have no effects and preserve outcome.
confidence() retains the last finite admitted value, initially zero.

reference/ includes official fixed commit 06c0b9544db2b1f39abf9cd3796225ccfb096103
SensorsManager.cs and GamepadMotion.cs with original bytes, sizes, SHA256 and Git
blobs. The runner rechecks the fixed official tree and static operation order.
This establishes HC's Reset -> Fusion -> Poll -> Offset -> Manual flow, not exact
HC transaction parity: HC polls for confidence==1 and applies offsets on timeout.
The user's >99% gate and YMCC source/receipt/time/finite/persistence rollback
policies intentionally define the new contract. Source evidence is not executed
C# or an independent physical confidence/convergence oracle.

The harness uses memory effects, including actual Session stationary processing.
No main code, Windows sensor path, request registry, JSON save implementation,
DLL/HID, physical device, sleep/PBT, Host or deployment is exercised.
