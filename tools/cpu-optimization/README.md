# CPU optimization — developer-only external tools

For future source-matched native captures, explicitly set the process-only
`YEMAN_NATIVE_SYMBOLS=1` before the existing `native/build_native.bat` build.
This keeps `/O2`, `/MT`, and `NDEBUG`, adds `/Z7` to the three YeManCC translation
units, and links a full `Build/App/Native/YeManCC.pdb` with release reference/code
folding and incremental linking disabled. The EXE records only `YeManCC.pdb` as
its symbol lookup name. The default build is unchanged. Existing packaging
selects the product binaries and excludes PDB files; keep the exact EXE/PDB and
source/dependency epoch together in development evidence. This option neither
deploys nor starts YMCC. It cannot resolve a different, already-installed EXE.

Canonical development source consolidated from already-tested observation evidence. These are **external, manually invoked developer processes**, not a feature of the application. `module.json` is descriptive only (`developmentOnly=true`, `runtimeEntryPoints=[]`, `shipping=false`); it creates no permission, gate, fixture ID or default suite.

## Access and ownership boundary

- Explicit source execution by a developer only, during development/production of optimized builds. Nothing is installed or embedded in YMCC. No product IPC, UI/menu/tray action, startup integration, service or autorun exists for this module. The explicitly requested portable ROG field kit adds a manual external launcher only; it is kept outside product payloads and never runs automatically.
- Existing publisher/deployer assemble built Web assets, explicitly selected Native binaries and declared payloads; they do not copy this source `tools/cpu-optimization` directory. No build/package/deploy configuration was changed. This is source inspection, **not installation acceptance** or inspection of a newly built R8 package.
- The scripts never launch, stop, restart, configure or control YMCC, Steam, games or devices. The developer/user owns application state, sleep and hardware. A read-only target query is not permission to change anything. Capture starts/stops only its own explicitly requested WPR recording, not the application. It does not elevate itself.
- Only the selected native `YeManCC.exe` root contributes target CPU counters. Observer discovery requires exactly one same-name native root. Capture supports an explicit PID, so the developer must supply the correct native root; its native image/birth/hash checks remain intact. The counter observer excludes child processes and WebView CPU; the separate short-peak mode uses ETW to measure the observed process tree.

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

Run a short bounded Memory capture first. Scheduling requests a 256 MiB system ring; Samples remains 64 MiB. Neither has a guaranteed history duration: inspect actual per-CPU switch coverage and event loss before accepting a peak interval. Keep the existing one-second tail for brief CPU peaks; increasing it can overwrite the triggering window. Use a bounded File recording if actual retention is insufficient. Whole-system tracing changes workload; diagnostic captures are not idle-CPU acceptance runs.

The offline PreciseCpu analyzer selects the exact last-five-counter-row window, ranks whole-machine processes and target threads by scheduled runtime, and retains interrupt-adjusted estimates separately. Missing CSwitch edges, PID/TID ambiguity, unknown ownership, time inconsistencies and reported loss must remain visible. Sample counts locate execution paths and are not CPU duration or function call counts. Exact source/PDB identity is still required for code attribution.

The offline interface also accepts an explicit saved row:

`PreciseCpu <captureRoot> <freshOutputRoot> --target-pid PID --warmup-seconds 180 --sample 905`.
Without `--sample`, the existing actual-trigger/maximum-counter selection is unchanged.
With it, the analyzer uses exactly that row and sample-minus-four, checks all five
row identities and clocks, and requires the whole explicit window to start after
the requested warmup. Missing/bad rows never fall back to another window. An
explicit result is labelled `explicit saved sample (not runtime trigger)`, even
when the original recording has a different runtime trigger. Input recordings
remain read-only; output must be fresh.

This can inspect a window suggested by saved process-cycle counters as well as
the highest CPU-time-counter window. Cycles are an additional selection signal,
not CPU milliseconds or percent; scheduling coverage, raw/converted sample
agreement and matching EXE/PDB/source must be verified for the selected window.
`--self-test-saved-sample` runs only the 11 saved-row selection checks in a fresh
retained input namespace per command invocation. Original `--self-test` and
`--self-test-liveness` selections remain separate. None is a default product gate.


Official Microsoft CPU Analysis and SystemProvider pages were retrieved and archived in the MethodUpgrade-20261102 evidence directory, including CPU Usage(Precise) details. Existing sampled-only traces cannot gain missing scheduling events by changing the parser.

## Short CPU peaks in the YMCC process tree

Use this mode for a bounded diagnostic reproduction. It uses the existing Scheduling file profile, with sampled stacks and context-switch events, and does not wait for a host-only CPU threshold. No CPU polling loop runs while recording. The default observation is 20 seconds, with an explicit range of 10-60 seconds. Start from an administrator PowerShell with no active WPR session:

```powershell
./Capture-YmccCpuStacks.ps1 -Mode Capture -PeakSweep -MaximumSeconds 20 `
  -TargetProcessId <pid> -ExpectedIdentityPath <json-with-exeSha256> -EvidenceRoot <existing-evidence-directory>
```

With the .NET 10 SDK, analyze the returned capture directory, using a fresh output path:

```powershell
dotnet run -c Release --project ./PreciseCpu -- --peak-sweep <capture-directory> <fresh-output-directory>
```

`PEAKS.json` ranks host, WebView and observed process-tree residency separately over sliding 25, 100 and 250 ms windows. It keeps up to three non-overlapping peaks per scope and duration, with roles, threads and same-window sampled stacks. Scheduled milliseconds include possible interrupts. `percentOfOneLogicalCpu` may exceed 100 when work runs in parallel; `percentOfAllLogicalCpus` has a separate denominator. Module offsets and PDB identity provide a lookup key for later symbol resolution. Samples without a matching running thread instance are counted separately, and sample weights are never used to rank windows.

For a producer such as HWiNFO that can restart outside the YMCC tree, append `--focus <verified-process-receipt.json>`. Use one receipt per trace: `pid`, `birthFileTime`, `imagePath`, `beforeUtc`, `afterUtc`, and `sameInstance` must describe the verified process spanning the recording window. The focus report keeps the best covered 25 ms window in each burst (at least 15 scheduled CPU-ms, 1 ms scan step, candidates merged within 250 ms), with that process's threads and matched stacks. Compare its cadence and CPU time independently of ancestry. A subthreshold or uncovered cycle can be absent, so a long gap alone does not establish a cadence change.

Append `--focus-all-stacks` after the receipt to write `FOCUS-STACKS.json` without changing `PEAKS.json`. Use its whole-interval stack weights to check whether a sampled call path moved out of the burst windows. Match module path, image size and nonzero checksum before comparing RVAs; the logged PE build stamp is not proof of a build date. `bestCovered25ms` measures the exact best covered positive window even below the burst threshold. A null window supplies no positive estimate; it does not prove absence of CPU work in an uncovered interval.

The parser refuses lost events, missing scheduling data and inconsistent root identity. It excludes windows that intersect a scheduling gap or unbound running owner, and reports an incomplete capture even when some peak windows are usable. Pre-existing child ownership comes from ETW rundown; detached services and unrelated WebViews are outside this tree. The capture does not prove why a task fired and is not a product CPU acceptance test. ETL may include machine-wide activity; retain it as local diagnostic evidence. The analysis removes its temporary index and retains the original trace unchanged.

### Evidence cycle

1. Capture two or more bounded windows for one named scene. Record the version, scene, child roles and the question each capture is intended to answer.
2. Run `Compare-YmccCpuPeaks.ps1 -PeakReport @(<PEAKS-A.json>, <PEAKS-B.json>) -OutputPath <fresh-replay.json>`. It verifies independent traces, the same installed instance and collector profile, and highlights repeated owners without treating them as a common cause. Compare tree membership before comparing total CPU.
3. Locate a repeated operation and its trigger with matching symbols and a controlled reproduction. If evidence is missing, name the missing field and add only that observation (for example an event when entering a fallback, rather than a periodic log); verify the observer on a positive and a negative control. If no next observation can distinguish the possibilities, stop that branch.
4. Change behavior only after the trigger and work are bound. Re-run the same scene with a matched control, include correctness checks, and report any unmeasured paths. Preserve original traces, loss/coverage, build identity and outcomes. Clean up only the temporary files owned by the round.

For a bounded filesystem diagnosis, explicitly select `-ProfileMode FileIO
-FileMode`. `YmccCpuFileIo.wprp` records CPU samples, scheduling and file
operations with selected operation stacks in a 128MiB collector pool. The
profile is file-only; the capture refuses this mode without `-FileMode` and
passes the same logging mode to WPR's profile-details query. This mode has
greater observation cost and is for attribution only. Check lost events,
actual retained windows, process/thread identity, filename resolution and
stack availability separately. Operation counts are not CPU durations; an
old executable still requires its own source/symbol evidence. The default
Samples/Scheduling/Rog recording selections, identity checks, duration limits
and owned-instance stop behavior remain unchanged. This adds no application
entry point, startup integration or shipping payload.
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


## 开发期固定缓冲计数

`rog-field/Observe-BufferedCpu.ps1` 要求 PowerShell 7，是显式 PID、出生 FILETIME、完整路径和 EXE SHA 绑定的只读入口。用新的外部 PowerShell 进程手动执行；不发现或重绑目标，不启动或控制应用、设备与睡眠，也不进入产品入口、安装或自启。原 ROG cohort/追踪入口和冻结采样包不随之重写。`ReadOnlyCounters.cs` 保持原字节；仅此新入口把该基底和 `BufferedCounterSource.json` 的三个精确片段组合成内存中的开发类型，记录基底、片段与实际编译文本的独立哈希，缺锚点/重复锚点或反向还原失败就拒绝。旧版 PowerShell 5.1 入口继续只编译原基底。

计数循环只向预先分配的数值数组写入真实 QPC/CPU/cycles/等待阶段数据；结束并归还查询句柄后才转换 JSON、写文件。迟到后从实际读取结束重新计时，不补齐短窗。末段不足一个采样周期的区间单列；间隔异常、退出、错误出生/路径/SHA和原生句柄关闭错误保持失败或未知。`COUNTERS_COLLECTED` 只说明采集结束，不能当成长期 CPU 峰值、Host 祖先关系、设备静止或具体函数热点验收。

示例参数需取自当前已验证的实例；不要套用旧 PID 或旧哈希：

```powershell
pwsh.exe -NoProfile -File .\rog-field\Observe-BufferedCpu.ps1 -ExpectedProcessId <PID> -ExpectedBirthFileTime100ns <FILETIME> -ExpectedImagePath <完整EXE路径> -ExpectedExeSha256 <SHA256> -EvidenceRoot <全新证据目录> -DurationSeconds 90 -IntervalMs 1000
```


<!-- PASSIVE-WINDOW-SCENE-20261110 -->
## 被动窗口场景证据（2026-11-10）

`rog-field/Observe-WindowScene.ps1` 是明确手动调用的外部开发观察器。它绑定指定PID、精确birth FILETIME、镜像路径/SHA及同进程HWND，只读取前台PID、选定窗口的存在/可见/最小化状态，以及显示、隐藏、销毁和前台变化事件；不读取窗口标题、按键内容或进程内存，不调用目标UI或设备接口。单次10–600秒，固定32768条数值容量，循环无JSON/文件IO；进程退出、窗口身份变化、溢出、hook失败或资源归还失败均保留未完成。三条OUTOFCONTEXT hook与查询句柄在退出前释放，无产品启动/打包入口。

```powershell
pwsh -NoProfile -File tools/cpu-optimization/rog-field/Observe-WindowScene.ps1 -ExpectedProcessId <PID> -ExpectedBirthFileTime100ns <FILETIME> -ExpectedImagePath <EXE绝对路径> -ExpectedExeSha256 <SHA256> -ExpectedWindowHandle <十进制HWND> -DurationSeconds 300 -EvidenceRoot <新的绝对证据目录>
```

可显式添加 `-RequireHiddenBackground`：在写入采样就绪的 `IDENTITY.json` 前，额外读取一次指定 HWND，窗口必须仍属当前 PID、不可见且该进程未占前台；不符便保留 `INITIAL-SCENE.json` 并拒绝。这只核初始场景，后续仍须核对完整 `SCENE.jsonl`，不能把中途唤起的 CPU 峰值算作静态隐藏后台。该选项不操作窗口、不给程序新增线程或定时器；省略时沿用原默认行为。

与CPU原始计数按准确PID/birth及实际QPC重叠区间配对；不得把上一轮的窗口状态移记到新进程或窗口。`SCENE.jsonl`中的eventTimeMs32是系统事件时间，beforeQpc/afterQpc是延迟回调的实际观察时间，二者不能混用。事件回调可延迟，未观察到变化不证明没有丢失，也不证明物理手柄静止；此工具不自动授予“全程后台”或CPU验收。它只补场景证据，不能代替调用栈。

`-SelfTest -EvidenceRoot <新目录>`只使用本观察器自有的不可见message-only测试窗口和NotifyWinEvent，检查真实hook回调、窗口/对象过滤、错误birth、未知窗口、容量耗尽与资源归还；不会操作YMCC。自测窗口相关写接口只在显式SELFTEST编译条件内，普通观察源码不含这些执行分支。该自测没有新增永久fixture ID，也不是默认发布门。
<!-- /PASSIVE-WINDOW-SCENE-20261110 -->


<!-- SELECTED-WINDOW-IDENTITY-20261110 -->
选择HWND时先核对同一PID的原生窗口类、线程及普通生命周期日志中的主窗句柄；不要将Process.MainWindowHandle自动当成YMCC主窗。桌面HUD开启时，本机曾返回YMCC_MonitorDragInput；主程序主窗与YMCC_DesktopMonitorHUD、屏幕按钮是同进程中的不同窗口。观察参数只证明指定HWND的状态；记录HUD窗口不能替代主窗状态，也不能把迟开的主窗补录回填到更早的CPU区间。HUD显示、拖动或切模板应保留为实际场景，和主窗是否隐藏分别记录。
<!-- /SELECTED-WINDOW-IDENTITY-20261110 -->

<!-- CPU-SCHEDULING-RETENTION-20261112 -->
The 2026-11-12 retention follow-up recovered complete coverage on all 32 CPUs with the same product build, a 256 MiB Scheduling ring and a one-second tail. The preceding 64 MiB / five-second-tail trace retained incomplete peak history despite EventsLost=0; that rejected evidence remains preserved. The larger ring is a developer capture setting, not a guarantee of retention or a product CPU optimization. Mainline profile and collector match the actually used follow-up bytes after UTF-8 BOM and line-ending normalization. See Testing/YMCC/Evidence/RogCpu-20261104/WindowOwnedPropertyNames-20261112/PeakRetentionFollowup/INTAKE.json. No runtime entry, publishing inclusion, autostart, target control, new fixture ID or default release check is added.
<!-- /CPU-SCHEDULING-RETENTION-20261112 -->

<!-- CPU-COUNTER-PARTS-20261112 -->
External stack capture additionally preserves kernelCpu100ns and userCpu100ns as decimal strings from the same already-required GetProcessTimes read. The existing cpu100ns sum, rolling-window trigger, sampling interval, cycles, read QPC bounds and target-identity/exit checks are unchanged. No additional native query is made. Existing reports can ignore these additive fields; future reconciliation must keep missing fields UNKNOWN and must not infer accurate CPU time merely from the 100-ns unit or convert cycles with an assumed clock frequency.
This is development-only evidence, not a product CPU optimization or completed reconciliation. The installed c20508c7 package, its frozen source/tools and original traces remain unchanged; the added fields have not yet been measured in a new Windows trace. The refusal and partial association from the game-background sample remain preserved under RegistryPropertySingleRead/InstalledValidation-20261112.
<!-- /CPU-COUNTER-PARTS-20261112 -->
