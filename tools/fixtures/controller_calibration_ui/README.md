# Controller calibration UI sequencing

    node tools/controller_calibration_ui_selftest.mjs

The current production SFC is read, frozen, parsed and compiled. The test then
extracts its exact calibration declarations/functions, real event registrations,
initial status query and whole unmount callback via TypeScript AST byte ranges.
Only TypeScript erasure is performed. Actual installed Vue refs/computed execute
in an isolated VM with deferred IPC, event delivery and controlled existing
watchdog callbacks. Expected states/orderings are explicit in cases.mjs, not
another implementation of the production request state machine.

reference/ preserves the exact original SFC bytes and capture metadata before
editing (SHA256 9225d9abe8558a8176afa20f16c6a1dbe99217e3ec36b8c4b30304564fad4be7).
The tool reproduces early result -> ACK -> busy, old result -> new request loses
busy, and no-ID terminal status -> active request loses busy using this snapshot.
Failed development fixture probes remain in the evidence root.

Controller events received during an invoke are buffered until an ACK establishes
the native requestId. Only matching Controller events are then applied. A local
invocation identity also rejects ACKs/watchdogs from a superseded Controller call.
Known active requests reject mismatched or absent-ID Controller terminal status,
including the initial delayed status query; idle no-ID RestoreTrust still works.
A completion cannot be reopened by a delayed request status, including a no-ID initial query sampled while calibration was active. Initial idle restore and terminal RestoreTrust states remain usable. The old
non-Controller result, saved/error, status, ACK and watchdog semantics are compared
to the exact original logic rather than changed by this patch.

Native uint64 IDs arrive as JSON numbers. Only positive safe integers can be
compared exactly in JavaScript; unsafe numeric ACK IDs fail closed. Exact decimal
uint64 strings are accepted without rounding (for a compatible host). No native
wire-format change or precision repair of an already rounded JSON number is
claimed. Live IDs are not inferred from completion order or unrelated status.

Four native event/ACK deliveries are tested in all 24 orderings, along with early
failure, replacement before/after ACK, old result/status, late initial status
query, duplicates and malformed IDs, old watchdog, locked and real unmount
callback, and Windows/SerialUSBIMU legacy comparisons. Semantic mutants must parse
and execute with assertion failures; runtime exceptions are not accepted as kills.

Fresh evidence is restricted to ControllerMotion/LegionCalibration/UI. Sources,
compiled snippets, AST byte offsets/hashes, SFC compiler and project typecheck
logs, mutation output and file inventory are retained. No new poll, delay or timer
is introduced; the one existing calibration watchdog is reused. Unrelated code,
template/style bytes and original CRLF are checked. No main/native/Txn/old tests
are modified.

This is isolated script/logic execution plus SFC compilation/typechecking, not a
browser render, WebView/native IPC run, physical sensor, file persistence, PBT,
sleep or deployment acceptance. Unrelated teardown functions are controlled no-op
stubs while the actual whole onBeforeUnmount callback executes.
