# YeManCC Steam 睡眠唤醒本地执行任务书

版本：2026-10-14。当前起始源码基线R34；后续以实际源码marker、最新交接及构建回执为准。用途：让机器端AI和操作者连续完成取证、定位、修复和验证，减少每轮原始ZIP回传。本文不要求安装本地模型；能在测试机器读授权文件、运行终端并保留上下文的执行端即可。

## 1. 完整目标与成功标准

主目标：唤醒后手柄直接控制睡前同一原游戏实例，不重启游戏或Steam，不手动切换设置。次目标：真实Steam运行关联恢复，大屏真实显示“继续”。不伪造AppID、Running或UI显示。

同一原实例必须联合验证YMCC单PID完整游戏规则、精确Steam tracker、OS PID/创建时间/路径/Windows会话、原Steam进程与账号；有native primary证据时同时绑定它。共享候选一致和分歧分别记录，不能修改共享owner来消除分歧。醒后不按前台或UI焦点换目标/账号。

真实输入由操作者在游戏中确认可见反应（例如移动、跳跃或视角变化），记录时间与动作；控件列表、配置存在、XInput warmed、输入回执、焦点、PID存在和Running行均不能替代它。Continue由实际大屏界面确认；overview/list数据只作关联证据。编译、离线模拟和marker不是实机验收。

第一批安排最多6轮实机周期，优先同一原实例连续3轮，以覆盖“第一次成功、第二次失败”。如原实例/Steam账号变化、前置条件不满足或同一失败签名连续2轮且无新信息，结束该实验分支并转定向分析，不机械完成剩余轮数。一组成功仅证明本机器/游戏/当前配置下的结果，不宣称全部设备族群修复。

## 2. 起步文件与路径

主线：`G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC`。
研究：`G:\YeManCC-Work\Docs\Research\Sleep\SteamNonSteamRecovery-20261011`。

进入机器后，顺序读取：

1. 工作区AGENTS.md、主线AGENTS.md、STEAM-UPGRADE.md和docs/steam-integration-map.json。
2. 研究目录LATEST-STEAM-CONTINUATION.md。若交接已更新，采用其明确的新证据，不回到旧轮次。
3. 起始R34的ROUND34-CAPTURE-BUDGET-GRACE/CONTINUATION.md、REQUIREMENT-AUDIT.json、validation.json。
4. LOG-20261014-1131519-STEAM/evidence-summary.json及REPORT.md；这是已分析结论，默认不重新解包重读整包。

在主线运行`node tools/steam-integration-audit.mjs`；初次机器接入或涉及接口移动时执行，不每个睡眠周期重复盘点。13类/308候选/0缺失0未归类是2026-10-14记录，仅盘点。

实际测试机器若使用不同路径，由操作者给出项目根、安装根和授权日志路径；记录映射。不要扫描桌面/全盘寻找包，也不要把固定G盘路径误当任意机器的已存在路径。

## 3. 当前事实：避免重复与误判

源码marker：`steam-wake-round34-capture-budget-grace`。

R34把Steam睡前只读捕获的累计预算150ms→1000ms；增加规则快照/锁刷新、tracker IO、tracker解析、native主PID读解析、候选YMCC/OS校验和最终重验的阶段/累计耗时。每阶段不重置预算。仍保留固定原睡眠边界、1500ms采样、58500ms健康结算、60000ms醒后操作期限和最多3次精确零发送预约。

既有验证已完成：307native、78接线、21导出、完整捕获主线快照隔离BUILD_OK。172/500/1000ms模拟延迟通过，1001ms与时钟倒退拒绝，身份/账号变化仍拒绝。没有新代码或失败时，不重复这些检查充当新进展。

已验证隔离EXE：`G:\YeManCC-Work\Build\Validation\SleepSteamRound34\native-current-delivery\Build\App\Native\YeManCC.exe`，SHA256 `4a40aa241c13478501b754e160dbb62afdb69daf8c86e25bcfe0597ea7dc1582`。这个哈希仅对应捕获快照产物；新的完整主线构建哈希可能不同，须以源/构建/实际EXE回执联合核验，不能复制旧回执。

1131519授权包真实运行R33，磁盘native SHA256 `83051b90dea8db9d79eec2db9751e608e4e0f15f745299e3964d8abb48b138c3`，不是R34实机。

- generation2：preferred9196累计172ms被旧150ms预算拒绝，capturePid0，未联合绑定，动作0。
- generation3：原11960/birth134364217869743774、Steam12752/birth134364214845564875联合通过；tracker两成员，native主PID与共享候选匹配。37样本原身份matched、ControllerCount全1；首份醒后overview4/listtrue，armed+9593ms首次11/listfalse，观察至58656ms，动作0。
- 原游戏在前台时，Steam导航上下文显示gamepadOverlay/interactive非idle；另有未知前台。GetFocusedWindowInstance来自导航active context，窗口类型不独立证明OS可见Overlay。是否误拒绝尚未证实。
- current是11:28另一次YMCC启动后的gen0/idle，last保留先前gen3；不能混用，不把导出elapsed当持续活体采样。

当次Steam版本1791918634，官方stable/publicbeta manifest均返回同版；已核验UI包ScopeRunningApps长度1038/FNV4095097876与R30已允许函数逐字相同。历史版本快照不保证以后最新，亦不证明field-loaded UI包哈希或全部Steam模块兼容。此版已知Scope契约不用再盲目改白名单。

## 4. 分工与执行范围

机器端AI连续负责：授权文件/只读OS取证、时间线和代际关联、定向生产代码复现、窄Steam修复、必要回归、隔离编译、批次摘要和交接。每个已授权且可逆的本地分析步骤无需等待中心AI回传。

操作者负责：启动目标游戏、真实睡眠/唤醒、真实手柄动作、实际Continue查看，以及自己决定并执行测试版本安装。本文只提供这些操作的测试步骤，不让AI自动触发睡眠/重启/部署。没有额外授权时，AI不覆盖安装或共享Build/App，不重启游戏/Steam，不改变输入设备/虚拟手柄/HidHide，也不自动更新Steam或切分支。

只处理Steam。保留其他任务改动；不改风扇、虚拟手柄、InputHost、HidHide、MSI黑名单、共享游戏阀门/睡眠调度/暂停所有权。设备配置固定为操作者已有状态；设备列表编号不用于猜测虚拟/物理根因。

保留一个默认开启开关，尊重用户Off。主功能测试须由操作者确认其当前开启意图；若Off，AI不自行开启，记录只读/取消结果。弃用Overlay关闭策略保持false且不可达。不采取listener覆盖、未知全局Resume、forceinputappid、伪装正版AppID、私有进程内存写入或伪造Running/显示。

## 5. 执行顺序与本地闭环

### A. 一次确认测试对象

确认安装EXE、正在运行的实现marker、源/构建回执、Steam实际版本和分支。磁盘EXE哈希不是loaded image证明；至少把运行marker与安装/导出回执分列记录。当前R34还没有本任务正式release/部署结果；如果机器仍运行R33，先报告版本不符，不把它作为R34成功/失败。

完整release必须同一源码和workspace完成，再核验实际EXE和ZIP。`pnpm run release`会写该workspace构建/发布目录；本文不授权覆盖既有共享目录。需要新构建时先在隔离源码/输出workspace完成，使操作者能审核产物；正式安装由操作者处理。不要把本任务的隔离native EXE单独塞进旧Release假装完整新包。

在执行终端中使用PowerShell7，并确认Get-FileHash可用；旧Windows PowerShell5.1在之前环境缺该cmdlet，最终7.6.5已通过，不重复当源码缺陷。

```powershell
# 以下先设置真实路径；不靠改旧回执匹配新源。
$steamTaskProject = 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC'
$steamTaskWorkspace = 'G:\YeManCC-Work\Mainline'
$steamTaskExe = '填写本批实际完整构建EXE绝对路径'
$steamTaskZip = '填写本批实际正式ZIP绝对路径'
& "$steamTaskProject\tools\verify-steam-sleep-export.ps1" `
  -ProjectRoot $steamTaskProject -WorkspaceRoot $steamTaskWorkspace `
  -NativeExe $steamTaskExe -Zip $steamTaskZip
```

若使用隔离构建，ProjectRoot和WorkspaceRoot必须改为该构建实际对应根，不能继续冒用上面示例的正式主线路径。

### B. 离线小矩阵：便宜地验证真实边界

只在本轮有代码修改、明确新回归风险或待复现缺陷时扩展并运行。使用生产header/生产JS和已有确定性OS/CDP seams，不另写一套永远返回成功的替代状态机。不将相同307组重复100遍算100批。

优先用如下6种累计延迟×8种条件，得到48个有意义的定向组合；模拟时间无需真实sleep等待：

| 累计延迟ms | 条件 |
| --- | --- |
| 0 / 172 / 500 / 999 / 1000 / 1001 | 原绑定保持、原PID出生变更、Steam进程变更、账号变更、native主PID变更、规则代际变化、用户Off、新睡眠代际 |

前提和期望分开：0–1000ms且全部准入条件不变才可接受完整捕获；1001ms或任一权限/身份变化不授权恢复。Off/新睡眠应在生产owner入口取消，不要求纯parser单独承担取消开关。不可让晚于原边界出生的进程因增加时间预算变成可接受。故障注入时点至少区分capture、发送前和发送后；已发送/timeout/未知stage不能退款或重复发送。

还需针对具体新证据验证：已知0发送前台取消最多3预约；原窗口延迟回读只继续只读观察、不重放RETURN；local11/未知状态不写Running；controller缺席不能仅凭cached focus判健康。已有R31–R34回归已覆盖的部分可引用，新增断言应填真实缺口。

修改后按影响范围运行：

```powershell
# 输出目录使用本批独立路径，避免覆盖历史验证。
python -X utf8 tools/sleep_steam_only_selftest.py --output <本批独立输出目录>
python -X utf8 tools/sleep_steam_wake_admission_selftest.py
# native交付与marker改变时才做导出守卫；OldNativeExe/NewNativeExe填实际对应产物。
& tools/sleep_steam_export_guard_selftest.ps1 -ValidationRoot <本批独立输出目录> `
  -OldNativeExe <旧EXE绝对路径> -NewNativeExe <新EXE绝对路径> -RealProjectRoot <当前源码根>
```

生成独立native/deps/tools源码快照，设置该隔离workspace的YEMAN_WORKSPACE_ROOT，再执行快照内native/build_native.bat。它会产出EXE/资源及相关native helper，不是只编一段测试代码。保存源码哈希、完整build日志、退出码、实际EXE SHA256。不编辑并行main.cpp来消除无关问题；若构建后并行修改发生，记录差异和Steam关键wrapper是否匹配，不无目的追着重编。

### C. 实机配对：优先打到第二次、第三次

保持同一游戏实例、Steam实例/账号及现有设备配置。每次睡前确认真正游戏输入可用、真实Steam关联现状，记录健康/不健康/未知；前置状态已经坏的周期可以作为诊断，不能算“健康睡前→失败唤醒”新复现。原实例变化就结束该连续实例组。

操作者手动进入真实睡眠、手动唤醒。AI依据真实kernel事件fileTime和YMCCgeneration分周期；单看处理日志时间不能替代事件时间，旧506/507不混入新一轮。没有入睡/醒权限或未联合绑定的周期标记“未进入恢复验证”，不计功能成功率分母。

醒后操作者在约+5s、+20s、+45s和+75s用手柄做同一个可见游戏动作，记录反应、动作和实际时间；偏离计划点时记真实时间，不补写。前75s本批AI只做轻量只读取证，把编译、离线压测和大体积导出安排在实测前后。产品60000ms恢复权限不延长，+75s是人工结果确认/只读观察，不授权迟到的修复动作。若发生明显失败，继续观察到该轮终态，避免只截几秒。

Continue查看由操作者记录真实大屏按钮及时间。配对期间尽量保留相同前台条件；若为了查看Continue切到大屏或返回游戏，记录每次人工焦点操作，不能把该操作当自动恢复。若会干扰第一/第二次对比，把Continue人工检查放在该配对末尾，前轮Continue标未知，另做同条件验证；后台overview不得补填为“亲眼Continue通过”。

最多6轮并非硬凑数。建议先同原实例做3轮，再依据新证据安排其余周期。每轮完成后AI在机器端立即分析，不等原始ZIP上传：版本/准入/事件顺序→同实例→运行关联→交互保护→发送/回执→实际输入/Continue。第一、第二轮同签名重复且已无新字段时停止同场景反复睡眠，转D中的定向取证/修复。

### D. 三个优先攻克的缺口

1. **捕获超时**：验证R34实机是否仍超1000ms；分清规则锁/刷新、IO、解析、最终重验。先针对慢阶段优化或提出有实测依据的时间容错，不把所有预算无限拉长。一次capture按同一起点累计，原边界与身份权限不放宽。
2. **UI交互误拒绝是否存在**：固定同原PID及Steam会话，对比原游戏OS前台、导航active context、Overlay窗口类型与可验证的实际Overlay可见/交互状态。新字段须从实际版本官方/一手源码和只读契约核验来源/类型/可用性。未知记unknown，不能因为原游戏在前台就凭空写“Overlay关闭”。仅证明某一保护条件误判后窄修，不直接删保护。走已有受支持原实例恢复通路，记录是否真正发送与之后真实输入。
3. **local overview4→11仍有原PID/tracker**：精确记录首次current和首次loss时间、主PID/Steam账号/Tracker变化、暂停释放、控制器列表、OS身份和用户动作。直接层是steam-ui-native-overview-store；不要扩大成所有后端已丢进程。现有Scope仅本机Running4/Launching1且列表缺项可重建UI列表；11/未知不伪写状态。只有已验证的同原实例接口才提出新恢复，不做伪装正版或信号锁定绕过。

失败若表现为Steam关联完整而设备输入链路自身不成立，分列证据和归因未知，不越界修改虚拟手柄/Host。controllerCount1、Config匹配或native ACK不能证明设备端没问题。

## 6. 每轮最小证据

每轮一份CASE-RECORD.json，可复制随书模板。必须区分null/unknown、false/fail与true/pass，不从文件名推结论。至少包含：

- 实际运行marker、EXE磁盘哈希及来源、构建/导出回执、Steam实际版本/分支。
- 本轮generation/configEpoch、真实kernel入睡/唤醒fileTime与处理时间；完整prepared/captured、wakeAdmission/readiness和终态事件。
- 原游戏PID/birth/path/session、Steam PID/birth/root/账号身份是否匹配；native primary/tracker、YMCC单PID、shared一致/分歧。
- 捕获各阶段累计/分段耗时，真实采样次数和lastElapsed；current/last来源代际和时间。
- 首份醒后overview/list/focus/context、首次loss的观察时间、原实例是否仍匹配；缺观察不得把导出elapsed当持续观测。
- input/inputDispatches/list/pidWindow计数；每个动作stage/requestDispatched/mutated/accepted/nativeAdmissionReason，未知回执字段记未知。预约次数不等于执行成功。
- 操作者原游戏输入和实际Continue的证据、时间、人工焦点/设置/重启操作。主/次目标分别判定，不混成笼统“好了”。

原始证据保留在本地指定批次目录，完整日志/用户授权ZIP有SHA256与文件名/行号索引；按白名单收集，避免每轮重新把全机日志搬一遍。可在批次末导出一次涵盖全部周期的包并按事件generation分段；current/last只有快照，须保留覆盖各周期的sleep-facts事件，缺任一周期终态就标不完整，不填造结果。

## 7. 每批回传：摘要即可先决策

回传BATCH-SUMMARY.md和CASE-RECORD摘要，不每轮上传完整ZIP。每批表格包括：case/generation、marker、原PID/birth是否保持、准入、首次关联丢失时间、实际发送、真实输入、真实Continue、结论/证据索引。报告各类分母：计划周期、有效健康睡前周期、准入周期、可判实际输入周期、可判Continue周期；不把unknown排除后声称百分百成功。

至少列出：新发现、已修代码、相关回归和隔离构建、尚未证实的假设、最小下一步。本地每一个有明确证据的窄修继续执行并验证，无需等中心AI逐条确认。到无可证实改进时记录缺的具体只读证据/真实行为或契约并等待；不因没有新ZIP而重复旧日志、编译、测试、猜测改代码。

中心AI只在需要源码/原始证据复核时索取对应片段或指定包。上传由操作者执行，AI不自行将文件发给第三方。

## 8. 本批交付标准

交付实际patch/owned.diff、改变文件清单与源哈希、定向回归、捕获快照构建回执、case记录、批次摘要、CONTINUATION.md与需求审核。保留完整主/次目标和模块边界。若仅修复capture预算或某个UI条件，只报告对应范围通过，不声称同原游戏真实输入/Continue已达成。

本机连续多轮真实输入和Continue满足时报告“本机本游戏本配置通过”，列出周期与证据。广泛设备/游戏族群验收另列未覆盖项。零写入可以正确完成健康会话，不要求恢复动作次数大于0。
