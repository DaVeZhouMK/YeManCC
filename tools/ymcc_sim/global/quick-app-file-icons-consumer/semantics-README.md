# Quick-app icon semantics consumer

Independent ID: `game-library.quick-app-icon-semantics-consumer`.
The existing adapter returns this second descriptor after the unchanged file-icon
consumer descriptor. Its original run/cases/spec/README remain byte-for-byte intact.

Run with the existing bound Node executable:

```powershell
node tools/ymcc_sim/global/quick-app-file-icons-consumer/semantics-run.mjs --repo-root <absolute-repository> --evidence-dir <fresh-absolute-owned-directory-outside-repository>
```

A D overlay can run this entry directly against G with the same two arguments.
Such execution yields `PROTOTYPE_PASS`, `passed:false`, `sourceApproval:false`.
An integrated entry uses the ordinary selected-source manifest checks. This file
adds no CLI/extra-adapters change: the already registered adapter returns both IDs.

The existing complete bundled-node-tool-authority implementation is imported from
that selected repository. Its exact Node/esbuild package graph is locked before
compiling and fully rechecked afterwards; both tools and outputs retain the 3072
artifact budget. The original 45000ms outer budget and 1000ms VM calls remain.
Only its pinned esbuild service invocation may start a child process.

Both real TypeScript modules are compiled separately on each positive/fault pass.
quickAppDefaults has no allowed imports; quickAppIcons may require only the actual
compiled ./quickAppDefaults module. No factory or algorithm is substituted. VM
code generation is disabled. No process, fetch, browser, Buffer, timers, filesystem,
icon-file, native API, SDK or device port is supplied to product code. Dependencies
outside the exact module allowlist and unknown case/method names are refused.

Ten new cases independently cover the five real defaults and fresh factory values;
case/slash/outer-whitespace identity; internal-space and exact-path boundaries;
renamed defaults; same-name custom paths; computer/browser B; other kinds A;
ordered name/path fallback rules; unknown-value fallback; and the fixed preview
table. Literal expected lists in cases are assertion goldens, not VM replacements.
Name-based fallback marks do not grant a custom path default identity.

Two one-statement mutations are applied only to owned in-memory source copies:
remove slash normalization, and change the selected computer/browser variant to A.
Each must compile both modules, initialize both VMs, then fail its named original
assertion. Compiler errors, dependency refusals and VM startup failures never count
as semantic detection. Raw inputs, transformed code/maps, calls/results and compiler
protocol hashes are retained. The original 14 cases and 2 faults are not added to
this consumer's counts and are not rerun by this entry.

Actual Vue rendering, icon-file extraction, persisted settings precedence, installed
product behavior and physical acceptance remain UNKNOWN. No package is produced.
