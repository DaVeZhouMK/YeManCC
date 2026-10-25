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
| positive | `late-hide-current` | pending 19 / 75, publication 1 / 7, inflight 1 / 8, inflight-edges 13 / 33 and orphaned-inflight 1 / 3, sharing one compiled EXE | all five PASS, total 35 / 126 |
| rollback-both | `late-hide-pending` | pending 19 / 75 | exactly 6 designated assertion failures |
| rollback-cleanup-only | `late-hide-pending` | pending 19 / 75 | exactly 2 designated token0 assertion failures |
| rollback-publication-only | `late-hide-publication` | publication 1 / 7 | exactly 2 designated publication assertion failures |
| rollback-inflight-only | `late-hide-inflight` | inflight 1 / 8 | exactly 2 designated in-flight BUS reentry assertion failures |
| rollback-orphan-owner | `late-hide-orphaned-inflight` | orphaned owner 1 / 3 | exactly 2 designated owner-recovery assertion failures |

The wrapper calls `node tools/input_coordinator_lifecycle_selftest.mjs --out NEW --suite SUITE`, adding the existing `--product-before SAVED_ROOT` only for controls. The positive's existing `late-hide-current` suite runs all five modes using the same `CoordinatorFixture.exe`. Each control compiles separately. The existing harness must PASS its requested polarity and exit0; each compiled rollback consumer must FAIL/exit1 with the exact designated assertion names. An arbitrary compiler/runtime failure never counts as a successful negative.

The consumers execute actual production enqueue/pending/completion/worker and queue-publication bodies with the existing inert I/O/log/clock ports. Positive observations include real→empty→same-real with hide counts 1→1→1, retained completion and already-completed refusal; an empty token0 worker retaining the newly submitted token1 episode; and the two-thread publication boundary guarded until pending metadata is committed. The pending suite also retains its existing family/registration/armed, retry, source-generation and exit boundaries. The in-flight consumer calls the real outer BUS producer again at the actual worker exec-decision log endpoint, then drains the actual worker and requires token reuse, queue depth zero, one hide and one applied completion. The existing in-flight edges consumer checks queued owner reuse, failure retry, a new source generation, stranded pending without an owner, completion during admission, and the legacy family/route boundaries. No behavior is reproduced here.

The current counts are inflight1/8 and edges13/33; the formal35/126 execution and five compiled controls are recorded at G:/YeManCC-Work/Build/Validation/VC-PREAMBLE-AND-GLOBAL-20261022/INTEGRATION-RESULT.json. Future runs must keep the same explicit count and assertion contract. Missing or different counts fail rather than silently changing expectations.

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

The wrapper checks all five positive results separately (19/75, 1/7, 1/8 and 13/33), forbiddenIo=0, normalProductMainExecuted=false, realDeviceIo=false, full translation unit, one compiled candidate per invocation, unchanged original/saved/generated inputs and no source/tool drift across all five compilations. Missing evidence, unexpected assertions, compiler errors, timeouts or drift remain FAIL. Old failures are retained and never relabelled as new executions.

`toolchain-inputs.json` names the installed compiler/linker inputs. The global runner and wrapper hash those before/after. This is native test compilation; it constructs no controller SDK or managed Host.

No real application, driver, device, registry, service, Steam, sleep, full application build/export or package is executed. Hide counts are calls through the fixture's existing inert transaction endpoint. The token0, publication and in-flight cases are directed source boundaries. The independent 224432 field audit records two nonempty in-flight reentry/applied chains separately from the original empty-completion issue and publication-order race; this wrapper does not assign a common root cause or rewrite that field evidence. No new general queue behavior or legacy-family control is added here. R5 and R6 frozen delivery directories, evidence and packages remain untouched.


The current positive also runs `late-hide-orphaned-inflight` (1/3), total35/126. It injects a diagnostic exception in the actual worker, verifies that the lifecycle claim exits with retained pending, then requires the next actual BUS notification to resume and complete. Its current rollback removes exactly the actual-owner atomic check plus the MSI-only exceptional marker guard/discharge; the original normal marker cleanup remains. Exactly the two named orphan-recovery assertions must fail.

There are six compiler candidates: one positive followed by five controls. Control compilation uses two test-only child workers with separate output directories. The original870000ms total and <=360000ms child budgets and original900000ms global scenario limit remain. Output is bounded8MiB/child; spawn, timeout, output-limit or missing-receipt failures cannot pass. All child completions are awaited and the summary is restored to fixed variant order. The global runner retains its existing owned Job/descendant cleanup on terminal exit. This changes no product thread or CPU scheduling policy.


2026-10-22 post-R7 fixture extension: four actual-consumer successor handoffs add
4 scenarios / 16 assertions to inflight-edges (13/33), combined35/126. They cover
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
