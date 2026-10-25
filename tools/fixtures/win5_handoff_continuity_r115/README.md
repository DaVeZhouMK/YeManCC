R115 Win5 actual-source continuity selftest
=========================================

This directory and tools/win5_handoff_continuity_r115_selftest.mjs are audit-only.
The runner extracts production code without rewriting it, builds an isolated C++
executable, and replaces OS/driver observations with inert process-local functions.
No Host, helper, device, service, Steam, or product is started. cl.exe and the fixture
executable are the only child processes started by the runner.

From G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC:

  node tools/win5_handoff_continuity_r115_selftest.mjs

Current repaired source returns exit 0 when all contracts pass. The original
audit snapshots still return exit 1 for their red contracts; the harness never
converts expected failures into a passing regression result.
Exit 2 indicates fixture/compile/infrastructure failure. Summary, raw results,
compiler output, frozen source, exact extracted slices and source bindings are
written by default under Build/Validation/VirtualInput-HelperLease-R115-20261016/win5-regression.
The original win5/ audit receipts are preserved.

Single-case reproduction (choose one):

  node tools/win5_handoff_continuity_r115_selftest.mjs --case red-unknown-occupant-guard --out G:/YeManCC-Work/Build/Validation/VirtualInput-HelperLease-R115-20261016/win5/reproduce-unknown
  node tools/win5_handoff_continuity_r115_selftest.mjs --case red-unknown-live-key-retains-old-admission --out G:/YeManCC-Work/Build/Validation/VirtualInput-HelperLease-R115-20261016/win5/reproduce-live-key
  node tools/win5_handoff_continuity_r115_selftest.mjs --case red-exit-prune-orphans-pending-plan --out G:/YeManCC-Work/Build/Validation/VirtualInput-HelperLease-R115-20261016/win5/reproduce-exit

Use --source MAIN_CPP to bind a saved actual-source snapshot. source-bindings.json
records the exact bytes, line ranges and hashes. Diagnostic-on/off builds must have
identical JSON results. Production bodies are never patched, and actual gate,
budget, queue, classifier, schedule, blocker, guard and full step are not mocked.

The small step() driver only removes the next actual queued handoff token, sets the
in-flight flag, advances a fake clock, and calls the real step. It is not a full
production worker integration test. Full worker/stop/restore/close/cancel bodies are
saved as static audit evidence and are explicitly marked executed=false.

Virtual device paths and source routes are synthetic ASCII identities. Assignment
and cycle leaves only modify integer slots and append JSON records. A successful
inert assignment is not a real driver acceptance, physical recovery, external
consumer receipt, native-helper validation, CPU measurement, or R114 acceptance.

The exit case proves an ownerless plan/mirror after actual queue pruning. It does
not prove delayed application exit, failed physical restoration or post-exit writes.
The source process will eventually discard the state when it terminates.

The only field archive is the historical Win5 2026-10-16 10:17:17 export. The runner
records its SHA256 and selected original log bytes; this archive has no evidence
that the three fixture boundary conditions occurred there. Known 045E skip is only
a control, with no proposal to bypass it or introduce rootcycle.

Parent integration adds the actual worker external-priority suppression slice, queued
exit cancellation consumed by the original serial step, live-key budget admission,
and query/accepted-result context invalidation. Final regression has 45 scenarios /
131 checks with diagnostic-on/off identical, including all three original red cases.
The full worker is still not executed; only its exact suppressed-dispatch block is.
