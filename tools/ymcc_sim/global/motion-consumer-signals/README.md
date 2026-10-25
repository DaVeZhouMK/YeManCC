# Motion consumer regression in the existing global simulator

The `gyro.machine-motion-consumers` adapter starts the original `tools/hc_model_motion_consumer_selftest.mjs` with its complete mutation suite, then verifies the actual executable reports and every generated file. Run it through the existing owned PowerShell runner:

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Profile full -Tests gyro.machine-motion-consumers -EvidenceDir <fresh outside-repository directory>
```

The result requires Release and Checked positive executables, all original models and case grids, and the independently preserved 114,560 original consumer assertions in each configuration. The report keeps the original subtotal separate from 385 reviewed resource assertions, four default-transition additions, and the capability assertions.

All original 35 reviewed faults and four private-provider faults must be present exactly once. Each fault requires two successful compilations followed by actual executable assertion failures. The original per-fault native JSON must match its run log, binary hash, source identity, and outer summary. Compiler failures, missing setup, source rejection, and an unexecuted capability grid do not satisfy a semantic fault. Private-provider, generic and capability faults execute the full capability grid; the remaining original faults retain their original consumer control mode.

Source files and fixed external historical inputs are fingerprinted before and after execution. This includes all pinned upstream HC inputs, three native historical snapshots, PC80 identities, the frozen generic contract and source scope, the archived native source, the processor-owner preimage, and the fixed CurrentCatalog EFFECTIVE-HC-MATRICES.json and RESULT.json. Missing inputs stay visible as BLOCKED. Generated JSON, native logs, compiler logs, sources, objects, PDBs and executables form an owned-directory hash closure. Repository outputs, existing directories and reparse paths are rejected before evidence creation.

This compiled specialist has an 840-second producer deadline inside the existing 900-second owned-job deadline. The measured old 29-fault partial run took 534.95 seconds, and the three generic controls took 136.20 seconds; the complete suite is expected to take roughly 710–750 seconds. The ordinary JS adapter ceiling remains 60 seconds.

The executed scope is extracted production C++ consumers and memory effect/event sinks. Host Main, physical sensors, device control, the full BUS and actual Windows sleep are outside this acceptance scope. Source-derived G1–G6 observation fixtures retain their separate provenance and do not replace the complete suite.

Small report/closure rejection tests are available without running the native compiler:

```powershell
node --test tools/ymcc_sim/global/motion-consumer-signals/report.test.mjs
```

The original dual-configuration `capability-deletes-windows-consumer` report is 34,346,160 bytes. The specialist raw JSON reader therefore has a bounded 64 MiB allowance; it rejects larger files before parsing. The global outer result remains a compact hash/summary receipt under the existing global limit. Complete original assertion failures and raw logs are retained.

The fixed historical oracle contains 215 legacy input cases, while the original native consumer report counts 168 consumed legacy cases. These are checked independently. A cross-check against the real fixed oracle is part of the small test suite before the expensive native run.

The per-case producer paths use `m/r` so MSVC can read the original longest snapshot header. A real failed compile showed an existing 260-character header path rejected by MSVC while its 259-character neighbor was readable. The entry checks the longest unchanged fault/header combination against a 248-character bound before creating output. It rejects an overly deep requested evidence root instead of treating a compiler failure as semantic detection. The original producer, fault names, matrices and assertions are unchanged.

## Mandatory matrix binding supplement

The same `gyro.machine-motion-consumers` leaf now calls
`tools/hc_motion_matrix_binding_selftest.mjs` **after** the original producer has
passed its unchanged process, `validateMotionReport`, input closure and
`verifyMotionEvidence` guards. The original producer invocation still includes
`--mutation-checks`, all 340,224 positive assertions per configuration, the
separate 114,560 original consumer subtotal, and all 39 Release/Checked faults.
The existing unified-binding leaf remains present; no new global ID is added.

The matrix positive uses `--source <this run's current main.cpp> --generated
<owned m/r> --profiles Release,Checked --out <owned m/q/p/e>`. It really starts
the checker using the owned process runner. It reuses the verified generator
outputs and does not run the complete original suite a second time. The
checker and `matrix_binding_probe.cpp` are both explicit new source inputs.
The module's new guard/runner files and five existing tool manifest files are
also included in the source closure.

All four supplemental controls are mandatory. They start from the main bytes
captured before the original producer ran, and change one unique hunk inside
its complete `inputCaptureApplyHcMatrixCandidate` method:

| Control | Expected assertion | Native configuration |
| --- | --- | --- |
| `rollback-to-table-search` | `MATRIX_LOOKUP_REMOVAL` | Release |
| `live-reviewed-bound-removed` | `MATRIX_OUTPUT_EQUIVALENCE` | Release |
| `wrong-registry-kind` | `MATRIX_OUTPUT_EQUIVALENCE` | Release |
| `gyro-transform-skipped` | `MATRIX_OUTPUT_EQUIVALENCE` | Release |

The rollback's whole function must equal the fixture's independently frozen
pre-D14 function. Each control records the original/mutant function and full
source hashes, byte range, unchanged prefix/suffix hashes and reverse proof.
Original and final main copies, private mutant inputs, generated inputs,
checker process receipts, compiler dependencies, logs and binaries remain in
the fresh **D-drive owned case**. The checker generates each mutant's own
`e/generated` inputs; faults cannot receive the positive `--generated` path.

A positive requires both build and native exits 0, nonzero paired matrix and
window comparisons, both admitted and rejected windows, equal nonzero old/new
GamepadMotion effects, nonzero original lookup/comparison counts and zero new
matrix lookup/comparison counts. The full 17-state grid size is derived from
the actual original producer oracle. Each fault requires checker exit 0,
compiler exit 0, natural native exit 1 and the exact specified assertion.
Compiler errors, missing launches, signals, timeouts, truncated output, stale
source/generated hashes, different oracles or other assertions always fail.

`m/q/result.json` combines the positive two configurations and four controls.
The adapter requires this aggregate, `m/result.json`, the positive checker
result, and each of the four fault checker results as structured evidence.
The unchanged outer source hash check binds the complete generated closure.
No preexisting standalone result can be used to skip the new executions.

## Tool provenance and finite budget

The old motion artifact expression covered HC/historical inputs but did not
list compiler tools. This supplement explicitly appends the complete existing
574-member allowlist inherited through the marker-ownership, single-PID,
game-pause, non-user-wake and native-recovery **data manifests**. It executes
none of those leaves. All old HC/artifact entries are retained, including
CurrentCatalog EFFECTIVE-HC-MATRICES/RESULT. Required members and their pins
are checked before the original producer, and the original before/after
source/reference guard remains unchanged.

The full allowlist includes prerequisites; it is not a claim all 574 files
were loaded. Each configuration separately records the exact actual
`sourceDependencies` sequence and hashes. A compiler-authored repeated path
is kept with its repeated count and checked hash. Every row must be an exact
known repository input, independently reconstructed owned probe/header or
pinned tool member. Directory declarations do not authorize siblings.
Unknown files fail with their path and reported/declared digest in the
supplement failure. The actual compiled probe bytes are reconstructed from
the verified original generator and frozen fixture; the only allowed header
change is the checker's counted lookup instrumentation. Release does not
require a PDB that MSVC does not generate; Checked's actual PDB and every
other generated file remain hash-bound.

The original producer ceiling is still 840,000 ms and the adapter/job ceiling
is still 900,000 ms. The supplement declares its own 240,000 ms budget and a
60,000 ms child ceiling, but only uses time remaining inside the original
adapter ceiling, reserving 10,000 ms for finishing. Five checker calls and the
final validation must fit that remaining time. A short budget fails; no
assertion, original fault, configuration or old timeout is removed or raised.
The original complete suite and this supplement have not been executed by
the bounded integration worker; the parent must execute the complete leaf
once after its current R10 run.

Node guard tests run without C++ compilation:

```powershell
node --test tools/ymcc_sim/global/motion-consumer-signals/report.test.mjs tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.test.mjs
```

Fabricated JSON cases are explicitly named schema samples and only test
rejection contracts. The small actual Node subprocess probes test process
exit/timeout/output handling; they do not count as native matrix execution.
Read-only comparisons to a parent's historical standalone evidence also do
not create a new acceptance receipt.

The supplement measures logical lookup work and extracted consumer
equivalence. The complete BUS, whole-process CPU/power savings, hardware,
Host, installed application and actual operating-system sleep remain outside
its acceptance scope.
## Original producer trace and approved motion prerequisites (2026-10-20)

The historical M26 motion leaf passed its original positive/39-fault and matrix
scope. Its original 80 native configurations did not record compiler headers or
linker library paths. Those historical receipts cannot supply the new trace.
The current integration adds a mandatory verifier after the unchanged original
report/executable guards, before the matrix supplement. The parent alone will
run the new complete leaf; bounded Node tests do not establish native acceptance.

Every original Release/Checked compile now adds `/sourceDependencies` with its
exact owned file and `/link /verbose:lib`. The existing flags, native test command,
all 340224 positive assertions and 39 fault definitions remain. Each compile saves
a complete predeclared input list and source bytes, then records actual header
and library path occurrences, their order and hashes, complete raw compiler
stdout/stderr and natural compiler/native process receipts. Mutants use the same
producer and bind their trace references into their original source-bindings.
Compiler errors, launch failures, signals, timeouts, missing/truncated output and
unknown dependencies fail; they cannot become semantic-negative detections.
The public verifier requires all 80 original configurations.

`toolchain-additions.json` is this motion module's independent first layer. Its
schema1/sharedManifest/sha pin links the existing five data manifests, unchanged.
It adds only the eight reviewed MD/MDd library prerequisites from fixed
MSVC14.44.35207 and SDK10.0.26100.0. The loader enforces exact membership/hashes,
rejecting missing, duplicate, replaced or changed tail members. All 574 inherited
tools and 174 historical inputs remain: 582 tools and 756 artifact inputs total.
The current source closure is 132 files, explicitly including the new helper,
public trace verifier and local manifest. Both the collector and independent
verifier use that complete authority. Only actual compiler rows count as observed;
a predeclared library is not claimed to have been linked or loaded.

An unknown observed dependency leaves `compiler-trace.failure.json` with its path
and hash. Source snapshots share an owned content-addressed archive across the
original independent children, retaining the existing closure bound. The original
pool concurrency2 and producer840s/adapter900s/child180s budgets remain; the matrix
supplement240s/child60s budgets remain. No shared manifest or global limit changes.

Bounded contracts, including schema samples, real Node-only process ports, exact
manifest membership/tail rejection and collector/verifier582-tool agreement:

```powershell
node --test tools/ymcc_sim/global/motion-consumer-signals/report.test.mjs tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.test.mjs tools/ymcc_sim/global/motion-consumer-signals/original-compiler-trace.test.mjs tools/lib/hc_motion_mutation_pool.test.mjs
```

The full native trace, any further observed dependency gaps and performance costs
remain for the parent's unique formal gyro run. This trace does not establish an
OS module-load graph, full BUS, whole-process CPU/power, Host, sleep or physical
hardware acceptance. This work adds no product feature.
## Mandatory primary-owner supplement (D16)

`gyro.machine-motion-consumers` now also runs the unchanged
`tools/hc_motion_primary_owner_selftest.mjs`. Its three inputs were already
listed; the new `owner-runner.mjs`, `owner-evidence.mjs` and
`owner-child-trace.mjs` are explicitly added to the source closure. There is no
new global ID. The original producer remains mandatory: two 340,224-assertion
positives, all 39 dual-configuration faults, all 80 original compiler/native
trace configurations, and the matrix positive plus its four controls retain
their existing gates. The owner stage runs only after those gates pass.

The owner positive reuses this run's verified `m/r` generator outputs. It checks
95 recipe descriptors, 9,975 bound pairs and 159,600 sample windows, with
2,404,129 assertions in **each** of Release and Checked for the fixed 63/215
oracle. The grid is also checked against the actual oracle and all 35 states,
two six-axis samples and eight pair controls. Live Bound decisions, admission,
matrix identities, actual effect counts and all output axes must remain equal
to the frozen pre-D16 bodies. Original table-search loops are instrumented
without algorithm replacement. Reviewed/default/legacy searches must become
zero in the new path while the original counters and total-catalog reads show
that the real classification paths executed. These are logical work counts;
whole-process CPU/power savings remain unmeasured.

All five original D16 controls remain mandatory, **each in both configurations**:

| Fault | Required native failure |
| --- | --- |
| `trust-claimed-kind` | `REVIEWED_CLASS_CANNOT_FALL_BACK_TO_LEGACY` |
| `skip-default-dmi` | `LIVE_BOUND_EQUIVALENCE` |
| `wrong-reviewed-route` | `LIVE_BOUND_EQUIVALENCE` |
| `rollback-class-search` | `NO_REPEATED_CLASS_TABLE_SEARCH:` plus nonzero original lookup counters |
| `wrong-primary-priority` | `GENERATED_PRIMARY_OWNER_PRIORITY` |

Faults change exactly one owned main/header/catalog copy, retain an inverse
hash proof and use the unchanged original producer's `--generate-only` path.
That preparation is not semantic detection. The real owner checker must then
compile successfully and its actual native child must naturally exit 1 at the
specified assertion. Compiler failures, missing results, stale hashes, bad
exits, signals, timeouts, truncated output, missing configurations and omitted
or duplicate faults reject the leaf. The parent requires the owner aggregate
and verifies all five fault identities again before setting PASS.

The preload only records the unchanged checker's synchronous calls. For each
checker, `child-processes/{0,1,2,3}.json` records the actual Release compile/run
and Checked compile/run processes: PID/parent, command, original 60-second and
8-MiB limits, exit/signal/error, input before/after hash and exact stdout/stderr
hashes. The evidence validator reconstructs the exact original probe and all
four instrumented headers, binds actual compiler `sourceDependencies` rows to
the existing complete tool allowlist and owned/repository inputs, and matches
the native reports to the raw process streams. Declared tools remain distinct
from observed includes; no OS loaded-module graph is claimed.

The unchanged 840,000-ms original producer and 900,000-ms owned-job deadlines
remain. The owner supplement has a 240,000-ms ceiling and each generator or
checker call a 60,000-ms ceiling. It also uses only the time remaining in the
original adapter budget, reserving 10,000 ms for final validation. Short time
remaining fails instead of skipping a control or increasing the old deadline.

The public descriptor additionally requires:

- `m/o/result.json` — owner aggregate;
- `m/o/p/e/result.json` — unchanged checker positive;
- `m/o/f0/e/result.json` through `m/o/f4/e/result.json` — five unchanged checker
  fault reports, in the table order above.

The original `m/result.json` and all `m/q/...` result files remain required.
The main result adds `ownerEvidence` (path/hash/status, positive configurations,
4 semantic faults, 1 lookup regression, budget/failure), `ownerValidation`
(2 positive configurations, 5 groups, 10 negative configurations), the complete
owner file hash closure, and separate `primaryOwner*` summary counts. Original
`compiledSemanticFaults:39` and `originalCompilerTrace.configurationCount:80`
retain their original meanings. The source authority now declares 138 source
files and the unchanged 582 tool members / 756 external inputs.

Local guard contracts, without a full producer/global run:

```powershell
node --test tools/ymcc_sim/global/motion-consumer-signals/report.test.mjs tools/ymcc_sim/global/motion-consumer-signals/matrix-evidence.test.mjs tools/ymcc_sim/global/motion-consumer-signals/original-compiler-trace.test.mjs tools/ymcc_sim/global/motion-consumer-signals/owner-evidence.test.mjs
```

The local original-checker runs are owner-only offline evidence. They do not
approve the complete global leaf, a new fixed snapshot, physical hardware,
Host, full BUS or actual OS sleep. The formal producer/audit remains the
coordinator's responsibility after a new source capture.


## 2026-10-22 budget exhaustion in the complete motion leaf

The original two-lane current run completed all native producer work normally in812067ms, then failed with MOTION_TRACE_PARALLEL_BUDGET_EXHAUSTED before the mandatory matrix/owner stages. The public adapter now selects the producer's existing supported maximum of four independent mutation jobs; the read-only original trace verifier also uses four workers. This supersedes only the historical concurrency2 statement above. Every one of the39 original faults, both Release/Checked configurations, all80 trace checks, per-file hashing without a cache, and all matrix/primary-owner stages remain required. Producer840s, adapter900s, original child180s, and supplement240s/60s deadlines are unchanged. Prior failure receipts remain; the revised complete public leaf must actually pass before acceptance. No device or product runtime setting changes.
