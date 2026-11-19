# Generator input maintenance

These two libraries support explicit source-only maintenance. They add no test
ID, module minimum, export hook or release prerequisite.

`generator-module-closure.mjs` parses JavaScript with the pinned TypeScript AST.
Use `collectGeneratorModuleClosure`, then `generatorModuleClosureSnapshot` and
`recheckGeneratorModuleClosure` with its original in-process context. The graph
contains module pins, literal edges, parser identities and unresolved sites.
Static unresolved sites are diagnostic leads, not individual bugs or new test
requirements. Static module identity does not certify dynamic data, child
processes, compilation or the complete tool closure.

`generator-input-observation.mjs` exposes `observeGeneratorInputs`. Install it
before importing the selected offline generator. It records supported file reads
and metadata queries, confines its visible writes to a fresh owned output root,
and throws before child-process calls. Its caller must supply an exact compiler
request predicate. Awaited import reads are observed; leftover asynchronous
reads or file descriptors remain failures even if the observer can clean them.
This diagnostic is not a security sandbox or a general cache admission contract.

The original generator can emit source and an intermediate manifest before its
first compiler request. An intentional STOP at that boundary is not compiler
exit zero, a completed compiler-input graph, native execution or behavioral
acceptance. Preserve the original exception, output bytes and failed closing.

The first real observation found that the original capture verifier also scans
the moving primary tree for comparison. A concurrent primary AGENTS.md update
therefore entered the diagnostic input set. Do not globally exempt AGENTS.md,
weaken a source hash or rewrite the failed observation. The private comparison
scope candidate keeps full frozen capture verification and separates an explicit
captured-only compile authority from independent current-source comparison.
Default current comparison, Package and Full requirements remain separate.

Authoritative progress, raw receipts and private patches are indexed by
`Testing/YMCC/State/Read-CurrentStatus.ps1`; request the exact
`currentFixtureWork.generatorInputs20261117` section. Candidate source is not
imported into the original pinned preparer. Further integration must retain the
same original generator and batch-token identity at one coordinated source
boundary; do not duplicate producers or rerun the eight native builds merely to
replace provenance metadata.
