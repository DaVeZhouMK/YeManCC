# 虚拟手柄执行与 YMCC 输出屏蔽维护入口

先读同目录`virtual_controller_runtime_contract.h`：启用和YMCC输出屏蔽共用一条时序，四个定义模块同时包含该契约，并在编译时校对30秒/10秒/单次保底约束。

本文件是不可执行的源码旁说明。实际行为以生产函数及 `virtual_start_protection.h` 的 `VC-LIFECYCLE-20261020-R3` 为准。不要增加第二套状态机。

## OneX 普通开启接入（2026-11-05）

软件检查点（此前冻结版本，非本次112441修补验收）：原定向40场景／927断言、forbiddenIo0；原实际日志writer161检查／23来源检查通过。完整原生编译、596项编译来源／20项生成输入、12个OneX核心正文和9组实际托管ON→OFF矩阵已核对；实机单手柄、实际内部端口可识别性、驱动／Steam消费与整包验收仍UNKNOWN。当前回执：G:/YeManCC-Work/Testing/YMCC/Evidence/VC-OneXFamily-20261105/VALIDATION.json。

本次112441／HC对齐检查点：原family-lifecycle --family onex完整47场景／1259断言通过，含15组实际私有SDK-free托管开启→关闭及1组截止负例；本轮完整Native夹具明确使用Od功能编译，默认O2及正常产品构建未改，不据此证明优化后性能／等价或硬件。端口external／unknown原ID实际只改变connector metadata，现按精确选中且自有源执行一次刷新并核新鲜返回；其他根拒绝／未知回执不重放负例保留。独立初始selected=A而journal=B未覆盖，真实过滤／Steam单手柄与OFF实体归还仍待现场。原三份依赖编译FAIL及累计费用保留，不退款；回执为G:/YeManCC-Work/Testing/YMCC/Evidence/VC-OneXFamily-20261105/field-112441/repair-R2/VALIDATION.json。

OneX 子步骤在 `native/virtual_controller_onex_start_runtime.h`，位于原 Start 的 HELLO／隐藏之后、PREPARE 之前，仍使用 R3 原 ticket 和 30 秒绝对截止。能力入口为 `capabilities/HcOneXVirtualControllerCapability.h`：沿用 14 个旧目录具体类，并从已审固定源码登记 X2、X2 Mini Pro 和 OneXPlayer3 的三个 source-only 身份，不冒充 DLL factory 类。型号只给身份，不代替当轮已选且归属明确的 USB 根、实际游戏源和槽位反查。

现场 Apex 已观测到 `045E:028E` 游戏源。112441现场报告connector可外接，而精确源已被选择并有完整隐藏责任；固定HC f2e58468的SetTargetController→Hide(true)并无每层USB口内置前提。本轮VC-ONEX-112441-R2将connector分类仅作诊断，刷新权限来自原请求已选的唯一物理游戏源、精确根／稳定位置、槽位反查和原隐藏责任；不把045E:028E或portKind=1/unknown改称内置。刷新返回后，重新发现同根源、核对当前代次的新鲜标准样本，再精确补隐该返回源。未知派发不重发；查询和补隐不转CLI，connect/write/read共用原截止。未选中／归属不明／换根及其他实际游戏ID保留原通用输出；不遍历刷新外接或上游hub，不因该可选步骤不可用封禁整项功能。共用external-priority分类策略保持原语义。

这段属于正常执行，不使用共用自愈预算，也不扩展 OneX 原生 OFF 屏蔽权限或更改玩家选择。背键、体感继续使用既有能力绑定。源码登记／生成、完整原生夹具、托管 pipe／目标生命周期和实机单手柄观察分别核收；详见 `Testing/YMCC/Evidence/VC-OneXFamily-20261105` 的当前交付，未有实机证据时不宣称外部单手柄通过。

## 当前验证与交付状态（2026-10-22）

生命周期代码契约仍为 `VC-LIFECYCLE-20261020-R3`；R4～R8 是交付/候选批次，版本号不能混用。唯一任务总记录在工作区 `Docs/Tasks/Gamepad/GP-虚拟手柄生命周期复核与CPU优化边界-20261019.md` 及同名 JSON。

- **R8 候选：已构建，尚未放行。** 冻结源码在 `Build/Exports/20261022-VC-COLD-RETURN-R8/Mainline/YeManCC-source/YeManCC`。包含共同 PnP 判空分层修复及环境错误提示修正；正式消费者回归、Build/Package/Verify 和 ZIP 602 文件一致性通过。完整 UI 门因 Settings 的 Steam 诊断真实挂载缺失有 10 项失败，等待确认移除意图。没有 R8 Deliverables/测试启动器。报告及逐项结果在 `Build/Validation/VC-COLD-RETURN-R8-20261022/REPORT.zh-CN.md` 和 `INTEGRATION-RESULT.json`。
- **最近已交付 R7：历史测试基线。** 工作区 `Deliverables/YMCC-Virtual-HIDMaestro200-20261022-R7/DELIVERY-VERIFIED.json` 绑定其源码、载荷与软件测试；完整说明在 `Build/Validation/VC-LATEHIDE-INFLIGHT-20261022/DELIVERY-R7-REPORT.zh-CN.md`。R7 不包含上述 R8 后续修复，历史 PASS 不能用于签当前主线。
- **聚焦×故障重建补测：软件范围通过。** 主线新增正式场景 `controller.focus-recovery`，既有 `--suite all` 同时执行；实际 `inputCaptureOnGamepad` 与专属管道断连覆盖两时点，2/78通过。单一临时中性化回退命中6项预期失败。原R8 ZIP保持冻结，本补测不是新交付；cached owner/XInput与SDK/PnP末端仍为假件。证据在 `Build/Validation/VC-CURRENT-WIRING-20261022/INTEGRATION-RESULT.json` 及 `REPORT.zh-CN.md`。
- **R9冻结复核：手柄及UI通过，整包尚未生成。** `Build/Validation/VC-SIGNAL-DELIVERY-20261022/REPORT.zh-CN.md` 与 `RESULT.json` 为最新交付检查。19:25冻结字节的focus-recovery 2/78、full-pipe 105/6457通过，459个native/21项托管来源一致；当前及冻结UI原门254状态/19400方向/484动作通过，旧Settings10失败不再是当前阻挡。完整发布链停在必需`plugins.decky-persistent-lifecycle`的`native.stalled-binding-stop-requires-fixture-release`：3200ms截止后仍有未结束callback资源，门真实FAIL，未制包/发启动器。仅修虚拟手柄夹具，不修改该插件、不跳过门；独立编译组件不是完整测试版。新故障的原始global证据在`Build/VC9/2026-10-22T11-54-02-556Z-d04f3315`。原R8历史失败照留；后续从这条新回执进入，不重复旧Settings确认或把ACK判成物理验收。
- **174243 G3E 新实机证据：输出异常仍未解决。** `Build/Validation/VC-FIELD-174243-20261022/` 证明最新 PID8224 冷归还和同一 SD Host 的唤醒后本地提交有进展；包内前段仍有归还/开启失败。用户明确补充“一直有异常输出”。Core2.0已绑定，整个运行包身份未精确对应R6/R7/R8；不要把这包称为旧包，也不要用ACK/fresh-source/success判定最终输入正确。17:33唤醒后约18秒存在数千条/秒提交的观察，根因未定。
- **持续信号取证：主线源码与软件验证已接，尚未整包交付。** `Build/Validation/VC-SIGNAL-EVIDENCE-20261022/REPORT.zh-CN.md` 为最新新增证据入口：MSI原始首64条后增加受详细门控的5秒持续样本与独立边界保留；`virtual_signal_evidence_runtime.h` 记录真实序列化请求/最终帧，InputHost的`HostVirtualSignalEvidence.cs`覆盖SD最终64字节及Sony/Elite调用实参。普通导出收集`input-host-signal.log/.prev`，完整tuple＋wire tick/sample才关联，SDK返回/参数hash仍不证明driver/game消费；SDK内部idle/touch是明确缺口。它们仅被动取证，不拥有设备、线程或timer，不改变R3准入/预算/屏蔽；不能从新增日志版本推断已经修复实机故障。当前交付与生命周期复跑以`Build/Validation/VC-SIGNAL-DELIVERY-20261022/`新回执为准；没有新整包放行回执时，不把独立组件构建当作安装包。
- **物理屏蔽侧补证：源码及软件验证完成，未整包交付。** `Build/Validation/VC-PHYSICAL-SIGNAL-EVIDENCE-20261022/`复核174243包：隐藏配置之后既有独立观察器仍收到1902主报告，最新会话共26256份；没有逐份raw语义及唤醒后连续覆盖，不能认定Steam泄漏或phantom。既有WM_INPUT主游戏collection新增按描述符解析、全observer每500ms最多一次、跨代际预算与首/近两条有界见证；vendor/consumer不解码，无原始字节/设备写入/新增轮询。既有raw入口3×51、decoder2×187、sampler2×30及2个指定负例、offline61、实际producer→consumer和probe编译自测通过；wake-1/2文件纳入同一离线审计。R3/屏蔽控制未改，完整包仍受上述Decky门阻挡，实机故障未解决。
- **204536新包与wake诊断接线补正。** `Build/Validation/VC-FIELD-204536-20261022/`记录最新6300同一Host11944/epoch1、启动183份物理主报告、gen3的115个空窗、gen2请求迟到拒绝；无新semantic marker，异常仍unknown。最新4973次短突发有DInput读佐证、Host未同幅增长，不判timer泄漏，也不替174243持续风暴结案。另在实际恢复函数resolved generation之后补MSI诊断receipt，保留早期receipt，避免Ready无Suspend/S4只记旧代际；真实函数/桥/Policy 79检查和指定删除反例、475输入主TU语法检查通过，已并入既有raw取证入口。详见`Build/Validation/VC-OBSERVER-WAKE-WIRING-20261022/`；R3/控制无变化，非本次迟到根因证明，未整包/实机验收。
- **WinMM高频来源补证与统计竞争修复。** `Build/Validation/VC-TIMER-EVIDENCE-20261022/`绑定实际callback timerId/event/thread/power及原create/4处kill回执；固定first/latest异常Snapshot经普通manifest `inputTimerEvidenceVersion=1`导出。busySkip锁外写/窗口读清已改原子收集，剥除诊断后的原控制文本一致；R3/节拍/取消flags未改。既有raw入口联合、actual callback/atomic/export 682+指定负例、Recorder两构建11场景、原signal2×37、resume79、当前477输入TU语法通过。owner绑定/排空/消费仍unknown，WinMM-only，未整包/实机验收。
- **实机与 CPU：待验收。** 同版 G3E 切换、退出重开、强退重启、连续睡眠及游戏消费，以及 ROG/GPD/Win5 和整应用 CPU 数据尚未全部证明。IRP 保留，WSK 模拟无总体收益未导入。

以下按批次保留历史事实；后续维护从上面当前状态及唯一任务总记录进入，不以旧批次的“当前”字样推断最新源码。

## R6 已冻结软件交付（2026-10-22）

入口为工作区`Deliverables/YMCC-Virtual-HIDMaestro200-20261022-R6/Start-Virtual-HIDMaestro200-R6-Test.cmd`。SDK仍为官方2.0原字节。R6加入下文的MSI队列/pending原子交接，软件回归、实际UI与600项ZIP核对通过；报告`Build/Validation/VC-LATEHIDE-PUBLICATION-20261022/DELIVERY-R6-REPORT.zh-CN.md`。源码/包绑定在同目录`export-r6-proof/final-01/`，global场景20/82及当前编译回退6/2/2证据在`VC-LATEHIDE-GLOBAL-20261022/frozen-r6`。全局目录未全覆盖，实机/其它家族/CPU仍待；原R3契约未更换。R5冻结保留，不含这项后续发布顺序修正。

## R5固定软件交付（2026-10-22完成）

R5测试入口为工作区 `Deliverables/YMCC-Virtual-HIDMaestro200-20261021-R5/Start-Virtual-HIDMaestro200-R5-Test.cmd`。同目录有完整ZIP及 `DELIVERY-VERIFIED.json`，源码/包绑定回执在 `Build/Validation/VC-LATEHIDE-20261021/export-r5-env-proof/final-02/`，总报告为 `DELIVERY-R5-REPORT.zh-CN.md`。R5含本文件下文的MSI late-hide默认pending防护与中性环境提示；未知清单/版本不阻断实际尝试，真实后端故障独立显示。原30秒/10秒/单保底/唯一终态契约不变，不另建生命周期。已过软件回归、两比例UI与600项ZIP核对；实机、其它家族与CPU测量仍待。R4原包与原失败冻结保留。

## R4已冻结交付与证据入口（2026-10-21）

R4 批次交付的完整测试版是 `YMCC-Virtual-HIDMaestro200-20261021-R4`，生命周期版本仍为 `VC-LIFECYCLE-20261020-R3`，SDK为官方原字节2.0。R4只是交付批次，不是第二套生命周期。总任务记录为工作区 `Docs/Tasks/Gamepad/GP-虚拟手柄生命周期复核与CPU优化边界-20261019.md` 和同名JSON。

- 当前需求表：工作区 `Build/Validation/VC-RETURN-20261021/acceptance-audit-r4/RESULT.json`，22项逐条区分软件回归与物理验收。表中的待实机项不能据离线PASS删除。
- 当前交付：`Build/Validation/VC-RETURN-20261021/DELIVERY-R4-RESULT.json`。完整Build/Package/Verify和实际600项ZIP内容核对已过；早期构建阻塞是历史状态。
- 完整实际消费者：同证据目录 `all15-env-return200/RESULT.json` 与 `source-manifest.json`；15模式含13正向和2预期破坏对照、435个实际编译输入。新鲜源/Host ACK只证明本地链，不能代替游戏消费。
- 包绑定：同目录 `export-r4-proof/final-with-setup-proof/`。初始化器独立路径重编的二进制差异、追加IL语义及纯自测证据均保留，不能仅用manifest自洽放行。
- 以下按日期/有限批次排列的补充记录保留当时事实；出现“尚未接线/未导出/候选”时，应结合上面当前需求表和实际源码核对，不得将历史状态当作当前缺口，也不得倒写历史失败。

本轮微星执行修复定位：`main.cpp::msiDinputRouteWindowDecideForWake`只在合法当前MSI ON原30秒预算中续接已有源观察；`msi_owned_return_runtime.h`保存真实ON terminal outcome，由OFF接续同一归还责任。未知/已尝试写入不能获得额外retry，不能通过退还terminal门重开一轮。正式夹具分别是 `tools/msi_dinput_route_lifecycle_selftest.mjs` 与 `tools/verify-msi-owned-return-runtime.mjs`；不要为迟到源另建生命周期。

公共环境检查属于 `GamepadPrerequisites/HidMaestroPackagePreparation.cs` 及原初始化器。完整当前/更新驱动供给、未知额外版本、自动维护权限和实际target/input分别判定；未知额外版本保留行且禁止自动维护，不冒充真实设备已可用。复测用已登记的全局 `controller.prerequisite-environment`。USBIP证据在 `InputHost/UsbipSetupEvidence.cs` 和既有诊断锁/普通导出中，内嵌版本、注册绑定、磁盘SYS与内存实际加载不能互相替代。

环境提示契约（2026-10-21补充）：SDK随包身份校验、系统驱动版本、Windows清单格式、自动维护许可及真实虚拟输出分别判定。已识别的完整新版本保留，不按旧版本号降级；未知清单/版本只表示环境检查未确认，禁止据此强制重装、卸载或阻断实际目标尝试。界面对此使用中性提示；真实后端创建/输出失败及已确认的安装器缺失或损坏仍单独报告。升级后重跑 `controller.prerequisite-environment` 的当前/未来版本/混存/未知格式及编译破坏对照，不能用“写入成功”替代输入证据。

本轮MSI late-hide定向修复仅对已登记且DInput已接管的MSI启用pending防护：空任务不能覆盖completed episode，也不能以token0清理新任务。真实worker回归在 `Build/Validation/VC-LATEHIDE-20261021`，旧1→2→3次隐藏对照修复后1→1→1；这是重复隐藏事务修复，不是全部历史乱码或CPU降幅的证明。R4冻结包不含这项后续修复，该修复现已通过R5最终软件导出验证；实机状态仍待本轮日志。

R4物理旧卡死恢复、关闭后实体归还、强制重启、连续睡眠、游戏消费以及ROG/GPD/Win5同版本证据仍待实测。CPU整机验收后置；源文件拆分和离线测试耗时不是CPU下降证明。共享主线其他任务的后续改动，不得借R4的固定回执冒签，也不要为外部改动反复重出本包。

前端调用方的保护/冲突/取消回执回归已登记 `controller.input-entry-lifecycle`，入口 `tools/controller_input_entry_lifecycle_selftest.mjs`。它执行正式镜像/overlay、配置仓库、API和IPC，原生保护回复是内存端点；不代替原生all15或实机验收。当前13场景58检查与三种编译破坏对照通过，源码与R4产品逐字一致，未修改R4。

## R5之后的源码修正：MSI任务元数据与出队发布（2026-10-22，已随R6交付；R5保持原样）

`inputHostLateHideEnqueueEpisode`原来先发布队列通知，再调用`inputHostLateHidePendingCommit`；BUS回调与生命周期owner属于不同线程。真实双线程夹具在既有只读单调时钟边界暂停生产者，原实现出现“accepted、pending=true，但queueDepth=0/requested=false”；后续另一生产者才救回。此为源码调度证据，未将其归因于现场日志。证据在工作区`Build/Validation/VC-LATEHIDE-PUBLICATION-20261022`。

当前仅已登记且armed的MSI传入栈上`InputHostLateHidePublication`，由同一个`inputHostQueueLifecycleWorkInternal`在既有队列锁内提交pending。队列锁保护到元数据提交完成，owner不能先读空；锁内只有metadata拷贝，不含日志、pipe、驱动或设备I/O。锁序为queue→late-hide-state；其它调用的默认nullptr路径保持原顺序。没有新增生命周期、重试或轮询，R3时间预算及唯一终态不变。

定向CLI为`node tools/input_coordinator_lifecycle_selftest.mjs --out <新绝对目录> --suite late-hide-publication`；`--product-before <带MANIFEST的原生产代码目录>`要求指定两条旧失败。`late-hide-current`同一编译候选分别执行pending和publication两种实际消费者，不共享case状态。全局场景`controller.msi-late-hide-pending`将这组交接与重复隐藏护栏纳入full回归。R5冻结包只包含前一轮空pending/token0修复，不含本次新增发布顺序修正。

## 呼出隔离设置

控制器页「呼出隔离」保存于 `input.ownership.frontendIsolationEnabled`，旧配置缺失/类型无效时默认开启，明确 `false` 时放行 YMCC 主窗口前景期间的手柄输出。`gamepadUiInputEligible` 继续只负责导航资格；`gamepadUiOutputIsolationEligible` 统一负责可关闭的主窗口输出隔离，独立轮盘/子窗口交接仍使用原所有权保护。

同一个设置同时接到最终虚拟输出与本机手柄的前景隐藏入口。设置变化只发布原子状态并排入现有 native lease 归还工作，不在设置回调中操作驱动、不切换虚拟类型；无源、物理借用、切换、退出保护不受该设置覆盖。开机在旧设置目录迁移后、窗口创建前加载，热路径不新增读盘或设备扫描。关闭设置时原 native 隐藏项由原生命周期 owner 完成归还，无法证实归还时继续保留原债务。

## 修改位置

| 要改的行为 | 实际生产入口 | 责任 |
| --- | --- | --- |
| 虚拟目标准入、物理 Steam Deck 自模拟例外、已绑定目标分类 | `virtual_controller_capability_runtime.h` | 消费现有机型身份、persona及MSI能力权威；不探测硬件、不另建机型库 |
| 开启、旧 Host 清理、新 Host 准备和输入源接纳 | `virtual_controller_start_runtime.h` / `inputHostStart` | 保留原30秒请求预算、原顺序及真实回执 |
| 关闭、归还隐藏和物理模式、退出补偿 | `virtual_controller_stop_runtime.h` / `inputHostStopLocked`、`inputHostStop` | 唯一生命周期owner串行执行；未证明归还保留债务 |
| YMCC及其已有子页/轮盘操作时，屏蔽最终虚拟手柄输出 | `virtual_controller_ui_output_runtime.h` / `focusIsolationPlanTick`及`*UiOwnsInput` | 保留物理采集和YMCC导航；只使最终虚拟报告中性化 |
| 初次开启/更换目标的排队身份 | `virtual_controller_queued_start_runtime.h` / `inputHostQueueLifecycleWorkInternal`、`inputHostRunQueuedLifecycleWork` | 保留原请求、目标、电源代际和截止；过期项不得借新请求启动；旧关闭项不得升格为开启 |
| PS输出旧端点归还 | `ps_endpoint_ownership.h` / `ps_endpoint_return_runtime.h`及既有quarantine owner | 先保留已接受的旧SDK root再撤运行态绑定；Present/Unknown保留责任，完整旧端点Absent才归还；未知root用输出类型的完整反证 |
| 既有延迟归还继续执行 | `virtual_switch_execution_return_runtime.h` | 执行层续接，不计入保底 |
| 强制退出后的历史责任 | `virtual_target_return_runtime.h`、`virtual_target_return_cold_execution.h`、`virtual_target_return_cold_runtime.h` | 写前持盘、精确旧writer/完整目标缺席、仅归还自有路径；同R3 continuation |
| 有效输入证明和10秒主动检查 | `virtual_switch_verification_runtime.h` | 不把预备成功、正常聚焦中性帧或已知暂停当故障 |
| 有限保底、tuple错误和正常唤醒 | `virtual_switch_recovery_runtime.h`、`virtual_tuple_recovery_runtime.h`、`virtual_wake_execution_runtime.h` | 保留请求/Host/电源/目标身份、原截止及共享预算 |
| OEM按键调用方的现有能力绑定缓存 | `main.cpp:oemUnifiedInputBindingFor` | 继续调用`capabilities/HcOemInputCapabilityBinding.h`的现有缓存；此次不改它 |

这些runtime头在main.cpp原定义位置引入，保留原翻译单元、静态状态、锁序和调用点。源文件拆分首先改善定位和修改，尚不是独立编译单元，也不能直接声称减少整机CPU或完整主文件编译时间。

## 能力总库边界

`capabilities/`仍是已有总库。机型身份继续读取`g_machineIdentity`；MSI权限继续调用`msiClawKnownBoardAuthority`；persona继续调用`controllerPersona`；不增加私有名单或用外接设备VID/PID代替机器身份。已接好的input、motion、feedback、configuration、OEM binding接口保持原状。能力声明不等于源已接通，也不能用缺陀螺仪或背键禁止整个虚拟目标。未知PC仍可按原规则尝试；物理Steam Deck禁止自模拟的现有例外保留。

`g_inputHostPrepareMayHaveReachedHost`只记录可能已发出的PREPARE创建责任：在现有请求的实际写入边界前置位，连接/期限拒绝且未写入不记债；由同一Host最终Clear清理。它不能证明准备成功或授权输出，但Stop必须把它作为可能尚存的目标，禁止在回执丢失且旧Host未退出时提前归还。

关闭执行和最终检查共用 `inputHostDisabledCleanupProvenLocked`：只按实际资源与归还债务证明完成；微星物理归还债务必须由合格回读解除。`StartBlocked` 是后续开启权限，不是尚欠的关闭资源，不靠清掉它伪造成功。

UI屏蔽用实际绑定persona和Host epoch，不用尚未执行的玩家请求代替。源、Host准备/中性回执、电源和现有写入门仍在原消费者实时复核；不能把这些活状态缓存成“机型能力”。不要把“YMCC有焦点”改成Stop/Start，也不要在此改OEM服务屏蔽或全局按键呼出。

## 联想家族接入边界

既有总库将 Lenovo SystemModel 分到 Tablet、Tablet2、SZ1、SZ2 四个具体类，共用基类与主板名不是独立型号。标准输入继续调用 `HcControllerInputCapabilityCatalog.bindStandardInput` 的当前 XInput provider，OEM/反馈/体感分别由原绑定消费者取得；motion-only vendor collection 不授予标准输入或隐藏权限，缺可选能力不禁止整个虚拟目标。

已解析联想家族在原 `hidHideApplyPhysicalTransaction` 中使用现有完整配置判据：新隐藏及 retained journal 返回成功前都确认非空完整目标、cloak、程序豁免和 inverse=off；逆向/未知/漏项不报配置已验证，不覆写第三方设置，不增加重试预算。retained journal 继续只代表应归还责任。标准读源与隐藏源的同 slot/path/container/代次是独立证明，配置通过不代替它。

当前 Lenovo native-OFF 前景隔离仍为 Unsupported；三种虚拟输出的公共呼出中性化不能转授 ROG root-cycle 或未登记设备写权限。GO S 与 Tablet vendor 协议、可拆左右 bank 和无线状态来源必须分别核对，修改与 motion/vendor 单写者协调。

## 唯一执行周期

玩家意图准入并锁定 → 旧目标退出与归还证明 → 建立目标/关闭完成 → 当前真实输入或完整关闭回执 → 执行检查 → 必要时有限保底 → 唯一最终结果。

最初请求的30秒是绝对上限；首次实际执行回执启动10秒主动检查，期间目标可用但按钮仍锁定。正常迟到信号和归还属于执行。一次请求只有一个最终成功或失败；重试不续预算。自愈复用执行层完成关闭再开启，不能并行另起目标；已知聚焦中性、录制、电源及外接优先暂停不触发保底。

## 验证入口

复用`tools/input_coordinator_lifecycle_selftest.mjs`的真实native TU + 私有managed Host管道夹具，和`tools/virtual_target_policy_native_selftest.mjs`。移动生产函数后，测试必须展开实际模块并绑定来源与生成内容；禁止复制算法做模型通过。保留原断言、原失败和故意撤掉生产逻辑的反例。定向检查先行，完整候选收口时运行相关回归；实际驱动/PnP、物理切换/睡眠和游戏消费另行实测。

持续记录：`Docs/Tasks/Gamepad/GP-虚拟手柄生命周期复核与CPU优化边界-20261019.md`及同名JSON。本文件只维护稳定的职责和接线索引，不堆每轮PASS历史。

聚焦模块现也收拢最终中性帧装配、DSU中性值、typed报告构造及权威提交身份复核。旧聚焦隔离原型的兼容状态/开关按原样保留，移动不代表重新启用；当前实际屏蔽入口仍为FocusPlan、各UiOwnsInput及最终完整中性帧。

收口正例可用同一夹具的`--managed-dll <已绑定私有Host> --suite all`：一次编译同一个EXE，分私有进程运行既有native各模式及完整pipe模式，保留原负例/断言。只复用本次同源候选，不复用旧源码EXE；定向生产故障回退仍各自编译。

状态发布使用现有R3快照：展示倒计时秒级，执行期限和回执日志仍保留精确值。只有同一有效活动请求的expiresAtUnixMs投影差异可在发布比较时忽略，其它字段、类型与未知成员均参与比较；无锁时保留原JSON比较。生产入口在virtual_output_transition_status.h的virtualOutputPublicationEquivalent，主线模拟入口为tools/virtual_output_transition_status_selftest.mjs及tools/virtual_output_publication_selftest.mjs。后者消费前者真实回执并运行字段/类型变更矩阵，不复制发布算法。

显式OFF/退出的旧visibility回收例外必须确认原Host退出，不能以玩家意图越过活着的旧目标。PS退役责任属于同一Host锁：正常完整RELEASE/SHUTDOWN/退出回执可证明端点等待已完成；异常分支用旧SDKroot的现有CM读取，不能以XInput空槽代替。物理归还失败时旧身份继续保留。已知旧root允许同类型外接peer继续在线；没有accepted旧root时，只能用完整输出类型消失反证，类型仍在或读取不完整就保留责任。新日志input-host-ps-return-proof按判决转场留存，无常驻新线程。普通native管道模拟器接管新头的全部6个SetupAPI/CM读取叶，未知API在编译前拒绝。


退出末段的 `hidHideSweepAllHiddenDevices("app-exit")` 也属于同一归还边界：先复用或尝试取得 Host 事务锁；活 Host、未证创建或 PS/MSI 归还责任未清时，不执行 dev-list、unhide、cloak-off。锁忙表示未知，立即推迟，不阻塞等待。后续归还只能消费实际owned journal/carried来源；全局driver名单不授予归还权。该门本身不代表冷启动OFF已归还。`STARTUP-RETURN-AUDIT96`保留原缺口；现在的持久producer及冷启动消费者见本文冷归还入口。每次仍须真实证明并持盘结清，不能以之后ON成功代替OFF归还。


## 启动来源证据99

`input_startup_diagnostics.h` 只格式化证据，不提供启动、归还或重试权限。`main.cpp:applyInputStartupOnce` 在既有设置事务里留存启动意图；`startupInputDiagnosticSnapshot` 在既有启动状态变化时复用当前 Host 锁，或只尝试取锁一次。锁忙输出未知/null；未取得锁不得读取 Host 字符串及 PS State。启动意图与上次异常退出标记在输入工作线程启动前固定，标记本身不证明设备已归还。

普通导出已有的 `native-lifecycle.log` 保留 `startup-input-state`、已接受的 `input-host-command-result` 和已有的 `ps-endpoint-ownership`。PREPARE 行保留既有 switch-reconcile detail 和回执 tuple；PS pin 要看随后真实 State.accept 消费后的同 tuple 事件，不能把原始回执 complete 当作接受成功。只提升已有观察的留存，不新增事件触发、周期设备清单、线程、定时器或慢查询。

新进程本地没有责任，表示当前进程没有载入旧责任；不表示前一进程的虚拟目标已经消失。99时公共PS跨进程旧owner尚无持久生产者/启动消费者；后续已由target-return记录及冷归还接线补入，旧报告只作原失败对照。当前仍须日志明确旧责任是否加载、证明及结清。MSI 和外接优先的持久责任继续各自消费；不能按同名手柄或同 VID/PID 正向认领旧目标。跨进程夹具是受控世界的证据，不等同于实机重启通过。


## 刷新责任记账100

`main.cpp:hidHideRefreshPhysicalHide`与初次Apply使用相同原则：当前快照未隐藏且本轮走到实际派发的路径，先保留可能hide责任，再执行原pipe/CLI顺序；失败/丢失回执不能删除该责任。当前已经隐藏但无原责任的路径不新增为自有；已有carried/borrowed责任保留。路径成员判断使用原大小写无关辅助函数。该修复不增加设备查询、不改返回结果或家族回读次数。

`tools/common_visibility_ownership_selftest.mjs`及同名fixtures是直接消费者回归，运行参数见夹具README。当前内存journal仍不是跨进程许可，尤其不能把旧版本留下的无来源条目整体转成持久所有权。重启基础模块由“虚拟手柄 (2)”按任务101独立实现codec/store；主线程接线时仍须覆盖初次hide、refresh、reconnect及carry/borrow真实producer。原Restore会调用全局清扫，冷归还不得无条件复用。


## 2026-10-21 CLI 子进程边界（有限105）

`main.cpp::hidHideRun` 是现有 HidHide CLI 的唯一入口。它在 `g_hidHideMx` 内保留 exact pending process/read-pipe 句柄；旧命令未证退出时，已有后续调用只做 Wait(0) 复核，拒绝新派发。终止请求、超时和父/Host 死亡均不作为已退出证明。没有新增后台线程、timer 或自建重试周期。

入口预算取原 R3 `g_inputHostSwitchRecoveryStartDeadline`、非零显式 deadline 和本次默认4秒的最早者。初次Apply、Refresh和归还等未传第四参数的调用也受原切换预算约束；不能借 CLI 重新计时。读到退出后还需重新确认管道为空，不能使用退出前的空读数放行。输出不完整、读失败或1MiB上限耗尽只返回失败。

永久实际消费者回归为 `tools/hidhide_cli_lifetime_selftest.mjs` 与同名 fixture，要求显式私有 `--out`、捕获 `--toolchain`；当前28场景/154检查每配置、5类编译反例。有限105同时通过原13模式、95管道/43切换/18跨轮次场景及默认native编译链接。101/103/104记录与文件会话尚未接生产hook，不能据此声称冷OFF自愈完成。


## 2026-10-21 目标责任写前记录与共同归还（有限109）

`virtual_target_return_runtime.h` 现在由实际 `inputHostRequestPreparedLocked` 所在main位置引入。仍使用唯一 `g_inputHostMx`、原R3请求和30秒截止；Session只管理数据与文件写者，不是第二套执行引擎。PREPARE/RECREATE到达真实可能wire边界前，先持久保留PID+birth、Host tuple、persona、epoch及targetMayExist；失败不发创建命令，文件IO后重新核对原身份和剩余预算。普通PC没有源、未知机型或缺可选能力不新增整目标禁用；是否可用继续由新鲜真实输入证明。

PS pin只在原 `State.accept` 后记为历史数据；不从磁盘回填当前准入。完整RELEASE/SHUTDOWN及exact旧Host退出后，只结清本runtime当轮创建的target-only origins，历史origin不被无关Stop消费。存储会话 `.owner` 只保护合作写者，同一实例仍由原Host锁串行；失败提交不发布candidate。加载记录和进程死亡都不授予物理归还权。

共同 `hidHideRestorePhysicalTransaction` 判据已收紧为virtual endpoint absent且既有ownedTargetsReturned；MSI额外ownedApplicationsReturned保留。ACK失败但路径确已移除可结清，ACK成功却still-hidden不得清journal；没有增加mutation。原nonMSI sweep和全局配置仍有独立边界，不能把这个函数原样作为新冷归还入口。

真实消费者夹具新增target_return_cases.inc；原13模式保留，100管道场景6217检查，105原7623断言原序保留；跳过reserve/pin/settlement三类编译反例分别命中159/2/5断言。共同归还永久入口为tools/common_visibility_return_selftest.mjs，11场景97检查每配置。普通基础日志事件input-host-target-return-record记录loaded/reserved/pin/settled及明确失败阶段，不进入逐帧路径。

finite109固定候选main为5ce0d885…，默认native与fixture371生产输入匹配。测试后共享Steam/Decky变更保留，current整主线未验收；当前52核心函数及可见性/CLI函数原字节一致另有作用域审计。所有被拒回执保留，未以窄范围PASS签当前整包。物理hide写前来源、CLI跨进程未完成责任及OFF开机冷接续仍待接线；后续修改继续在现有R3执行时序上完成。


## 2026-10-21 物理隐藏责任与归还（有限116）

Apply、Refresh、Reconnect 的真实隐藏入口在实际 driver/pipe 调用前调用 `inputHostTargetReturnBeforeHideLocked`。它要求当前 Host 生命周期锁、原请求/电源截止、选中物理源与 target 的有限 CM 祖先证明、当前 source/cache/topology 代际和写入者可用；只对事务前未隐藏、确实属于当前源的路径建立 exact origin/path debt。责任先持久化，随后才允许 hide。Carry/borrow 不创建新 origin，原 root/PID+birth和journal责任继续保留。

Restore 使用既有 exact owned path 回读；unknown、仍隐藏、旧CLI未结束、旧Host Alive/Unknown或当前传输故障均保留责任。完整不隐藏回读、端点归还条件和写入者结束证据齐备后，才消费单个path。ACK本身不等于物理归还，物理消费者和游戏信号仍是不同层。

physical-return-runtime111的真实小型夹具在Release/Checked各38场景158检查通过，包含实际Session/store/TransactionMutex/VirtualStartProtection/Refresh消费者；compiled controls分别捕获166/4/7条预期失败。source-prep115、原focused74/97和完整pipe/switch/tuple回归均通过。默认native116实际编译1590输入、373生产文件，包含物理path、process、record、store、session、runtime六个模块；原C4858/C4996警告保留。

该轮完成的是正常生命周期内的物理持责与归还判定。强制关闭后跨native CLI写入者的身份接续和OFF冷启动尚未接通，不能把当前记录文件当作归还授权；下一轮先验证跨进程旧CLI仍活的事实，再设计有限的cold consumer。


## 冷启动历史归还（2026-10-21 接线候选，未冒签实机）

`virtual_target_return_cold_execution.h` 是实际消费者算法，`virtual_target_return_cold_runtime.h`只接环境；前者由真实持盘Session的故障夹具直接调用。`Origin.cliWriterProtocol=1`仅由实际新producer写入，旧记录缺字段保留0，不自动升级成已证明writer。

ON在任何微星模式准备/源准入/新Host之前处理旧责任；OFF启动先加载历史，再排入同一lifecycle worker。延迟归还扩展既有`virtual_switch_execution_return_runtime.h`，同一ticket、power、persona和绝对30秒；最多三次归还观察，间隔一秒，无常驻新线程/设备清单，也无额外Start/Stop或fallback配额。执行完成仍经过原10秒主动检查和唯一出口。

冷归还需要强机器绑定、旧native与Host精确身份死亡、跨native CLI及job结束、完整present-PnP中旧ID及所有受支持虚拟族消失。只对记录里原本未隐藏且确实预约的exact path派发unhide；若仍隐藏，须当前物理root ancestry证明；旧devnode已消失时，仅允许用完整present-PnP再次证明该exact旧path及全部虚拟族都Absent，才删除自有配置key，不能拿CM查询失败直接放行。完整driver读回以及持盘consume成功才结清。当前本机前台借用、录制/电源/外接优先等暂停仍受尊重，不把正常屏蔽判成故障。

历史pin只作负证据；target仍Present、读取Unknown、旧writer协议0或弱绑定均保留责任并有限失败，不能使用名字、VID/PID、XInput空槽或旧记录冒充当前admission。未知真实SDK/PnP残留的清理权仍须另有证据；不调用全局cloak-off或应用名单回滚。


## 目标责任结清与默认回归（2026-10-21，有限145之后）

`inputHostTargetReturnAfterPsQuarantineLocked`只在原隔离归还owner已证明exact旧PS端点缺席、旧Host退出及物理归还后执行。它在清retirement/旧tuple前提交当前runtime的target-only持盘结清；文件提交失败必须保留旧证据，由原owner重试。历史origin、异tuple和含物理path的责任不被这个出口认领。

`inputHostTargetReturnAcceptedRecreateLocked`只接受真实已验证的positive RECREATE_TARGET/target-prepared回执，以及同Host/run/epoch/persona和本次新reservation。旧目标在既有managed串行Release和prepare前端点清理中已完成生命周期，因此只结清更早sequence的当前target-only责任；新目标仍持责，丢失/错误回执不结清。不能提高maxOrigins来掩盖旧目标记账累积。

本轮actual persona管道覆盖三类型首次开关、六个有向persona切换和同Host连续20次RECREATE：14组753检查。原完整管道101组6300检查，真实文件阻止atomic replace后仍保留quarantine与retirement，解除后重试可结清。后续改动仍须直接执行这些生产消费者，不能用只看结构/能力库的模型代替。

`tools/input_coordinator_lifecycle_selftest.mjs --out <新私有目录> --managed-dll <已绑定私有Host> --suite persona-lifecycle`可定向执行；默认`--suite all`已将该模式追加到原13模式之后，合计14。146只改变运行计划表达式，不改变C++生产代码或原模式顺序；147用同一已编译fixture分批补跑了剩余12模式，与已有persona/full-pipe共同覆盖14种实际mode。单次当前`--suite all`入口未重新执行，不声称另属任务的public reviewed wrapper已更新或批准。

普通日志导出149还会经原addFile/snapshot路径收集稳定产品目录`controller-return/target-return.json`和`controller-return/hidhide-cli-writer.json`。只读已存在字节；缺文件保留availability原因，损坏字节不修复、不重写；不收集`.owner`或临时候选。文件和日志只证明历史/软件边界，不能代替真机端点或游戏消费验收。

能力总层需求输入见工作区`Build/Validation/VirtualController-Lifecycle-20261019/player-switch-lock12/contract-state139/FIELD-STATES.md`及JSON：31字段、六域、九缺口；能力声明/消费者接通/当前运行准入分开，Unknown/不支持/未连接/阶段Ready分开。此表不新增第二运行状态层或机型权威。


## 2026-10-21 G3E现场回归边界

现有R3不变。RELEASE/PREPARE回执20秒ceiling包含SDK自己的清理；各pipe阶段和OFF仍只能消费原30秒剩余预算，不因连接/写入/重试续期。新persona检查须绑定实际bound persona，旧Host帧不能提前开检查时钟。冷归还允许CM单NUL空字符串列表，随后继续完整census；未知/目标存在仍保留持盘责任。具体节点/type/bytes/head/tail只作有限失败证据。回归位于既有pipe_cases/persona_lifecycle_cases及virtual_target_return_pnp_absence夹具；现场和验证见Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch。


## 遗留卡死必须在正常启动路径恢复（2026-10-21）

新增 `tools/fixtures/input_coordinator_lifecycle/cold_archive_cases.inc`，复用实际 ColdEnvironment、原R3执行队列、真实私有 Session/store 与受控的 managed Host 管道。`--suite cold-archive --cold-record <绝对路径历史target-return.json>` 可定向验证；`--suite all` 带同一参数会在既有14种模式后追加本轮。原记录按原始字节导入私有目录，不访问安装数据。记录机器绑定和旧进程身份来自该文件，但重启/进程结束/PnP是显式受控观测，不能视为掌机实证。

验收包括：OFF启动自行结清并重复启动幂等；保存SD/Elite/PS5时先落盘结清旧责任、后发唯一创建；目标迟到、writer Unknown或枚举错误消失后由原ticket继续；持续不可证时保留原责任且三轮有界结束、30秒解锁、只发布一次失败。不得以删记录或伪造物理归还替代恢复，不允许把手动复位列为测试前提。Windows单NUL空列表后仍须继续枚举后续目标或错误。


### 终态失败后的正常OFF仍要处理历史责任（2026-10-21补查）

正常worker的OFF分支先区分：当前Host/journal或其他当前归还责任，继续完整Stop；只有历史责任、没有当前Host/epoch/PS retirement/MSI模式债务或模式交接时，复用 `inputHostPerformColdStartupReturn` 处理。函数返回“本owner处理了历史责任”时，不再并发发出空Stop的候选回执。它沿用本次OFF原ticket/预算与同一个冷归还算法，不提高枚举/解隐/删除权限；历史不明继续有限观察。新注释解释该旧函数名覆盖启动OFF与后续OFF两个入口，算法/owner仍唯一。

已用原现场文件复现：冷启动有界失败后，外部状态恢复为可证，正常点击OFF却仍不结清。旧代码三种场景失败，修复后通过。另用实际Session先加载历史记录，再断言mode-owed和route-armed阻止冷记录分支，避免未加载历史的早退把测试假通过。完整回归的cold模式12场景398检查；不能拿受控模式事实的测试代替实机mode1写入验收。最终证据 `Build/Validation/VirtualController-Lifecycle-20261021-G3E-XboxSwitch/post-terminal-off-accepted/RESULT.json`。


### G3E 新意图跨越旧隔离回收（2026-10-21）

`VC-LIFECYCLE-20261020-R3` 仍是唯一周期。新玩家 ON 可能在旧失败 Host 已退出、隔离回收尚未确认时到达。MSI 路线在串行工作线程中将这个尚未产生候选结果的新意图交给现有 `switchExecutionReturnRuntime`；既有隔离回收证明准确旧 epoch/runId 的端点消失及实体归还后，才执行原请求剩余的 Start。

两个交接位置必须一致：队列先被消费时保留旧回收身份；旧回收先于队列消费完成时，在唯一隔离结算口保留当前新意图。不得把这个预期等待记成创建失败，不增加 fallback 次数，不续期 30 秒，不由旧 OFF 创建新目标。已失败或已过期的同一请求不能借此重开。ROG/GPD 保留现有策略。

对应生产函数：`inputHostSwitchExecutionPreserveMsiIntentLocked`、`inputHostSwitchExecutionHoldMsiIntent`。实际消费夹具位于 `tools/fixtures/input_coordinator_lifecycle/persona_lifecycle_cases.inc`，覆盖新意图在归还前、归还后及归还先于队列消费三种顺序。PS5 的驱动 catalog 失败是独立证据，不得用上述接续修正宣称其驱动已经修好。


## 2026-10-21 上游依赖 2.0.0

当前唯一 vendor 为原版 HIDMaestro.Core 2.0.0.0，身份见 `../deps/hidmaestro/upstream-release.json`；不可把仅替换 DLL 当作安装或实机验收。2.0 create/deploy 共用官方锁与版本策略，初始化消费者在 `../GamepadPrerequisites/HidMaestroPackagePreparation.cs`；`--check` 保持只读，`--prepare` 在既有 owner/权限/客户端门后调用一次官方准备入口。

2.0 的 raw driver 与 signed package 分目录，原始 driver 目录没有 CAT 是预期情况；只读证据入口为 `../InputHost/XboxSdkDiagnostics.cs`。SDK 的 `RemoveAllVirtualControllers(true)` 语义为 Orphaned + preserveInstall，不可改为 Everything。SDK 自己的进程归属和清理不代替上面的原请求/精确旧端点/物理归还证据。

退出与 SD raw report 接管共用 `../InputHost/HostControllerIdleCtsRelease.cs` 的 DeviceClock 屏障；output reader 屏障为 `HostControllerOutputStop.cs`。两者不得再按 1.12 的 idle CTS/queue 形状取字段，不能靠写 SDK readonly 字段或强造释放标记放行。被动队列观测在 `UsbipTransportObservation.cs`，它没有切换或自愈权限。

当前报告回归入口 `../tools/hc_motion_host_wire_selftest.mjs --mutation-checks` 会展开此处实际 runtime 头、执行完整 Host parser/报告和 stock codec，并在隔离内存验证；完整切换、强制退出记录回放继续用现有生命周期管道夹具。升级 SDK 不改变 R3 生命周期版本、不另起生命周期，也不改变能力总库的机型/可选能力准入。


## 2026-10-22 R7：MSI同episode单次责任与环境判定

R7冻结导出见G:/YeManCC-Work/Build/Exports/20261022-VC-MSI-INFLIGHT-R7；软件交付和真实消费者回归见G:/YeManCC-Work/Build/Validation/VC-LATEHIDE-INFLIGHT-20261022/DELIVERY-R7-REPORT.zh-CN.md及DELIVERY-R7-RESULT.json。只在既有MSI已登记+armed分支复用同episode的queued/inflight责任，必须存在真实执行owner；异常出口以局部栈守卫清退在途标记，保留pending供合法后续通知，不能把遗留标记当仍在执行。普通队列谓词及ROG/GPD原行为不变。31场景110检查与五个编译回退是专项证据，不是所有设备实测。

环境维护清单/版本、已加载SDK身份、实际创建/新鲜输入分别验收；未知清单与更新版本不得直接标成不可用或触发降级。对应GamepadPrerequisites/HidMaestroPackagePreparation.cs和ButtonMappingView.vue保持已验证R5/R6字节；升级按controller.prerequisite-environment实际消费者和环境UI组合回放。唯一生命周期VC-LIFECYCLE-20261020-R3和原30秒/10秒/一次保底不变。当前主线与冻结包分别记录身份；本说明更新不等于重新编译已冻结R7。实机和CPU仍待验收。


## 2026-10-22 R7后真实消费者交接补测

新增失败/决策异常/多代际替换/成功后日志异常4种交错。现有msi_late_hide_inflight_regression.h通过实际BUS与worker证明后继责任和通知保留，专项35/126、原9/17断言不变。代码对照及执行证据见G:/YeManCC-Work/Build/Validation/VC-POST-R7-LIFECYCLE-20261022/REPORT.zh-CN.md和INTEGRATION-RESULT.json。global计数已登记，本轮未重跑完整global回退矩阵；R7冻结包及原31/110回执保持原样。本轮无产品行为修改，物理与CPU未验收。


## 2026-10-22 恢复维护成本M1

测量入口为tools/fixtures/input_coordinator_lifecycle/RECOVERY-COST.md所述recovery-cost suite，执行完整原inputHostRecoveryMaintenanceTick与真实TransactionMutex；8状态120样本/360万调用已测，报告G:/YeManCC-Work/Build/Validation/VC-RECOVERY-COST-20261022/REPORT.zh-CN.md。仅终态/兜底标志下的每次检查是本机约72—115ns的受控成本，未证为整机瓶颈；不因进锁静态怀疑删检查、降频或改变原R3预算。后续完整worker测量需与M1区分，不能重复加算或冒称实机CPU下降。R7运行包不变。


## 2026-10-22 完整空队列worker成本M2

永久入口tools/fixtures/input_coordinator_lifecycle/WORKER-COST.md；实际报告G:/YeManCC-Work/Build/Validation/VC-WORKER-COST-20261022/REPORT.zh-CN.md。显式测试suite选择正式元数据发布分支，真实ContextPublisher只向自有本进程映射写人工元数据，保留生产worker正文。普通受控调用约0.10—0.59微秒；日志开启后关闭的mapping仍发布已单列，不将它视为已证CPU故障而删掉。M2已含M1不能相加；单次维护到期有计数器/隔离端点开销。当前不改调度/恢复预算/能力总库/R7运行包，真实线程/整机CPU仍未验收。


## 2026-10-22 管道读回执范围M3

实际原Runtime私有消息管道对照见G:/YeManCC-Work/Build/Validation/VC-PIPE-SCOPE-20261022/REPORT.zh-CN.md；入口tools/INPUT-PIPE-LATENCY-COST.md。pipeReadUs是成功接受帧read范围elapsed，包含内核等待，不是排他CPU；n与BUS/其他嵌套阶段不同。当前不改超时/异步所有权或重做R39/R46/R47已拒绝缓冲优化；R45历史采用与当前头验证分开。新测量宏不影响旧75项correctness夹具入口，R7保持原样，实机与整机CPU未验收。


## 2026-10-22 当前正式global与preamble范围

G:/YeManCC-Work/Build/Validation/VC-PREAMBLE-AND-GLOBAL-20261022/REPORT.zh-CN.md记录当前35/126与五个真实源码回退6/2/2/2/2的正式global结果，结束元数据与输入哈希独立保留；不能再说新版矩阵尚未执行，也不能把总目录INCOMPLETE_COVERAGE当全局通过。当前scope计时器Release/Checked各80067和finish-discharge指定负例证明诊断语义，入口tools/VIRTUAL-PREAMBLE-CURRENT-SCOPE.md；不执行整BUS、不据不同n/嵌套elapsed均值删检查。R7冻结包不变，物理/实际CPU仍待。


## 2026-10-22 环境提示与升级判定边界

环境准备、包内SDK原件身份、实际目标创建/输入是三个独立证据域。GamepadPrerequisites/HidMaestroPackagePreparation.cs保留完整current/newer驱动供给，未知清单或额外未知版本不将既有手柄判不可用，也不授权降级/卸载/重装；包内SDK身份校验仍随明确升级更新，不能用放宽系统驱动版本比较来跳过原件校验。实际创建/新鲜输入失败由原生命周期唯一结果报告，准备工具退出码不应冒充它。

控制页统一展示策略见src/bridge/gamepadEnvironmentNotice.ts；通用exit12/13仅在当前报告确认hidHideSetupNotice5/6时显示HidHide安装器缺失/校验失败，否则保留中性未确认说明。该层仅展示，不修改准入、预算、能力总库、开关或真实设备。环境回归用controller.prerequisite-environment实际消费者；提示回归用tools/controller_display_browser_selftest.mjs真实组件，界面导航沿用controller.ui-navigation。证据目录G:/YeManCC-Work/Build/Validation/VC-ENV-NOTICE-20261022，验收状态以其中最终RESULT为准。


## 2026-10-22 G3E两笔冷归还责任的真实生命周期回放

现有tools/fixtures/input_coordinator_lifecycle/COLD-MSI-ARCHIVE.md及cold_msi_archive_cases.inc直接执行共享ColdEnvironment、coldExecution、持久化Session、原R3串行队列和真实私有托管Host管道。新增global controller.cold-msi-archive从2026-10-22 080106原始1287字节记录回放两笔SteamDeck责任，覆盖14种明确OS观察交错；不改原单Elite夹具断言或把真实记录清空。只读系统ID与持久化写权限ID的生产分层修复保持并行AI原代码，模式/OEM/能力总库不变。

正式软件门需current14/545，随后只恢复旧ID字符集的编译负例应精确失败14条并重现device-id-invalid；12个正向托管子进程需由原生持有句柄证明退出且controller/context均归还。原30秒、10秒主动检查、一次保底和唯一终态不改。若源在测试中变化，保存语义结果但拒绝签为当前主线，改在明确冻结快照重跑；不能忽略无关任务的源漂移。证据目录G:/YeManCC-Work/Build/Validation/VC-COLD-MSI-ARCHIVE-20261022，最终状态以INTEGRATION-RESULT.json为准。实机枚举/驱动、Steam或游戏消费、强退重启/连续唤醒与其他家族仍独立验收。


## 2026-10-22 信号取证正式检测入口

`tools/ymcc_sim/global/controller-signal-evidence/` 将原 `tools/msi_external_raw_receipts_selftest.mjs` 联合检查接入既有全局模拟器，场景为 `controller.signal-evidence`。它实际执行现有 RawInput 内存消费者、descriptor 解码、语义见证预算、WinMM Recorder、完整恢复函数代际接线及实际 callback/原子统计/export 提取；不能改成仅检查头文件字符串或用旁路模型替代。源码、指定失败反例、回执和当前工具身份逐层核对；具体结果以 `G:/YeManCC-Work/Build/Validation/VC-SIGNAL-GLOBAL-20261022/` 的最终记录为准。

消费者原文须写独立 `diagnostic-result.json`，`result.json` 归全局 runner，避免回执被执行摘要覆盖。子进程只继承必要 OS 环境位置，工具和输出由适配器决定；已有实际 compiler/SDK/include/lib 回读必须匹配声明。工具启动失败、缺文件、指定负例失效、源码漂移和当前工具身份差异都保留失败，不能复用旧 PASS。

虚拟输入实际拥有周期 timer、订阅和 Host 跨电源事件的会话，属于 persistent。此入口只证明被动取证代码和接线，不运行真实 timer/RawInput/设备/Host/Steam/睡眠；不得伪造生命周期观测来获得放行。诊断检查 PASS 后，父功能的真实退出、挂起销毁、晚到回调、OFF 和睡眠/故障恢复验收仍须保留未验收，原 R3 的 30 秒/10 秒/一次保底和唯一终态不变。取证随正式匹配整包交付，单个测试 EXE 不算可测版本。


## 2026-10-22 完整回调回收屏障

实际提取的完整 `inputCaptureTimerEventProc` / `inputCaptureWorkerFault` / `inputCaptureThreadProc` 消费者证明：仅普通异步timer取消加BUS锁排空，无法证明BUS前暂停或busy-skip尾部的完整回调已经退出。旧源码会在事件关闭后继续SetEvent；worker初始化异常时stop仍false，还可能在退休后进入BUS。这是进程级采集退出竞态，不能据此归因现场高频或说每次persona切换新建timer。

当前创建保留8ms/TIME_PERIODIC，并加TIME_KILL_SYNCHRONOUS；两个worker外层catch入口先release-store采集stop=true，再调用原WorkerFault与取消。WorkerFault本身保持无capture-stop写入，避免其它Host调用误停采集。取消不持BUS，保留原BUS drain/Shutdown/Close顺序。新增等待按实际stop读取与BUS所有权审查：deepBUS是原drain已经等待的部分；其余prefix/tail未发现反向等待worker。整个main仅该worker有三处实质改变及一行注释，callback与其它区域字节不变。

原raw联合入口和正式全局 `controller.signal-evidence` 已包含 `tools/input_capture_worker_retirement_selftest.py`，当前生产函数必须逐字提取执行；旧源码按新合同应在晚到回调点失败，另有只删除fault-stop的指定负例。原 `tools/input_capture_timer_retirement_selftest.mjs` 继续单独测真实私有WinMM叶子，不在全局memory消费者内运行。证据唯一入口 `G:/YeManCC-Work/Build/Validation/VC-ACTUAL-WORKER-RETIREMENT-20261022/`。

本修复不等于完整退出有界：timeKillEvent失败仍不能当drained、callback停滞、Stop两秒等候未检查、真实采集Shutdown/Host/设备与游戏消费仍分别未验收。不要删除这些UNKNOWN，不改R3的30秒/10秒/一次保底或其它家族的模式/屏蔽/能力。新prefix/tail或同代重启入口必须重新审查取消等待关系。源码变更须进入匹配正式整包才可进行现场验收。


## 2026-10-23 采集Stop等待、句柄和证据边界

`inputCaptureStop` 的 2000ms 是原来的单次线程等待参数，不是整个Stop或R3切换的总预算。当前仅 WAIT_OBJECT_0 后尝试CloseHandle，且关闭成功才清空句柄记录；其它等待结果及关闭失败保留记录。GetLastError在相关失败后立即采样。原Lenovo revoke、DSU→Host收尾顺序和唯一生产caller（WM_DESTROY）不变；没有新增生产retry或进程/线程终止手段。

普通事件 `input-capture-worker-stop` 精确登记到base allowlist，区分threadExitVerified、handleClosed、ownerRetained、waitResult/waitWin32/closeWin32；JSON/日志失败不跳过既有独立收尾。ownerRetained仅表示非空记录；WAIT_FAILED后不保证它是有效内核句柄。没有handle不等于证明此前资源已销毁，故不捏造等待回执。

| 等待与关闭结果 | 线程退出证明 | 句柄关闭 | 记录 |
| --- | --- | --- | --- |
| WAIT_OBJECT_0，Close成功 | 已确认 | 已确认 | 清空 |
| WAIT_OBJECT_0，Close失败 | 已确认 | 未确认 | 保留 |
| WAIT_TIMEOUT | 未确认 | 不主动关闭 | 保留 |
| WAIT_FAILED／其它结果 | 未确认 | 不主动关闭 | 保留；句柄有效性未知 |

既有联合入口和正式 `controller.signal-evidence` 包含 `tools/input_capture_stop_wait_selftest.py`。它编译完整当前Stop及实际base分类器，内存边界覆盖timeout/failure/关闭失败/日志失败；另外在测试自有Windows线程与事件上执行真实Wait/Close，以controller的复制join句柄证明旧代码关闭产品句柄时线程仍活着，随后正向释放并加入所有自有线程。fixture中的第二次Stop只证明控制器已确认完成后可归还该自有句柄，不代表生产caller已接续重试。

保留句柄不是销毁完成。整个生产worker/Host的有界销毁、独立Host收尾与活worker的关系、取消失败、真实睡眠/强退与游戏消费仍未验收。父合同必须包含workers和handles，不能从资源清单删除它们以获得通过。原VC-LIFECYCLE-20261020-R3的30秒/10秒/一次保底不变。

另行取消失败取证中的97是非合同/未知错误对抗输入；11仅是文档形错误码，pending-callback仍是未证假设。条件内存结果不能叫真实OS复现或产品根因，不能据此扩大模式/屏蔽/驱动控制。记录见G:/YeManCC-Work/Build/VC-CANCEL22/RESULT.json。当前Stop证据唯一入口G:/YeManCC-Work/Build/Validation/VC-CAPTURE-STOP-OWNERSHIP-20261023/。


## 2026-10-23 可中断的采集启动延迟

完整 `inputCaptureStart` / `inputCaptureThreadProc` / `inputCaptureStop` 的私有Windows线程消费者已复现一个确定窗口：旧worker先Sleep(2500)，Stop却只Wait(2000)，停止发生在delay前或delay期间时首轮等待超时。该窗口是进程级采集启动/退出，不是persona切换定时器证据；不能直接解释现场约5kHz、乱码或屏蔽漏信号。

当前正常启动仍延迟2500ms，改由 `input_capture_startup_wait.h` 的同一原子stop谓词和条件变量等待。Stop先发布原stop并唤醒；Start只在旧句柄记录已清空的原有准入之后进行受检reset，reset失败不创建新worker。没有第二stop标志、后台轮询、额外线程或timer。worker两个fault catch保持直接store，因为当时该worker自己的startup wait已返回。requestStop的锁失败只提供已发布stop和best-effort通知，不能冒称可靠唤醒；Stop仍通过真实线程等待/关闭结果判定，不把唤醒当销毁。

回归接入原raw联合入口和 `controller.signal-evidence`：独立头文件Release/Checked正反例验证通知/谓词；新完整生产三函数消费者使用真实私有内核线程、复制join句柄、Wait/Close和真实正常延迟。COM在明确fake边界拒绝，因此不运行采集设备、Host或WinMM。旧worker/Stop专用夹具的startup helper仍是显式fake叶子，仅新startup消费者验证真实同步；不得混称。

R3的30秒硬预算、10秒主动检查、一次保底与唯一终态不变。原正常8ms节奏、OEM模式/屏蔽策略、能力总库和退出caller不变。此修复不证明任意停滞worker、真实Shutdown/Host有界销毁或物理游戏消费，父持续合同仍未验收。证据在 `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-Startup23/`；实际受测源码与匹配包必须另行核验。


## 2026-10-23 晚到采集初始化与不可逆退出闸门

实际 `inputHostRaiseExitFence` / `inputHostResetExitFence` / getter / 工作准入谓词消费者证明：退出先抬闸后，迟到的 `inputCaptureInit` 会通过旧reset的exchange(false)重新开放终态。正常进程默认false来自static初始化；这个同进程清闸并无新进程初始化的必要性。当前reset仅观察flag；已观察raised则记录reset-refused/terminal-process-exit并返回，保留拒绝计数；普通初始化保留诊断counter与phase兼容，不再写false。退出Raise的队列裁剪及cleanup allowlist保持原样。与Raise并发时闸门仍只可能从false变true；诊断counter与日志快照不是原子事务，不能拿它们替代flag准入。

既有raw与controller.signal-evidence接入完整五函数消费者：Release/Checked执行先init后exit、先exit后late-init、重复exit、9个显式join线程的并发和正常初始化；历史clear代码及仅恢复exchange(false)的指定负例必须失败。框架不运行安装版、Host、设备、服务或输入timer；这不是现场异常输出归因，也不是整个capture销毁验收。

另外，完整当前Stop、真实WM_DESTROY的exitRequested路径和main-finally片段已在私有Windows线程上重放：held-worker时即使Stop保留了句柄，路径仍会PostQuit、seal和返回前清理；fixture随后才释放并join自己的worker。这是仍未关闭的生产退出接续缺口。消费者 `tools/input_capture_exit_continuation_selftest.py` 必须保留productionExitContinuationAccepted=false/非零退出，不允许用fixture手动第二次Stop把它洗成生产自愈。真实capture/Host跨锁清理、挂起有界销毁和最终接续仍待修复；R3、OEM策略及能力总库未改。证据统一位于 `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ExitContinuation23/`。


同轮内置自测兼容：inputHostExitFenceSelfTest与msiG3eCapabilitySelfTest原先用生产reset恢复自测状态；现在各自在函数范围内定义同一显式fixture恢复lambda，8个原恢复点改调该lambda。它先恢复自测自己的flag，再调用生产reset整理诊断；不引入生产可调用的开闸入口。前者新增late-init保持终态与拒绝计数的断言。正式fence消费者从两套实际自测提取同一lambda并执行“自测恢复可清自己的现场，但随后的生产Init仍不可清退出”场景；不冒充完整native selftest命令已运行。自测两函数之外的生产源码逐字不变，证据selftest-adaptation/CONTROL-PARITY.json。


## 采集进程资源归还（2026-10-23）

`input_capture_completion.h` 记录 capture 进程资源责任，与 `VC-LIFECYCLE-20261020-R3` 玩家切换事务分开。它不增加新的 persona 状态机，也不延长原30秒、10秒主动检查或单次保底预算。

- `main.cpp:inputCaptureStart` 使用 Admission 预留资源发布权；Stop 遇到迟到 Start 时必须等发布权结算。
- `inputCaptureStop` 使用单一 StopLease；只有 worker 等待已确认退出、句柄成功关闭后，才执行 `inputCaptureRetireResources`。
- `inputCaptureShutdown`、`inputCaptureEmergencyCleanup` 共用退休检查。ready=false、停止请求、线程signal、函数返回都不能替代子资源归还；失败close、token撤销、Delete或FreeLibrary保留原责任。
- 应用退出顺序为原fan交接 → Host QUIESCE/neutral → capture归还检查 → 原OEM/Host归还 → 清理保留配置。`WM_DESTROY` 只发停止请求，最终主线程也必须经过原后台退出owner。
- `inputCaptureExitOwnershipReleased` 只证明 capture scope，不等于 Host/pipe/端点/物理归还或游戏消费。已signalled但未通过后续检查、WAIT_FAILED或Close失败，继续消息等待并保留责任。

采集结果日志 `input-capture-exit-ownership` 的 version=1、scope=capture-resource-only;Host-return-separate；30秒 `input-capture-exit-retained` 仅说明尚欠责任，不能当有界销毁或完成。任意COM/driver/child永久阻塞及完整Host父链仍未验收。现阶段不要把保留后等待当成总目标已完成。

定向fixture沿用 `tools/ymcc_sim/global/controller-signal-evidence`，有18组消费者；实际Shutdown/Emergency/RetireResources以明确memory leaves执行，真实kernel对象场景单独标注。原FAIL、历史SHA、负例和UNKNOWN保留。当前证据写于 `G:/YeManCC-Work/Testing/YMCC/Evidence/VC-CaptureReceipt23`；正式全组状态以新运行回执为准，不能用登记成功替代执行。


## 2026-10-23 Host pipe 的完整归还凭据与 ROGX 现场补证

`input_host_pipe_transport.h::Runtime` 是原 pipe 的唯一 owner；main 中 serverPipe 只作借用。停止时先交回借用，再以 `inputHostPipeRetirementProvenLocked` 消费 `Snapshot.available && Snapshot.stopped`。pending=0、断开完成、进程退出都不代替 active/retained lease、cache、原句柄/duplicate/event 和当前失败 Close debt 全部归零。failed Close 保留原 owner；第一次 adopt 尚未接管的 raw handle 也保存在单一受检槽位，不允许下一次创建覆盖。Clear 未取得该凭据时保留旧 tuple、HELLO/PREPARE 历史及 quarantine；物理归还/重武装接续同样先过这个局部门。该门不证明 SDK 端点消失、OEM 模式恢复或游戏消费；任意 OS 永久阻塞仍 UNKNOWN，不新增第二套玩家生命周期。

`input-native-signal-window` 的新增字段是被动观察，沿原详细日志开关和每秒见证取样：`foregroundObservation` 在被选中的实际输入请求旁记录前台 HWND/PID/class 和读前读后是否相同，没有标题/进程路径扫描，class 名不等于 Xbox 全屏会话身份。`window.selectedSourceKnownReads/LiveReads/NonzeroReads` 保留两次见证之间的选中源活动，避免短按恰好落在秒级采样间隙；exact-nonzero 包含噪声，窗口可能跨前台，不证明硬件包新鲜或消费者收到。未被选中的帧不调用前台 API，也不构造日志 JSON。此观察不改变输入、屏蔽、焦点或自愈策略。

ROGX 20261023-083438 现场分析和源码定向验证保存在 `Testing/YMCC/Evidence/VC-ROGX-20261023`。这包只有四个候选 Xbox shell 见证，物理 pad 均为零，不能据此宣称已找到全屏无效根因；后续按新增实际请求旁的观察取证。30 秒/10 秒/一次保底、能力总库和家族控制边界继续沿 R3。


## 2026-10-23 CreateProcess 初始线程句柄责任

Host process 与 CreateProcess 返回的 primary-thread HANDLE 是两项独立责任。Start 在首次 Close/日志/HELLO 前，先将两个句柄发布给 HostMx 下的唯一 owner，再撤销局部 PROCESS_INFORMATION 所有权。primary Close 失败保留同一个句柄，不再随局部变量丢失；它不代表线程退出，也不等于虚拟目标故障。

Clear、Owned/Disabled 和 quarantine lifecycle 将 primary HANDLE 归还纳入同一现有顺序。尚欠 primary 时保留旧 tuple、阻止新 writer；无实体源也由既有 BUS 的1Hz quarantine节奏派原 lifecycle 重试。没有新线程、定时器或并行开启/关闭。状态变化事件 input-host-primary-thread-retirement 常开，未变化错误去重。永久OS失败仍保留责任并为UNKNOWN，不以函数返回或进程退出写作归还成功。

新消费者 tools/input_host_primary_thread_selftest.py 执行实际创建后所有权发布片段及完整Clear/BUS/lifecycle/Owned函数，使用私有真实Win32线程、独立join句柄和Close保护失败；过程/端点/物理为明确memory事实，没有启动真实InputHost或SDK。旧生产片段与五个门控变异保留失败，所有正例必须在进程退出前join/close且不使用oracle兜底。证据 Testing/YMCC/Evidence/VC-PrimaryThread23。原34过程、95组合、172 Clear和64 source-free回归仍单独保留，不能拼接为整机/整条父生命周期完成。


## 2026-10-23 Host 唯一终端结算与父退出本地资源门

InputHost/HostTerminalCleanup.cs 是管道会话的唯一终端结算。完整管道循环返回退出原因及已受理 SHUTDOWN 的原释放结果；Main 消费这笔结果，不能再次对空对象的 no-op 释放把原失败洗为成功。只对明确发生在 Dispose 前、保留原 controller/context 的四个停止屏障 timeout，同一个 continuation 和 owner 直接重试一次。永久停止失败保留 managed-release=false 和非零退出；SDK Dispose、neutral、混合原因或未知异常不扩大重试。terminal-cleanup-complete/failed 日志属于 resource-settlement，日志成功、Host 退出码均不代替原资源结果。没有新建后台 writer、重建目标或增加玩家重试预算。

native/input_host_exit_retirement.h 只负责应用退出时的自有进程资源。权限要求 app-exit、退出 fence、capture 资源归还、普通 Stop 已尝试四项同时成立；只使用本次 CreateProcess 原始 process HANDLE 与其 PID/creation-time 身份。正常观察退出优先；仍存活且身份一致时，仅允许一次 TerminateProcess，随后仍须实际 wait-signalled 和 checked Close 才撤销 owner。接受终止不等于退出，WAIT_FAILED/身份不明不允许 force，Close 失败保留原 owner，观察重试不能更新 force 预算。它不用于 persona 切换，不处理 Steam、OEM 服务或其它进程。

native/input_host_exit_owner_runtime.h 沿现有退出 worker，在普通 Stop 后接续本地 process/primary/pipe 归还。所有 WM_APP_EXIT_READY、主消息循环与最终 Ensure 出口都必须消费 inputHostExitLocalOwnershipReleased，不能靠已发 READY、cleanup thread signalled 或其句柄关闭跳过该门。其他模块先完成正常清理，末段才等待尚欠的本地 owner；30 秒日志仍欠责任即 UNKNOWN，不能把永久 OS/IO 失败写成有界完整销毁。该 30 秒观察与 R3 玩家事务上限分别记账。

本地资源门刻意不证明 SDK 销毁、PnP 缺席、HidHide 解隐、OEM 模式恢复或游戏消费。强制结束 Host 后不得用缓存的通用 XInput 平面冒充目标消失；仅已有精确 MSI-SD/PS PnP scope 或已证明从未发目标命令的通路可继续原 quarantine 归还。持久责任未结时继续保留，以现有冷启动归还机制承接。USB/IP 安装/实际加载版本及维修策略由独立环境任务负责，不用重装掩盖本地回收错误。

本轮证据入口为 Testing/YMCC/Evidence/VC-HostManaged23/terminal-patch 与 VC-NativeExitOwned23；各回执分别标注实际源码、私有真实进程/线程和显式 memory leaves。原 VC-NativeExitParent23/r4 及 managed old-zero-false 失败保留。正式全组、驱动和实机状态以各自新回执为准，局部编译或资源测试不替代总体验收。

旧 controller.host-parent-lifetime 的五场景消费者已适配生产 LoopResult 和实际 HostTerminalCleanup；原 parent-cancellation 三个指定失败仍保留。其 HostSession release 是计数器，只证明父消失的管道取消和终端接续，不替代 managed-terminal 的实际 worker/controller 归还证据。见 Testing/YMCC/Evidence/VC-HostParentCurrent23。


## 2026-10-23 统一检测接线接续

原controller.signal-evidence保留21个原顺序并接入第22个managed-terminal真实消费者；默认队列与独立登记入口共用runManagedTerminalGroup。新增controller.host-exit-owned正式场景复用实际exit-retirement、完整worker/Ensure和两个最终完成入口，仍明确persistent且不补造睡眠/物理测量。两者分别保留原38/primary48等反例和所有历史失败。

现有controller.lifecycle-native-managed-pipe消费实际LoopResult与HostTerminalCleanup：唯一结算前快照与首次失败后owner快照分开，所有16私有Host进程/primary线程实际join/checked-close，pipe Runtime全资源归还后才允许fixture输出成功。玩家保护、持久target账本、SDK/PnP/USB维护均为明确wire边界；不把fake输出停止返回当真实SDK线程已退出。

本轮完整22组在明确冻结生产字节上执行，原worker审批/失败不改写。固定main仅作为离线测试输入，不授予部署/驱动/现场验收。新快照补接分类器事件位置和capture-only Host已归还leaf，再独立实测；原固定失败和所有子任务出口保留。完整current105/R3编译路线的exact21→22接口迁移仍待独立版本化验证，本轮wire16和managed40不得替代。

证据入口：Testing/YMCC/Evidence/VC-ManagedSignalWiring23、VC-NativeExitRegistration23、VC-HostWireComposition23、VC-Signal22Frozen23。生命周期优先、自愈其次、CPU最后；本轮不新增CPU收益结论。


## 2026-10-23 ROG 原生关闭态接口刷新失败取证

普通133711导出包尾进程16320在虚拟配置disabled时，13:35:50.704发起xusb-game-interface-refresh，13:35:50.824实体XInput由1变0，13:35:56.183原helper以6退出，helperDiagnostic缺失，随后仅有restore-refresh-unverified。配置回读无自有隐藏项、target-return账本为空都不能代替实体接口归还。背部/专用键仍可用属于独立用户观察，不等于游戏/震动通路仍有效。

MsiNativePortCycle的gameRefresh失败事实沿现有单次stdout收集、native普通生命周期日志导出。Schema1/Kind=ymcc.gameRefresh.failure/Result6逐阶段保留prepare/current Locate、同节点、Disable/Enable是否进入和原返回值、异常类型/HResult；原gameAuthority8语义不变。记录不新增CM调用、重试或权限，不把停用成功或重新启用返回0冒充PnP状态/物理消费证明。关闭成功后重新启用失败的责任与恢复仍是待修复项；本轮不能依据笼统6指定本机的具体失败步骤。证据Testing/YMCC/Evidence/VC-ROG133711-23，原失败和部署身份限制保留。


## 2026-10-23 ROG 上午／下午屏蔽回归基础节点

VC-ROGX-AMPM-20261023-R1 保留真实HID/XUSB识别，分离实体停启权限。13:37包中三个会话的同一ROGX HID节点刷新后87/109/120ms掉光XInput；08:34包此前13次延后。ROG VID0B05/PID1B4C的原生前台屏蔽在任何隐藏前、刷新叶子及helper消费三处禁止该HID停启，无父根fallback；虚拟HidHide非破坏性源规划仍保留。原生屏蔽此时只报deferred，不以识别成功推导写入安全或屏蔽生效。

执行清理的原节点一次Enable补偿不改变原失败6；最终生产调用只准入已取证ROG PID1B4C，MSI/GPD/ROG1ABE调用保持旧行为。持久devnode-enable责任仍未接通，配置返还/空账本/补偿0均不代表完整物理归还。当前证据Testing/YMCC/Evidence/VC-ROGShieldRegression23/DELIVERY.json；原正式native404、最新helper344/2951及日志消费1918分开保留来源，全局仍INCOMPLETE_COVERAGE、实机未验收，不改R3唯一玩家生命周期版本。


## Win5 / ONEXPLAYER / AYA 选中节点的驱动栈取证（2026-10-25 R4）

`controller_device_stack_evidence.h` 只在原 `hidhide.visibility-snapshot` 过渡日志点，由当前journal自有目标与选中的HID/root读取PnP属性，字段为`pnpDeviceStackEvidence`。GPD、OneXPlayer、Ayanero三类可调用；ROG/MSI不进入这个新增读取分支，机型/来源仍由原总库与原采集消费者决定。没有新的常驻扫描、线程或输入判定。

每次最多16个精确节点，读取Device_Stack、Service和Upper/LowerFilters，各自保留CONFIGRET、类型、长度、解析状态与节点身份前后校对。unsupported、失败、截断或预算耗尽保持unknown，不能当空列表；Device_Stack中列出HidHide与配置过滤项是不同证据。`filterAttached`、`externalVisibility`仍为unknown，日志不会证明Steam现存句柄已经停止消费。

原native首次取得隐藏责任到R3 handoff整组持盘之前仍有恢复缺口：当时没有真实Host，不能伪造Host Origin；one-shot刷新可能在旧parent死亡后仍处于设备调用中，必须取得writer终态证明。R4日志补强本身不授予新解隐、刷新、服务或模式写权限。


## 2026-10-26：历史归还优先于本机前台再接管

`msiNativeForegroundFacts`沿原Host锁读取`inputHostTargetReturnHistorySettledLocked`缓存：历史责任未结时不进入native-only借用/隐藏。`inputHostTargetReturnColdPassLocked`持盘结清，并确认原OFF/power/exit上下文仍有效后，复用`msiNativeForegroundUiEvent(true)`把当前前台意图交回原队列。禁止新建第二个请求、计时器或重试；不会用模式名/写入ACK替代归还证明。原R3的30秒/10秒/一次保底与三次cold观察保持。证据：工作区Testing/YMCC/Evidence/VC-Win5Return26-R9/DELIVERY.json。

原coordinator可显式选择`off-lifecycle`或`family-lifecycle`；后者复用原first/Start/success/close，共9个GPD/OneX/AYA人格变体加物理Deck禁止自模拟负例。受控DMI来自能力库声明，但不冒充总库运行解析或硬件验收，不赋OneX/AYA未实现的native写能力。


## 2026-11-03：ROG 首次延迟隐藏的刷新责任（R3 内补齐，不新建生命周期）

114820 的空 journal 门缺口已有 deferred Start 责任。131128 现场 PID11164 的前两代实际补 hide/readback 成功，Steam 仍持续枚举 0B05:1B4C 实体与 28DE:1205 虚拟两入口；因此 `applied`、hidden list 和旧 `hideCycle=true` 初值均不是外部单手柄证明。

新增行为仅限 ROG：原 source-pending Start 在没有 active journal 时保存一次刷新责任。源归来后先沿原 `hide(false)` 完成配置，再由 `inputHostDeferredPhysicalHideRefreshLocked` 对该 journal 精确自有、经原 ROG 型号/game-root 规则及原 USB 拓扑权威证实内置的同一 USB root，沿原 `hidHideCycleInstance` 至多尝试一次刷新。继承 journal、普通 reconnect、045E 外接源和其他家族不新增 cycle；OFF/归还链、能力总库、用户 persona 均不变。

全部写入继承同 request/run/Host/power/persona 与原绝对 30 秒；cycle 的 pipe 预算不超过 5 秒或原剩余时间，实际 issue 前仍验证源与意图。busy 未结只延期，不补新预算；失败/未知 ACK 保留责任，不报完成且不重新发 cycle。成功 ACK 后允许该次刷新自身造成短暂掉源，但仍复验原意图和 deadline；sourceGen 变化不是重新 arm 或重复 cycle 的许可。这个同步 CyclePort 可能造成数秒采集/帧空窗，不能称无缝或硬件通过。

同包后两代的 `refused-deferred-start-context` 没有具体 guard 事实，不能断言超时或旧 Stop 清债。保留原判据，完成行增加首个拒绝原因及 worker/transaction 阶段，不因该候选扩大共享采样链或放宽权限。原定向夹具扩展其 ROG 场景与语义反例；模拟不执行驱动操作，不代替 Steam 外部单虚拟与 OFF 实体归还验收。证据根：`G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-DoubleController-20261103-131128`。


## 2026-11-03：ROG 首次无身份掉源在目标创建前接回

151907 PID10440的头两次SD开启：原pretarget槽先准入，native归还刷新时RawInput身份尚空；掉源后expected/current均空，原严格reconfirm正确拒绝，locked/sourceMask0又阻止late-admission，两次原30秒过期。切PS5后的新轮才形成完整身份并沿原刷新恢复。

`ymStartRecoverRogPendingIdentity` 是原inputHostStart在HELLO后、首次hide/PREPARE前的一次执行补强，仅ROG的原HID型目标请求、无已创建或可能已创建目标、无journal。原单槽pending、空身份和未解析selected、完整当前四槽边界、slot resolvedEpoch及无reprobe均成立后，复用原完整唯一PnP扫描与内置ROG game-root/USB拓扑权威，锁外读取、try-lock提交并重验原ticket/Host/power/意图/期限/缓存代次，再进入原ymStartAdmitSource。不会令空身份相等、替换已知源或在已发布目标后换源。

该执行补强不新发cycle、不新建timer/常驻扫描或第二自愈周期，也不续期。正常初始hide仍可能走原CyclePort。扫描/锁/身份无法证明则仍按原source-pending处理，不把unknown报成功。OFF改善与其他家族不变。原夹具19/437通过，含实际Start接线及精确回退4FAIL；这不等于硬件或Steam单手柄验收。通用双入口的一次保底与不同第二方法由用户另交“风扇 (2)”推进，继续遵守原R3预算、唯一出口及已知操作排除。


## 2026-11-03：虚拟手柄常驻成功帧回执精简（CPU1/CPU2，仍在稳定性核验）

共享主线候选使用每次成功 PREPARE_TARGET/RECREATE_TARGET 新生成的 256-bit 随机 frameReplyBinding，将回复格式绑定到当前 HostSession 的实际 controller 对象。完整 target-prepared 回执经过原 tuple、Host instance、asset、目标 PnP 准入后，native 才记住该值；完整回执的全部原附加器运行结束后，Host 按 UTF-8 16KiB 预算附加 token，空间不足则保持原完整格式。旧 caller 不请求此格式，旧 Host 不提供 token，两端继续原路径。

请求仍保留原完整 nonce/tuple/sequence/帧内容与新鲜度验证；仅该次请求明确附上当前 token 后，成功 SUBMIT_FRAME/frame-accepted 可回 compactFrameReplyVersion=1、协议版本、请求 ID、序号、token、状态、firstFrame、submitElapsedUs 及原动态附加字段。每次 ACK 重新生成，原游戏输出/电机 revision/stop 附加链不缓存；输入采集、SDK report 节奏、来源守卫和恢复预算不变。Native 附 token 增加 87 字节，接近 16KiB 上限时该次仍请求完整 ACK。超时或拒绝不自动补发同一帧。

有效序号后的旧/错误 token 在 SDK 调用前返回完整 state-rejected/frame-reply-binding-mismatch，按原 post-sequence 拒绝合同消费该命令。目标替换、QUIESCE、RELEASE、SHUTDOWN 及实际本地 teardown 撤销绑定；熵生成失败只影响可选格式。Native 退役/HELLO 与相关生命周期命令也撤销所记 token，不能靠相同 PID、run/epoch 或旧 PREPARE 回执授权另一目标的紧凑回复。

紧凑回执的 fast 与 DOM 回退都检查本次请求 token、动态请求身份及严格无符号版本/序号；DOM 只能在完整 raw message 经 observeWire 检查（非空、16KiB 内、无裸 NUL）、无重复关键键、无混合 legacy 身份字段后投影已绑定身份。旧完整 DOM 的已有兼容语义不被此协议放宽。定向测试发现的版本截断/浮点转换缺陷已经修复，原失败保留。

本阶段没有 CPU 降幅或整体虚拟手柄稳定性验收。真实当前 Host 构建通过；原生 3ad0ba74 epoch 的 Release/Checked 定向各409条/1378断言通过，私有 Win32 pipe 每配置5条/193分片通过。其后 native 只增加87B请求预算守卫，另记源码身份及编译结果，不倒签旧回执为新 whole-main 验收。实际产品 pipe/完整 timeout 事务仍受旧生命周期夹具闭包缺口限制，未知项保留；完整夹具默认 NOT_RUN。固定成本尚未执行，876→271字节仅是同语义离线样例的 ACK 体积差异。

证据入口：G:/YeManCC-Work/Testing/YMCC/Evidence/VirtualGamepadSteadyState-20261103/CompactFrameReply/。旧2026-10-25 UTC成本有界归因位于同专项 LegacyLogAttribution，不能冒当前版本基线。新 native 诊断入口仅显式 tools/fixtures/inputhost_compact_reply/native.mjs --out <该证据根下新目录> --case-group compact-regressions，续作消费原 --budget-ledger；不增加默认minimum、父门或产品启动项。


## VC-ROG-PARTIAL-HIDE-20261103-R1 — 原执行事务半完成补齐

164155现场PID11292：原SD请求已写HID子节点，USB根归还责任已持久保留，但hide事务失败，刷新未请求；Steam仍有实体与虚拟入口。此处补的是正常执行，不把它记成Stop/Start自愈。

唯一接点为原late-hide worker：首次Apply失败且deferredStartHide时，仅ROG原Host/run/request/power/persona与原截止仍有效、writer结清、原内置完整身份与owned HID/root责任都明确，读回恰好一项已隐藏一项缺失，才进入一次原事务补齐。共享debt.executionContinuationAttempted先消费，不因同源通知重置；refreshAttempted仍独立控制原一次cycle。无新定时器、常驻扫描或target。

补齐复用原active Apply/Refresh与BeforeHide的持久归还责任，TLS同时钉住原完整物理identity、root、HID和两项目标集合。实际Refresh的新发现或目标集合漂移立即拒绝；其第二份配置快照也必须有效、cloak on、应用登记且inverse off。补写接受后再次读回全部配置/原两项目标，重新确认Host transport、writer与原意图，才允许原单次根刷新。端口ACK可能导致短暂掉源，仍沿用原IntentCurrent收尾，不要求错误地立即source live。

未知或失败保留责任，不授权第二次补齐/循环cycle。日志input-host-rog-hide-continuation区分write accepted/readback/configurationCompleted，仍不把配置读回称作Steam外部抑制证明。共用最终检查和Stop/Start保底由原R3自愈独立维护；非ROG、已有完整隐藏、OFF归还改善与玩家SD/PS5选择保持。

原controller.msi-late-hide-pending在原19个场景中扩ROG/ROG-X反例；旧FAIL与旧437范围保持。新实际检查数和运行身份以后续独立执行回执为准，不能仅凭本注释计PASS。

## 2026-11-05：OneX 开启后迟到拓扑通知的扫描证据接续

163720 的当前进程 PID3200 有9个唯一终态，其中8成功、req5 Elite 原30秒失败。req5 已完成一次 USB cycle、slot0 重新准入及同根新 HID 子节点登记，随后 `onex-post-refresh-same-source` 失败；重确认时边界为88，稍后 late-hide 的 topology 已为89。旧日志没有保留其后的连续 boundary/sample 谓词，不能把单个候选条件宣布为现场唯一根因，也不能把其他目标的新请求成功称作同请求自动自愈。

`VC-ONEX-FAMILY-BOUNDARY-20261105-R1` 只补原 OneX T1 等待中的活性缺口：源已准入而迟到设备通知使四槽扫描证据过期时，`preTargetSlots!=0` 会使普通首次准入入口不再补扫描，原等待又先要求同代扫描，造成逻辑等待至截止。仍在原 Host/current-ticket/缓存/反向槽/唯一物理源/selected 身份围栏内，向原采样 owner 提交该代的一次 `FullScanOnce`；已有 pending 不覆盖起点，不能直接写 `BoundaryScanEpoch` 或 `BoundaryScanned` 成功。请求后走原释放锁及采样机会，下一轮重新核对完整同代边界、fresh sample 和隐藏准入。

不递增 topology/identity，不重发已可能执行的 cycle，不新建 Host、不续原30秒、不改变 persona 或其他家族。扫描请求不是扫描完成；若原采样 owner 不能给出新证据，仍按原 deadline 唯一失败和原清理义务结束。原事件新增 `boundaryScan` 有界诊断，保留 `freshSameRootSource` / `externalSuppressionVerified`，最多4条请求事件及最终累计；其布尔值不代表外部 Steam 收到输入。原47场景身份和原失败保留，新参数执行以独立回执为准，尚未运行时不得引用旧47/1259代替。证据：`Testing/YMCC/Evidence/VC-OneXFamily-20261105/field-163720`。
## 2026-11-05：OneX 首次刷新前的同根当前 GAME 子节点接续

180756 当前PID10788已运行 BOUNDARY-R1，SD/Edge成功，Elite请求3和5都在PREPARE前以 refresh-unissued-context-retired 到期。hide resolver对已选实体允许完整同根同VID/PID的新child返回，但不更新已resolved的RawInput selected；journal拿IG_00，selected仍为IG_01。family新增的最终精确selected检查因此可以明确拒绝这个正常同根接续。它也防止不同根B借用A的数值槽，所以不删除该守卫、不用忽略大小写或同槽号替代身份。

VC-ONEX-SELECTED-CHILD-20261105-R1在原Start派发前接续selected：仅旧selected和journal同一个完整USB实例根、同GAME能力及VID/PID时，重读共同resolver折叠前的候选；必须完整、单根、去重后唯一child，与journal/current physical的完整identity、interface和base path吻合，并再取原反向槽证明和Root/location。原sampling/cache/raw三锁重验旧selected、库存代、topology/identity/power、同一ticket/Host/意图/未派发与source mask后，发布完整current identity，继续原final exact MayIssue。锁竞争只沿原等待，不增预算。

共同只读resolver新增可选eligibleChildren输出；未传时保留原返回根列表、首选算法和complete语义。一个折叠后的root不当成唯一child证明。多个活GAME child、不完整inventory、跨root、slot复用、上下文变化不获得发布权限；外来A/B仍继续由原final guard拒绝。新诊断selectedCacheAlignment区别接续前/owned child、完整性、是否实际发布；发布selected不是已刷新USB/目标就绪或Steam消费回执。原30秒、至多一次cycle、未知派发不重放、唯一final和归还义务不变。此为普通开启完善，共用自愈未改。

本段参照与实际代码同交付；新夹具参数和产品编译分别留在field-180756/selected-child-repair-r1，未执行不得借旧47/1259或上一轮编译认定本轮覆盖。旧R2/BOUNDARY日志与原失败全部保留。
## 微星屏蔽下的事件式手柄模式恢复（2026-11-05）

现有 `input.msiControllerShieldEnabled` 同时授权微星固定手柄用途，不新增开关或后台服务。仅既有微星已知板号、屏蔽开启、非隔离运行时接入；ROG和公共隔离不经过本逻辑。

- 首次屏蔽意图生效、YMCC窗口打开，以及已提交唤醒代次，向原输入生命周期线程提交一次事件。
- MSI固件的LB＋RB切换组合独立观察原physical-read快照，不依赖YMCC呼出、快捷规则、前台或录制准入。完整按下后等3秒；长按和断连不重复武装，需两个肩键释放后再接受下一次。窗口打开不能提前同次肩键请求的3秒期限。
- 开启虚拟手柄的原 `inputHostStart()` 完成旧Host清理、冷归还、quarantine、visibility、显式重武装与资源门后，在 `ymStartEnsureRoute("main")` 前仅恢复微星官方手柄策略，实际模式由原Ensure直接一次切入mode2，不增加mode1→mode2的两次重枚举；即使肩键3秒未到，也不提前做模式查询或mode1写入。已有DInput源/目标或未结归还责任时，绝不强发mode1；虚拟模式仍由原DInput采集、HID写入与持久归还owner承担。

原生命周期维护仅以原子pending位提前返回，不增加线程、timer、周期模式查询或设备扫描。肩键边沿仅消费已读取按钮；有事件时才读取官方配置、进行一次有界模式查询或模式写入。原3秒延迟在既有worker下一次调度后执行，忙时允许晚于3秒，不提前；过期/跨电源/退出/关闭屏蔽的旧事件不写。查询已经是XInput则不再写；肩键查询不确定或取消I/O未归还时不猜测写入。有效DInput采集保持，只有失源且当前查询报告mode1/mode4时，本次显式事件可用原mode2原语纠偏一次，不扩原自愈重试预算、不新建目标。HID写入回调只读取原子代次与已持锁区内的事实，禁止在命令slot持锁期间获取配置/Host/source锁。

官方策略只在有效32位视图的两项现有REG_SZ均可识别时借用：`Component\User Scenario/AutoSwitchMode=0`、`OsdEditor/ControlModeUserSet=XInput`。不创建缺键、不写64位视图或临时同名键。窄adapter复用原 `PolicySession` 的写前持久化、原值比较、写后回读与反向条件恢复；文件为PowerControl下 `msi-force-gamepad-policy.json`，PID/出生时间、板号绑定及进程间claim拒绝并行未知owner。用户事件重新发现已知外部改值时，先条件释放保留外部值，再重新借用。关闭屏蔽/真正退出时仅恢复仍等于本次自有目标值的配置；恢复失败保留责任，后续事件/进程可继续，不静默丢弃。

官方两项配置语义依据固定研究包，不代表所有MSI版本或全部固件writer已被证明阻断。模式响应无nonce，按原严格0x10/0x27协议识别并保留未知；配置读回、命令ACK与原生编译不当作游戏实际输入/全机型验收。

本轮证据位于 `Testing/YMCC/Evidence/MsiForceGamepad-20261105`：`final-runtime`执行实际runtime与planner、仅I/O/时钟边界内存替换的24项（含前置不双切模式与不提前肩键期限的反例）；`oem-shield-existing`复用原OEM屏蔽484条断言；原生完整TU冻结来源语法检查与策略事务测试另列原始回执。本轮未修改安装版、真实任务/注册表或实际微星硬件，尚未打包部署。
