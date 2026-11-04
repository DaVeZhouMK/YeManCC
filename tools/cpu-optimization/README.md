# CPU optimization — developer-only external tools

Canonical development source consolidated from already-tested observation evidence. These are **external, manually invoked developer processes**, not a feature of the application. `module.json` is descriptive only (`developmentOnly=true`, `runtimeEntryPoints=[]`, `shipping=false`); it creates no permission, gate, fixture ID or default suite.

## Access and ownership boundary

- Explicit source execution by a developer only, during development/production of optimized builds. Nothing is installed or embedded in YMCC. No product IPC, UI/menu/tray action, startup integration, service or autorun exists for this module. The explicitly requested portable ROG field kit adds a manual external launcher only; it is kept outside product payloads and never runs automatically.
- Existing publisher/deployer assemble built Web assets, explicitly selected Native binaries and declared payloads; they do not copy this source `tools/cpu-optimization` directory. No build/package/deploy configuration was changed. This is source inspection, **not installation acceptance** or inspection of a newly built R8 package.
- The scripts never launch, stop, restart, configure or control YMCC, Steam, games or devices. The developer/user owns application state, sleep and hardware. A read-only target query is not permission to change anything. Capture starts/stops only its own explicitly requested WPR recording, not the application. It does not elevate itself.
- Only the selected native `YeManCC.exe` root contributes target CPU counters. Observer discovery requires exactly one same-name native root. Capture supports an explicit PID, so the developer must supply the correct native root; its native image/birth/hash checks remain intact. Child processes and WebView CPU are **excluded**. There is no application-wide CPU claim.

## Explicit interface contract

Use named PowerShell parameters. `ExpectedIdentityPath` and `EvidenceRoot` are mandatory nonempty strings for **both** scripts. Identity JSON must contain the full expected `exeSha256`; there is no historical default. Supply the current candidate's identity explicitly; a historical R7 identity is not acceptance of R8.

`EvidenceRoot` is an existing development evidence directory, preferably an absolute path. `OutputRoot` is optional, resolved to a full path, and must remain lexically within that explicit root. Defaults are computed from the supplied root only:

| Source | Manual interface and unchanged limits | Output |
| --- | --- | --- |
| `Observe-CandidateCpu.ps1` | `-ExpectedIdentityPath <json> -EvidenceRoot <dir> [-OutputRoot <dir>] [-DurationSeconds 360] [-ExpectedImagePath <exe>] [-MinimumBirthUtc <UTC>]`; duration 2–86400 s | Default `<EvidenceRoot>/CandidateCpu/<unique UTC+GUID run>/`; `IDENTITY.json`, `SAMPLES.jsonl`, `SUMMARY.json` |
| `Capture-YmccCpuStacks.ps1` | `-ExpectedIdentityPath <json> -EvidenceRoot <dir> [-OutputRoot <dir>] [-Mode Preflight\|Capture] [-TargetProcessId 0] [-ExpectedBirthFileTime <native birth>] [-ExpectedImagePath <exe>] [-TriggerPercent 0.30] [-MaximumSeconds 360] [-TailSeconds 1] [-WarmupSeconds 0] [-FileMode]`; trigger 0.01–100%, maximum 10–600 s, tail 1–10 s, warmup 0–600 s | Default `<EvidenceRoot>/StackCapture/<unique UTC+GUID run>/` in **both** modes; Preflight writes `WPR-PREFLIGHT.json`. Capture writes identity/start/CPU/capture/stop evidence, optional trigger and saved ETL; failure can leave partial evidence. |
| `analyze-candidate-cpu.mjs` | `--runDirectory <recorded run> --evidenceRoot <existing absolute dir> [--outputDirectory <fresh dir within evidenceRoot>]`; kebab-case aliases `--run-directory`, `--evidence-root`, `--output-directory`, or positional run directory accepted | Default `<evidenceRoot>/<run basename>/`; exclusively creates `ANALYSIS.json` and `ANALYSIS.md`, refuses overwrite. Original lexical and realpath/junction output guard retained with the supplied root. |

Output directories are development workspace boundaries, not an OS sandbox. Do not point them at product/source/configuration directories or link PowerShell outputs through junctions. The analyzer's guard API is now `assertOutputDirectory(outputDirectory, evidenceRoot)`; lossless parsing and analysis APIs are unchanged. Analyzer exit codes: 0 = descriptive analysis (even above CPU target), 2 = raw validation errors, 1 = refused/error. No code means product acceptance. PowerShell 5.1/7 syntax is checked, not live execution revalidated by this consolidation. Node uses only built-in modules; PowerShell uses the original small native query declarations, not a new compiled helper project.

## Measurement facts and caveats

- Observer retains the one-shot CIM discovery, query-limited held process handle, native PID/birth/image/hash binding and final checks. Original 250 ms schedule, monotonic Stopwatch denominator, nearest-1000 ms windows with startup minimum 875 ms, 0.1% comparison and missed-slot accounting are unchanged.
- Machine CPU percent is `100 * delta CPU ms / actual monotonic wall ms / machine logical processors`, not percent of one core. Root and sampler CPU are separate; sampler cost is never subtracted. Counter granularity, startup/final hashing, compiler-child exclusion and observation overhead remain visible. Overlapping windows are not independent spikes; no measured zero proves zero cost.
- Capture retains its original ~250 ms counter reads, five-point windows, trigger/warmup/tail/deadline limits and own-instance stop semantics. Only after successful WPR start does `finally` stop `-instancename YMCC-CPU-<unique run>`; failed start never authorizes foreign stop/cancel. No generic WPR cancellation is added. WPR must already be available; Capture requires an already elevated developer shell.
- `YmccCpu.wprp` is byte-identical and still loaded from `$PSScriptRoot`. Its SampledProfile stacks/ProcessThread/Loader CPU ETW profile is system-wide: ETL may contain other processes even though trigger counters and later target attribution are root-scoped. The requested memory ring is 64 MiB; retention/lost events must be inspected. File mode is time-bounded, not a fixed disk-size cap. ETW kernel/stack-walking and WPR start/stop costs are not covered by the collector's self CPU. ETL alone proves neither source attribution nor improvement; check PID/birth, module/PDB identity and retained frames. This profile does not add context-switch stack capture.
- Offline analysis preserves exact integer tokens and BigInt deltas, UTC birth-age segmentation (<120 s startup), monotonic CPU denominators, original sliding/window/cluster arithmetic, raw identity/liveness/summary validation, and all FAIL/UNKNOWN blockers. Requested duration is not actual stable coverage. Root background visibility, inactive fan/virtual controller and live-now state cannot be inferred. `candidateCpuAcceptance=false` and the inherited `completeR6Acceptance=false` remain unchanged; the latter is a preserved schema field, not a historical source dependency.

## Minimal explicit commands (examples only; not autorun)

These paths exist in this workspace. They reference **historical R7** for reproduction, not the current installed candidate. Before a future manual live operation, independently choose the correct current identity/root and obtain that operation's authorization. No live operation, preflight or capture was run during source consolidation. Preflight is not a pure offline syntax check: it queries the target, WPR status/profile and privileges.

```powershell
# Future manual observer example ONLY; do not use an old identity to accept a new export.
powershell.exe -NoProfile -File 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-optimization\Observe-CandidateCpu.ps1' -ExpectedIdentityPath 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\ExportR7\CANDIDATE-IDENTITY.json' -EvidenceRoot 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\CpuDeveloperModule' -DurationSeconds 360

# Future explicit preflight example ONLY; Capture is a separate -Mode Capture request.
powershell.exe -NoProfile -File 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-optimization\Capture-YmccCpuStacks.ps1' -Mode Preflight -ExpectedIdentityPath 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\ExportR7\CANDIDATE-IDENTITY.json' -EvidenceRoot 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\CpuDeveloperModule'

# Offline reproduction; choose a fresh output directory, never overwrite validation evidence.
node 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\cpu-optimization\analyze-candidate-cpu.mjs' --runDirectory 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\RuntimeObservation\CandidateCpu\20261101T233413245Z-0d0461c1' --evidenceRoot 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\CpuDeveloperModule' --outputDirectory 'G:\YeManCC-Work\Testing\YMCC\Evidence\BackgroundIdleOptimization-20261101\CpuDeveloperModule\Manual-R7-fresh'
```

## Provenance and bounded validation

Original evidence remains read-only under `G:/YeManCC-Work/Testing/YMCC/Evidence/BackgroundIdleOptimization-20261101/RuntimeObservation`: `CandidateCpuTools/Observe-CandidateCpu.ps1`, `StackCapture/Capture-YmccCpuStacks.ps1`, `StackCapture/YmccCpu.wprp`, `CandidateCpuAnalysis/analyze-candidate-cpu.mjs`. There is no import/runtime dependency on those paths.

Source pins, exact reversible transformations, unchanged-region SHA256 witnesses, syntax-only receipts, publisher/deployer inspection and one existing R7 offline comparison are recorded only at `G:/YeManCC-Work/Testing/YMCC/Evidence/BackgroundIdleOptimization-20261101/CpuDeveloperModule`. No sampling, WPR/preflight, 480 s run, application lifecycle operation, installation, export, heavy compile or new fixture/default gate is part of this validation. Prior CPU failures and scenario UNKNOWNs are preserved, not promoted to installation or CPU optimization acceptance.

## Bounded compatibility-file observation

`Observe-MonitorRelay.mjs` runs only as an explicitly invoked external development process. Supply absolute `--directory`, `--identity`, `--evidenceRoot` and a new `--outputDirectory`; `--durationSeconds` defaults to15 and is limited to5–60. It watches the four monitor relay files plus `game-target.json`, records directory change notifications and start/end file metadata, then closes automatically. It does not read file contents or call product IPC, change settings, control an app/device or run from product startup. Caller binds the same native PID/birth/path/EXE before and after; this tool only retains that supplied reference. Notifications can coalesce, do not identify a writer PID and are not write/Flush counts or CPU acceptance. Run separately from sensitive numeric sampling and preserve the original evidence.


<!-- SCHEDULING-METHOD-UPGRADE-20261102 -->
## CPU scheduling attribution (2026-11-02)

Use the canonical external capture with `-ProfileMode Scheduling` to select `YmccCpuScheduling.wprp`. The default `Samples` mode remains available. Both modes preserve held-handle PID/birth/path/EXE checks and only stop the owned WPR instance. Scheduling adds CSwitch/DPC/Interrupt events and keeps SampledProfile execution stacks; it does not walk a stack for each context switch. Source remains development-only with no product UI, IPC, startup or shipping entry.

Run a short bounded Memory capture first. The requested64MiB ring has no guaranteed duration: inspect actual per-CPU switch boundaries and event loss before accepting a peak interval. Use a bounded File recording only if actual retention is insufficient. Whole-system tracing changes workload; diagnostic captures are not idle-CPU acceptance runs.

The offline PreciseCpu analyzer selects the exact last-five-counter-row window, ranks whole-machine processes and target threads by scheduled runtime, and retains interrupt-adjusted estimates separately. Missing CSwitch edges, PID/TID ambiguity, unknown ownership, time inconsistencies and reported loss must remain visible. Sample counts locate execution paths and are not CPU duration or function call counts. Exact source/PDB identity is still required for code attribution.

Official Microsoft CPU Analysis and SystemProvider pages were retrieved and archived in the MethodUpgrade-20261102 evidence directory, including CPU Usage(Precise) details. Existing sampled-only traces cannot gain missing scheduling events by changing the parser.
<!-- /SCHEDULING-METHOD-UPGRADE-20261102 -->


<!-- HELD-HANDLE-LIVENESS-20261103 -->
External collectors open QUERY_LIMITED_INFORMATION | SYNCHRONIZE and use zero-time WaitForSingleObject on the held process handle after reading counters. WAIT_TIMEOUT is live, WAIT_OBJECT_0 is exited, any other result is rejected. GetProcessTimes exit FILETIME is a diagnostic only: Microsoft defines it as undefined while the process is alive. New numeric summaries and trace finalTargetIdentity record explicit wait evidence. Offline readers retain legacy raw counters but do not promote exitTime=0 alone into confirmed liveness. The manual PreciseCpu --self-test-liveness mode covers this boundary without repeating the original20 scheduling cases. This source-only developer-tool repair does not ship, start a monitor or change historical measured CPU peaks.
<!-- /HELD-HANDLE-LIVENESS-20261103 -->


<!-- CYCLE-WITNESS-20261103 -->
External stack captures also record optional QueryProcessCycleTime cumulative process cycles as decimal strings, success/error state, and QPC bounds around the sequential counter reads. This adds no process enumeration, thread sampling or product runtime entry. Cycle counts include kernel and user work but are never converted into milliseconds or CPU percentage; CPU timer behavior can vary with hardware/frequency. A failed optional cycle query remains unknown/null and does not become zero work. Existing GetProcessTimes trigger arithmetic and old captures stay unchanged. A new capture can only support its own selected window, never retrospectively explain another run peak.
<!-- /CYCLE-WITNESS-20261103 -->


## Portable ROG field capture (explicit user request, 2026-11-04)

See `rog-field/README.zh-CN.md`. This manual external kit reuses `Capture-YmccCpuStacks.ps1`. Its existing default native/threshold behavior remains unchanged; optional `-TargetImageName YeManInputHost.exe` requires the matching full EXE identity and supports explicit PID/birth/image binding. `-FullDuration` records the complete bounded interval without threshold stopping or a first-threshold selection. `-ProfileMode Rog` selects the bundled system scheduling profile plus limited CLR JIT/loader/GC, method-map and WaitHandle events (`0x40000020019`, CPU1-confirmed WaitHandle keyword), with a 64MiB system buffer pool plus 16MiB CLR pool. Field traces use explicitly bounded File mode. Event loss and retention still require offline verification; profile acceptance is not successful trace capture.

`rog-field/Observe-RogCpu.ps1` independently binds an exact full-path native root and initially discovered `YeManInputHost.exe` descendants using held query/synchronize handles. It records raw timers/cycles, its own CPU and unknowns; it never uses counter failure as zero. Only one process discovery occurs per observation. Later Host starts/replacements remain UNKNOWN and need a separate user-initiated run. It does not enumerate all threads; the optional ETL supplies thread scheduling evidence. Product UI, drivers, settings and device lifetimes remain controlled by the human. The new scripts are source-only development tooling, not an automatic fixture or shipping feature.

## ROG 计数离线分析（2026-11-04）

复用 `analyze-candidate-cpu.mjs`。输入目录须恰好存在 `CPU.jsonl`（ROG）或 `SAMPLES.jsonl`（旧计数）之一；混合格式拒绝。`IDENTITY.json`、计数文件、`SUMMARY.json` 只读，输出独占新建在显式 development evidenceRoot 中。可选 `--candidateIdentity <CANDIDATE-IDENTITY.json>` 只适用于 ROG，并纳入输入 SHA256 与二次读取一致性核对。

```powershell
node tools/cpu-optimization/analyze-candidate-cpu.mjs --runDirectory '<已回传的ROG运行目录>' --evidenceRoot 'G:/YeManCC-Work/Testing/YMCC/Evidence/RogCpu-20261104' --outputDirectory 'G:/YeManCC-Work/Testing/YMCC/Evidence/RogCpu-20261104/Returned-S2-Analysis' --candidateIdentity 'G:/YeManCC-Work/Testing/YMCC/Evidence/RogCpu-20261104/Delivery/CANDIDATE-IDENTITY.json'
```

每个初始绑定进程及采样器分别复算原始整数 CPU/QPC/cycles、区间均值和 875–1125ms 滑窗；用 BigInt 求差后换算，cycles 不换 CPU 时间。漏槽字段按累计增量断开滑窗，累计倒退拒绝；超出请求周期1.5倍的间隔、无效身份/读取、坏行等也断链。未知root/Host祖先保留原始计数，可比覆盖/峰值为UNKNOWN，缺失不填0；非原子读取的各进程不合并总 CPU，不把采样器 CPU 从目标扣除。每个窗口保留 PID/birth、原始行号、精确 QPC/频率、读操作边界、UTC 范围和进程年龄，供同 ETL 窗口审查。

进程年龄120秒为 startup/stable 分界；这不代表陀螺仪开启时间。场景名 S2、磁盘文件端点匹配和有限窗口数值均不能证明静止/日志模式、已加载模块、PDB、功能或 CPU 达标。`candidateCpuAcceptance` 恒为 false；后起/替换 Host 仍 UNKNOWN。当前 `PreciseCpu` 的 capture loader 仍消费原栈采集格式，不能把本报告直接冒充其旧五样本输入；ROG 窗口与 ETL 的 TID、调度覆盖/丢失、模块/PDB、JIT/GC/WaitHandle 必须独立同窗核对。

本次验证复用已有 owned fake root+Host 计数和旧 R19，只测离线解析、数学、身份边界与 CLI。证据在 `Testing/YMCC/Evidence/RogCpu-20261104/OfflineAnalysis`；软件 smoke 不是掌机、真实虚拟手柄或 CPU 收益证据。现场包保持冻结；离线分析使用当前主线工具身份。

<!-- EXPLICIT-ROOT-NATIVE-PATH-20261104 -->
## 显式进程实例与CIM隐藏路径

Observe-RogCpu.ps1可选ExpectedRootProcessId与ExpectedRootBirthFileTime100ns，两者必须一起指定，并继续要求ExpectedImagePath。发现阶段只选该PID，随后用持有的原生查询/同步句柄核验完整路径与精确birth；不匹配时在首样本前拒绝。默认未指定的匹配/歧义拒绝不变。已证明祖先关系的Host/中间节点在CIM不返回ExecutablePath时，可从同一原生句柄读取路径并核验snapshot名称，原始CIM缺失和path authority保留。每轮仍只有一次进程发现，采样不换PID、不逐拍枚举线程/全机进程。

离线analyzer同样核验参数中的显式root PID/birth；仅一项、错误PID或错误birth都取消root/Host可比资格，原始计数保留。PS5.1解析/默认参数绑定、真实错误birth首样本前拒绝，以及33项离线边界+6CLI有界检查均已执行。真实本机样本在Testing/YMCC/Evidence/RogCpu-20261104/LocalLiveHost。新增参数与本轮采集源均属于主线外部开发工具；已交付ROG ZIP仍保持冻结身份，不把不同工具SHA混作同一采样版本。
<!-- /EXPLICIT-ROOT-NATIVE-PATH-20261104 -->
