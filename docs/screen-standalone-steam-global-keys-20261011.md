# 无虚拟手柄 Steam／三个点专用键：全局按键边界（2026-10-11）

## 用户确认的边界

这是功能键触发，不是“给当前选中窗口发送快捷键”。YMCC 的触发许可与 Windows 当前前台 HWND／PID 无关。
不因为焦点不对而激活 Steam，不调用 SetForegroundWindow、SetFocus、AttachThreadInput，不自动切大屏模式。
按键请求是否送达，与 Steam 是否改变菜单状态必须分别记录。

调查初期的“绑定前台 PID／焦点必须不变”方案已按用户反馈撤回；同目录 Boundary 前的验证记录只作历史，
不能作为最终行为交付。此次也撤销了原生产链路中仅当前台为 Steam 才发送 Ctrl+1／Ctrl+2 的回退。

## 最终生产链路

屏幕按钮有效 UP → standaloneDispatch → 原 gamepadSerialSubmit → Steam 本地原生按键接收入口。

| 屏幕按钮 | 请求 | Steam 自身接收方法 |
| --- | --- | --- |
| Steam | menu.button / main | OnHomeButtonPressed() |
| 三个点 | menu.button / quick-access | OnQuickAccessButtonPressed() |

这是直接调用 Steam 客户端内部的按键处理器，不是虚拟 SteamDeck／Xbox HID 报告，也不是 Windows 键盘 VK 注入。
SteamUIStore 提供自己的 ActiveWindowInstance／大屏接收上下文；该选择属于 Steam 内部路由，不读取或绑定
YMCC 选中的 Windows 前台窗口。Steam 原处理器负责自己的防抖、菜单抑制及导航，YMCC 不直接修改 MenuStore 标志。

- MSI 与 ROG 没有分开的屏幕功能键发送分支，不依赖物理手柄接口类型。
- 现有退出、录制、电源和配置代次保护保持；不要求虚拟输出目标存在。
- 菜单能力／菜单状态读回也仅作为观测：即便菜单读回缺失或未知，只要接收方法存在且返回，仍记录按键送达，不把菜单状态反过来当触发许可。
- 原生入口不可用就明确报告，不抢焦点，不把 Ctrl+1／Ctrl+2 发进其他应用，不猜测 DLL／协议命令。
- 旧键盘工具函数仍可用于既有隔离夹具，但这个屏幕 Steam 功能键生产 dispatcher 不再调用它们。
- 此入口使用已存在且经身份验证的 Steam 本地 CDP 通道；没有启用调试端口、修改 Steam 配置或启停 Steam。
  它是已确认的客户端内部接口，不冒称为稳定的公开 Windows／Steamworks SDK 接口。

## 实际信号与日志

沿用基础事件 screen-standalone-special，单次请求用 requestId／generation 配对 queued 和 completed；
工作线程可能先完成再记录提交回执，应按 requestId 配对，而不是假定行顺序。

完成记录包含：

- actionName：steam-main-menu / steam-quick-access。
- signal：steam-native-button；handler 为两个接收方法之一。
- keyDelivered：接收方法已返回；异常或传输不确定时不冒称已确认送达。
- menuBefore／menuAfter／menuStateVerified／steamResponse：Steam 的菜单状态响应，与触发分开。
- liveReason／liveStage：通道不可用、账户变更、Steam 拒绝或传输不确定等原因。
- inputSubmitted=0、virtual=false、focusChangedByYmcc=false：这条生产链路不注入键盘、不发送虚拟报告、不改焦点。

Steam 因自己的防抖、临时菜单抑制或导航返回而没有改变预期菜单，并不等于 YMCC 没触发该按键。
一般功能键及不带恢复票据的调用仍不自动重发。2026-10-24 起，触摸三个点按下可单独授权下述 3 秒有界响应恢复；不把未知回执当作补发许可。
日志是事件级、每次按键有界记录；没有增加常驻轮询、设备扫描或新线程。

## 一手核查与验证范围

- 只读检查本机 Valve 安装文件 chunk~2dcc5aaf7.js，确认两个方法及其原生 Guide／QuickAccess 处理语义。
- 通过实际 Windows QueryFullProcessImageName 校验本地监听进程是安装目录的 steamwebhelper，父进程是 steam.exe；
  没有降低原 CDP 进程／端口身份校验。
- 在运行中 Steam SharedJSContext 只读调用生产 menu.inspect，确认两个接收接口存在，前后菜单状态均为 0；
  没有实际调用按键、发送 OS 输入、创建虚拟目标或改变真实焦点。
- 全局边界测试模拟任意 foregroundPid 参数、空上下文及辅助窗口，证明它们不构成 OS 焦点许可门；
  同时检查生产 dispatcher 不含前台读取、PID 绑定、焦点操作、SendInput 或键盘回退。
- 最终软件验证通过：全局按键边界 45、native standalone／Core 195、Steam 通用脚本 71、浏览器 138、实际主 EXE Core 108、触觉 574 项，native→Host 报文夹具通过，来源不匹配 0。结果见 boundary-final-verification.json；真实 MSI 点击、实际游戏和 Steam 覆盖层显示尚未验收。
- Native／Web 产物位于 BoundaryBuild；没有替换安装版。

## 证据

- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\boundary-correction.json`
- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\boundary-steam-actions-selftest.json`
- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\BoundaryFinalStandaloneVerification\standalone-special-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\boundary-live-production-menu-inspection.json`
- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\installed-steam-menu-api-snippets.json`
- `G:\YeManCC-Work\Build\Validation\ScreenStandalone-SteamSignal\local-steam-cdp-identity.json`

最终主 EXE SHA256：`7804B7CB089E57090318F30FB68C766DFDCC1ACD73049EBBC93B6B554A914726`。

## 2026-10-11 补充：官方快捷键与同接收器验证

用户再次明确：直接调用两个 Steam 自身处理器优先；Ctrl+1／Ctrl+2 只作对照验证，不代表授权加入自动键盘回退。
本轮不改上述生产调用顺序，也不增加任何前台窗口许可、焦点操作或虚拟手柄依赖。

### 证据的层级

1. Valve 官方 2023-02-10 Steam Client Update 明确记录修复 Control-1／Control-2 快捷键在 Linux 上失效。这证明快捷键属于客户端自身；Windows 上映射到两个处理器的证据来自下述本机 Valve 安装文件，不能把 Linux 更新记录直接说成 Windows 接口文档。
2. 本机 Valve 安装文件 `D:\Game\Steam\steamui\chunk~2dcc5aaf7.js` 的 Windows 键盘翻译器明确将 Ctrl+Digit1 映射为 STEAM_GUIDE（27），Ctrl+Digit2 映射为 STEAM_QUICK_MENU（28）。全局菜单事件分别进入 OnHomeButtonPressed() 与 OnQuickAccessButtonPressed()。
3. 两个处理器属于 Steam 客户端内部 UI 接口。此次查阅的公开 Steamworks ISteamFriends 文档没有这两个方法；公开的 ActivateGameOverlay 是游戏内覆盖层对话框入口，不能据此当成这两个全局功能键的公开 SDK 替代。
4. 微星与 ROG 的 YMCC 屏幕专用键生产分支相同；本机接口存在与隔离测试通过，不等于已经验收微星掌机的实际点击或游戏覆盖层。

### 本轮新验证

- `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\steam_installed_menu_keyboard_selftest.mjs`：从安装文件提取真实方法体，在无系统输入、无真实菜单变更的隔离 VM 中验证 19 项，包括两个快捷键的翻译、DOWN、重复抑制、UP、Alt／Shift／可编辑节点边界及生产无焦点／键盘回退约束。不能将此描述为真实 Windows SendInput 验收。
- 当前全局按键边界回归 45 项、原生 standalone／Core 195 项通过；Steam 通用脚本回归及嵌入一致性检查通过。
- 再次使用 QueryFullProcessImageNameW 验证已有 8080 本地监听进程与父进程的真实安装路径后，仅调用生产 menu.inspect。两个方法均存在；菜单状态均为 0，实际接收器为 Steam 自己的 active context。没有启用调试、实际发送按键、改变焦点、启动游戏或部署产品。
- CDP 通道仍是既有依赖，不能把方法存在当成所有掌机都具备已启用／可用的通道。接口不可用要记录 live-api-unavailable／传输错误，不假称成功；不擅自启用调试、不自动转发 Ctrl 快捷键。

Windows SendInput 只向系统键盘输入流注入，受 UIPI 和已有修饰键状态影响；它本身不是 Steam 专用全局接口。
因此“可用来验证同一 Steam 接收器”与“可在任意前台状态可靠替代直接调用”不能混为一谈。
本轮快捷键验证没有在真实前台应用中注入按键，下一步若测试 OS 输入，应由操作者明确选择现场环境，独立记录输入送达和 Steam 响应；不能因此为生产功能键增加焦点门。

本轮证据目录：`G:\YeManCC-Work\Build\Validation\SteamOfficialKeys-And-TouchpadCpu`。


## 2026-10-24：触摸三个点的有界响应恢复

正常触发保持：SD 虚拟手柄在 DOWN 发原 QAM 位，无虚拟手柄的 V 社 API 在有效 UP 调用原 OnQuickAccessButtonPressed。只有触摸三个点生成恢复票据；普通快捷键调用不启用恢复。首次触发后的 750、1500、2250 毫秒各有一次机会，至多补 3 次；3000 毫秒起不再发送。串行线程迟到会跳过机会，相邻实际补发至少间隔 750 毫秒，不追赶连发。

仅在接收端记录到本次原始按键、原状态为打开意图且当前菜单未打开时，使用同一 Steam 接收函数补一次。不重写菜单状态、不重新发 SD 报告、不同时叠加两条正常触发路线。已打开／曾打开、关闭意图、新三个点输入、主菜单／路径变化、菜单未知、Steam 锁／VR／上下文／账户变化都停止。抑制或处理器尚未完成则跳过当前机会；可能已派发而返回不确定时终止。

Steam 未运行、未登录或没有有效接收端时结束恢复；不启动 Steam、改焦点或降级注入快捷键。SD 原始按键信号的 UP／CANCEL、配置／睡眠／退出释放独立于 Steam 回执。Steam 和三个点两类 SD 菜单位额外具有 3000 毫秒发送上限：即使 UP 丢失或 HWND 所有者停顿，现有 buttonSnapshot 发送投影也会归零；原始所有权在随后 UP／CANCEL／退出清理。背键和其他 persona 的长按行为不变。配置先关闭、随后才到达的匹配 UP 仍负责释放自己的位。

原生 steam_qam_followup.h 在现有串行线程上执行工作，仅一次点击窗口内保留定时器。传输采用 350 毫秒短预算与 try_lock；不等正在忙的其他操作，不在 UI／BUS 等待，不新增常驻线程。鼠标 MOVE 不参与。发送上限只在菜单位按下时读取一次系统 tick，没有新轮询。

menu.qam.prepare／observe／retry／dispose 是内部协议。prepare 在模式变化时尽力安装被动接收记录器；observe 本身不发键；dispose 仅在仍拥有原方法包装时恢复。JS 没有补发定时器或延迟输入回调。模式退出尽力清理包装，原生退出立即撤销代次和所有发键资格；即使通道不可用，残留被动记录器本身不会发送。SD 首次按下前未成功准备、接口不可写或接收边缘不可确认时跳过补发，保持原按键可用。

本轮证据：G:/YeManCC-Work/Testing/YMCC/Evidence/Touchpad-QAM-Followup-20261024。隔离行为、真实生产函数消费、源码身份和编译结果以该目录 REPORT.md／RESULT.json 为准；不代表 MSI／ROG 实机 Steam 菜单已验收。


## 2026-10-24：一次反向健壮性复核

本轮保留旧失败，再执行生产函数，修正以下边界：

1. 仅保留最新接收事件时，延迟的第一次观察会把后一次用户输入当成本次原始按键。记录器 v2 固定保留两个接收边缘；检测到中间已有新输入便终止，不重放。时间更旧／相同时间而 ID 不同的请求不能覆盖新票据；同毫秒无法判定先后时保留原按键、停止恢复。升级 v1 会先恢复自己仍拥有的包装，再安装 v2。
2. 3 秒投影归零后，原触点若一直不收到 UP，仍可能挡住后续 DOWN。仅在新的菜单键 DOWN 到来且旧触点已经过期时，复用该按钮原有捕获丢失清理，先释放旧映射／反馈／自动隐藏接触，再接纳新触点；旧 ID 迟到的 UP 不能释放新触点。MOVE 不增加此检查或任务。
3. null、非对象、错误字段类型或缺少恢复状态的回执，可能在原生 complete 中抛错。现在按字段类型读回，只有已知未结束状态才能继续；busy 必须明确 mutated=false。坏回执停止恢复并清除 pending，不猜测重发。
4. 即便 complete 本身不抛错，原始 dispatcher 的其他字段读取或传输仍可能抛错。给该原始回调补上自有异常清理：只结束本代动作，撤销对应恢复，保持不确定送达状态，旧代不能清理新代；原动作已经完成后，即使记录步骤抛错，也不再次结束同配置下后来接纳的新动作。只有 standaloneDispatch 真正接纳原始动作时才建立 API 恢复；队列拒绝立即撤销。

本轮没有新增常驻计时器／线程、扫描或 MOVE 工作。保留正常首次触发、3 秒节拍和无 Steam 的失败边界。证据 G:/YeManCC-Work/Testing/YMCC/Evidence/Touchpad-QAM-ReverseReview-20261024/REPORT.md；隔离竞态与异常注入不冒充现场同一故障或下游驱动已经收到释放。
