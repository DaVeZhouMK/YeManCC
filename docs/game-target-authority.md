# 唯一游戏目标与广播合同

2026-10-24：原生游戏阀门负责识别和选定，前端各模块只消费同一份权威快照。

## 专属配置与输入边界（2026-11-18）

Y 菜单以固定行显示游戏名、添加/删除专属和自动记忆状态。`game-custom.json.autoMemory` 是全局记忆开关，缺失时关闭；开启后由 `gameProfileAutoMemory` 消费同一游戏身份通知，调用原专属性能与独立帧率写队列创建配置，不增加进程扫描或轮询。当前游戏手动删除后不因重复通知立即重建；旧身份、关闭自动记忆、退出和睡眠失去准入会撤销尚未提交的创建请求。

按用户最新要求，游戏专属不再管理虚拟手柄或陀螺仪。输入只由全局设置及四模式管理，Decky 和轮盘消费同一全局输入。旧 EXE 输入字段仅保留磁盘兼容，前端不再建立覆盖，原生 `gameInputOverrideActive` 始终拒绝旧覆盖，普通输入写入清除 `input.gameOverride`。这条规则取代旧文档中“首次专属同时记忆手柄/陀螺仪”及“游戏专属输入优先”的约定；性能、帧率和快捷规则的其他游戏边界保留。

## 数据链

1. nativeValveAcquire 在锁内捕获 NativeGameValveSnapshot，包含游戏身份、generation 与 identityGeneration。
2. nativeGameTargetJson 同时用于 game.detect IPC 回包与 game-target.json 文件，字段不再分别拼装。
3. nativeValvePublish 先尝试原子写入有效快照或无游戏墓碑，再通过原有 game.target.changed 事件通知变更；snapshotWritten 仅诊断外部文件写入结果。
4. src/bridge/gamedetect.ts 是前端唯一调用 game.detect 的模块。它排序、合并、验证并发布结果，保留唯一 1250ms 后台兜底；原生事件、恢复界面与唤醒均复用该读取器。
5. gamePolicyTarget 是同源兼容门面，不另留 PID、不另做 10 秒选择防抖。顶部、Decky、修改器、插帧、变速、专属配置及其自动记忆与动态背景消费该身份。

## 两种代次

- generation：一次原生观察/发布序号。正常心跳递增；较旧回包不能覆盖新观察。
- identityGeneration：选定对象及其准入规则的租约。PID、进程启动时间、路径、来源/规则变化及退出会换代；正常心跳与显示元数据刷新不换代。持续无游戏只更新观察序号。

processCreated 使用十进制字符串，不能转换成 JS number。路径/游戏名不能代替 PID 和启动时间。Decky 的 game.identity 为不透明串，实际有身份代次时将其纳入；客户端不可自行构造。

## 调用规则

- 显示读取使用 getCurrentGame、getPolicyGame 或 detectGame(false)。检测初始化、事件刷新和明确动作需要新观察时，使用 refreshGameStatusStrict / detectGame(true)，所有成功读取都会发布到同一个源。
- 用户显式呼出时捕获的 preferredPid 只属于主动选定入口。普通操作不得拿旧 PID 调用识别器重新仲裁；使用 validateGameTarget 或 validateLockedGameTarget 校验当前权威目标。
- 初始化未知、传输失败和协议异常不等于游戏退出。保留最后显示快照，但 isGameStatusCurrent / isCurrentGameTarget 返回 false，新的游戏相关操作需等成功校对。
- 原生事件中的 PID/路径没有操作权限。事件仅唤醒读取并抬高过期水位，身份必须来自原生回包。
- 每个异步动作捕获完整身份和本地操作代次。排队前、外部副作用前、await 返回后和 UI 状态提交前按原所有权检查；旧成功、失败及 finally 都不能更改下一轮目标或清理下一轮锁定。
- A→B→A 即使 PID 和启动时间仍相同，旧租约也失效。修改器准备任务同时检查其目标 epoch；性能/输入所有者及回收键包含身份代次。
- 进程事实与操作租约分别处理。变速已注入状态属于原进程，不能因选择换代就假称 DLL 已关闭；新操作及其 PowerShell 执行前仍需匹配当前租约。显式清理旧暂停/变速会话只可使用原保存的进程实例，不能借机选定新的全局目标。
- 已发往外部程序的动作不能靠前端广播撤销。保留各功能原本的边界、预算、回读和必要清理；过期结果不得被显示为新目标的成功。

## 验证与交付

本轮证据：G:/YeManCC-Work/Testing/YMCC/Evidence/GameTargetBroadcast-20261024/REPORT.md。

覆盖原生锁内快照/序列化、原生 JSON→IPC→policy、原始 PowerShell 校验、快捷锁竞态、连续游戏切换、PID 复用、未知状态、订阅重入与异常隔离、Decky、修改器、变速和关闭游戏。只读内存回放不等于真实多游戏、Steam 全屏或安装版验收。保留唯一低频发现兜底；工作集准入变化不能假设总有可用事件。


## 风暴、CPU 去重与有界恢复（2026-10-24）

- 尚未发送的显式 PID 选择先核对 selectionEpoch，仅最后仍有效的选择进入原生。已经发出的请求仍按代次、出生时间与租约验证，不能把取消前端等待当作撤销原生操作。
- 原生变更、窗口恢复与唤醒复用同一自动读取入口。健康时首次通知立即读，100ms 窗口内其余通知共用一次尾部校对；成功提交代次的重复旧通知直接忽略。显式动作的严格读取不受此窗口阻塞。
- 唯一 game-status 任务仍为 1250ms，最近已有共享读取时跳过相邻重复槽。普通 heartbeat 只更新观察，不重发相同游戏配置；原生本来已去重 game.target.changed，外部共享文件的新鲜度刷新继续保留。
- 当前读取失败保持最后显示快照并撤销新操作准入，最多另外唤醒三次，间隔 500/1500/4000ms。恢复成功才重置预算。预算耗尽后继续依赖原低频任务、新事件或显式操作，不建立第二条永久高频轮询；故障期间通知风暴也遵守退避。
- webview.recovering 在空闲时也立即撤销准入，至少等 500ms 后通过同一权威入口只读校对。失败、协议异常和无游戏回执保持区分；绝不借恢复自动重放业务写入或捕获的旧 PID。
- subscribeGameTargetReadiness 独立发布准入布尔变化，重复值不通知；它本身不保有轮询。Decky 用它刷新灰/亮状态，同一游戏恢复不重发性能、输入等身份配置。
- 最后身份订阅退出时立即取消本模块的尾部/恢复 timer。纯后台观察的晚回包不发布、不补读；未回包的单次 IPC 最多 8 秒释放。若已有明确动作共享这次读取，该动作保留读取所有权与原有有界校验。
- 游戏标题规则的正则只初始化一次，策略门面直接返回唯一源已隔离的副本；保留不同消费者间的快照隔离。

本轮主报告：G:/YeManCC-Work/Testing/YMCC/Evidence/GameTargetStormRecovery-20261024/REPORT.md。

边界：同一原生进程的合法乱序可通过新读取追上；错误超高事件水位与“保留旧前端连接另一个原生宿主”的跨实例恢复尚无认证会话协议。两者继续拒绝操作，不降低代次绕过。若共享调度器永久卡死、三次额外恢复也耗尽且无任何新事件或动作，本轮不承诺继续自动恢复。模拟请求数不能当作整机 CPU 百分比或实机睡眠验收。

## 双进程游戏与 Steam 公共接洽（2026-11-13）

游戏识别与 Steam 保持两个唯一事实所有者。`NativeGameValveSnapshot`、`NativeGameSelectionReceipt` 和前端 `DetectedGame` 始终表示原游戏层选定的实际游戏进程。启动器、Steam 注册主进程或叠加层宿主不覆盖它；进程组成员顺序也不表示操作顺序。

`gameSteam.context.get/refresh` 可附带 `steam.processGroup`，由唯一 Steam TrackerOwner 已有的 `groupObservation` 投影。它保留完整 GameID 十进制字符串、选中游戏 PID、原始日志行号和最多 32 个历史成员。`processValidated/currentSource/roleValidated` 固定为 false，没有成员出生时间或动作授权。字段缺失兼容旧协议；可选组损坏仅降级该字段，不清空已知游戏和原关联。投影不增加 Windows、注册表或 Steam 查询。

动作使用原消费门：暂停、关闭、变速、修改器、插帧及返回游戏消费实际游戏的 PID/出生时间/路径及选择租约；恢复旧暂停或变速只消费其原保存实例。Steam 需要不同角色时，由公共 `ProcessGroupLease` 的持有对象和角色检查提供，消费者仍须保留自己的当前选择、账户、电源和最终动作门。历史日志、共享显示 DTO 和角色拓扑均不能单独授予动作权。

共用查询使用 `gameSteamContext` 在途合并与原生唯一 TrackerOwner；显示和菜单不得另扫进程或解析 Steam 日志。已知 `Shipping.exe` 与启动器同属于一个 GameID，并不要求把所有游戏操作扩散到整个进程组。

本次元数据原件：`Testing/YMCC/Evidence/SteamGameIdentity-20261113/GameOwnerDualPid-20261113/METADATA-MAINLINE-IMPORT.json` 与 `METADATA-QUALIFICATION.json`。完整消费链核收与 Steam 动作角色配套分别登记，不把该显示 DTO 通过称为所有双 PID 操作或实机恢复通过。

## PID 消费端收口（2026-11-13 后继）

| 消费能力 | 身份与最终操作 |
|---|---|
| 暂停／恢复 | 暂停回包按原请求 root PID＋birth 匹配；不取数组第一项。按钮也带 birth 读取状态。恢复仍消费原保存实例，切换游戏不转移暂停债。原生暂停只对合格 Shipping 根进程写 marker／调用 NT，启动器保持独立。 |
| 关闭游戏 | 前端与两个原手柄入口共用 `game.close`。快捷键在入原串行队列前捕获 PID／birth／path／identityGeneration；最终在唯一选择锁内复核，并对同一持有句柄调用终止。只关闭实际识别的游戏进程，不遍历 Steam 组、启动器或子进程。原 BAT 文件保留兼容用途，YMCC 这些入口不再调用它。 |
| 等待退出 | `game.waitExit` 持有原 PID＋birth 的句柄，复用公共电源撤销事件作一次有界等待。PID 重用表示原实例已结束；拒绝访问或等待错误仍是未知。没有 `tasklist` 循环，也不重新选择游戏。 |
| 恢复后回焦 | 手动恢复只把成功回执对应的原请求 birth 和开始电源代次交给原焦点 owner。旧记录缺 birth 时可以沿 marker 恢复，但不能通过重采当前 birth 来认领替身窗口。 |
| CPU 策略 | apply、restore、失败 rollback 都在各自实际写句柄上核 birth，保留原 CPU 策略、选择、顺序和还原值。 |
| 修改器／CE | 当前游戏身份交给原准备／启动／扫描 owner，缓存文件与可启动权限分开；启动脚本获得 Shipping PID＋birth。外部修改器内部的进程选择不是 YMCC PID 授权。 |
| 小黄鸭 | `gamePid/gameProcessCreated/gamePath` 属于游戏；`pid/processCreated/exe` 属于 LS 工具。顶部和 Decky 共用本体 owner、共同锁与完整当前目标检查，历史 launcher 元数据不改变任一身份。 |
| 变速 | 正常操作消费游戏层当前租约；清理只消费原持有实例。目标退出而启动器仍在时不得改为操作启动器。OpenSpeedy 最终 pipe／注入的真实二进制边界另列实机范围。 |
| 专属／输入／锁帧／媒体／监控 | 继续消费 `gamedetect`／同源 `gamePolicyTarget`。EXE 配置键、显示窗口 PID、工具 PID 和 Steam 角色各保留用途，没有新增第二个游戏仲裁器。 |

动作与等待的 IPC 在原后台派发链执行，没有新增常驻线程、定时器或 EXE。正常成功关闭不再额外调用 `process.identity` 和 `taskkill`；退出确认不再每 200ms 同时调用 identity 与 tasklist。1001 个并发识别读取仍合并一次源查询，缓存消费不替代最终持有实例核对。

本后继精确写回见 `GameOwnerDualPid-20261113/CONSUMERS-MAINLINE-IMPORT.json`。实际范围包括 native close/exit、CPU 写句柄、原生暂停、恢复回焦、前端状态、修改器、变速，以及真实顶部／Decky／LS owner 的定向交错；各 epoch、原反例、准备错误和 UNKNOWN 分开保留，不拼完整默认父 PASS。固定合并 main 已做一次语法检查，完整 Native/link/包、真实 Windows 游戏/睡眠和 Steam 角色 engine/driver 最终配套不由该检查代签。


手动暂停/关闭还必须在前端串行队列入队前固定完整选择，不能在出队后从新快照补造原请求。手动原生暂停在原操作锁等待前捕获同一选择与公共电源；最终调用 NT 时保留原选择锁、同写句柄 birth/image 与公共 current/撤销检查。最终检查抛异常时先归还 HANDLE；尚未派发 NT 只移除本次新 marker，已经 attempted 的恢复债继续保留。原睡眠捕获默认路径不借用此 Ready-only 手动选择门。具体原件为 `GameOwnerDualPid-20261113/native-manual-pause/SOURCE_READY.json`；20/26 与异常后继5按各自源码核收，不拼为一个当前完整父 PASS。


重复的真实可执行后缀（例如 `Stray-Win64-Shipping.exe.exe`）必须保留。规则用 `@exe:` 标记完整真实basename，生产/成员/文本/native/UI共用此明确语义；Windows进程名仍只去实际末层，不用PE OriginalFilename代替，也不放宽glob。UI显示真实文件名。未知或畸形标记在写前拒绝并保原数据。

RTSS复位确认、等待和关闭消费唯一游戏层的当前结果；未知不等于无游戏，取消/隐藏/失活后的迟到回包不得复活请求。启动轮询可显式使用原processInventory250ms缓存，但成功前保留fresh确认，普通proc.running仍fresh。该前端门不代签native已经排队的Kernel动作原子性。完整来源、各epoch和余项见 `GameOwnerDualPid-20261113/ALL-CONSUMERS-CURRENT.json`。

<!-- GAME-RTSS-NATIVE-IDLE-CONSUMER-20261113 -->
2026-11-13 双PID消费后继：RTSS页显式关闭／复位用 `rtss.stopIfNoGame`，原IPC receiver入队前捕获唯一Valve已确认无游戏receipt。健康空闲刷新保持，未知／游戏／规则／电源ABA拒绝旧点击；小型撤销序号只在原Valve锁内管理，不是新游戏事实。固定原5工具在同持有HANDLE核PID／birth／path／session，整组复核后在原选择锁内终止，锁外沿原30秒及共享power取消确认；失败／partial不taskkill回退、不继续复位，普通／开机原路径保持。无新后台timer／thread／EXE。

原生core45／epoch反例、前端143／原路反例、helper35项281断言前缀与13项106后缀及3旧体控制分来源核收；两夹具FAIL保留，不拼完整父PASS。18实际RawProgram Job关前0，固定完整main /Zs及API／桥类型通过；6文件精确导入保留并行字节。中文原因仅字符串后继，不重跑143。SOURCE_READY只签此配套源码；完整Native link／Web／包／部署、外部二进制与实机、Steam角色engine／driver正式组合仍OPEN，整体ACTIVE。

原件 `Testing/YMCC/Evidence/SteamGameIdentity-20261113/GameOwnerDualPid-20261113/rtss-native-no-game/SOURCE_READY.json`、`MAINLINE-IMPORT.json`、`FINAL-BINDING.json`；唯一总索引仍是同父 `ALL-CONSUMERS-CURRENT.json`。不重跑旧暂停／LS／trainer／speed及R1／R2组，不追赶并行旧包。
<!-- /GAME-RTSS-NATIVE-IDLE-CONSUMER-20261113 -->

<!-- GAME-STEAM-R38-CLASSIFICATION-INTAKE-20261113 -->
2026-11-13 R38 接洽核收：公共 `RoleLease/StrictNativeClassification/SOURCE_READY.json`（047d2eda…）仅表示一次原生角色分类与 ABI6／dispatcher1 配套源码就绪。6 个主线文件与冻结来源相同，35 个官方 sourcePins 及原 DLL／embedded 配对已回读；15 份原 Job 回执与 129 个引用文件身份通过核对，本次没有编译、运行夹具或实际 Steam／游戏操作。

原游戏识别 selected PID／birth／path／identityGeneration 继续唯一；公共历史 processGroup 仍只作元数据。原生 P=F=selected 时，helper 不加入原游戏操作的有效性要求；只有合格的非 exact 两成员采样才由引擎启用完整 group current／reference v2，消费层不得自行猜角色。修改器、暂停、关闭、小黄鸭等已登记入口无需因此换游戏 PID 或重跑。

当前恢复 adapter 仍用原 8 参数，新增可选 group lease／独立 request cancellation 的完整 Current、Prepare／Arm／dispatcher 三事件与清理接线由恢复 owner 收口。菜单 registrationFact 短 AppID／full GameID 表示修复由公共 Steam 层负责，不推断 Host PID 与游戏 PID 相同。`fullActionSourceReady`／menu 仍为 false；完整 Native／Web／包、实机验收不由本核收代签。

原件：`Testing/YMCC/Evidence/SteamGameIdentity-20261113/GameOwnerDualPid-20261113/strict-classification-intake/INTAKE.json`、`SEAM-REVIEW.json`；原阻塞记录保留为历史状态，总目标未完成。统一接洽继续使用同父 `ALL-CONSUMERS-CURRENT.json`。
<!-- /GAME-STEAM-R38-CLASSIFICATION-INTAKE-20261113 -->

<!-- GAME-STEAM-R39-REGISTRATION-INTAKE -->
R39 接洽核收只覆盖同一持有 actual-game／Steam PID 的完整 Type2 命令行 GameID 与 UI 完整短 AppID 表示。新增 registration fact4 继续经原 HostInspection／projection／menu !=Unknown 消费，旧0..3含义及原PID／birth／path／session/current不变；唯一游戏层的metadata仍使用完整GameID字符串，不因UI短号改游戏PID。3配套文件、35官方sourcePins、80引用和9份原Job回读通过，本轮无新测试／编译／查询。原件 `Testing/YMCC/Evidence/SteamGameIdentity-20261113/GameOwnerDualPid-20261113/canonical-registration-intake/INTAKE.json` 与 `REQUIREMENT-DELTA.json`。正常恢复Current／Prepare／Arm／dispatcher三事件资源组合、异actual／registeredHost关系、配套完整包与物理仍分别OPEN；不把源级配套就绪写成完整动作已通过。
<!-- /GAME-STEAM-R39-REGISTRATION-INTAKE -->

<!-- GAME-OWNER-PAIRED-155729Z-INTAKE -->
155729Z固定完整测试包（74723cb5…／native38090628…／build1bcfa5fb…）已只读核收本侧18源／22原函数体、capture及build identity、实际1499 include／56 lib图和2原Job自然0；定向ZIP回读与原619文件index对应。唯一game消费、RTSS no-game、literal及R38／R39配套来源已进入该完整Native／Web／包。只补来源交付证据，未重跑业务或构建，fixtures NOT_RUN、正式发布未验；本聊天无安装进程、Steam／设备／睡眠操作，不签已加载或实机CPU／双PID效果。公共正常恢复动作及物理仍OPEN；原件 `Testing/YMCC/Evidence/SteamGameIdentity-20261113/GameOwnerDualPid-20261113/paired-export-155729Z/INTAKE.json`、`REQUIREMENT-DELTA.json`。不追赶并行后继重编本固定包。
<!-- /GAME-OWNER-PAIRED-155729Z-INTAKE -->
