# First-old independent stage executor

This entry consumes the existing prepared first-old invocation and executes only
its reviewed SDK-free MTA route. It does not generate/compile/relocate a policy or
change original run.mjs assertions, scenarios, global registration or adapters.
The two modes prepare-only and run are explicit. The separate rebind-review mode
only produces JSON after exact checks. No mode is the default.

## Stage-plan schema

All references are absolute { path, sha256 } pins. The complete envelope is:

```json
{
  "schemaVersion": 1,
  "kind": "onex-first-old-stage",
  "invocation": { "path": "<FIRST-OLD-ARGUMENT-BOUND-PLAN.json>", "sha256": "<SHA256>" },
  "review": { "path": "<ONE-INVOCATION-REVIEW.json>", "sha256": "<SHA256>" },
  "preloadStep": { "path": "<relocated-preload-step.json>", "sha256": "<SHA256>" },
  "preloadReceipt": { "path": "<relocated-default-preload-receipt.json>", "sha256": "<SHA256>" },
  "preloadProcess": { "path": "<execute-receipt.json>", "sha256": "<SHA256>" },
  "dotnet": { "path": "<exact dotnet.exe>", "sha256": "<SHA256>" },
  "frameworkMetadata": { "path": "<existing framework-metadata.json>", "sha256": "<SHA256>" }
}
```

The invocation supplies the exact launcher, producer, policy, five original
firstOldArgs, 10 dotnetArguments, owned root, two output paths and 37 existing
compile-output pins. The stage recomputes the vector and compares it byte for
byte with the bound argument metadata. Existing ScopedLoadAuthority remains
responsible for policy/metadata preparation, Attach and approved target loading
inside the original SDK-free MTA. This Node tool never calls an Assembly.Load API.

## Exact managed-load basis

Post-run matching uses full identity + exact path + SHA. It admits declared
scoped policy pins, the independently pinned reviewed MTA, five explicit CLR
identities from the pinned existing Default preload actualLoadedManagedAfter,
and only netstandard from the pinned pre-existing framework-metadata.rows.
The netstandard row must have identity netstandard Version 2.1.0.0, token
cc7b13ffcd2ddd51, exact SHA ce09cef0d758196f2467f320d840c03202453efe9fb1920dddd24ea8277bbbb3,
no entry point/module initializer, and its file must still match. No TPA-directory
or simple-name fallback exists. Metadata evidence is never borrowed from the
business result being assessed. Raw HC SHA 08a38660... is metadata-only; the sole
runtime HandheldCompanion must be the exact synthetic 6d7bff05... pin.
Declared custom-context ownership stays in the original policy. The launch
receipt does not record observed ALC IDs; this is not full CLR isolation proof.

## Concrete parent r3 entry

The supplied D PARENT-R3-STAGE-PLAN.json is already metadata-checked against the
parent r3 invocation db4a608d... and parent review de049b57.... Its SHA is:

f2c055890226e7d7c48d35d69d0f0bfb06f4c84258be8690a80b4f4d7b5f389d

After the parent applies these three new source files, a fresh preparation can
be inspected with the following command. This command was not run from G.

```powershell
& 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe' 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/ymcc_sim/global/onex-fan-coexistence/first-old-stage.mjs' --mode prepare-only --plan 'D:/Codex/2026-10-18/kai-s/work/global-fan-first-old-portable-executor-20261021-r1/PARENT-R3-STAGE-PLAN.json' --sha256 'f2c055890226e7d7c48d35d69d0f0bfb06f4c84258be8690a80b4f4d7b5f389d' --evidence-dir 'D:/Codex/2026-10-18/kai-s/work/global-fan-first-old-parent-r3-portable-prepare-20261021-r1'
```

For the parent's separately authorized single business validation, the same
entry accepts --mode run with that exact plan/SHA and a different fresh evidence
directory, e.g. global-fan-first-old-parent-r3-portable-run-20261021-r1. The runner
allows no command override. It uses exactly the pinned dotnetArguments, cwd,
45000ms timeout and 8MiB capture bound, scrubs the original CLR forwarding
environment prefixes, requires natural exit and preserves all original parent
business gates (8+ cases, 63+ checks, all passed, native0). It rehashes the full
input/compile-output scope before and after and pins new JSON outputs afterwards.
The launch/result filenames must both remain absent; consumed r2 is refused.
prepare-only returns PREPARED_NOT_EXECUTED / passed:false / producerExecuted:false.

## Exact r2-review to r3 rebind

Use a second envelope with exactly schemaVersion:1, kind:onex-first-old-review-rebind,
fromStage (pin to the old complete stage envelope), toInvocation (new prepared
invocation pin), preloadStep/preloadReceipt/preloadProcess/dotnet (new exact pins).
The old frameworkMetadata pin is inherited. An actual input is supplied at
D:/Codex/2026-10-18/kai-s/work/global-fan-first-old-portable-executor-20261021-r1/R2-TO-R3-REBIND-PLAN.json, SHA 462b8784971f4a640e862fcb2ca54a5292ba0c1e74e41d5e5e7363fa304f5491.

```powershell
& 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe' 'G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/tools/ymcc_sim/global/onex-fan-coexistence/first-old-stage.mjs' --mode rebind-review --plan 'D:/Codex/2026-10-18/kai-s/work/global-fan-first-old-portable-executor-20261021-r1/R2-TO-R3-REBIND-PLAN.json' --sha256 '462b8784971f4a640e862fcb2ca54a5292ba0c1e74e41d5e5e7363fa304f5491' --evidence-dir 'D:/Codex/2026-10-18/kai-s/work/global-fan-first-old-parent-r3-portable-rebind-20261021-r1'
```

Rebind permits only owned compile/entry/result/launch path relocation and the
corresponding exact argument-metadata policy pin. It requires identical output
set/SHA/original mappings, original source/inventory pins, fixed first four
business arguments, context IDs/types/requests and all other policy semantics.
It checks original review source/IL pins and original/copy bytes, emits
ONE-INVOCATION-REVIEW.json plus FIRST-OLD-STAGE-PLAN.json with a new review SHA,
then loads/verifies the complete new envelope. A policy/context/target/compiled
byte change is refused. Outputs remain REBOUND_PREPARED_NOT_EXECUTED, not a new
business PASS or a source/fullHost approval. Parent's own de049b57 review is also
supported directly in PARENT-R3-STAGE-PLAN.json; it has not been overwritten.

## This delivery's finite evidence

22 pure plan/recorded-result tests passed, including actual offline assessment of
recorded r2 business/launch/process: 8 cases, 63 checks, native0, all 33 managed
rows accepted with basis 26 policy + 1 MTA + 5 preload CLR + 1 framework metadata.
No business target was reexecuted. Original r2 outer FAIL remains unchanged.
Actual CLI metadata checks refused consumed r2 with exit3, rebound r2->r3 with
exit0, and prepared r3 with exit0/passed:false. R3 target outputs were still absent.
The independently supplied parent r3 review was also metadata-checked, target0.

Tests can be rerun against a separately pinned TEST-INPUTS.json using:
node first-old-stage.test.mjs --inputs <absolute-json> --sha256 <SHA> --evidence-dir <fresh-owned-dir>.
They do not invoke dotnet, a producer or any target.

Only this runner, its pure test module and this README are candidates. G/B51/B52,
old deliveries, original run/assertions/scenarios/CLI/adapter remain unwritten.
No new real business execution, Host startup, whole workflow, fullHost, hardware,
registry, OS identity or foreign process acceptance is claimed. The original
119 FAIL / 360000ms / 7+2+3 ports / 3072 gates remain.
