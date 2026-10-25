# Controller calibration actual-main composition

Run node tools/legion_motion_calibration_consumer_selftest.mjs. Evidence is a fresh
child of ControllerMotion/LegionCalibration/Consumer. No product is launched.

The new harness reuses the existing consumer topology/globals/native effect
boundaries through a read-only include. Its 31 scenarios are not executed here;
the independently run old consumer entry point retains that regression. Both
entries compile the real current Advance path, not a handwritten replacement.
Evidence artifact folders and per-scenario JSON filenames are intentionally short
to stay within the native fixture process path limits; full scenario/mutant names
are retained in receipts and effects.
The existing extractor freezes actual main bodies and registry declarations,
including Request/Status/Store/WriteAtomic/Advance/Abort/RestoreTrust and helpers.
Session and CalibrationTransaction are the real production headers.

The new extractor additionally freezes exact UI/IPC/lifecycle source anchors. The
UI page/IPC callers are captured source-only. UI helper and event/ACK timing are
independently owned and are not executed here. Native result events are checked
directly for explicit saved/persisted/applied values, including failures.

platform_boundary.h replaces only external clocks/events/native callbacks and
Win32 file APIs. JSON Store, validation, file parsing, serialization, WriteAtomic
control flow, registry, transaction decisions and consumer remain actual main.
JSON IO is real and confined to fresh evidence-private paths. Simulated replace
does not prove real Windows atomicity or crash durability. Every native callback
rejects Windows handle and retired handles. The live path provider never runs.

Scenarios cover duplicate requests, missing/stale samples, pre-request receipt
training refusal, repeated receipt/timeout, source retirement, left/right saved
records and Windows/unrelated record preservation, file failure rollback,
request replacement ID visibility, log exception exit cleanup, missing callbacks
and failed create cancellation, and persisted offset/trusted restoration under
a new capture epoch without extra per-tick reads. Legacy or inconsistent trusted
metadata cannot claim a trusted status. The exact tests/check counts are in each
run summary, not inferred from the scenario plan.

Release and Checked retain explicit NDEBUG-safe checks and actual header hashes.
Semantic mutants must compile and return exit 1 with named failed witnesses in
both configurations. Compile errors, runner failures, exceptions/crashes and
timeouts do not count. All failed runs are retained. Whole onGamepad, IPC handlers,
shutdown and OS delivery remain source-only. No HID/DLL/registry/configuration,
real PBT, Host, threads, physical firmware or hardware acceptance is claimed.
