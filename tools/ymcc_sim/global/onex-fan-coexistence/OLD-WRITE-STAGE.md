# Old-write exact prepare stage

Independent prepare-only entry: `prepare-old-write-stage.mjs`. It imports only
Node filesystem/path/crypto/url and its own pure metadata-audit module. No
child_process, compiler invocation, Assembly.Load, MTA, producer, Host, HC or SDK
code is executed by this entry. Existing first-old files, run assertions, adapter
and global CLI registration are not changed.

## Exact entry

```powershell
node tools/ymcc_sim/global/onex-fan-coexistence/prepare-old-write-stage.mjs --mode prepare-only --inputs <absolute-OLD-WRITE-PREPARE-INPUTS.json> --sha256 <exact-input-SHA256> --out-dir <fresh-absolute-owned-root>
```

The input schema is schemaVersion:1, kind:onex-old-write-prepare, mode:prepare-only,
with an exact `gate` and absolute `{path,sha256}` pins for baseInvocation,
hostProvenance, frameworkMetadata, dotnet, sourcePins[] and rawReports[]. This
candidate's actual input is the delivery-root OLD-WRITE-PREPARE-INPUTS.json,
SHA `078591d3a39dac8d18667bbbbeba5515be336afb33c79649bfe7cee8215f4398`.
The old-write gate stays mode old-write / 6 cases / 9 goldenWrites / native0 /
45000ms / DEFECT_REPRODUCED_IN_MEMORY. These are preserved expectations, not new
business results. The entry has no run mode and never installs an execution plan.

The prepared base is the existing exact first-old invocation with its original
37-output compiler inventory. Three current actual G entry sources
(OldWriteProbe.cs, OldWriteLoadContexts.cs, Harness.cs) must equal their pinned
immutable compiler source-view copies. All 37 copied outputs must retain their
original SHA; managed PE identities are carried from that pinned compiler
metadata into a new owned-output-manifest. No recompilation is needed or claimed.
Pins, source-view copies, original policies and old deliveries remain unchanged.

Only explicit source/PE/metadata reads and new owned output writes occur. A
missing/wrong pin, symbolic redirection, duplicate conflicting pin, changed source,
unknown context request, changed 6/9/45s gate, raw HC runtime pin or an existing
output directory is refused. The owned root must stay outside the bound G/B51
source trees. The original 3072 bound is retained for the finite pinned scope.

## Prepared policy, argv and outputs

The candidate copies existing compiled tool bytes under a fresh owned-stage.
Default stays at 44 exact full-identity/path/SHA pins and the same request policy.
Its custom context is exactly:

`OldWriteProbe.Context|<uppercase absolute Host-a678 directory>`

with registration type `OldWriteProbe+Context` and the exact copied producer
registration assembly. Its only target pins are the B51 a678 Host and its actual
6d2bcbd4... Backend. The old first-old Host/Light context is replaced; all other
Default policy, original deps and framework pins are retained. Unknown-name
fallback is not added. The only runtime HandheldCompanion remains the exact
synthetic `6d7bff05991ee407538b4ef35e12092b21a155d15ab5ca60fe0eae21846391f6`;
raw HC `08a38660...` is a Cecil/metadata input only.

Generated files include old-write-policy.candidate.json, old-write-argv.json,
OLD-WRITE-PREPARED-PLAN.json, owned-output-manifest.json, INPUTS-VERIFIED.json,
OLD-WRITE-METADATA-ANALYSIS.json and SUMMARY.json. The plan contains both explicit
vectors below as metadata, never commands executed by this prepare entry:

- Business: `[exactMTA, 'run', policyPath, policySHA, freshLaunchReceipt,
  'old-write', exactHostA678, exactRawHc08a3, freshBusinessResult]`.
- Metadata-only: `[exactMTA, 'validate', policyPath, policySHA,
  freshMetadataValidateReceipt]`.

The corresponding existing dotnet.exe is separately pinned. Both vectors are
recomputed and checked against exact argument metadata. Business, launch and
metadata-validate receipts must still be absent when preparation finishes. Any
future execution must independently recheck freshness, the precise vector, policy
SHA, all compiled output/source pins and the original 45000ms/native0/6/9 gate.
prepare-only returns PREPARED_NOT_EXECUTED / passed:false / producerExecuted:false.
It is not execution approval and is not compatible with blindly passing the
new kind to the first-old runner.

## Finite raw metadata and branch proof

The delivery reuses parent old-write r1/r2 reports (31 roots and 89 selected direct
methods, 118 unique after overlap). Additional queries used only the previously
reviewed RawOldRouteMetadata.dll raw PE reader with denied external resolution.
It reads explicit selectors and records target0; it does not load target images.
A failed exact CloseEvents selector from the first query is preserved; corrected
selectors are in the separate r3b report. That failure is not a passing gate.

Final union: 301 distinct selected methods across exact Host/producer/synthetic
HC/Backend byte identities. No target module initializer was found. Analysis does
not claim every possible static constructor or whole-module call graph was read.

The fixed synthetic hierarchy is checked from actual metadata:
OneXPlayerX1AMD -> OneXPlayerX1 -> OneXAOKZOE -> IDevice -> System.Object.
The exact a678 MsiFanDeviceLifecycle.IsMsiFamily body is pinned by its instruction
listing SHA 82e5e05b...; it loops FullName/BaseType looking for ClawA1M. The exact
OpenHcDevice body SHA b8030f7b... branches at IL 66 to IL 161 when false, skipping
MSI ctor at 83 and Open at 111. Only with that complete witness does the algorithm
exclude those MSI instance bodies and derived frontier. A changed hierarchy or
predicate retains the frontier; it cannot silently prune by a simple device name.
MSI is not recursively queried.

Fixed reflected body bindings include actual Host Backend ctor/open/duty/profile/
temperature/close, inherited synthetic profile callback/SetFanControl/SetFanDuty,
OneX lifecycle ownership methods, exact synthetic OLS/EC terminals and the real
scripted FanProfile SetTemperature/GetFanSpeed. Missing fixed bindings: 0.
OneX lifecycle direct local MethodDef frontier: 0. Synthetic direct local frontier: 0.
These do not imply all virtual/reflection, exception or policy paths are proved.

## Concrete remaining terminal review

The finite analysis retains 56 conservative direct MethodDef frontier entries,
each with exact image/token/caller/IL offset. Many come from MSI curve or unrelated
branches read conservatively; they are not business blockers merely because they
exist, but are not accepted transitively. NEXT-BOUNDED-REVIEW.json separates the
already excluded MSI instance set from remaining callback/control/close roots.
No further full Host/native graph traversal is requested by this tool.

- Actual HWiNFOTemperatureMonitor.Dispose invokes Timer.Dispose and
  InvalidateCachedMapping, whose body can invoke UnmanagedMemoryAccessor.Dispose
  and MemoryMappedFile.Dispose. The actual fresh monitor ctor does not set these
  resources; old-write sets sampleReadTick and never starts the monitor. Parent
  must confirm that invariant through the selected synchronous branch. A non-null
  external resource requires an accounted inert leaf or refusal; this slice does
  not implement a new replacement or weaken the original assertions.
- Actual OneX CloseCore and CleanupTransport have no remaining direct local edge;
  their reflection calls target exact inherited EC release and owned synthetic
  OpenLibSys.Dispose bodies. The generic X1/OneXAOKZOE Close bodies intentionally
  throw and are not substitute Close implementations.
- The synchronized Host callback roots are read, but remaining helpers such as
  EnsureHcSessionReadyForControl, ResolveFanCommandChannels and close-manager
  observation need a finite actual-branch review before execution approval.
  A fixture-only root/body list is not terminal proof.

No business target or MTA validate was run. The complete new plan remains a
candidate for the parent's review and next exact metadata validation. It does not
approve fullHost, real devices/registry, installed behavior, all CLR/static/native
isolation or the parent workflow. Original 119 FAIL / 360000ms / 7+2+3 ports remain.

## Finite tests

`prepare-old-write-stage.test.mjs` accepts explicit --inputs, --sha256,
--prepared-dir and --evidence-dir pairs. It reads existing metadata only and runs
10 checks for exact positive plan/copies/context, the branch proof and its negative,
unchanged gate, exact argv/context, raw-HC refusal, unpinned request refusal,
run/wrong-SHA refusal and fresh-output refusal. Original 6 business cases and 9
goldens are not run or counted as new test cases. Repeated prototype preparations
are implementation receipts, not independent business successes.

## Reusable old-write execution stage (2026-10-21)

`old-write-branch-review.mjs` now closes the selected 19 branch/diagnostic helpers
against 336 exact selected methods. `old-write-stage.mjs` consumes an explicit
prepared invocation, its branch-review inputs and its matching Default preload
receipt. It runs the same original old-write producer through the exact MTA. Its
stage envelope is:

```json
{
  "schemaVersion": 1,
  "kind": "onex-old-write-stage",
  "invocation": { "path": "<absolute OLD-WRITE-PREPARED-PLAN.json>", "sha256": "<sha256>" },
  "branchInputs": { "path": "<absolute branch-input.json>", "sha256": "<sha256>" },
  "defaultPreload": { "path": "<absolute matching preload receipt>", "sha256": "<sha256>" }
}
```

The branch inputs must bind the same exact invocation pin. Prepare the invocation
with the existing prepare-only tool, run its MTA metadata validation and matching
Default44 preload, then invoke:

```powershell
node tools/ymcc_sim/global/onex-fan-coexistence/old-write-stage.mjs --mode run --stage <absolute-stage.json> --sha256 <stage-sha256> --evidence-dir <fresh-absolute-directory>
```

Use `--mode prepare-only` for the same finite preflight without launching the
producer. Both modes require unused business/launch output paths; an already
consumed invocation cannot be replayed over its original receipts. Branch review
performs pinned PE-report reads in Node and creates its own fresh evidence. It
does not invoke another target or initialize a hardware runtime.

Acceptance retains exactly 6 ordered defect cases, all 35 original assertions,
9 golden conversions, nativeAttempts=0, natural exit within the original 45000ms,
and exact loaded managed identity/path/SHA checks. Unknown loads, actual raw-HC
execution, missing checks, incorrect conversion, forced exit or a longer budget
fail. Input and loaded-file hashes are rechecked after the producer returns.

Primary actual execution through this reusable stage has passed those original
6/35/9 gates. The independent stage-receipt refusal tests contain 14 checks, and
the branch-review tests contain 10 checks. These are separate evidence sets, not
additional old-write business cases. Scope remains fixed a678 Host with the
reviewed synthetic OneX IO terminals. Current Host, full 123-case fan composition,
original 119-negative/7+2+3 ports and physical acceptance remain open.
