# 睡眠唤醒 Steam 自动修复（Steam-only）

## 范围（2026-10-11 用户明确裁决）

本开关只负责 Steam 链路，主目标是：唤醒后让睡眠前的原游戏重新获得 Steam 输入归属，从而可以继续用手柄控制；次目标是让大屏依据真实运行态显示“继续”。风扇与虚拟手柄是独立链路：不读取 InputHost 就绪/隔离作为 Steam 的准入条件，不排队输入设备会话刷新，不重建手柄，不控制风扇。

原有睡眠游戏暂停/解冻机制及手柄、风扇实现不由此补丁改写。隐藏的旧手动诊断 API 保持兼容，其回归与 Steam-only 回归分开。

## 使用

在“睡眠优化 → 睡眠操作”开启 **睡眠唤醒 Steam 自动修复**。仍然只有一个开关，不要求输入 AppID，不要求点击检查或确认。

首次/缺少偏好时默认开启，记忆用户主动关闭的选择。开启只保存设置，不立即改当前游戏。现有明确用户唤醒通知按电源 generation 去重，在 Ready 后等待至少 1.5 秒，原生后台自动处理；页面不必保持可见。

## 自动识别与修复

1. 使用原游戏 PID、进程创建时间和可执行文件路径，与最近 2 MiB（最多 20,000 行）的 Steam gameprocess 日志关联。
2. 按精确整数转换非 Steam 的 unsigned32 AppID、signed32 日志别名和 64 位 shortcut GameID。64 位值不经过 JS Number。
3. 收到系统睡眠通知时先读取原实例身份（不执行 CDP），再转交到同一/紧邻电源代际，避免延迟的 506/睡眠队列在唤醒后绑定替代进程。在睡眠边界绑定原游戏 PID/创建时间、Steam 会话和 Steam 输入上下文；睡眠租约未释放时等待，不解除任何暂停所有者。
4. 唤醒后读取 Steam 焦点、控制器列表、控制器配置上下文和游戏窗口/进程证据；只在没有用户菜单、Overlay、配置编辑器、锁屏、VR 或其它游戏占用焦点时继续。
5. 若 Steam 本机状态是 Running=4 / Launching=1，但 UI 列表遗漏（含 raw signed/unsigned key 错配），重新验证绑定后，只调用一次 Valve 原生 `ScopeRunningApps()`。
6. 若列表已经正确但 Steam 输入焦点落在桌面/大屏，则使用 Steam 原生 `Apps.RaiseWindowForGame()` 返回**原始 64 位 GameID**对应的游戏，再核对原游戏 PID/创建时间确实成为前台窗口。
7. 主目标与次目标分开：本机 UI overview 为已核对的 ReadyToLaunch=11 时，若睡眠前原实例仍存活、gameprocess tracker 精确绑定且原 Steam 会话未变，可签发仅供原生内部使用的 `captured-original-tracker` 返回凭据。最多尝试一次相同 `RaiseWindowForGame()`，不重算列表、不改运行状态；原生/JS 均再次检查 GameID、焦点/菜单/编辑器、账户和原进程。未知本机状态、无睡眠捕获、换实例/账户、手动暂停或其它窗口占用均拒绝。
8. 返回后立即交给原生 PID/创建时间/路径核验原实例前台窗口；Steam UI 焦点只作辅助读回，不把它当作直接查询实际 Steam Input 活动上下文。输入仍需现场按键验收；ReadyToLaunch 等非运行 UI 状态仍把 **Steam 运行关联/继续** 标为受阻。一次返回成功也不能把整个流程写成 recorded 或制造“继续”。

只接受已核对的 Valve 方法源码指纹：998 字符，FNV1a32 `0x870779ff`。Steam 更新导致方法变化时停止写操作，等待重新核对源实现，而不是猜测新 API。

不直接修改 `m_runningAppIDs`，不改 `display_status`，不调用 LaunchApp/RunGame/forceinputappid，不重启 Steam，不伪造 AppID、appmanifest 或 Steamworks 初始化，不重写控制器配置，不默认关闭 Overlay。

输入上下文读回、Steam Overlay DLL 存在、窗口响应或 Steam 焦点切换都**不是实际游戏手柄验收**；日志始终保留 `gameInputVerified=false`，只有掌机现场按键/摇杆实测才能把主目标判为通过。

自动流程最多一次 Steam 列表重算和一次 Valve RETURN；仅在 RETURN 已完成但原窗口仍未回、全部原实例/会话/交互保护重新核验通过时，可在同一事务内使用既有 YMCC PID 窗口方法最多一次回焦。保留有界重连后复查，不重放上述动作。唤醒后至少观察 30 秒，总上限 60 秒，间隔 1.5 秒；控制器可见、原实例/前台窗口、Steam 输入焦点和本机/选中客户端 Running 态连续稳定 9 秒后才结束为“已记录”，仍不代签实际游戏输入或大屏按钮验收。手柄列表为空时，即使缓存焦点等于原游戏，也继续等待，绝不提前判为 context-current。电源未就绪等待 30 秒截止，原游戏睡眠租约释放等待最多 30 秒，排队/执行阶段有界。关闭、再次睡眠、账户/客户端/目标变化会撤销旧准入，模糊回执不自动重放。

## 状态与日志

- `steamRecovery.status=current`：原实例、Steam tracker、本机运行状态与原始 UI membership 已核对。
- `stale-ui`：本机仍运行，但运行列表遗漏，可尝试原生重算。
- `steamInputRecovery`：记录 Steam 输入焦点是否回到原游戏；`context-current` 只表示 Steam 上下文已对准原游戏，仍不代表游戏实际消费按键。
- `steamInputRecovery.returnBasis=captured-original-tracker`：主目标返回准入来自睡眠前原实例的实时身份 + Steam tracker，而不是库页面的运行状态。可与 `steamRecovery.status=blocked` 并存，绝不表示“继续”已恢复。
- `blocked/unknown`：本机 UI overview 非运行、目标/会话身份不可确认、读取通道不可用或方法指纹变化等。历史兼容原因 `steam-backend-not-running` 的直接观测层是 `steam-ui-native-overview-store`，不是已经证明所有 Steam 后端都丢了进程。

`associationVerified` 只代表 Steam 关联证据。`contextMatched` 只代表 Steam 焦点/上下文读回；`continueButtonVerified=false`、`healthVerified=false`、`gameInputVerified=false`，不代签“大屏继续按钮”或真实游戏输入验收。

运行时 `Sleep/wake-recovery-last.json`（schema 4 / scope=steam）仅代表最近 **已结束且允许落盘的代际**，不可用来判断一轮尚未结束的最新唤醒。含睡眠原实例快照、输入上下文、Continue 候选态、观察时间线、等待原因、动作计数和最终核验。

“导出全部日志”同时生成 `Sleep/wake-recovery-current.json`：只复制内存中的当前流程，标出 `pending/terminal/superseded/idle`、当前电源 generation、开关、动作计数、完整最近状态及原始本机/选中客户端 status、焦点、旧 completed report 的 generation 是否匹配。不调用 inspect/run/arm/tick，不触发 Steam、游戏、设备或风扇修复，写盘失败时不加入旧 current 文件。`sleep-steam-inspected` 事实与观察时间线也保留原始 overview/focus，避免最新一轮只留下原因字符串。空 PID/创建时间/路径相等不再被误算成 `suspendCapture.available=true`。

上述两份报告和 gameprocess/controller/controller_ui/Overlay 日志有界尾部均走已有导出入口（每文件最多 2 MiB，缺失/截断写入 manifest），无需额外表单或手工复制文件，不自动上传。`implementation=steam-wake-round18-pid-window-return` 可确认本轮代码是否进入测试包（此前 Round17 为 `steam-wake-round17-pid-postflight`，Round16 为 `steam-wake-round16-original-window`）。

## 验证

- `pnpm run test:sleep-steam-only`：生产 C++ parser/observer/coordinator + 生产 JS/真实 Valve rescope 方法 mock。
- `pnpm run test:sleep-recovery-ui`：真实 Vue 页面的单开关测试。
- `tools/sleep_steam_readonly_runtime_probe.mjs`：仅通过已启用本机 Steam 调试上下文执行生产 `sleep.inspect`；绝不调用 `sleep.reconcile`。不启用 CDP。

本机只读集成核对不能替代症状掌机的实际睡眠唤醒与大屏按钮验收。

## 弃用旧关闭 Overlay 方案

旧“Steam大屏唤醒手柄卡死修复”（通过关闭在游戏中启用 Steam 叠加界面）已过时并放弃。界面、确认弹窗和事件入口移除，历史 true 偏好规范化为 false，旧 IPC 与后台队列拒绝重新激活。弃用不自动关闭或恢复 Steam 自身叠加设置；普通 Steam 叠加设置仍由用户自行决定。

## 2026-10-11 会话重连等待补充

Steam 身份不再仅由 ActiveProcess 注册表 PID 推断。若该 PID 已退出或指向其他程序，只接受现有事件驱动 observer 缓存中、同安装目录且路径为 steam.exe、创建时间/存活状态重新核对通过的进程；不新建常驻轮询、不改注册表。JS 在每次读写前仍核对原账户，0/尚未就绪与非零不同账户分开：前者有界等待，后者立即拒绝。

报告里的 game.identityMatches 只代表原游戏 PID/创建时间/路径；recoveryBindingMatches 和 steamSessionState 分别代表完整准入与 Steam 会话。不能因 Steam 暂未就绪把活着的原游戏写成“身份丢失”。睡眠租约先由已有机制释放，随后必要时显示 session-wait；全部等待共用原 60 秒截止，不重开游戏或 Steam，不解除别人的暂停，不把唤醒后的新会话当成原会话。

## 2026-10-11 原窗口回焦补充（Round16）

Steam 输入焦点 AppID 命中原游戏，只是缓存命中。只有 Windows 前台窗口的 PID/创建时间/路径也命中睡眠前原实例，才可标为 context-current。若缓存仍指向原游戏，但 OS 前台已回到 Steam/允许的 shell，仍应通过现有一次 Valve RETURN 恢复原窗口；内部 `originalWindowReturn=true` 必须搭配重新验证的 `originalTarget` 凭据，不能由任意布尔开关、UI 缓存或前台其它程序授权。缓存命中时不抢占活动 YeManCC 设置窗口；Overlay、锁屏、菜单、配置编辑、其它游戏、手动暂停和账户/实例变化继续拒绝。

因为 JS 缓存已命中，Steam 焦点读回可能早于 Windows 真正激活原窗口。原生后验最多等待 750ms/30 次读取，仅查询原实例身份/OS 前台；不会再发 RETURN 或创建轮询线程。期间睡眠、暂停、新进程或用户换到其它程序立即撤销接受。返回一次仍不代表游戏消费按键或“继续”恢复。

本机 API 元数据只读调查确认现有 Steam 包含用户级 ResumeSuspendedGames 和 AppOverview/Lifetime 注册接口，但现有签名/源码没有按原 PID/GameID 定向恢复的证据，且 BootstrapData getter 在参考机当前 Apps API 不存在。不调用这些全局恢复/注册入口，不重启 Steam，不以猜测接口换取表面成功。参考机没有运行症状 2077，调查不属于掌机验收。


## 2026-10-12 原 PID 后验与 UI 缓存不一致（Round17；实测日志日期 2026-10-11）

复用已有 `nativeValveReadCurrentTarget` 游戏 PID 契约及 `sgReadSleepTarget` / 睡眠租约状态；不另行扫描或挑选唤醒后的替代游戏。原实例必须同时匹配 PID、创建时间、可执行路径，原 Steam 账户/进程/创建时间也必须保持一致。只剩 UI 缓存落在 Desktop/ClientUI，不足以认定原游戏或实际 Steam Input 断开。

- `sleep.input-resume` 不再在 `FocusedAppWindowStore` 上等 3 秒然后抛出 `live-readback-failed`。Valve RETURN 完成后立即保留真实新快照，进入原生原 PID 前台后验（最多 750ms / 30 次只读查询）。
- 所有内部 RETURN 路径都携带重新核验的原 PID/创建时间凭据。JS 的 `actionAcknowledged` 仅表示该调用完成，不代表输入生效。`windowReturnObserved` 仅表示动作完成后看到原窗，不证明就是此动作导致回焦。
- 只有调用完成、Steam 目标/GameID/控制器及交互状态保持有效、原实例/窗口后验正确且只有辅助 UI 缓存尚未同步时，才能给出 `verificationPending=true`。此时 `accepted=false` 原样保留，仅继续原 60 秒预算内的只读复查，最多一次 RETURN，不补发动作、不重启 Steam、不切换设置。日志事件为 `sleep-recovery-automatic-observing`，不冒充 accepted。
- 游戏退出、PID 重用、客户端/账户变化、手动暂停、用户切到其它程序、Overlay/菜单/配置编辑器/锁屏等不属于 UI 延迟，仍拒绝。RETURN 本身拒绝/超时没有完成凭据，也不能获得 pending 权限。
- 列表重算及 RETURN 的失败分支也重新 inspect；报告 `after` 不再使用动作前的 `report`。新的 `originalGameRecovery` 单独保留原实例、租约释放、原窗口和辅助 UI 读回，各项仍 `gameInputVerified=false`，不会把 PID 存活转换成 Steam Running 或“继续”。

23:07 测试包里 S4 后同一个 2077 PID4284 的本机/选中状态是 4/4，自动流程因缺少当前有效睡眠捕获没有修复，不算本功能成功。S0 后同一实例仍存活且 tracker 未移除，但本机/选中状态已变 11/11；原生 controller_ui 在 23:06:31 已切到该 AppID，UI 缓存仍报 Desktop。Round17 修正原 PID 后验与过早终止/报告快照问题，**没有证明解决 11/11 的真实关联恢复，也没有新的掌机手柄实测验收**。


## 2026-10-12 原 PID 窗口回焦补充（Round18）

原 PID 不再只用于识别/核验：一次 Valve RETURN 已完成且原窗口尚未恢复时，允许复用既有 `focusFindWindowForPid`、`focusIsRestorableExactWindow`、`ymcc::resumeWindowResponds`、`ymcc::requestWindowForegroundAsync`。不修改共有 `resume_focus.h` 或游戏暂停/恢复 worker，不走其 input-ready mailbox，不读取 InputHost/fan admission，也不解除任何暂停所有者。此路径不执行进程启动/扫描换游戏、不发送模拟按键、不附加输入队列、不跨线程 SetFocus。

- 原窗口已经在前台：跳过原 PID 回焦与探测，不重复动作。
- 原窗口还没回来：先完成原有最多 750ms / 30 次前台只读核验。仅原实例/Steam 会话/当前代际未变、睡眠与手动暂停均已释放、前台窗口仍与 RETURN 准入时一致，才寻找原 PID 的有效窗口并做既有 150ms WM_NULL 响应探测。
- 响应探测之后重新读取 Steam 状态；原 GameID/AppID/type 必须与已完成 RETURN 的真实回执一致，控制器列表必须未变。Overlay/其它游戏焦点、配置编辑、锁屏、键盘、VR/Overlay 窗口、菜单或未知保护状态均拒绝。不能拿探测前的空闲 UI 快照授权一个刚打开菜单的窗口操作。
- 真正请求前再次核对原 PID/创建时间/路径、窗口所有者与允许前台。最多发一次已有异步窗口恢复/前台请求；之后最多 750ms / 30 次只读原窗确认。系统未接受或原窗没有回来时保留失败，不补发焦点、RETURN、键盘或设置动作。
- `receipt.inputResume.pidWindowActivation` 记录是否尝试、API 回执、失败/跳过原因和后验时间；报告 `attempts.pidWindow` 为 0 或 1。这不是实际手柄验收，也不是动作导致回焦的因果证明。
- ReadyToLaunch=11 的原跟踪实例也可使用该窗口返回路径；Steam 的 11/11、关联未恢复、Continue 候选 false 仍原样记录。没有把窗口回焦当作 Running/“继续”。

本轮检查当前 Valve 源码确认 `local_per_client_data` 是固定 `clientid == "0"` 项，不依赖被选中的 Remote Play 客户端；因此原日志本机与选中状态均为 11，不能靠换“本机”流式选择解释或修复。AppOverview 从真实原生 protobuf 更新，而 ScopeRunningApps 在 Windows 不以单纯有游戏窗口替代非运行状态。没有调用 SetStreamingClientForApp、重注册 AppOverview/lifetime/focus 回调或重复 User.ResumeSuspendedGames。

Round18 新增的是对“Steam RETURN 完成但原窗没有恢复”的主目标实施路径。23:07 实测中原窗已被共有 PID 恢复链拉回，此路径会跳过，**不能声称已修好那一轮的 S0 11/11，也没有新的实机输入/继续或全家族验收**。


### Round18 真实会话身份与发现来源分离

之前动作前把整份 steamClient 诊断 JSON 相等当成会话未变；注册表 PID 从旧值恢复、验证来源在 registry/observer 间变化，或一次完整性等级只读探测暂不可用，都可能在真实进程/PID/创建时间/账户/安装目录没变时误拦截 RETURN/列表重算。稳定观察的 stamp 也会因此反复重置。

现在动作前只比较已核验的真实 PID、规范创建时间、规范安装目录/账户/PID/birth 构成的 identityToken，以及 processVerified/accountKnown；原生 admit/postflight 仍再次核对真实完整账户/PID/birth/root。目录 slash/case 规范化与现有 sameSession 使用同一 pathKey 语义。发现来源、注册表 PID、完整性诊断仍完整记日志，但不冒充会话身份、也不单独重置 9 秒稳定窗口。缺失/未验证进程或账户、零/重用 PID、不同创建时间、不同账户/目录或真实会话仍拒绝/重置。

这不是 23:07 那轮的已证原因（其注册表 PID13172 一直正常）；本项修正的是其它正常重连过程的错误准入条件。


## Round19：S0 最终唤醒准入（2026-10-12）

01:49 测试包报告标识为 R15。该轮 01:48:07 的 S0 入睡过程中已有 PBT resume 广播，修复在 07.454 启动；最后明确的 Kernel507/Reason1 事件时间是 23.583，Steam Controller0 在 25 才重连，原生游戏 focus 在 44 才再次出现。旧报告失败分支 after 与 before 完全相同，不能用旧 4/4 样本证明最终“继续”。

- 在原睡眠 PID 捕获中保留同步 `powerBoundaryFileTime`；提前准备的原边界随同原 PID/创建时间转交，不以唤醒后采集时间替代。
- Kernel 订阅回调仅记录 Steam 自己的证据：507/Reason1 或 107/Target5；核对当前 generation/configEpoch、原边界、非零真实事件时间，拒绝边界以前/相等/未来的事件。回调不启动 worker，不改共享睡眠分类或手柄链路。
- 在 Modern Standby 设备上，中间 PBT 只留下 `wake-proof-wait` / `waiting-confirmed-wake`，不启动 60 秒观察预算，不消耗 seenGeneration 或 RETURN。最终 S0/S4 UI 消息处理后调用 Steam 自己的准入函数，才允许按当前代际唤醒。
- S3 机器保留 RESUMESUSPEND 的既有入口，不等待 S0 专属事件；S4 在 S0 能力设备上使用独立 107/Target5 准入，不误等 507。已确认事件先于共享 Ready 时，Steam 的初始等待允许共享提交完成，但不替它提交或释放租约。
- 新日志 `sleep-steam-wake-await-kernel` / `sleep-steam-wake-proof-recorded` / `sleep-steam-wake-proof-rejected`，最终报告新增 `wakeAdmission`（证据来源、原边界/事件时间、候选等待时长）；实现标识 `steam-wake-round19-confirmed-wake`。
- 缺失可靠 S0/S4 事件时，不回退到同一中间 PBT，也不虚构唤醒证明；本次确认的 S0 入口覆盖已有真实电源键证据。其他有意唤醒来源的跨设备证据还需验证，不能宣称全家族验收。

这是时序修复，不是 Running 状态重建接口，不改显示、不重启 Steam、不启动游戏、不关闭 Overlay。单开关/默认开启/尊重保存关闭保持不变。编译和确定性回放不证明真实手柄输入或最终“继续”。


## Round20：保护窗口等待与延迟关联丢失证据（2026-10-12）

用户澄清：唤醒后最初仍保持关联，过一会儿才丢失。02:12 的 R18 包中，generation3 在 02:08:01 仍4/4、02:08:02 已11/11，实际 RETURN 到02:08:11才执行；PID7620仍被追踪到02:10:10。generation4 的原 PID11740/4/4 保留，但 `overlayOrVrWindow=true` 导致纯 preflight 拒绝，RETURN并未执行。

- Native `inputInteractionBlock` 在派发前、原 PID 窗口补充和稳定观察中复用同一正向编辑器/交互保护。繁忙或未知时 `waiting-interaction`，仅在本轮60秒预算内读取，不消费 RETURN，不关闭菜单/叠加界面/配置窗口。缓存焦点恰好等于原AppID也不能跳过保护。
- 分开记录 `windowKinds.vr/gamepadOverlay/desktopOverlay/controllerConfigurator`，保留合并字段兼容旧日志；方法缺失/返回非布尔时记录 null，不能因此宣称 idle。`focusEvidence` 明示这是 Steam UI 窗口实例缓存，不是 OS 前台或游戏按键证明。已参考本机 Valve WindowInstance 的四个只读布尔分类方法，不猜测私有 ABI，不调用新的动作 API。
- UI/控制器可能在两次读取之间变化：仅严格 `mutated=false`、known PRE-mutation 阶段/原因、原身份/会话/暂停/允许前台后验仍安全的拒绝，才允许重新等待准入。最多3次派发，保存原 rejected receipt 到 `preflightRejections`，新增 `attempts.inputDispatches`；一次真实 RETURN 执行后永不重放。后动作错误、缺失/错误类型 mutated、未知阶段/超时/身份变化一律不能重标为纯 preflight。
- 每轮在租约等待、交互等待以前记录可靠 native overview 的首次当前关联与首次后续丢失。`associationHistory` 与 `sleep-steam-running-association-transition` 包括读取时间、local/selected状态、列表成员、原PID、是否此前已标记RETURN mutation；这只是采样证据，不写运行态，也不证明“继续”或实际游戏按键。
- 11/11时原PID/账户/Steam实例仍有效且交互空闲，沿用已存在的 `captured-original-tracker` 主目标通道，不能因为次目标未恢复而禁止原窗口返回；也不能把这种返回说成真实 Running 已重建。
- 保留R19最终唤醒准入：S0中间resume不耗预算，S3/S4独立路径。实现标识 `steam-wake-round20-interaction-wait`。

默认开启、用户主动关闭、旧 Overlay 关闭策略固定弃用、睡眠优化内单开关不变。没有部署/启动生产EXE/实机睡眠/风扇或虚拟手柄改动。实测仍须验证唤醒后的原游戏按键和真实“继续”，不能用这些确定性回放宣称全掌机家族验收。


## Round21：主目标与次级列表修复解耦（2026-10-12）

- 原实例在本地Native overview确为Running4/Launching1、只是UI列表遗漏时，新增内部 `captured-original-local-running` 凭据。Native捕获PID/创建时间/路径/原Steam会话仍是准入根；JS也核对该凭据、真实GameID/type和本地4/1。它只允许RETURN，不写列表、运行态或“继续”。
- Native、JS及有界PID窗口后验都贯通这个凭据。缺捕获、非法PID/birth、远端运行、本地11/未知、GameID不一致仍不能借此走新通道；11/11沿用既有、另有明确原tracker依据的通道，不将它伪造为4。
- 已知次级错误 `steam-rescope-unavailable/schema-changed/not-confirmed` 后，只有 fresh原实例/Steam/暂停证据仍安全且保留明确原依据时，才继续主目标/只读观察。列表动作本轮不重试；身份变化、取消、未知错误仍终止。
- 完整保留 `secondaryRejection` 和 accepted=false ACK，不用GET/readback提升它。日志使用 `partial-observing` 区分部分动作和整体未验收；即使下一次才等到控制器重连，也不把先前列表拒绝遗忘成“整体accepted”。后续真实观测可能变current，但拒绝回执保持原样。
- 正向交互保护、三次纯preflight上限、一次真实RETURN/一次附加原PID请求、30–60秒观察、关闭取消与原租约等待全部保留。实现标识 `steam-wake-round21-primary-independent`。

早期保持关联仍在研究：共享恢复采用五次观察，到末轮才退睡眠标记；acceptedOnce只是过去操作接受，不是此刻无暂停证明。本轮未跳过marker/释放任何暂停所有者，未放宽空控制器准入，未对Steam/game注入DLL或伪造native状态。两个真实目标及全掌机范围仍需实际验收。


## Round22：关联仍在时的原窗口提前准备（2026-10-12）

目的：用户看到“唤醒后仍关联，稍后才丢失”。此前空控制器会使整个RETURN等到枚举回来，可能错过窗口期。本轮不是伪造关联或输入修复ACK，也不是跳过暂停租约，而是严格限制的窗口级准备尝试。

- 必须是捕获的原PID/birth/path、原Steam会话、最终唤醒准入，原暂停/睡眠租约已真正释放，Native确为Running4且在运行列表，原OS窗口已经在前台。干净编辑器、无其他游戏/Overlay/键盘/菜单/VR、控制器列表已初始化且确为空时，才给出独立 `windowPreparationRepairable=true`；`repairable/contextMatched/gameInputVerified`仍false。
- 内部请求 `preControllerOriginalWindow=true` + `originalWindowReturn=true` 只沿用已存在的 `SteamClient.Apps.RaiseWindowForGame`。本机Valve生产代码在Windows游戏launch完成和return菜单调用同一方法，不以控制器枚举为该窗口动作准入；这仅支持操作范围判断，不证明能保持真实Running。
- JS核验原凭据basis=current-association、精确GameID/type/Running4/列表成员/交互保护；异步读取结束、动作之前再次检查Running4。缺失/错误类型控制器列表不能当空；本地1/11、列表遗漏、未捕获、原窗口不在前台不能借此提早请求。11/11仍走原已知主通道，不伪造。
- 即使UI焦点已同步/控制器刚在请求中出现，窗口准备ACK也 `accepted=false/runtimeAccepted=false`，只记 `verificationPending/windowPreparation`，输入为not-tested。后续稳定核验需要实际非空控制器、原窗口/上下文/真实关联；没有实际游戏按键证明。
- 原窗口若在请求中丢失，不允许用额外PID激活fallback追抢焦点；只读后验失败终止，不能重放。整个周期仍最多一次真实Valve RETURN。
- 报告/导出/inspection实现标识统一 `steam-wake-round22-early-original-window`，回执记录windowPreparation/controllerReconnectPending。此早期操作会消耗原周期单次RETURN；如果后续仍丢关联，不用循环窗口操作掩盖失败。

这是明确的、尚待实测的预防性尝试，不保证解决Steam后端延迟4→11，也不保证跨掌机输入自动恢复。既有单开关/默认开启/尊重关闭/旧Overlay关闭策略弃用不变；没有共享租约提前清除、其他暂停所有者释放、风扇/虚拟手柄修改、Steam重启、游戏重开或DLL注入。


## Round23：最终唤醒证据与原捕获的乱序（2026-10-12）

进一步对照排除了“网络重新登录是共同必要根因”：23:06:20已采样到原PID4284的11/11，Steam成功重登录在23:06:35；02:08:02的PID7620丢状态则确实紧随02:08:01重登录。只能说有时相邻，不能据此改网络或声称因果。BasePanel警告在多个包启动阶段就有，也不能单独认作睡眠根因。

另一个实证时序：23:06:18.847收集到Kernel507最终唤醒，原捕获到19.446才落下。R19–R22只在capture已经完成时收证据，会丢掉这种先到的最终事件。本轮修复此顺序缺口，不改变共享睡眠分类：

- Kernel回调只保存一个当前epoch/generation的候选；缺时间/未来事件直接拒绝。有已知同步prepared边界时仍检查真实eventTime晚于边界；候选本身不启动worker/预算/RETURN。
- 同步prepared边界优先于仍保留在旧代际的已完成capture；`boundaryGeneration`随已有capture转交，既有transfer只到同一或紧邻下一代。
- 最终arm必须重新匹配实际capture代际/epoch、非零原边界、事件时间及边界对应；跨代候选仅可跟随同一prepared boundary的紧邻转交，不能任意借gen+1或补造边界。
- 已有明确wake入口的请求也可随这个prepared边界转交；capture-ready仅在请求和候选都已存在时尝试重新准入。捕获本身/偏好开关/Kernel回调都不能单独调度修复；旧seenGeneration不能因此重放。
- 无更早prepared边界、到真正退出之后才首次捕获的情形仍不得回溯授权；新入睡/不同边界/跳代/关闭或epoch变化取消原凭据。报告保留proofObservedGeneration/proofOriginallyAwaitedCapture/proofFollowedPreparedBoundary。

实现标识 `steam-wake-round23-ordered-wake-proof`。R22早期窗口、R21主次分离、交互保护、暂停所有者及一次真实RETURN上限保留。此顺序修复不等同Steam后端真实Running关联重建，不证明实机手柄/继续/全家族目标达成，也不部署或运行生产EXE。


## Round24：RETURN 后按原 PID 保留收敛观察（2026-10-12）

依据用户 07:31:43 导出包，确认 R23 两轮 S0 均触发；第二轮原 PID10100/Steam16080 会话不变，07:30:04 RETURN 有回执，06.050 从真实4/4+running list转11/11+无列表，07.585前台转Steam后整个周期提前结束。第一轮07:28:39也因暂离原前台结束，关联仍是4/4。此包没有实际游戏按键证明，不能当作主目标成功。

- 保留不可替换的睡前 PID/birth/path、原 Steam 会话、暂停所有者、最终唤醒准入和既有30/60秒观察预算；不以唤醒 UI 焦点更换游戏目标。
- 已完成并经原 PID 窗口后验确认的 RETURN，随后暂离原前台不再立即结束所有观察。该样本只读，不重放 RETURN、不追加 PID 激活、不执行未使用的列表修复，也不抢其他前台。未知/其他程序前台始终不能满足稳定核验。
- 待真实前台、Steam 输入上下文、控制器枚举及真实运行关联自行收敛，再重新计算连续9秒且不少于30秒的既有稳定窗口。只有原前台安全恢复，既有一次性且通过全套保护的列表修复才仍可能执行；并未增加任何输入动作次数。
- native pending 回执已证明刚才的原窗口后验，新的 after 读若稍后前台变了，只重置观察就绪性；仍要求相同原实例/Steam 会话/无暂停，且保持 accepted=false/verificationPending、游戏输入未验证。原后验本身失败的动作仍拒绝，不放宽抢焦点保护。
- 每个有界观察记录 game/Steam client/tracker/控制器 index/type、foreground、running overview；终端及导出新增 postReturnObservation，日志记录只读等待进入/退出。原进程退出/PID复用、Steam更换、手动暂停、新入睡、用户关闭仍取消。
- 实现标识 `steam-wake-round24-pid-bound-settling`。新增07:31真实日志 fixture，并与纯模拟后续收敛分开标注；没有把模拟的后续状态当作实测。

这解决的是自动流程过早结束和真实收敛追踪缺口，**不声称修复了Steam真实4→11的根因**，不声称已经恢复游戏按键或“大屏继续”。两个原目标仍需新包实测；全掌机范围尚未证明。UI仍一个默认开启开关，尊重已保存关闭，旧Overlay关闭策略继续弃用。没有风扇/虚拟手柄/驱动操作、游戏重开、Steam重启、Running写入、伪造AppID或注入。

### 同轮发现并修复：Steam 大屏的进程名规范化错配

07:29:53/07:30:07日志的真实steamwebhelper前台被分类unknown。生产sgBaseName转发monitor_identity.h的nativeIdentityNormalizeName，按设计去.exe并转小写；Steam恢复却把其结果与steamwebhelper.exe/gameoverlayui.exe比较，永远不匹配。这是可复现的代码错误，不只是对焦点缓存的猜测。此前Steam harness用自写filename()模拟sgBaseName，保留.exe，掩盖了该问题。

本轮先令harness引用生产同一份monitor_identity.h，新增真实Steam UI路径准入测试，在未改产品时复现断言失败；再把Steam UI分类改为已核实完整镜像路径中带扩展名的精确文件名，保留真实Steam安装根目录前缀/分隔边界及进程birth核验。其他安装、相似根目录、备份后缀、无扩展名、非Steam程序、无法核实或birth=0仍unknown；交互Overlay/编辑器保护不放宽。

该修复使已核实Steam大屏前台不再错误挡住针对睡前原PID的首次RETURN，包括列表已丢失但原进程真实存在的主通道；前台Steam UI进程绝不会取代游戏目标。它修复了明确的准入缺陷，但不证明Steam原生overview的4→11根因已解决，也不把实际手柄按键验收改成窗口/配置观察。


## Round25：睡前 Steam 原游戏绑定与旧唤醒凭据隔离

依据用户21:38包：R24最后一轮Steam真实跟踪2077 PID17060，但sleep capture为MSI Center M PID14904，input/list/PID-window动作全为0；不能算给2077执行修复后失败。悟空轮还把20:29:56旧退出事件带到20:45 capture，真正20:48最终唤醒前就worker-timeout。

### 原目标绑定

- 睡前先验证YMCC候选的真实PID/birth/path/当前Windows会话及Steam原生tracker精确绑定。不能仅因YMCC已把它当game就赋予Steam游戏权限。
- 候选不具Steam绑定时，仅在完整有界tracker库存中，存在唯一可验证、拥有自身有效游戏窗口、经过YMCC生产nativeScanGameOnePid完整游戏规则定向校验、且出生时间不晚于原边界的实例时，作为Steam-only目标固定下来。相对helper命令行不提供精确映像绑定；多个自身窗口或另一个窗口进程身份无法验证时拒绝猜选。
- 库存仅用于发现，不是进程活着/游戏运行的证明；须逐一核验真实进程路径、birth、liveness、会话、原生add窗口，并再检查原Steam会话。日志截断、扫描超过20000行、live库存超过32条、150ms软读取预算耗尽均不授权。未在Steam UI线程同步调用CDP/原生动作。
- prepare时选择后不可变；capture仅转交同一或紧邻下一代的prepared边界。不能在唤醒后用新当前候选/前台/标题/缓存AppID替换；PID复用也不能借fallback换成新实例。没有全局进程枚举。
- 新targetSelection记录preferredPid/created/exe、selectedPid/created/exe/appid、绑定来源、候选/无法验证窗口数量、日志完整性及拒绝原因。诊断在绑定失败时优先报告此原因，而非把MSI的identityMatches或空AppID当成2077恢复结果。
- 不修改YMCC共享游戏候选、规则、暂停标记或其他所有者。Steam目标被独立确认后仅等待自己的暂停状态；不会为修复Steam擅自释放MSI或其他进程租约。

### 最终唤醒凭据

- Steam订阅原有Kernel出口之外也记录506真实入睡时间。main.cpp仅给已有Steam只读钩子的条件增加entering，未改共享分类/重试/设备流程。回调不启动worker或改变共享power状态。
- 接受的最终507/Reason1或107/Target5必须晚于原prepared边界及最新真实506、并在既有60秒预算对应的凭据新鲜度内；17分钟前的退出不能借prepared代际转交启动新恢复。S3现有PBT路线保持。
- 较晚506进入若否定了已准入的旧proof，worker的current/admit立即失效。tick只有在list/input从未预约、inputDispatches=0时，才撤回该纯观察准入，等待真实较晚出口重新开始预算；已有任何动作/未知回执不会退款代际或重放RETURN。
- 采用原事件FILETIME而非采集顺序；晚收到更早入睡事件不否定较新真实出口。报告保存proofFresh/proofMaximumAgeMs/latestEntryFileTime/supersededByEntryFileTime。

实现标识：steam-wake-round25-original-steam-binding。新增sleep_steam_capture.h和21:38真实tracker fixture；fixture中的2077实际birth/窗口未由用户日志提供，OS层为明确模拟，不冒签真实现场恢复。

220组原生生产头回归覆盖目标错误、唯一性/身份/会话/窗口/截断/预算、按真实日志重新绑定、旧出口/新入口/队列race/动作后不重放。JS、UI、兼容API及构建见本轮validation材料。

这是修正明确入口缺陷，不声称Steam本地Running的4→11重建已解决，不把窗口或模拟配置稳定作为游戏按键消费证明。两个原目标仍需R25实际新唤醒验收及其他掌机覆盖；单默认开启开关/尊重关闭/旧Overlay关闭策略弃用不变。没有风扇、虚拟手柄、InputHost或驱动变动、游戏/Steam重启、假Running、伪装AppID或DLL注入。


### 2026-10-13 补充：YMCC–Steam 双向 PID 关联，不冒签共享候选一致

用户指出MSI Center是OEM前端、不可仅靠YMCC当前候选也不可仅靠Steam库存。本轮不修改MSI黑名单，不接管其他任务的共享游戏候选或暂停所有权。

- 正向：YMCC当前候选 → Steam精确tracker绑定 → 再走共享nativeScanGameOnePid及OS PID/birth/path/Windows会话复核。
- 反向：Steam库存发现真实PID → 唯一可用自身窗口及精确tracker绑定 → 同一份YMCC nativeScanGameOnePid完整判定 → 两边必须指向同一OS实例。Steam添加的启动器、助手或拥有窗口的小程序不能仅因出现在库存就授权修复。
- 定向检查复用普通游戏500MiB、白名单50MiB、已配置程序例外、同会话和名称/镜像身份等现有规则；不自行写新“像游戏”算法，不枚举全机进程，不发布共享候选。规则快照在原mutex内读取，规则epoch变化/身份变化/预算耗尽均拒绝部分关联。
- targetSelection新增bindingPolicy=ymcc-steam-exact-instance-join-v1、jointBindingVerified、ymccPidValidation、ymccSteamJoin与sharedCandidateMatched/status。jointBindingVerified只表示关联证据成立，不表示输入消费或继续成功。
- sharedCandidateStatus=different-instance：共享候选与本次Steam目标仍不同；本次选中PID另经YMCC完整单PID判定及Steam跟踪交叉验证。绝不把它写成共享检测器已经改选了正确游戏。sharedCandidateModified始终false。
- 睡前固定后不因唤醒焦点改变改选新PID；后续RETURN沿用该原实例，不使用另一进程的睡眠租约。两个验收位gameInputVerified/continueButtonVerified保持false。
- 原生harness现在直接抽取main.cpp生产单PID校验函数并编译，仅OS/API由模拟层提供；新增9组阈值/权限/规则epoch/PID复用/超时/候选分歧回归，总计229组PASS；源接线37项PASS。真实21:38夹具的OS birth/窗口仍为明确模拟。

仅改Steam关联头、Steam测试和此文档；本次补充没有修改main.cpp、黑名单、风扇、虚拟手柄、设备或游戏阀门实现。是否真能唤醒后消费手柄输入、持续保留真实运行态，仍需要新包实测。


## Round26：ROG 唤醒时共享生命周期停在 Resuming 的 Steam 专用可执行判定

2026-10-13 ROG 日志已经证明 YMCC PID、Steam tracker 和 OS 实例可以正确联合：失败轮的 `input=0`/`list=0` 不是动作失败，而是自动任务在共享 PowerLifecycle 长时间为 `resuming` 时被 `steam-power-not-ready` 超时。

Steam 自动所有者现在允许在共享状态为 `Resuming` 时继续自己的只读检查及既有单次 Steam 动作，但仅限已确认的本次 Kernel 507 Reason=1 或 Kernel 107 TargetState=5 最终唤醒凭据。凭据必须与原睡眠边界、generation、config epoch、原生事件时间、UI wake 请求、事件类型完全匹配，仍在60秒预算内，且未被更新的506入睡事件、睡眠重试或用户关闭撤销。

此路径不把 `Resuming` 改成 `Ready`，不打开共享硬件写门，不修改或释放游戏暂停租约，不调用InputHost/风扇逻辑，不重放不确定动作。报告增加 `wakeReadiness`，明确 `sharedPowerLifecycle`、`basis`、`operationAllowed`、`sharedStateModified=false`、`inputDeviceGateUsed=false`，并继续保留 `gameInputVerified=false` 与 `continueButtonVerified=false`。没有最终 Kernel 凭据时仍按原等待/阻止路径处理。

本轮只改Steam自动所有者及其测试；实机输入和大屏“继续”仍须使用新包验证。


### R26 补充：Steam 会话读取的短暂竞态

唤醒或Steam前端切换时，ActiveUser/Steam transport可能在第一次只读检查瞬间变化。Steam inspect现在最多进行两次、每次75ms的只读重试；只重试`steam.inspect`，不重试`ScopeRunningApps`或输入恢复等任何写操作。

重试成功还必须与睡前捕获的Steam账户、PID、创建时间、根路径保持同一会话；若账户或Steam进程确实改变，则继续fail-closed，不把新会话当成睡前原会话。`steamSessionRetry`和`steamSessionState`会写入inspect报告及sleep-facts，方便区分短暂读竞态和真实会话变化。


### R27：进程绑定的 Steam 界面账号

2026-10-13。R26 现场日志在两个唤醒代际都通过最终 Kernel 唤醒准入和 YMCC/Steam/OS 原实例联合绑定，但因注册表 ActiveProcess.pid 指向游戏，Steam 原生入口把未验证 ActiveUser 当成账号真值，最终在只读 preflight 以 `steam-live-account-changed` fail-closed。R27 在睡前 Ready/Suspending 阶段通过已验证 Steam UI/CDP 上下文读取并固定账号；唤醒阶段只复用该账号和 Steam PID、创建时间、根目录见证。固定读取是只读的，失败、漂移、进程重启和代际变化都会阻止动作。

R27 额外验证了生产 `steamLiveRequest` wrapper、UI account read、同父 Steam webhelper transport、重复 suspend 捕获和外部导出 marker。隔离完整 native build 已通过，实机手柄输入和真实 Continue 仍未验收。
