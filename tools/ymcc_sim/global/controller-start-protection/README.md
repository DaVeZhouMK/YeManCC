# Virtual controller launch protection

Uses the actual production class, 81 assertions in Release and Checked, real native threads/mutex and a supplied monotonic clock. Covers terminal+10 seconds, the total30 second ceiling, stale receipts, delayed consumers, all-player ON/OFF/persona locking and independent safety cancellation and 32,000 concurrent requests. Six compiled semantic negatives must trigger the original named assertions.

Global id: controller.start-protection. Full native IPC and complete coordinator are separately executed by tools/input_coordinator_lifecycle_selftest.mjs; the Vue consumer uses the existing controller.ui-navigation adapter. None proves physical input/driver return or sleep.

Existing operational recovery may observe/inherit an already admitted ticket without renewing it or opening the external admission gate. Explicit cancellation and persona changes never inherit. This differs from a fresh external request.

The single executable contract is native/virtual_start_protection.h, version VC-LIFECYCLE-20261020-R3. After the first candidate, execution inspects actively for 10 seconds; output can already be usable. Classified fallback is reserved only after inspection, at most once per original request. Mandatory fault cleanup is execution, never a fallback budget charge. All paths share the original 30 second ceiling. The code comment is reference text and has no runtime interpreter.

2026-10-22: executes the actual extracted cold-start observer against the production request authority in Release/Checked. Sources arriving after 15 seconds may continue inside the original 30 second request; expiration and cancellation remain authoritative. Three compiled behavioral negatives restore early freeze, renew the observer deadline, or revive cancellation and must fail their named assertions. Xbox full-screen hardware acceptance remains separate.
