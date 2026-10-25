# CPU lock actual consumer

This module executes the existing legacy `src/bridge/cpulock.ts` public functions and actual `settingsRepository.ts`, `yeman.ts`, `api.ts` and their dependency graph. Only the external WebView `ipc.ts` transport is replaced by `memory-ipc.mjs`. Product code runs as an IIFE in a VM with no process, require, filesystem, timers, browser, fetch or OS ports. The compiler is the existing esbuild and Node is the existing fixed Node 22.14.0. No installation or product source change is required.

The source file declares that current page/App/autofloat no longer call this compatibility API. The adapter therefore claims a legacy bridge behavioral mock only. It must not be presented as current CPU UI, native CPU writes, physical frequency/power effect, sleep recovery or global acceptance.

Run after merging this directory into the existing repository:

```powershell
& 'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe' tools/ymcc_sim/global/cpulock-consumer/run.mjs --evidence-dir D:/YMCC-Evidence/cpulock-fresh
```

For an unmerged overlay, supply `--source-root G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC`. Evidence must be a fresh directory outside the product repo. `--audit --source-root <repo> --evidence-dir <same evidence>` rehashes the recorded source, fixture, tool and output closure without executing product/native code. The original run receipt is preserved. Audit freshness is limited to recorded source members, not the entire repository or artifact set outside this consumer.

`--prepare-toolchain --source-root <repo>` explicitly captures the existing global complete Node package authority into `toolchain-inputs.json`; it does not install anything. Review a changed pin before using new tools. This is a predeclared package scope, not an observed OS loader graph. Each actual compiler onLoad and esbuild service process is separately recorded. The esbuild and worker processes must end naturally. Compiling with and without metafile must emit identical executable bytes.

The 23 source-derived scenarios cover runtime surface and copied getters; legacy migration/sanitization; independent flags; observers and unsubscribe; full and side set/update/unlock overloads; patch ownership and serialized updates; implicit lock values and real registry read conversions; missing DC fallback; unsupported reads; no-lock apply; AC-only/DC-only/both-side batches; real scheduling, frequency, turbo, minimum-state and throttle conversion; apply options; rejected/exception/malformed settings receipts; committed-document feedback; read rejection; generation invalidation and queue recovery; failed/throwing native power receipts.

Three deliberate faults modify only a private compiler input of current cpulock source, leaving every scenario assertion unchanged: update an unlocked side; publish cache before durable save; submit both AC/DC while only one side is locked. Their worker exit 1 and precise assertion failures are required, while the unmodified worker must exit 0. These are current mutation controls, not historical field failures. The original failing receipts are kept.

Current yeman intentionally swallows batch rejection/exception. `applyCpuLock()` can consequently return true while all native memory receipts failed. That observation is a scope limit and does not prove successful native writes. All-missing registry reads yield yeman defaults, so the cpulock null-read fallback and caught apply exception/false branches are not fabricated with a replacement yeman function.

`export-manifest.json` enumerates every actual declaration, including types and overload lines. There are 13 function exports plus the compatibility path constant. The four tiny aliases have only the evidence of invoking their existing forwarding/accessor path; they are not separate native operations. `CPU_LOCK_FILE` is inspected as a constant and never read as a real path. Types have no runtime behavior. Inventory association and source-file inclusion are not behavioral acceptance.

`adapter.mjs` exports `getCpuLockConsumerAdapters`, but no shared registry is edited by this overlay. The parent can review the separate static registration suggestion and merge the directory, then register it in the existing global adapters. `source-inputs.json` declares the captured product/support source graph and `toolchain-inputs.json` declares all bound external tool files. A source/dependency graph change requires reviewing and updating that declaration before registration.

Remaining work: native IPC transport and registry/power write implementation; native error/permission/unsupported-device behavior and actual hardware effects; actual current CPU page/App/autofloat routing; real UI/gamepad navigation; native settings lock/disk durability; sleep/resume/software scheduling integration; full global and installed/physical acceptance. This task starts no YMCC/Host/Manager and performs no system commands, real registry/PnP/CPU/device operation, network access or deployment.
