# Applied TDP runtime display consumer

`getTdpRuntimeDisplayAdapters()` registers `performance.applied-tdp-display` in the existing global environment. It executes the unchanged `tools/monitor_tdp_runtime_selftest.mjs`, including the original extracted public `setTdp` body and production runtime state, passive display feed, HUD model and packing consumers. Its 52 named checks comprise **49 behavioral checks and three explicitly static source contracts**. Import graphs and static counts do not prove every branch or full HC coverage.

The original script's fixed Build/Tasks output namespace is mapped only to each private evidence case. Reads use frozen reviewed sources and pinned compiler/runtime roots; writes stay in the private case. Network and unrelated process launches are denied. The original hardware, settings and terminal dependencies stay inert. The real shared Build directory, installed configuration, product Main and hardware bridge are not executed.

The observer captures esbuild's actual graph and output, requires no external compiled imports, and verifies that captured bytes equal the original script's executed bundle. A second build using the saved original options with observation disabled must produce identical bytes for both positive and fault cases; these rebuilds are not executed. Sources, toolchain and all actual evidence artifacts have hashes; node_modules compiler junctions are excluded from recursive artifact walking.

The isolated source fault removes only the finish owner/epoch fence in `tdpRuntimeState.ts`. It must naturally fail with the original AssertionError at original script line 55: `AC to DC to AC does not resurrect a matching-side old ACK`. A zero exit, timeout, compilation error, different first assertion frame or existing final report is rejected. Original assertions and production sources remain unchanged. The initial fixed-output recursion failure is preserved in the owner delivery; paths.cjs captures the native resolver and never uses path.relative from the overridden path.resolve.

Run the contract checks with the pinned Node executable:

```powershell
node --test tools/ymcc_sim/global/tdp-runtime-display-consumer/runner.test.mjs
node tools/ymcc_sim/global/tdp-runtime-display-consumer/run.mjs --evidence-dir D:/evidence/new-tdp-display
```

After coordinator import **and spread** registration, execute the original global entry and audit its original receipt. Standalone results, contract tests and artifact-integrity counterexamples do not replace global execution. Hardware/OEM physical readback, full native IPC, actual installation and full product acceptance remain separate. The CPU/GPU catalog is identification/source/declaration metadata only, distinguished by machine and CPU name/model. Existing TDP switching keeps its provider and consumers; no new HC CPU call or catalog takeover is permitted. GPU has no implemented consumer and remains unavailable.