<!-- NO-AUTO-STEAM-UI-REBUILD-20261022 -->
2026-10-22 本机回归裁决：自动自愈只能接续服务/transport，不得重建整个Steam UI。20:16:55自动连接修复调用RestartJSContext后，20:16:57原Steam进程创建BPM窗口并抢前台。此要求覆盖下文历史“automatic refresh”许可；保留历史证据不沿用错误准入。native ManualRepair::request拒绝Origin::Automatic，实际JS recoverConnection在模式读/marker/定时器前拒绝；必须点击显式修复才允许完整UI刷新。既有无整页重建的认证绑定、后端事件准备、CSS刷新保留。现场见field-record-no-auto-ui-20261022.json；run.mjs新增repair-auto-ui-negative，移除原生禁止必须在真实自动dispatch断言失败，实际JS/embedded对应负例必须复现一次RestartJSContext。普通启动、100重复事件、新文档/Ready/替换Steam均不可据此刷新；手动修复回归继续保留。证据Build/V/SteamNoAutoFront22/REPORT.md，未据软件回归宣称安装版/真机验收。
<!-- /NO-AUTO-STEAM-UI-REBUILD-20261022 -->

# Decky 更新后断开连接回归

现场：2026-10-20 本机旧 YMCC 退出后，Decky Loader 和镜像服务正常退出；新版启动却停在 `ymcc-startup-observation-only`，Steam 保留旧插件与已失效的旧 bootstrap。字段取自只读现场，详见 field-record.json；不包含凭据或账号。

统一入口：`tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests plugins.decky-update-recovery -EvidenceDir <全新证据目录>`。

执行从当前 `native/main.cpp` 提取的真实准入函数和共享模式完成回调，结合正式 Recovery、ModeObserver 和 Runtime。外部 Steam/模式、设置仅在内存注入；正常 Job 只创建并关闭本测试自身的无业务子进程。分别编译当前消费者和去掉更新接续例外的旧策略，旧策略必须在现场回归点失败。执行完整与嵌入的正式 Steam 模式读取，验证仅回传已运行 YMCC 模块的版本证据，不含 token/地址。升级器使用当前实际生成的启动表达式，Start-Process 以无进程的函数替代。

正常启动不凭保存的开启状态重注入；仅明确更新交接或已验证的新旧模块差异可以恢复 Decky。共享 CSS/Steam 自动启动权限保持独立。关闭优先，旧会话/桌面/禁用/缺证据/同版本正常启动不自动启动 Loader。显式用户再次开启可以重新准入，但仍须通过会话/大屏/来源哈希等检查。

这里只证明已执行的消费者和范围。真实 Steam 重新连接、安装器整包提交/回滚和实物手柄验收须独立记录。

另执行真实 MirrorClient 与前端更新脚本：旧连接上的待确认操作被拒绝，新 bootstrap 改用新地址并重新读状态，旧 socket 的迟到消息被丢弃，已有插件经官方生命周期取得新版本 receipt；健康后无重试定时器。

底座单独升级也有专门反例：当侧栏模块buildId相同，但Steam内存中的官方Loader版本与当前锁定版本不同，可以接续；版本字段只接受严格稳定版字符串，不回传凭据。原生源码准入检查仍会重新确认实际大屏、Steam会话和固定Loader摘要。

更新交接依据正常运行的自有Loader与active镜像，不要求侧栏当前可见/peer仍连接；隐藏侧栏不应丢失整包更新后的接续。关闭、未运行Loader或镜像未active不生成交接。


2026-10-21：同一注册入口增加加载前文档门与 CSS 自动应用、预设退役检查。旧文档残留可以形成 Decky 更新接续资格，但 deckyFrontendLoaded=true 必须阻断子进程启动；只有用户自然产生的新上下文才继续。CSS 基于实际大屏及 PID/birth/root，独立允许补应用，不依赖 Decky 资格。执行实际 PS Automatic Refresh、实际 CSS worker/源码回调、观察不应用负例和旧 CSS 联动描述符迁移；没有任何真实 Steam/Loader/设备操作。较深证据根可能使真实 Win32 临时目录达到旧路径长度边界，建议选 G:/YeManCC-Work/Build/V/<本轮名>；不将该夹具目录问题伪装为产品通过。

2026-10-21：同一场景加入原生 Steam 手动/开机启动实际消费者。34项正例覆盖原生事务、独立副本配置和锁、隐藏无操作子程序；四个编译成功的源故障分别证明开机重放、已有实例重开、过早二次启动、CSS重复联动会被测试拒绝。启动脚本保留为用户独立工具，正式按钮和开机设置不再调用。普通YMCC启动只迁移配置文本，不消费开机权限或启动Steam。

2026-10-21 补录实际 Steam null-window 错误（field-record-20261021.json）。upstream-reinject.json 保存当前锁定官方 helpers.py 的来源提交、原文件哈希和注入语句，仅供测试，不修改 Loader。实际 native launchExpression 与这段官方语句共同回放：旧策略必须触发一次延迟 RestartJSContext；当前保护必须为零；移除保护必须在禁止重启断言失败。发布输入绑定这两个现场/官方契约文件与测试，升级 Loader 时要求重新核验。旧日志缺少调用栈，此用例只证明阻断已知的重复注入重建触发器，不能声称复现或根除所有 ResizeObserver/getComputedStyle 错误。


## 2026-10-21 已有侧栏的传输恢复

普通重开 YMCC 时，共享模式读取可确认同一 Steam PID/birth/root 中仍活动的 YMCC 模块。仅当模块构建 ID 与磁盘插件一致、官方 Loader 版本与锁一致、处于已确认大屏且旧桥接存在时，走 retainedBinding 恢复认证数据连接。此分支没有 Loader 子进程或启动权限，不调用官方入口、插件导入或 Steam 页面重建。实际 CDP 目标在写入凭据前再次核对模块、注册和官方版本；watch 后续上下文事件保留同一道门。

用户/故障 OFF 和崩溃预算优先；会话、来源、模块或版本变化须停止。失败连接沿既有 250/1000/3000/10000/30000ms 退避，重入失败事件不能重置退避；健康连接没有轮询。未确认写请求作废，新连接只读快照。frontend-attached 仅表示 YMCC 桥接已建立，不代表官方后端或第三方插件全部恢复；实际面板可用性另查认证 peer 和 snapshot。

模块/Loader 不一致时继续阻断重复 Loader 注入，由用户下一次自然重开 Steam 加载磁盘新版本。本次 08:06 本机重开 YMCC 又断线的证据为旧驻留模块 09135801…、磁盘 efd897cd…，应与相同模块重连场景分别记录。

同一 plugins.decky-update-recovery 场景执行实际 native admission/runtime、完整生产 bootstrap JS 和 MirrorClient；删除 retained 准入必须在 retained frontend must restore broker without official Loader 断言失败。100次重复模式事件不能重复接续健康连接。软件证据不能代替安装版同一 Steam 会话中重开 YMCC 的验收。


2026-10-21 TDP 抖动现场：新增实际队列34项、实际TSX14项与五个精确失败反例。重复调节不得插入/删除状态行；待应用状态放在原有配置来源行内。发出请求后保留草稿直到真实应答或来源改变，拒绝不重放；自己的busy回传不清除正在应用的草稿。Steam原生ToggleField使用controlled=true并保留字段key，防止TDP改变保存版本时重建所有开关。升级Steam时须重新核验原生CheckboxBase的controlled契约，不能仅靠mock证明原生支持。五个反例分别恢复提前丢草稿、应答到达即清草稿、动态待应用行、动态回执提示行和保存时重建开关，必须在相应业务断言失败。该回归由同一注册场景及发布输入身份强制执行。

官方v3.3.0在非Linux平台回传supported=false，并显示Unsupported Installation支持范围通知；这是上游平台声明。YMCC不改写官方supported/versionInfo，不修改官方Loader字节。Windows集成由YMCC维护；该通知与握手失败、业务写入失败须分别记录。现场截图codex-clipboard-13ada3e2-59cf-4602-9367-0cee86138537.png；本机09:53已收到认证peer和新模块efd897cd…的snapshot请求。

TDP回执和状态快照可以先后到达：客户端保留草稿到新值/新来源确认；只等待一次最长5秒，断线、关闭、来源切换均取消等待，无设置重发、无健康轮询。该有界读回等待不宣称设备功耗遥测已跟随。

补齐实际MirrorClient + 实际Content消息顺序检查，排队、保存回复和随后的新快照不得增删普通反馈行。CSS链接退役夹具每次使用新的短证据子目录，避免标准publish在同一workspace重跑时旧测试备份污染reimport断言；原失败保留，产品CSS迁移逻辑未修改。


### 2026-10-21 TDP 页面重建与迟到事件

用户仅退出再进入大屏、未重启 Steam，反馈闪动消失；同一版本中页面实例变化是现场事实，临时状态清除是待确认解释，不能写成新包已经安装验收。正式 Content 现为每段可见生命周期保留独立交互 token，隐藏/卸载使旧 token 失效；begin/change/end 均校验，避免旧页面事件排入旧值、提前释放新拖动或取消新等待。普通可见刷新保留 token。源码注释直接说明边界。

现有 sliderStabilityTests 统一执行正式队列37项、TSX30项、通用客户端54项和15个指定失败反例；TSX夹具执行 effect cleanup，并通过实际 MirrorClient 的内存 WebSocket 回放隐藏/重新进入、真实 Content 替换、旧成功/失败回执、回执后等待、30次替换无遗留定时器及100次普通刷新。四项新保护分别删掉后，必须在对应旧 change/release/unmount/begin 断言失败；原五项布局、草稿、回执和开关负例保留。注册场景的 evidenceInputs、运行回执及发布身份绑定实际产品和测试源码。

这些是无硬件 IO 的实际消费者软件回归，不能替代安装版 Steam 的完整拖动/应用周期、物理功耗或手柄验收。


多 Content 所有权回归：Steam 同一模块可能有多个 QAM 页面实例。MirrorClient.setVisible(visible, owner) 按稳定 Content owner 聚合可见性，只有最后一个 owner 隐藏才暂停快照/取消本机未确认请求；默认 owner 保留原调用语义。每个 Content 独立持有性能与风扇草稿队列，隐藏/卸载不清掉其他页面的调整，模块关闭统一释放仍活动的队列。夹具先复现 inactive 页面挂载导致活动页丢快照，再在仅修可见性后复现另页隐藏取消拖动；两处各有独立源码删除反例。还覆盖跨页草稿隔离、另页卸载不拒绝正在提交的写入、80次隐藏页替换没有额外设置/定时器。现场多个隐藏页面是已观察事实，这两个消费者缺口由内存回放证实，仍不能当作本机闪动根因已被现场确认。


传输重入回归：busy/超时状态发布期间，消费者可以同步切换桥接或隐藏页面。mutate 捕获发起时的 socket/epoch/bootstrap，并在首次实际发送前及失败收尾核对 pending 身份；旧发送异常不能关闭新通道。超时读回仅在原通道仍有效、侧栏可见且没有新请求时发出，避免把旧 runId 送入新桥接，或恢复已暂停的 HUD 后台业务订阅。通用客户端新增5项，原源码先复现跨通道旧写入，再增加4项精确源码删除反例，全部由现有 sliderStabilityTests 调用。通用客户端及两个内存/路径夹具已同时纳入场景和整包发布输入，防止仅测试文件变动却沿用旧发布回执。这是同步回调故障注入证据，不能标为本机原始闪动原因已唯一确认。


### 2026-10-22 旧页面驻留、后台已退出的明确修复

现场 Steam19552/CEF20344保留旧模块5d1dee…，新包c46f85…与YMCC16648已更新；官方后台不存在，普通启动正确停在decky-context-already-loaded。用户明确要求后只刷新一次JS界面，现有YMCC正常准入拉起官方v3.3.0，新peer及实际ready快照均成功。字段见field-record-20261022.json。

正式入口为Steam设置的“修复连接”按钮→deckySidebar.repairConnection→native/decky_sidebar_manual_repair.h实际消费者→steam_live.js中的decky.repairConnection。仅该明确点击持有当前Steam PID/创建时间和runtime revision；已开启、已加载旧页、精确模块/官方版本、当前大屏、资源哈希及空闲端口均须通过。最终发送前重新核对；OFF从不被修复清除，30秒预算含不确定结果，不重放。后台恢复完成须以新peer/current build/ready snapshot判断，requested不是通过。

自动loaded-context保护保持，普通启动/升级/观察者不会刷新Steam界面。本次不增加EXE、不修改官方Loader、不以商店失败全局关闭。32项实际全量/嵌入脚本回归及同一C++编译夹具覆盖本流程；删除明确同意校验必须复现未授权刷新。实际Vue/engine的A触发和两比例导航另由原UI门验证。

### 2026-10-22 Decky 外部修改器交互启动试用

正式消费入口仍为 TrainerLaunch.ps1；同目录 TrainerForeground.ps1 仅承接一次交互式 Shell 启动。新前端只在 Decky ready-only 调用显式传 InteractiveShell；原菜单保持默认 Process.Start。已安装旧前端在已核验的同会话 Steam 前台，或精确匹配的 YMCC 父进程主窗口已隐藏/最小化时，使用原 AppDirectory 参数兼容选择该分支，无须改官方 Loader、添加 EXE、注册 URI 或重开 Steam。

独立修改器仍由原 GCM 当前库、文件 SHA、当前游戏 PID/创建时间校验。交互路径通过桌面视图的 Application 执行，保留参数、工作目录和 open/runas 设置；已运行实例仍走原恢复入口。只有获取 Shell 对象失败且尚未派发，才可使用原启动器；派发异常、PID 未确认或窗口未到前台均不触发第二次启动。前台不确定时返回 launched=true、ok=false、trainer-window-unconfirmed 及真实 PID，保留可读原因。

现有全局场景 game-tools.trainer-foreground 和 Decky 更新发布门直接执行同一消费函数，覆盖11项及吞掉不确定性后回退启动的具名失败反例。桌面模式独立 EXE 创建/原字节和无重复实例已实测；前台存在失败回执，Xbox FSE 实际拉起及用户真实修改器仍待用户测试，不能把过程回执或较早的单次前台成功写成该环境已修复。


### 2026-10-22 retained bridge / official backend distinction

Field record: field-record-backend-20261022.json. A current frontend can reconnect its YMCC bridge after YMCC restarts while the official Loader/backend is absent. Transport attachment is not store acceptance. The explicit repair consumer also admits the exact enabled frontend-attached/retained-frontend-connected state with owned pid=0; final Steam identity, official bytes, free port, OFF and cooldown checks stay mandatory. No automatic context refresh or backend launch is added.

The production orchestrator marks an exception after entering dispatch as mutated/uncertain and keeps its budget spent. Native fixture negatives remove each new behavior and must fail the original named assertion; an exception before dispatch still reports no mutation. Actual Vue/production gamepad engine checks cover both repairable states, no dispatch on status events, and OFF removing the button.


### 2026-10-22 automatic and wake recovery supersedes manual-only policy

The user now requires automatic self-healing plus a manual fallback. Runtime connectionRecovery runs the shared production orchestrator on its existing worker. A verified retained document and absent backend may recover once automatically; OFF/fatal close, native power readiness, exact Steam identity, official byte/resource and free-port checks remain. The automatic origin is native-only and cannot be supplied to the manual IPC. An uncertain refresh is never replayed into the same document. The existing event-driven transport outage recovery remains independently bounded with increasing backoff.

On committed Ready, the native owner requests one binding refresh per generation and an explicit connection check. Raw RESUMEAUTOMATIC is not a Ready grant. field-record-wake-20261022.json contains a responding authenticated peer with ready:false under the native standby hold; UI must show connected/waiting-for-wake rather than claim a transport disconnect. The host fixture verifies held writes fail and a later Ready snapshot restores availability without replaying writes. Full physical standby acceptance remains separate.


<!-- DECKY-EMPTY-FRONTEND-20261022 -->
2026-10-22 本机自动恢复第二缺口：官方v3.3.0后台连通但loadPlugins等待已错过的UI模式通知，启用列表有YMCC而前端为空。原生验证的bootstrap仅在非retained接续授权时，用公开getPluginsFromBackend/importPlugin恢复YMCC插件；12秒正常启动宽限、最多3次只读探测、一次导入、版本ready回执和不确定不重放。官方禁用/缺失、外部桥接、版本不符、OFF和已在加载均拒绝。field-record-empty-frontend-20261022.json与frontend-boot.json纳入同一发布场景。实机睡眠未验，ready:false不冒充断线。
<!-- /DECKY-EMPTY-FRONTEND-20261022 -->


<!-- DECKY-OWNED-MANUAL-CURRENT-CONTEXT-20261022 -->
2026-10-22 本机补充：Loader仍运行时保留手动修复按钮，认证transport和powerReady独立显示；Broker connectionRevision排序拒绝同一runtime revision的旧连接回执。manual路径只刷新已验证Steam文档、保留自有Loader；Origin::Automatic仍拒绝有pid的runtime，不因该按钮扩大自动刷新。显式manual新增Dependencies::freshMode，一次ymcc::steamui::read(...,true)读取当前模块，避免启动期旧mode cache拒绝正确页面；自动仍读现有observer。正式夹具直接执行真实Runtime、临时loopback Broker、freshMode分支；stale-mode negative必须在原修复点失败。最终本机一次真实按钮accepted、2087ms认证重连、ready:true，YMCC/Loader/Steam/游戏FILETIME身份不变；原生159、UI234状态/18552方向/418动作通过。真实G3E睡眠唤醒未验。
<!-- /DECKY-OWNED-MANUAL-CURRENT-CONTEXT-20261022 -->


<!-- DECKY-ACTUAL-WAKE-CHAIN-20261022 -->
2026-10-22 13:40 唤醒链补充验收：从正式main.cpp提取WM_POWER_RESUME_COMMIT整段，对真实Runtime、自有无业务子进程、正式Host和MirrorClient串联执行。native173项、链路20项通过；缺电源Ready条件/缺绑定刷新/缺power.resumed/缺Host监听四个负例在指定原断点失败。同代重复和旧代不再次绑定，下一代更新一次，OFF优先。跨run认证更换拒绝旧未确认操作，用户意图发送1次、重放0次、真实硬件写入0次。908份产品源码与已安装最终候选逐文件一致，本轮仅增回归，没有再部署或触发睡眠。证据 D:\Codex\2026-10-18\d\work\decky-auto-repair-ui-20261022\wake-chain-recovery-gate-r2\result.json、native-wake-commit.json、wake-chain.json；下一次G3E普通唤醒导出仍是实际验收缺口。首次独立夹具代数与前一场景冲突的FAIL保留，改场景代数后通过，未改产品去重。
<!-- /DECKY-ACTUAL-WAKE-CHAIN-20261022 -->


<!-- DECKY-XBOX-FULLSCREEN-20261022 -->
2026-10-22 Xbox全屏冷启动：field-record-xbox-fullscreen-20261022.json记录 Steam 已确认BPM而YMCC仅观察、无插件输出的断点。主线只有在系统Gaming Full Screen API确认支持/激活、同Windows会话和Steam当前PID/出生时间/路径、大屏模式、用户ON与原生Ready同时成立时，给Decky缺失启动独立准入；不改变Steam/CSS共享许可、不启动Steam、不重放模式请求。进程名和窗口最大化不作为FSE证明。正式helper和launch callback均提取到夹具；内存端口只替代Windows系统标志。实际Runtime启动一枚自有无业务子进程，100次重复事件不重启、OFF实际销毁且10次晚到事件不复活。已加载文档、unknown、用户/故障OFF、崩溃预算和官方载荷检查保持。api不可用维持原准入，不猜测或自动关闭Xbox。窗口/设备/启动脚本由其他任务维护；实机Xbox全屏仍需安装后的日志验收。
<!-- /DECKY-XBOX-FULLSCREEN-20261022 -->

## 2026-10-22 内置Decky资源预热与全启动时序

预热复用Runtime唯一工作线程的Config.prepareEnvironment，在保存ON、已知Steam状态（包括明确未启动）且无存活自有Loader时先执行。main.cpp的正式回调复用Setup固定官方资源检查与原子调试标记准备，读取消谓词、Ready和2500ms检查预算；OFF/睡眠/代次变化取消准备，成功才记本轮已完成。资源准备不授予Loader或页面绑定权限，不改变YMCC默认后台/提权，也不由BAT管理官方哈希。requestPreparationRefresh只在原committed Ready内提交并合并代次，不重置admission/launch/crash预算。

场景从main.cpp提取正式预热、准入、模式完成和Ready函数，执行实际Runtime及Setup消费者。新增YMCC先于普通桌面Steam再手动BPM、YMCC先于Steam自身BPM设置、桌面Steam先于YMCC再手动BPM三条路径，验证资源先到、桌面不注入、最终单一自有子进程、健康事件防重复、OFF退出；另验证资源暂失→真实Ready回调在无Steam时恢复与OFF阻止晚到恢复。field-record-native-startup-20261022.json记录用户要求与原现场，不把软件回放冒充真实Steam。两项指定负例分别恢复“必须Steam运行才能预热”和删除Ready准备请求，须在新增真实断言失败。原有更新/绑定/唤醒/故障OFF/崩溃预算负例全部保留。

Runtime在取消和join之前撤销自有Job/子进程，使绑定彻底卡住时也不留下存活的根子进程。严格persistent生命周期门仍保留不响应取消的同步回调失败：该worker和原持有句柄不能在3200ms内回收，不能用子进程已退或夹具最终释放冒充完整自愈。原异步生产请求及其截止、关闭预算继续使用；全物理恢复仍需运行版普通日志。BAT现已去掉联动读取/启动和Decky重复准备代码，CSS/YMCC/Steam/Xbox边界保留。


### 2026-10-23 ROGX retained connection stops after a ready snapshot

`field-record-retained-session-rogx-20261023.json` records owned Loader pid0, official WS disconnection, one ready snapshot and then binding-session-stopped / mirror-bootstrap-session-changed. Retained transport does not certify official backend readiness. The original frontend/native manual predicates excluded this exact unavailable state. They now retain an explicit repair entry only while enabled and Steam is known/running; the production manual consumer still freshly checks current context and repeats identity/resource/port/OFF admission at dispatch. No session mismatch is reclassified as trusted. Automatic Steam UI refresh remains forbidden by the 2026-10-22 no-foreground-rebuild policy; the historical automatic-refresh paragraph above does not override that later decision.

The original update-recovery producer executes `retainedSessionRepairTests` as a required additional consumer. It compiles actual ManualRepair and executes actual TS against this recorded state, including stale identity, OFF/power/exit, resources/ports, final-admission races and uncertain no-replay. This repairs the manual fallback; it does not prove automatic backend recovery or explain the exact session field mismatch. Full producer/lifecycle failures remain failures; no installed/physical acceptance is claimed.


<!-- DECKY-MANAGED-PACKAGE-20261023 -->
### 随 YMCC 更新的自动维护

生产者现在执行正式嵌入 `decky_package_maintenance.ps1` 的真实文件事务，覆盖缺失/损坏修复、升级/禁止降级、精确退役旧代码、半更新监听器和外层握手回滚。只使用隔离证据目录及惰性载荷，不运行官方 Loader、不操作真实 Steam。回归失败保留完整负例。社区特征记录仅作触发依据，不能把 Linux 问题当作 Windows 现场复现。自动外观缺失重应用负例同时识别当前 `Reconcile` 和历史 `Refresh`，仍须唯一锚点且替换成 Status 后在原应用断言失败。

首次旧升级器兼容：`YeManCC/decky-package.json` 在所有发布envelope中存在，包括去掉update-manifest的旧bootstrap。新原生只在没有已安装托管记录时读取该配套身份，仍校验七文件与官方版本；后续更新以该身份防止倒退并建立正式记录。对应正式setup51项、maintenance26项。受管事务删除回滚和放开降级的语义破坏必须在原断言失败。完整场景与58/58生命周期的软件证据见本轮AUTO-MAINTENANCE-REPORT.md；旧callback负例保持FAIL。


<!-- DECKY-LOCAL-RESOURCE-SELFHEAL-20261023 -->
### 当前资源自愈消费者

生产者同步执行 `tools/decky_resource_cache_selftest.mjs` 和 `tools/decky_resource_worker_selftest.mjs`。前者正式缓存24项，含缺失/同长度损坏/缓存损坏再填充/占用/取消/路径保护；后者同一EXE私有入口、真实Job和pipe，含EOF、卡死、Runtime OFF和Ready实际消费者，原3200ms不放宽。官方Loader只作为原字节数据缓存/还原，不执行。缓存没有独立来源权威，不绕过配套清单、OFF、电源、崩溃预算或Steam页面准入。

PS事务增加晚到回滚竞争和Windows5.1直接源码编码回归；测试只映射真实Mutex命名空间，禁止争抢用户正式维护锁。发布Build为每次资源回归创建新的证据路径，保留旧原件。最新严格父门INCOMPLETE保留两组typed迁移缺口，单项绿不能作为整体或安装版通过。证据：本轮REPORT.md。


<!-- DECKY-CACHE-INTERRUPTION-AND-LOCAL-PID-20261023 -->
### 2026-10-23 继续加固与本机未自愈

缓存消费者现为32项和4个语义负例，覆盖真实自有Job硬中断后的固定暂存恢复。原本机连接失败另定位为主观察器允许陈旧注册表PID回退，而worker仍严格比较PID；新SteamLease正式14项+2语义负例登记于现有发布门及持续父门current.steam-session-lease-regression，不增加原更新树240秒预算。真实只读Steam租约通过不等于安装版已接回面板。

完整门冻结r2仍FAIL（52/55，含原240秒超时及typed故障关闭观察器失败），新租约修复后未重跑整门；当前main语法检查也因screenpads接口未声明失败。旧绿回执不放行新源码。原始失败、临时探针错误与补齐源码闭包后的48项官方独立通过均保留，见上述两份REPORT.md及RESULT.json。

### 2026-10-29 ROG S0 Ready / upgrade alternation

`field-record-wake-20261022.json.followups` appends the 2026-10-29 ROG evidence without replacing the older wake case. The 10:28 user wake opened shared Ready through `sgAbortSleepIntent`, bypassing `WM_POWER_RESUME_COMMIT`; Decky now notifies its existing preparation/binding/admission owners once for this real Ready generation. No power state is manufactured, no healthy timer is added, and OFF / exact session / crash limits remain authoritative.

The compiled `ManualRepair` consumer pins the **observed document Loader version** when an explicit repair refreshes an equal/older stable document. Numeric stable comparison covers 330→331, 331→340, 340→400 and 3.9→3.10; a newer loaded document is refused by an older install. This does not relax exact Loader/build matching for automatic retained transport or allow automatic Steam UI rebuilds. Actual document/module identity is rechecked by the existing source and embedded JS before dispatch and inside its queued callback.

The existing update-recovery producer now passes serialized actions from `retained-session-repair/native.json` into `manualRepairTests`; the native→JS upgrade cases are mandatory there, not an optional parallel model. The existing persistent-lifecycle typed consumer requires all 16 native cases, including S0 owned/retained wake, preparation revalidation and the previously existing healing cases. Missing required cases, negative source overrides, wrong compiled S0 excerpt, stale source identities, uncleared resources or the unchanged 3200ms stop boundary fail validation.

A reverse test also exposed Job count zero preceding final descendant-process signalling. The existing process owner now takes bounded, read-only handles to verified Job members only during stop, waits for actual exit under its original deadline, and accounts for those handles until cleanup. It does not scan healthy sessions, kill foreign processes, or call user callbacks during cleanup.

Targeted evidence: `Testing/YMCC/Evidence/Decky-WakeHealing-20261029/REPORT.md`. Native Runtime/Client/Owner and file transactions execute in isolated evidence using inert source-built peers. No actual Windows sleep, installed replacement or whole-package release acceptance is claimed by these results. Future upstream private API/lifecycle changes still require official-contract review and fresh release receipts.
