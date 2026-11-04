<!-- AI-NAV: UV-DATA-SOFTWARE-DELIVERY-20261104 -->
更新 2026-11-04T03:23:11.968154+08:00（香港时间）：本轮“继续补UXTU电压能力到YMCC能力总库”已完成现有AMD全核CO/Intel core-cache数据、实际消费与普通主线入包的软件范围。普通0.0.37包为 `G:/YeManCC-Work/Deliverables/YMCC-Mainline-Quick-TestPackage-20261104-024813/YeManCC.zip`，SHA256 `8f5327474ca499c5267082146131b41604ec421315eb0e7d266a03b63acaf285`；02:49:46+08:00冻结helper源3ec527…/policy22679…/bridge6dba…/Card1b01…，实际EXE c39dfe…与正常包清单一致。前端原48、协议原6组及保留负例、typecheck通过；局部真实组件/engine在720/800共8实例176断言通过，仅软件模拟与诊断范围。库schema38通过，维护总376.3248ms计同90秒账，累计82.678826秒，旧FAIL/NO_RUN与修正前清单保留。

主线已采用配套EXE/base_library和清单/锁；原21项PawnIO集合保留，19项未变，固定HC87文件与三模块字节保持原身份，无DLL新增/裁剪。包是供用户测试的普通导出，fixtureValidation=NOT_RUN/formalReleaseAccepted=false；未部署或运行本次helper/驱动/硬件。ROG/Z2E同CPU/BIOS实际固件接受、读回与稳定性仍UNKNOWN；该剩余现场验证不被改写成型号不支持，也不冒称全库已完成。全库/C01/电池/灯光等原边界保持PARTIAL。正式最新证据：`G:\YeManCC-Work\Testing\YMCC\Evidence\HC-Code-Capability-Upgrade-20261031\Codex\HC-CODEX-061-UV-ORDINARY-EXPORT\reviewer\FINAL-RESULT.json`。本导航在导出后更新，不倒写已冻结来源。
<!-- /AI-NAV: UV-DATA-SOFTWARE-DELIVERY-20261104 -->

<!-- AI-NAV: CPU-MANUAL-CO-CANDIDATES-20261103 -->
## 当前补充：手动CCD与CPU电压偏移的同库数据

2026-11-03用户分工：能力总库补数据/生成规则，原CPU执行者补实际消费者。既有`ccdControl.activation=true`、`voltageOffset.activation=true`和`manualControls.mode=explicit_settings_apply`继续生效；下方2026-10-31“核心/降压未激活”段落只描述历史阶段。旧`activation.coreSwitch=false`/`voltageScheduling=false`、不启动还原/不自动重放保持；CCD是按Windows L3域的进程亲和性，不是物理断电。HC CPU/GPU声明仍为来源资料。

单一入口仍为`uxtu-tdp-policy.json`，所有14个familyRecipes必须提供`co_commands=[{transport,command}]`；不支持CO的四条为空。`coall/co_transport`保留既有首选，`co_commands[0]`严格相符，其余同名上游候选依来源顺序补齐。10条source-declared CO配方共15个候选；其中五条补第二候选：FT6 new/new-generic/old的MP1 `0x4C`→RSMU `0x5D`，AM4v2的MP1 `0x36`→RSMU `0x0B`，AM5保留MP1 `0x36`→RSMU `0x07`。AM5固定上游原列RSMU07在前，YMCC有意保留工作首选，不悄悄倒序。FP10/AM5为独立HSMP `0x08`、u16，其余可用全核CO为u20；不能当mV、GPU/SA或per-core消费。

来源是固定UXTU26.3.1 `RyzenSmu.cs`（SHA256 93141b2250bac12d3092a8623e2d634b982cd02eff3a86847bb2581d38c3141a），归档与库内快照相同。原Execute650–669对非OK继续候选，并非只UNKNOWN；另本地无效mailbox可合成FE。YMCC仅允许当前请求有明确typed `origin=firmware_response`、`mailboxStatus` 属于 `[0xFE,0xFF]`，且底层返回值与该固件状态一致后尝试下一条。`0xFF`在上游也可能由transport/mutex合成，裸值绝不能授权推进；成功即止，BUSY/PREREQ、IO/timeout/写入未知都不重放，每候选最多一次且每次用原参数，仅适用于已准入的static all-core同值setter。`voltageOffset.amd.candidateSelection.retryOnly.mailboxStatuses`是生成并由消费端读取的唯一推进规则；这是固定上游全失败继续中的受限子集，不证明FAILED没有中间副作用或型号硬件支持，保留原身份/代次/停止/未知门。

`validateUpstreamRecipes`校验完整有序候选、原primary、禁用空数组、transport与source branch，不再仅判断有一条`some()`。定向生成器把同一数据送入helper标记区、TDP域元数据、native头与TS手动flag；不维护第二张CPU型号/机型白名单。生成只拥有helper标记区，须保留原消费者非生成区原字节，并与其写者协调。

Intel既有core/cache消费从同一`voltageOffset.intel.protocol`读取MSR `0x150`、domain 0/2、SET `0x11`、1.024缩放、11bit/shift21及成功写入后100ms等待；固定26.3.1 `Intel_Management.cs`139–164提供直接写来源，round保留最近整数正中取偶数及原浮点运算顺序。该路径只证明PawnIO传输写入成功，不加写后GET或固件完成条件。现有GET `0x10`、轮询和完成码属于YMCC既有独立消费，指定UXTU固定源依据UNKNOWN；`readback=true`表示可探测能力，不表示SET回执有读回或已物理生效。GPU/SA仍不准入。

CPU来源命令存在≠固件接受≠写后可读/物理生效；AMD CO仍readback=false。此补齐不能证明它是本次ROG/Z2E失败根因，也不能证明任一Z2E固件支持CO。软件、实际helper/模块/冻结包身份及真机效果分别给证据，旧错误/未知保留。原消费者和正常配套导出负责后两层，不因库生成成功标硬件PASS。
<!-- /AI-NAV: CPU-MANUAL-CO-CANDIDATES-20261103 -->

# TDP 接回 YMCC 机型能力总库

<!-- AI-NAV: TDP-TOTAL-LIBRARY — 先读本说明，再查 tdp 执行域、performance 资料域和原消费者；此文件不参与运行分配。 -->

更新：2026-10-31。用户要求“TDP调度开始升级，我要求接回机型能力总库，你负责核对和整理”。该要求覆盖 2026-10-19 的“TDP接线暂不纳入”约定；HC CPU/GPU 来源资料、未启用的核心开关/降压/GPU 功能继续保持原边界。

**唯一能力入口仍为本目录。当前为源码接入与核对中，尚未取得本次升级完整的消费者、交付和硬件验收。** `uxtu-tdp-policy.json` 是总库中的 TDP 策略输入，不是第二个机型库。现有调度器、native 和 helper 继续负责执行。

## 阅读与修改顺序

1. [ACTIVE-INTEGRATION-SCOPE.json](ACTIVE-INTEGRATION-SCOPE.json) 的 `performance.tdp` / `tdpIntegration`：本次明确授权与仍禁止自动激活的功能。
2. [USER-SCOPE-DECISIONS.json](USER-SCOPE-DECISIONS.json) 的 `performance.tdp-local-uxtu-library`：授权来源。
3. [uxtu-tdp-policy.json](uxtu-tdp-policy.json)：固定本地 UXTU 来源、CPU 配方、OEM 路线、模块身份与应用请求范围。
4. [hc-device-catalog.json](hc-device-catalog.json) 的 `devices` / `domains.tdp`，以及 [hc-performance-metadata.json](hc-performance-metadata.json)：同一机型身份和来源事实；HC 数值不能直接用作当前限制。
5. [EXECUTION-CHAINS.json](EXECUTION-CHAINS.json) / [INTEGRATION-STATUS.json](INTEGRATION-STATUS.json) 的 `tdp`：实际入口、待验事项及既有测试引用；`performance` 保留HC CPU/GPU资料边界。

本库先前的性能元数据有 92 个冻结源码类，80 个 factory 类，17 类带精确 CPU 名称分支证据，2 类仅有 CPU 条件，73 类未明确 CPU 名称。这些是源码统计，不能解释成支持清单或实机数量。维护分类当前为 10 个域（原 9 域加 TDP），已启用能力为 9 类；2026-10-20 数量文件仍是历史快照。本次策略当前有 13 个配方条目（包含未知/拒绝项）及 1 条已有 OEM 路线；13 不是 CPU 型号或掌机数量。新增上游机型仍走同库原升级流程，不能从这些数量推断全部 TDP 适配完成。

## 数据和执行职责

| 内容 | 唯一来源／既有入口 | 消费规则 |
|---|---|---|
| 机型、别名、继承、来源身份 | 原总库设备表（仅诊断/来源） | 通用 TDP 不按机型准入或限幅；descriptor 的 modelId/modelRegistered 只作诊断，不新建 TDP 专属 DMI 匹配库 |
| 实际 CPU 身份与兼容配方 | 现有 native CPU 身份和 helper `detect_amd_family`，策略提供配方数据 | 通用 TDP 以 vendor/family/model/CPUID 与固定 UXTU 配方为准；未知 CPU 不猜写；相同 CPU 不因机型登记状态改变能力 |
| 应用请求范围、已有 OEM 传输条件 | `uxtu-tdp-policy.json`，生成的 `TdpCapabilityCatalog.generated.h` | 2–200W 是应用请求域；G3E 既有通道最小 8W。200W 均不是已确认的固件最大值；MS-1T91 板名只是既有 OEM 传输条件，不是 CPU 白名单 |
| 页面、配置、热键、开机/唤醒与调度目标 | `src/bridge/yeman.ts` 的 `getTdpExecutionCapability` / `resolveTdpTarget` / `setTdp`，现有 `performanceSchedule.ts` | 复用同一描述符和目标计算；保留保存意图、实际下发与读回的区别 |
| native 特定通道 | `native/main.cpp` 的 `tdpExecutionCapability` / `msiG3eTdpSet` | 通用能力与 CPU 就绪来自 CPU；板名只选择既有 OEM 传输；保留现有真实身份、generation/deadline、范围、所有权及读回检查 |
| 默认 CLI/daemon 通道 | `PowerControl/pawnio/YeManTdpCtl.py` 的 `apply_tdp` 及既有请求入口 | 同策略生成 CPU 配方，仍由原函数执行；未知/未验证配方不能因登记而可写 |
| 退出、睡眠、恢复和失败 | 原 native/前端/helper 生命周期与操作回执 | 不缓存写入权限；不以配置保存、启动成功或进程退出 0 代替硬件结果；不在不确定写入后自动换通道 |

G3E 策略的 `exactNormalizedBoard` 名称沿用已有声明；实际 native 检查的是启动机型身份中标准化后的 `product` 是否为 `MS-1T91`，并保留独立 CPU 条件。字段名称不构成对其他 DMI 字段或 MSI 家族的授权。

`hc-performance-metadata.json` 继续为 `reference-metadata-only`。其中 `tdp.route=existing-independent-consumers` 描述执行引擎职责；新增 `catalogBinding` / `sharedCapabilityPolicy` 才说明能力声明已接回同库。两者同时保留：**统一能力来源，沿用现有执行引擎**。HC 的 nTDP/cTDP/CpuClock/GfxClock 是原始局部声明，不能直接套用到当前设置；GPU 实际身份不能从 CPU 营销名称推断。

## 生成与后续升级

修改前先核对固定的本地 UXTU 源码与模块 SHA256；版本名称不是完整身份。策略变动先审来源和命令含义，再由 `tools/hc_capability_catalog_generate.mjs` 更新同总库派生物。`tools/tdp_capability_catalog_generate.mjs` 是同一份输入的定向入口，用于 TDP 派生物及 `--check`；不能维护另一套配方。

派生物包括 `catalog.domains.tdp`、native TDP 头、现有 helper 的生成数据段以及性能资料中的绑定引用。人工不直接改生成段；两个生成入口必须保持一致。产品构建/制包的来源和资产门仍复用原入口。

静态配方在初始化/身份变化时绑定，重复调度沿用原消费者；运行时不读本说明、AI-INDEX 或 OVERVIEW，也不增加每次调度的跨进程能力查询。新鲜度、代次、所有权、deadline 和失败状态继续实时判断。局部开销、实际完成下发数量、完整链路开销分开记录，当前没有整程序 CPU 收益百分比。

## 本次核对结果与未完成项

已观察到升级任务建立了上述策略、生成器和 native/helper 接回代码；本聊天负责总库范围、索引、消费路线和维护计划的核对。源码已出现调用不等于通过验收。

- 总库维护计划原先把 `performance` 域视为资料域。本次仅允许带明确用户决定及完整边界的 `performance.tdp` 在同总库的 `tdp` 执行域进入维护计划；原核心/降压/GPU 限制继续有效。
- 原生命周期所有权目录只覆盖先前 8 类能力，而且绑定具体源码/范围字节。新增 TDP 与消费者改动后须重新审查对应绑定；旧回执不得改签。TDP 生命周期分类和软件验收仍待升级任务提供，不能自动补成通过。
- helper 源码、PyInstaller 成品、所用 RyzenSMU/IntelMSR 模块、发布资产锁及运行清单必须一致。只改 `.py` 或编译通过不能证明交付包已消费新配方。
- 原 TDP 测试引用为 `performance.tdp-protocol-consumer`、`performance.tdp-frontend-consumer`、`performance.tdp-schedule-consumer`，按实际改动与原依赖选择；这里没有新增全局测试 ID，也没有扩大默认最小集。
- 验收须覆盖真实提取消费者的正例、未知 CPU/机型、命令拒绝、CLI/daemon/native 路线一致性、失败回执、停止归还与睡眠恢复。涉及模块 ABI、交付来源时继续原身份门。本文只定义核对点，不宣称现有任一测试覆盖全部项目。
- 保留原错误与未覆盖结果。硬件操作、安装部署和实际功耗/CPU 性能验收本次未执行。

本轮核对保存了以下结果：7/7 份固定本地源码身份匹配；最新定向生成检查为 CURRENT，且检查前后输入身份一致；按原制包规则选中的 helper EXE、RyzenSMU、IntelMSR 三项均匹配资产锁。此前生成检查曾因 AM5 命令/传输映射不一致而拒绝，原日志保留，最新结果另存。以上不代替本次实际消费者、helper 源码到 EXE 构建来源、生命周期或硬件验收。TDP 所有权查询仍为 UNKNOWN / UNCLASSIFIED_CAPABILITY。

本轮维护检查原件在 `G:/YeManCC-Work/Testing/YMCC/Evidence/HC-Code-Capability-Upgrade-20261031/tdp-review`。检查结论只对其记录的来源身份和范围成立；升级任务仍在修改产品源码时，不能把该维护检查当成最终当前版本验收。

## TraeCode CN 执行记录（r004，CPU-only，待 Codex 审核）

2026-10-31 用户纠偏：**TDP 只按 CPU 适配，不按机型准入**。本轮的结论与改动：

- **通用 TDP 未发现机型门。** `native/main.cpp::tdpExecutionCapability` 的 `selected` 只选择既有 MSI `MS-1T91` OEM 传输；`ready` 在该传输上只依赖 CPU 身份（`tdpIsG3eCpu`），其它板卡默认走 `default` provider，请求域固定 2–200W，与 DMI/品牌/机型登记无关。前端 `resolveTdpTarget` 只用 `capability.ready/minPL1/maxPL1/selected`，`modelId/modelRegistered` 仅诊断；helper 只按 CPUID family/model 路由。因此保留代码，不制造“移除机型门”的改动。
- 生成元数据 `domains.tdp.modelBinding.source` 改为 `diagnostics-only; not capability, admission or watt limits`，并移除 `handheldCoverage` 引用；`hc-performance-metadata.json` 的 `tdp` 绑定仍为只读资料。
- 机型和 factory 覆盖不再作为测试验收入口：`tools/tdp_capability_catalog_selftest.py` 删除相应强制校验，新增 CPU-only 元数据检查，并在实际 native 消费者夹具中新增两项行为证明——同 CPU + 同板卡下，已登记别名与未登记 modelClass 的通用能力字段完全一致；MSI 板上的 CPU 就绪与 `modelRegistered` 无关。
- `capabilities/uxtu-tdp-handheld-coverage.json` 保持 **历史/已撤销** 标记（`superseded`），仅作资料来源记录，不再被元数据、运行时消费者或测试引用，也不再扩建。
- 既有 MSI OEM 传输仍按其设备/协议条件验证，不当作 CPU 通用白名单；正常成功通道、失败回执与不跨通道重放语义未改。
- 复用原消费者：`performance.tdp-catalog-consumer` 本轮实跑 PASS（20 项）；`performance.tdp-protocol-consumer`、`performance.tdp-frontend-consumer` 的旧回执按原源范围保留；`performance.tdp-schedule-consumer` 仍 FAIL 并保留原失败。未知 CPU 拒绝由既有 protocol 回执覆盖，未加强或放宽断言。
- 核心/电压仍 false；模块/EXE/安装、源码到 EXE、TDP 所有权与 AYA2S 现场效果继续 NOT_RUN/待处理。

审核结论：`Codex/reviews/0004/REVIEW.json`（SHA256 `2d4db586…f9b1`）`ACCEPTED_WITH_DEFERRED_ITEMS`，接受当前 CPU-only 软件范围；本有界任务停止，后续生效逻辑/激活/部署需用户另行授权。

执行升级的现有聊天：[评估调优能力库最小升级与TDP修复](codex://threads/01a18e11-5454-72c3-82c2-4add8d8b6179)。调度实现、模块与交付资产由该任务维护；本说明与 tdp 域记录作为共享核对入口；performance 域继续维护HC CPU/GPU来源资料。
