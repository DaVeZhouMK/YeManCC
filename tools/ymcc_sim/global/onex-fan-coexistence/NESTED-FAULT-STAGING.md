# Nested proof readonly fault payload staging

The 11 existing proof modules now reuse the existing fault-payload-staging.mjs only at18 destinations subsequently written by the original Cecil instrumenter/mutator. This fixes copying a sealed read-only Host onto a writer output before image.Write. No helper, product code, mutator, assertion, failure ID, clock, timeout, CLI registration or entry argv changes.

Source scope: bootstrap-proof, constructor-proof, capability-acl-proof, factory-proof, validation-proof, sta-proof, msi-lifecycle-proof, public-factory-proof, msi-replay-proof, joined-factory-proof and lifecycle-proof. Their only edits are one helper import apiece and the exact classified staging calls. STAGING-DELTA.json pins before/after files and literal replacements. Tests reconstruct candidate bytes and reverse them to prove every other original byte, including CRLF, preserved. Changes to original budget/assertion statements fail that source-delta gate.

## Branch distinction

There are19 original directory-copy sites. Eighteen have a concrete downstream new Host writer, including first port instrumentation in positive mode. validation-proof.prepareGenuineValidation copies a real original Host for read/hash/deployment validation and keeps its original fs.cpSync. Two bootstrap Newtonsoft copies and the validation runtime/shared copy function also remain unchanged.

constructor-proof stages only mode===native; other constructor fault modes create HC outputs elsewhere and the positive keeps candidate. Existing generated positive Hosts used by capability ACL, joined/public factory, OneX/MSI lifecycle and replay remain the positive selected input; their per-fault staging stays under mode!==positive. Factory/validation/STA/bootstrap leaf payloads are fresh writer destinations even in positive mode. No blanket cpSync replacement was used.

BRANCH-TABLE.md gives every before/after line and branch. BRANCH-TABLE-with-writers.json additionally binds the actual C# method and image.Write line for each of the18 changed destinations; WRITER-SOURCE-PINS.json pins those10 unmodified C# source files. This source review is not an approval to execute their CLI, lifecycle, SDK or Host branches.

## Finite regression

nested-fault-staging.test.mjs imports only the existing helper, checked by exact SHA before import. It never imports or invokes the full proof modules, a managed tool or product. It reads each actual candidate staging expression, binds its path variables to owned small text files and executes that expression against the real helper. Filenames use YeManFanHost.dll solely because that is the helper's contract; bytes are explicit non-PE text and never passed to a CLR or Cecil reader.

The source files are actually read-only. The old full-copy followed by an own file write reproduces EACCES/EPERM and preserves its original bytes. All18 new staging expressions leave root Host absent, preserve other members (including a nested same-name resource), permit a fresh own text output and leave source bytes/modes unchanged. The genuine original-copy expression preserves the original Host for reading. Source/helper/parent-owned pins are checked afterward.

Actual final receipt: D:/Codex/2026-10-18/kai-s/work/global-fan-nested-fault-staging-readonly-20261021-r1/actual-readonly-r2/RESULT.json,23/23 finite checks. The first test attempt had a modeling error: MSI public predecessor baseHost was incorrectly bound to the source; the unchanged helper refused overlapping roots. That FAIL receipt remains under actual-readonly-r1. Only the final23 count; earlier passed checks are not added.

Invoke with the same fixed Node22 authority:

    node nested-fault-staging.test.mjs --manifest D:/Codex/2026-10-18/kai-s/work/global-fan-nested-fault-staging-readonly-20261021-r1/STAGING-DELTA.json --sha256 8dcc3fe1b1a41de9dbf21abea11f8cea0bf66d3c962bae8d5ce13793f3fcb596 --out-dir <fresh-absolute-owned-directory>

The manifest is the separately pinned before/after audit input. Future source versions need new before/after pins, not relaxed hash matching. The test supports only this finite staging/source-delta review and does not simulate business PASS.

## Integration boundaries

Deliverable write set is11 proof modules plus this document and the dedicated test. The existing helper is referenced, not copied or changed. run.mjs, adapter.mjs, the parent's four default-case/initialize files and all original C# writer sources are read-only inputs. PATCH-MANIFEST.json records actual G-before/after SHA256; nested-fault-staging.patch is exact and reversible.

No Host/SDK/HC/producer, mutation generation, original CLI consumer or full parent was executed. Original business failures, all case assertions/failure IDs, budgets and physical-port boundaries remain for parent execution. No fullHost or physical acceptance claim is made.
