# controller.msi-late-hide-pending

This full-profile scenario invokes the existing complete native translation-unit consumers. It does not copy a production algorithm or lifecycle model.

Run it through the existing global entry:

```powershell
node tools/ymcc_sim/global/cli.mjs --mode run --profile full --test controller.msi-late-hide-pending --out-dir ABS_NEW_EVIDENCE_ROOT
```

The PowerShell front door is `tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests controller.msi-late-hide-pending -EvidenceDir ABS_NEW_EVIDENCE_ROOT`.
The direct wrapper accepts `node tools/ymcc_sim/global/controller-msi-late-hide-pending/run.mjs --out ABS_NEW_DIRECTORY [--compile-out ABS_NEW_DIRECTORY]`.
Use fresh directories. The global adapter places compiler output in the fresh run's `late-hide-tu/` sibling to keep MSVC include paths short; commands and bindings record the actual location. It performs six bounded compilations and is registered only for `full`.

## Executed suites

| Variant | Existing suite | Actual consumers | Required outcome |
| --- | --- | --- | --- |
| positive | `late-hide-current` | pending 19 / 519, publication 1 / 7, inflight 1 / 8, inflight-edges 13 / 33 and orphaned-inflight 1 / 3, sharing one compiled EXE | all five PASS, total 35 / 570 |
| rollback-both | `late-hide-pending` | pending 19 / 519 | exactly 6 designated assertion failures |
| rollback-cleanup-only | `late-hide-pending` | pending 19 / 519 | exactly 2 designated token0 assertion failures |
| rollback-publication-only | `late-hide-publication` | publication 1 / 7 | exactly 2 designated publication assertion failures |
| rollback-inflight-only | `late-hide-inflight` | inflight 1 / 8 | exactly 2 designated in-flight BUS reentry assertion failures |
| rollback-orphan-owner | `late-hide-orphaned-inflight` | orphaned owner 1 / 3 | exactly 2 designated owner-recovery assertion failures |

The wrapper calls `node tools/input_coordinator_lifecycle_selftest.mjs --out NEW --suite SUITE`, adding the existing `--product-before SAVED_ROOT` only for controls. The positive's existing `late-hide-current` suite runs all five modes using the same `CoordinatorFixture.exe`. Each control compiles separately. The existing harness must PASS its requested polarity and exit0; each compiled rollback consumer must FAIL/exit1 with the exact designated assertion names. An arbitrary compiler/runtime failure never counts as a successful negative.

The consumers execute actual production enqueue/pending/completion/worker and queue-publication bodies with the existing inert I/O/log/clock ports. Positive observations include real→empty→same-real with hide counts 1→1→1, retained completion and already-completed refusal; an empty token0 worker retaining the newly submitted token1 episode; and the two-thread publication boundary guarded until pending metadata is committed. The pending suite also retains its existing family/registration/armed, retry, source-generation and exit boundaries. The in-flight consumer calls the real outer BUS producer again at the actual worker exec-decision log endpoint, then drains the actual worker and requires token reuse, queue depth zero, one hide and one applied completion. The existing in-flight edges consumer checks queued owner reuse, failure retry, a new source generation, stranded pending without an owner, completion during admission, and the legacy family/route boundaries. No behavior is reproduced here.

The current pending contract is 19 scenarios / 519 checks after the ROG initial-identity and partial-hide execution repairs; inflight remains 1/8 and edges 13/33. The historical formal35/126 execution and five compiled controls are recorded at G:/YeManCC-Work/Build/Validation/VC-PREAMBLE-AND-GLOBAL-20261022/INTEGRATION-RESULT.json. Current full runs must use the updated 35/570 contract; the historical 35/126 remains a past execution, not current coverage. Missing or different counts fail rather than silently changing expectations.

## Current compiled rollbacks

All five controls are **current compiled rollbacks**, with `historicalOriginal:false`. They are not historical originals. The wrapper captures current main.cpp/header bytes into evidence, then applies only these unique transformations:

- `rollback-both`: set the existing pending default from true to false and remove the known worker empty-item cleanup if-clause.
- `rollback-cleanup-only`: remove only that cleanup if-clause; the pending default stays true.
- `rollback-publication-only`: replace the unique two-line `const bool msiOrderedPublication = ymcc::msi::latehide::buildEnabled && inputHostLateHideManagedMsiRouteType();` expression with `const bool msiOrderedPublication = false;`. The header stays byte-identical with default true and both existing worker guards remain intact.
- `rollback-inflight-only`: remove only the unique queue-lock if-block calling `inputHostLateHideReuseQueuedPublication`. The regex includes the complete known if-body, comments, coalesced assignment and early return; it refuses absent or non-unique matches. The helper definition remains, the header stays byte-identical with default true, and ordered publication plus both existing worker guards stay captured unchanged.

Each control keeps `product-before/native/main.cpp`, `native/msi_late_hide_pending.h`, a hash-constrained `MANIFEST.json` consumed by the existing suite, and `ORIGIN.json` containing source hashes and exact byte edits. Other product bytes are retained. No product file is modified.

The six pending failure names are:

```text
empty leftover invokes no second hide
empty leftover preserves the completed real episode
completed real episode enqueue is refused
same real episode is never hidden twice
empty-token0 worker preserves newly enqueued token1 pending
new token1 episode is consumed and completed exactly once
```

Cleanup-only requires exactly the last two. Publication-only requires the existing `expectedPublicationFailures`:

```text
accepted MSI late-hide work retains a drainable notification or completed receipt
accepted MSI notification executes without a second producer
```

Inflight-only requires the existing `expectedInflightFailures`:

```text
same in-flight MSI episode reuses the worker token without enqueueing another hide
one MSI source episode has exactly one actual successful hide after in-flight BUS reentry
```

## Evidence and acceptance limits

`RESULT.json` is the global result file. `SOURCE-BINDINGS.json` and `INPUTS-BEFORE.json` retain current source/tool hashes, each actual compiler source manifest, generated inputs, rollback manifests and execution receipts. Every compiled variant retains its executable SHA, build command/log, command/stdout/stderr and each mode's original fixture receipt. Source counts are read from actual manifests rather than fixed to a prior release.

The wrapper checks all five positive results separately (19/519, 1/7, 1/8 and 13/33), forbiddenIo=0, normalProductMainExecuted=false, realDeviceIo=false, full translation unit, one compiled candidate per invocation, unchanged original/saved/generated inputs and no source/tool drift across all five compilations. Missing evidence, unexpected assertions, compiler errors, timeouts or drift remain FAIL. Old failures are retained and never relabelled as new executions.

`toolchain-inputs.json` names the installed compiler/linker inputs. The global runner and wrapper hash those before/after. This is native test compilation; it constructs no controller SDK or managed Host.

No real application, driver, device, registry, service, Steam, sleep, full application build/export or package is executed. Hide counts are calls through the fixture's existing inert transaction endpoint. The token0, publication and in-flight cases are directed source boundaries. The independent 224432 field audit records two nonempty in-flight reentry/applied chains separately from the original empty-completion issue and publication-order race; this wrapper does not assign a common root cause or rewrite that field evidence. No new general queue behavior or legacy-family control is added here. R5 and R6 frozen delivery directories, evidence and packages remain untouched.


The current positive also runs `late-hide-orphaned-inflight` (1/3), expected total35/570 (updated full combination not executed by the bounded ROG repair). It injects a diagnostic exception in the actual worker, verifies that the lifecycle claim exits with retained pending, then requires the next actual BUS notification to resume and complete. Its current rollback removes exactly the actual-owner atomic check plus the MSI-only exceptional marker guard/discharge; the original normal marker cleanup remains. Exactly the two named orphan-recovery assertions must fail.

There are six compiler candidates: one positive followed by five controls. Control compilation uses two test-only child workers with separate output directories. The original870000ms total and <=360000ms child budgets and original900000ms global scenario limit remain. Output is bounded8MiB/child; spawn, timeout, output-limit or missing-receipt failures cannot pass. All child completions are awaited and the summary is restored to fixed variant order. The global runner retains its existing owned Job/descendant cleanup on terminal exit. This changes no product thread or CPU scheduling policy.


2026-10-22 post-R7 fixture extension: four actual-consumer successor handoffs add
4 scenarios / 16 assertions to inflight-edges (13/33), historical combined35/126 (current pending extension makes the expected total35/570). They cover
failed old Hide, an exception before Hide, replacement by two new source
generations with100 latest-source notifications, and a completion-log exception
after old success is committed. Every case publishes via actual BUS, drains the
actual serial worker, and asserts notification/pending ownership and eventual
completion without a fresh producer. Preemption uses the existing inert log
endpoint only. These are software interleavings, not new field attributions.
R7 frozen sources/package keep their original31/110 receipts. Current-source
positive and global/control executions must be reported separately; registration
of the extension does not itself mean the global matrix has been rerun.


2026-10-22 formal selected global execution: combined35/126 plus all5 current compiled rollback controls6/2/2/2/2 PASS. Evidence: G:/YeManCC-Work/Build/Validation/VC-PREAMBLE-AND-GLOBAL-20261022/INTEGRATION-RESULT.json and REPORT.zh-CN.md. Overall catalog remains incomplete; hardware and R7 field acceptance remain unproved. This documentation update follows the immutable executed snapshot; no runtime or test assertion was changed.

## 2026-11-03 ROG deferred-Start field counterexample

The original pending consumer parameterizes its existing compatibility rows; the canonical ID and 19 scenario names stay unchanged. It now executes 210 checks, adding the 114820 archive epochs 3/4 (SHA256 08205ba9d182c981789a1a0765d2d518dd4e8c5a332fcaee84e32406c20ed43d): source-pending initial skip, no journal/no wake hold, confirmed ROG source return, old gate refusal and actual same-Host worker hide. OFF/capture OFF, changed Host/run/persona/power, original deadline, release/exit, unlocked/absent source, cleared debt and revoked power refuse late writes. A failed actual hide retains the original bounded debt for the next confirmed event; at that historical stage, successful hide alone consumed it (superseded by the 131128 first-refresh completion below). Existing MSI/GPD compatibility and failures remain. No archived path is a fixture runtime dependency.

This directed consumer drives the real outer producer and worker after seeding the production deferred-debt state. It does not invoke full Start or prove the Start-to-arm call at runtime (`actualStartArmCovered=false`); static production wiring and compiled full TU are separately bound. No normal product Main, Host/SDK or hardware is run. The ROG repair runs only the pending mode; it does not claim new execution of the full five-mode/five-control wrapper.

The orphan-owner rollback suppresses only the MSI arm of the shared exception claim, preserving the new deferred-Start arm and normal discharge. The existing two expected orphan assertions and historical failures stay unchanged.

The pending extension also adds 48 transaction-stage predicate checks: after injected query/reservation delay, OFF/power/persona/original-deadline retirement must reject durable-context adoption, final pipe admission and the actual shared CLI fallback admission. These predicates are real production code; no real query, durable reservation, pipe or CLI is executed by this bounded stage test. The first 19/154 intermediate candidate and its OFF receipt expectation failure remain archived, not relabelled as the final 19/210 execution.

Eight further checks execute the actual callback used by the transport Runtime immediately before issue, after injected preparation-time invalidation. The request enables the existing transport guard for deferred HIDHIDE_HIDE/CLOAK/APP_REG only; SUBMIT_FRAME retains its existing frame checks and UNHIDE is outside the new scope. No transport algorithm/header or real WriteFile is exercised by these callback checks.


## 2026-11-03 131128: hide/readback succeeded but Steam still double

The same ROG and ROG-X rows now cover the new field counterexample SHA256 `dc22b8e3448eb7409699f6812a2e20f13e56b464c2d43d1f0f5c71a25d4b3e0b`, Native PID11164 epochs1/2. Configuration success and `applied` alone did not prove external single-controller visibility. No runtime dependency on this archive is added.

The original `late-hide-pending` consumer is extended, not duplicated: **19 original cases / 337 checks**, including 127 additions for deferred first-hide(false) -> one owned-root cycle, original deadline/request inheritance, success-ACK source loss, retained journal and ordinary reconnect no-cycle, failed/unknown ACK no-reissue, busy deferral, invalid ownership/root/identity, same-vendor other-product and external/unknown USB connector topology, pre-issue context retirement, and retired ACK completion. Only the existing hide seam and explicit cycle/topology OS leaves are inert; the actual product producer, worker, refresh helper and admission predicates execute. The internal USB topology algorithm and actual driver/consumer are not exercised by these OS leaf substitutes.

The bounded positive ran at `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-DoubleController-20261103-131128/lifecycle-r1/RESULT.json`: 19/337, zero failures or forbidden I/O, no source drift. This does not run the whole five-mode/rollback wrapper; **35/388 was that round's expected combination, NOT a new full execution claim**. Older 19/210 and 35/261 remain historical scope, not rewritten evidence. `actualStartArmCovered=false`; product Main, physical driver/Steam consumer and hardware remain unverified.


## 2026-11-03 151907: empty initial identity before the first target

The existing ROG/ROG-X cases now replay PID10440 epochs1/2: the selected pre-target slot dropped during native return before RawInput supplied its identity; expected/current strings remained empty, the strict reconfirm gate correctly refused, and locked-with-zero-mask prevented late admission. Both original SD requests expired; the later PS5 request readmitted and refreshed. These are not all successful starts, and the field does not prove the new candidate on hardware. Archive and version proof live at `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-InitialResidue-20261103-151907`.

The original consumer has **19 cases / 437 checks** (100 added, 50 per original ROG row). It executes the actual HELLO-only helper and original admission, then exercises the actual production Start/Stop callsite: no prior target, known identity or slot-map stale/reprobe can enter recovery. Full unique discovery, exact internal game source, query-time OFF/Host/power/deadline/topology/cache changes and external/unknown/multiple sources are covered through inert OS leaves. Normal reconfirm still refuses empty identity equality; no new cycle/timer or late target-source reselection is authorized.

Actual positive `lifecycle-r4/RESULT.json` is 19/437/0 with stable source and zero forbidden I/O. r1 was the smaller 19/433 helper-only consumer. r2/r3 abnormal exits are retained; the complete Start had reached the fixture's intentionally forbidden module mutex. The existing exact-runtime private settings/module mutex isolation was then enabled for the original `late-hide-pending`/`late-hide-current` selectors. No installed product namespace is admitted. The full five-mode combination remains **NOT_RUN**; that round's expected count was 35/488.

An evidence-only compiled negative disables only the new Start callsite, leaving the direct helper and all assertions unchanged: raw FAIL/exit1 with exactly four specified failures (ROG and ROG-X each miss pre-target readmission and hide-before-PREPARE). Its first launch without the original runtime environment was rejected and preserved separately; the same executable with the required owned `YEMAN_POWER_CONTROL_DIR` produced the exact four semantic failures. This is not a new permanent suite or a historical-package rerun.

This proves the new initial-identity Start insertion and fixture fault sensitivity. The older deferred-start Arm branch still has its earlier coverage limits. Real InputHost SDK/driver/topology/Steam consumer and hardware were not executed. Global double-controller fallback strengthening is separately assigned by the user to the fan2 collaborator; this repair is execution-layer only.


## ROG164155: complete the original partial hide once (2026-11-03)

Current bounded pending result: **19 original scenarios / 519 checks / zero failures**, forbiddenIo=0 and no tested-source drift, at `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-Stability-20261103-164155/execution-fix/lifecycle-r5/RESULT.json`. The original 437 scope and R1-R4 compile/runtime failures remain historical receipts.

The actual worker and new execution helper handle an explicitly partial original ROG HID/root hide, one shared-debt completion attempt, unchanged player request/Host/persona/power/absolute deadline, internal-root and ownership guards, private real CLI-writer Session/quiescence, corrupt-writer refusal, before/after configuration, query-time OFF/Host/power/identity/deadline/transport changes and no new target. Hide/discovery/driver endpoints remain modeled; the helper and worker are not replaced.

Only under the partial scenario, the original writer-record path and machine-binding environment leaves use the exact private case file and fake machine identity. Other fixtures retain their original forbidden/owned paths; writer algorithms are real. The retired fake-Host suspicion was corrected by refusal provenance: R4's forbidden calls were specifically the two not-yet-admitted CLI-writer environment leaves, not proof of production Host failure. An invalid private writer record must still refuse continuation.

The companion original physical-return integration executes actual Apply -> active Refresh and exact new source/target/config predicates: **39 cases / 475 checks / zero failures**, including five second-snapshot boundaries, source/target/context changes, existing Apply/return/cold duties. Its explicit deferred context is a modeled current/refused fact, not actual full Host/debt acceptance. Product Main, real sensor/driver/Steam and hardware are not exercised.

Current five-mode parent expectation is **35/570**, including the unchanged sibling modes; **the full parent/its five rollback controls were NOT_RUN in this bounded round**. Separate semantic controls are recorded in this round's evidence. No duplicate suite or default parent was added. The gyro stale-pair observation remains separate and is not accepted as fixed by these controller checks.
