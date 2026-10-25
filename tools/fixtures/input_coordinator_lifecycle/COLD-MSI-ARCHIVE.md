# Archived MSI cold-return replay

`cold-msi-archive` extends the existing complete native translation-unit fixture.
It takes the original `target-return.json` from the 2026-10-22 08:01:06 G3E export,
with two SteamDeck responsibilities: two reserved physical hide paths and one
possibly existing target. It never rewrites that input into an empty ledger.
The old single-Elite `cold-archive` cases and their assertions remain present.

The actual `ColdEnvironment`, `coldExecution::run`, durable private `Session`,
original serial lifecycle queue, R3 deadline/finalization and production managed
HostSession/codec/pipe loop execute. Explicit terminal adapters supply process
liveness, PnP nodes, ancestry and HidHide configuration; neither the real SDK nor
system hardware is called. Native parent handle waits prove owned test-child exit.

The 14 scenarios cover saved SD/Elite/Edge intent, OFF, reserved keys already
visible, lost unhide ACK with successful readback, transient target/writer/census/
ancestry/visibility uncertainty, an old physical path still present, missing old
devnodes after reboot, and original-budget failure followed by an admitted OFF.
Read-only OS census nodes include a single-NUL empty property and unrelated opaque
colon/Unicode instance IDs. Actual offending field ID is still unknown; these
are deliberate counterexamples, not a claim to reconstruct the exact OS list.

Use the permanent `controller.cold-msi-archive` simulator adapter when available,
or invoke the original runner with explicit artifacts:

    node tools/input_coordinator_lifecycle_selftest.mjs --out ABS_NEW_DIR --managed-dll APPROVED_SDK_FREE_FIXTURE_DLL --cold-record ORIGINAL_FIELD_RECORD --suite cold-msi-archive

A second compiled run adds `--mutation rollback-cold-observed-id`. It changes only
the production census leaf back to the strict persisted-path alphabet. Raw
production sources remain pinned; the actual compiled generated header is
separately observed and hashed. The same original assertions must reject it at
`MSI opaque OS ID permits durable two-duty cleanup before saved ON creation`.
Compilation errors, missing results, source drift and leaked children cannot count
as detecting this fault. The current software expectation is 14 cases/545 checks
for production and 14 failed behavioral assertions in the rollback candidate.

Outputs are software replay evidence. Actual G3E driver/PnP state, Steam/game
consumption, forced reboot, repeated sleep and other families still need an
identified delivered build and ordinary field logs. Do not erase a live recovery
record or treat Unknown as Absent to obtain success.
