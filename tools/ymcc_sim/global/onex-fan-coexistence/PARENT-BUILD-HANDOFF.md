# Original fan parent: owned build preparation and resume

This extends the existing `run.mjs` entry. It does not replace the original cases, proof functions, per-child budgets, exact policies or fresh execution mode.

`--prepare-parent-build-only` takes a SHA-bound request containing the repository root, existing reviewed compiler-toolchain request, exact Cecil and reviewed compiler audit target. It requires an absent evidence root outside the repository, writes an owner record, generates the original fixture, and actually builds Harness/STA/MTA at `<evidence>/test-build`, with source/real compiler input before/post checks. No producer or Host entry starts. `PARENT-BUILD-PREPARED.json` binds the owner, code, sources, tools, outputs, compiler records and compilation time.

`--prepared-parent-build <manifest> --prepared-parent-build-sha256 <SHA>` requires the same `--evidence-dir` and a complete current `--tool-load-plan` / SHA. Verification rejects changed code, sources, artifacts, compiler records, redirection, an unrelated root or an already claimed execution. A verified in-memory context is required before the one-time execution marker is created. Resume reuses the exact Cecil path pinned by preparation; an explicitly different path is refused before claiming even if its bytes match. Ordinary fresh mode still rejects existing directories.

Resume keeps the original three initial build calls and actually executes them again with the exact audited offline commands and owner/output paths. It verifies the compiler outputs again before any producer launch. Total active preparation time (including source checks and compilation) plus resume active time (including verification) shares the original360000ms limit; each original120000/45000/60000/90000 child limit remains an upper bound. Budget exhaustion or altered outputs cannot count as a negative-case detection.

The remaining two original tool builds, all old/candidate proofs, generation stages, faults and assertions still follow the original parent sequence. Current stage policies and typed qualifications are still required before their corresponding target calls. This protocol by itself does not make the five-project plan complete, authorize a new load closure, prove new compiled stages passed, or constitute fullFan360/global/sleep acceptance.

Desktop-only invocation recipe:

```powershell
node tools/ymcc_sim/global/onex-fan-coexistence/run.mjs --prepare-parent-build-only --parent-build-request <request.json> --parent-build-request-sha256 <SHA> --evidence-dir <fresh-owned-parent-root>
node tools/ymcc_sim/global/onex-fan-coexistence/run.mjs --prepared-parent-build <same-root/PARENT-BUILD-PREPARED.json> --prepared-parent-build-sha256 <SHA> --tool-load-plan <current-complete-plan.json> --tool-load-plan-sha256 <SHA> --evidence-dir <same-root>
```

The request schema is `schemaVersion:1`, `kind:ymcc-original-fan-parent-build-handoff`, `repoRoot`, plus exact `{path,sha256}` objects `toolchain`, `cecil`, `auditTargets`. The existing source-bound Host compiler request supplies the approved .NET10.0.400/10.0.11 compiler tool pins; arbitrary commands/build arguments/environment fields are rejected. The tool accepts no installed app/device/sleep permission and does not kill unrelated processes.

The new owner/refusal tests are source contract checks. The real prepare/replay processes and any business results must be recorded separately, never manufactured from these checks.

## R72: original Bootstrap and Joined paths are prepared too

Parent preparation now compiles the two later tools from the actual current projects, in addition to the original Harness/STA/MTA:

- BootstrapFixture: `<evidence>/original-bootstrap/build/bin/BootstrapFixture/release/YmccBootstrapFixture.dll`.
- JoinedFactoryProbe: `<evidence>/joined-factory/probe-build/bin/JoinedFactoryProbe/release/JoinedFactoryProbe.dll`.

The existing status string is retained for compatibility. `preparedToolCount:5` and `laterStages` explicitly describe the additional tools; the prepare return contains all five tool bindings. The original initial-three manifests/receipts and their exact six-project audit remain intact. The later stages record separate exact BootstrapFixture and JoinedFactoryProbe/ConstructorIoPorts/ScopedLoadAuthority audits, raw compiler process/command pins, original output paths, source pins and complete pre-execution stage inventories. A complete tool plan must bind the later entry/deps/runtimeconfig/helper members at these original proof paths as well.

Bootstrap generation calls the original `generateBootstrapFixture` against the actual fixed repository sources. The complete device source membership, template, original generator and supporting source files are pinned before extraction, after extraction and after compilation. Generated source/provenance and the actual compiler before/after closure are pinned. No old DLL or historical generated source substitutes for that compilation.

Only a claimed, verified in-memory parent context can open a prepared proof root. The stage marker must match that parent's exact owner/manifest, original root/build paths, pinned helper, compiler records and the complete prepared file/directory inventory. Unexpected existing files, changed sources, alias/junction/redirection, foreign owners and repeated opens are refused. Ordinary fresh proofs retain their original exclusive `fs.mkdirSync(root)` behavior. This is not a recursive-mkdir exception or a general way to adopt an old root.

The two original proof functions still regenerate/build using their original compiler statements, arguments and 120000ms limit. The parent recognizes those exact statements and replays the recorded offline compiler command at the same paths; there is no skip/old-receipt branch. A second replay or changed original argument vector is refused. Replay output identity is checked before target routing continues. All original business/proof bodies, faults, case assertions, 30000/45000/60000/90000 limits and source/typed/policy gates remain in place.

Initial and later preparation compilation share one active 360000ms allowance. Each preparation compiler is bounded by `min(120000, remaining parent budget)`. The prepared compile subtotal includes all five calls; active elapsed also includes generation/source/audit work. Resume still starts at execute entry and tightens every original child through the same remaining budget. A business PASS additionally requires both later original compiler calls to have replayed. Handoff idle time does not consume active budget.

`run.mjs` has only two context-passing additions (Bootstrap and Joined) and a successful-completion check for both later compiles. This patch grants no new load/typed/runtime authority. Exact later target invocation policies and all original software business assertions still require separate evidence. This delivery is an overlay only; it does not alter a live G checkout or the current FIRST OLD/source-count evidence.

## R72 final correction: source-before membership and explicit helper lineage

Parent calls the read-only `collectBootstrapSourcePins(repoRoot)` before writing its first SOURCE-BEFORE, includes all 95 inputs in `bootstrapSources` and the deduplicated `sources` union, and verifies the same membership/hashes before and after actual generation/compilation. The narrow r5 snapshot was tool211 plus Bootstrap95 with two overlaps (union304); it is historical evidence, not a new current source-count lock.

`parent-stage-authority.mjs` binds the reviewed authority source/project inputs, actual before/after Csc membership, existing managed comparator and metadata-reader SHA. It requires whole managed IL/EH/typed metadata/static-data equality, narrow PE native-entry/import/resource/relocation/debug-surface equality and exact portable-PDB bytes before copying. It does not claim raw whole-PE equality or business admission.

Only `joined-factory/probe-build/bin/JoinedFactoryProbe/release/ScopedLoadAuthority.{dll,pdb}` is normalized to the initial parent canonical. The Csc reference/primary output at `probe-build/bin/ScopedLoadAuthority/release/` remains original bytes. Original audit rows are never re-signed. Both prepare/replay save original consumer DLL/PDB, compiler primary DLL/PDB, raw before/after TSV, compile-items and actual Csc arguments. Their separate `authority-lineage/{prepare,replay}/LINEAGE.json` and COMPARISONS record original/saved/canonical/after pins and owner-relative paths. All comparisons finish before any consumer replacement. Artifact inventories include PDBs. Initial parent25 artifacts are not overwritten.

The original proof compiler calls still execute; audited offline replay uses `-t:Rebuild` and natural0 precedes compare/save/unify/output verification. In addition to two context arguments and the successful-completion check, run.mjs records actual child duration/result for replay finalization. The original proof bodies and parent business body remain unchanged after removing only those context arguments.

Bounded r5 validation used an explicitly external s canonical fixture (DLL8e55c3c960405a30fd41dfaf186677c8e01bf3ca14f1654c60619427fb729ca3, PDB3e1a61ce10046e6d459609754d369c4a95ebf2a216a649d3683d9a4f85d233f6). Its historical incremental Csc-args file is BOM-only and explicitly unavailable, never fabricated. New stage prepare/replay Csc-args are complete. Fresh integrated parent uses its own initial canonical and source-before. Dependency/evidence pins are in delivery SOURCE-CONTRACT.json. Actual two compiles, two original compiler replays and 11 contract tests do not constitute a full parent claim, FIRST OLD acceptance or fullFan360 result.
