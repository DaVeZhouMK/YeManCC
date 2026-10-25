# R119 recovery submit cost fixture

Production decision: **no-change**. This directory contains an inert C++ fixture template, not product code. The runner extracts the actual current functions and Submit statements into the evidence directory and compiles those bodies against the actual pure headers.

Run from `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC`:

```powershell
# Count-only, narrowly scoped known-invalid consumer equivalence check.
node tools/recovery_submit_cost_r119_selftest.mjs --ineligible-boundary-only

# Full normal-path count/timing reproduction (not required again for R119 freeze).
node tools/recovery_submit_cost_r119_selftest.mjs --rounds 7 --iterations 200000
```

All generated files stay under `G:/YeManCC-Work/Build/Validation/VirtualInput-RecoveryClosure-R119-20261016/recovery-submit-cost`. The runner uses the existing MSVC vcvars64 path or `YMCC_VCVARS64`. It starts only compiler tools and its own inert fixture executable. It does not start the product, InputHost, Steam, devices, services, a reader/producer, or a full build. Existing tests and production files are never rewritten.

Counted mode adds entry/expression counters and atomic/mutex probes which delegate to the original operations. Timing mode uses original adapter/header bodies with supplied snapshot/Host globals; caller `InputHostMutex` is modeled by an uncontended `std::mutex`. The caller lock timings are therefore fixture observations. Source, exact extracted ranges, dependency hashes, substitutions, compiler commands/logs, generated code, assembly and raw samples are retained per run.

Normal no-recovery live submissions have zero persona/string copies, zero allocations and zero additional health locks. Idle Observe plus request/accepted performs 35 atomic reads for either XInput route and 43 for the supplied MSI board/route, with one healthySince store. Request + accepted alone performs 31/39 reads. It is not valid to skip all idle accepted processing: T1 still uses its health timestamp. A negative control compiled from the actual function demonstrates that failure.

The only fixture candidate is:

```diff
-if (!source) return {};
+if (!source || !eligible) return {};
```

This is consumer equivalence for current known-invalid health projections, not equality of the returned Source metadata. The old result always has `live=false`, so actual Window validity rejects it and clears the window; the empty result also rejects and clears. The two producer call sites always pass `true`, while request/latest values flow only through actual Window consumers. The count-only matrix checks four routes, tracking on/off, and Submit/Observe consumers, including subsequent normal reads. A new Source consumer would require reviewing this proof again.

For eligible=true the candidate saves no counted work and adds a guard. For a known-invalid MSI non-recovery neutral submission it removes eight source-classification atomic loads, three route string comparisons and twelve Board finds on the supplied MS-1T91 board. These are operation counts, not measured CPU savings or proof of product-level necessity. No candidate benchmark was run and no production candidate was applied. R116 Board deduplication was not rerun.

Final evidence entry points are `REPORT.md`, `FINAL-REVIEW.json`, `summary.json` (normal 52 checks and earlier isolated timings), `ineligible-boundary-summary.json` (118 count-only checks), and source binding files. Later parent additions of diagnostic header getters are recorded separately from the normal timing run; executed health method bodies and normal count results are checked against that earlier run. The header ABI is Context=40 B, Source=72 B, Window=112 B with the supplied MSVC x64 build.
