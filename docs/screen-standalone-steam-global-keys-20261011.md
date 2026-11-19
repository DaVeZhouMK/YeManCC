# 无虚拟手柄 Steam／三个点专用键：全局按键边界（2026-10-11）

## 2026-11-10：Win5 21:29 现场与点击路由证据

用户新日志 YeManCC-logs-20261110-212927-触控专用对焦不对.zip（SHA256 60816c905e50f3c91be9359a7e34e321f9a228289790df6a12ea699fda9499e5）中，本次进程9248从21:18:39.509到21:29:16.603有34条完成记录：33次QAM、1次Steam，34次keyDelivered=true，32次本窗口菜单状态确认，全部windowType=0/isGameOverlay=false/source=steam-active-context。按键已进入Steam主窗口，不是按键没送到；本机直接脚本成功时选择的则是游戏覆盖层。两台机器和输入入口不同，不能由此证明触控夺焦点。

导出磁盘EXE哈希bf3ff0f3d0f30dfedf27dc5de835f58634bb21fe3d2925a1e049622cf190d3df，与21:05固定快照包及本机对照文件一致；清单明确该值不是加载内存镜像证明，不能据此抹掉运行身份边界。Steam在21:29:01追踪Front Mission 3 Remake，不等于已存在该游戏的可用覆盖层；包内gameoverlay_ui日志停在2026年1月，也不能代替当次覆盖层状态。

旧menuTarget没有缓存AppID/PID、查找失败原因或已有覆盖层信息，所以目前只能确认选了主窗口，尚不能区分缓存为空、signed AppID被原校验拒绝、覆盖层未创建、接口抛异常或类型不匹配。gameprocess内存在signed AppID记法只是一条线索，不代表已观察到FocusStore返回负值。本轮不改选择、回退或补发行为，不猜运行中游戏，不改焦点。

现menu.button/menu.inspect在原menuTarget内附带routing.version=1：实际active/selected窗口数值身份、当次缓存AppID/PID（保留负值）、lookup结果、最多8个已有覆盖层的数值身份及截断标记。不采标题、路径、账号、SteamID或原始窗口引用；不读新OS前台，不调用原生覆盖层接口。数据随原常开screen-standalone-special完成行落盘，无新线程/轮询/定时器/MOVE调度，记录失败不会拦截按键。QAM内部接收器检查不构建该清单。

原消费者137项通过，原132项断言保留，新增5项覆盖PID0诊断、负值保留、读取失败不阻断和清单上限；3个Raw Job自然结束且关闭前active0。当前主线JS/生成头同步，未重编/打包/部署；新日志需要下次EXE构建。证据为 Testing/YMCC/Evidence/Touchpad-Win5-Receiver-20261110-212927。根因结论保持ROUTE_TO_MAIN_CONFIRMED_CAUSE_UNKNOWN，不把增加证据写成Win5已修复。

## 2026-11-10：2077 本机开关链与合法 PID=0

用户开启 2077 并明确要求本机模拟后，读取现有 Steam CDP、实际加载的方法体和安装版 sp.js。实机回报游戏 appid=4131894616（非 Steam 快捷方式），覆盖层宿主 PID=77064，GetFocusedOverlayPID()=0。Valve 的 GetOverlayInstance(app,pid) 明确把 0 作为按应用查找的合法值；上一版本方只接收 pid>0，导航上下文保留主窗口时会错误回落。现仅将这处菜单路由条件改为 pid>=0，不改其他 PID/进程身份门。

真实本机保留“主窗口导航 + 游戏 appid + PID=0”时，对旧源和候选分别执行只读 menu.inspect：旧源 isGameOverlay=false，候选 isGameOverlay=true；两边菜单均保持0。这把源码反例和本机实际状态对应起来，没有改导航或制造焦点。

用户把游戏保持前台后，通过生产 JS 的 menu.button 实发一次三个点，实际截图看到侧栏覆盖在 2077 上，主窗口菜单始终0。游戏菜单0→2、合成模式0→2；调用同一原始接收器关闭后，两者2→0，截图确认返回2077。关闭只在同一实例、同一按键边缘及路由未变化时执行，临时观察标记已删除。该次验证是直接调用生产JS；没有重建/部署安装版EXE，也没有用鼠标点击完整走触控UI→原生队列。

链路为：选择已存在的游戏窗口 → 该窗口 OnQuickAccessButtonPressed → 该窗口 MenuStore → Valve ComputeOverallMinimumRequestedComposition/EnsureCorrectComposition → 原生 Window.SetComposition 与 Overlay.SetOverlayState。Windows 此链不依赖 Gamescope 的 NotifyGameOverlayStateChanged 分支。本方只选对原接收器，不直接写合成状态。

原消费者本轮132项通过（上一版123项，合法PID=0的开/关/补发/上下文变化及原正反边界合计新增9项）。旧源在PID=0路由正例失败；4个Raw Job均自然结束、关闭前active0。另用本机实际Valve方法体隔离回放3项：旧源只开主界面、候选向游戏提交合成2、关闭提交0；第一次回放因替身缺少纯日志GetName方法失败，原FAIL保留，补齐替身后3项通过。

低CPU路径仍仅在点击/既有750ms补发时读取缓存并查找实例，没有新增常驻线程、轮询、MOVE工作或刷新率。没有实测本轮CPU百分比。证据为 Testing/YMCC/Evidence/Touchpad-QAM-Live2077-20261110；主线JS/生成头配套，完整二进制、掌机触控和游戏内长期稳定性仍未验收。

## 2026-11-10：屏幕菜单优先现有游戏覆盖层

用户反馈直接三个点只出现在 Steam 主界面，游戏内原本大部分可用。本轮查 Valve Steamworks 官方覆盖层/ISteamFriends 文献，并下载当前 Steam 客户端 `sp.js` 镜像原件。官方公开覆盖层功能要求目标游戏已挂接覆盖层，公开 ActivateGameOverlay 是游戏侧接口，不是第三方全局 QAM SDK。本方仍使用既有客户端内部按键接收器，不声称为稳定公开接口。

Valve 的 OnQuickAccessButtonPressed 操作的是所属窗口自己的 MenuStore；ActiveWindowInstance 又优先已存在的导航上下文，再看 FocusedAppWindowStore 的游戏覆盖层回调缓存。后台主界面导航仍被保留时，本方沿旧 active/main 路径会切主界面的菜单。用户新现场没有本轮运行日志，所以这是源码确认的可复现路由缺口，不把所有现场情况归为同一个原因。

现菜单接收器保留当前已有的游戏覆盖层；否则优先读取 Valve 自己的 GetFocusedOverlayAppID/GetFocusedOverlayPID，用 WindowStore.GetOverlayInstance(app,pid) 精确取已有且 IsGamepadUIOverlayWindow 为真的接收器。查不到、API缺失/异常、非Gamepad覆盖层仍沿原 active/main 兼容路线。没有用 GetOverlayInstanceWithFallback、运行中游戏列表猜测、OS前台PID、窗口创建、SetOverlayState 或改变焦点。只选择 Steam 自己已记录的游戏覆盖层，不把“游戏运行中”当作“应该切走当前Steam界面”。

原始菜单键、只读菜单检查和750ms有界补发共用同一选择器。覆盖层目标/上下文变化时，旧点击补发终止，不能落到主窗口或另一个游戏；已打开/关闭意图、未知响应和退出/电源/取消保持原边界。账户门移除保留，设置/睡眠等账户操作不受此修改影响。Steam主菜单也使用相同接收器以保持这对屏幕功能键一致；不改实体/虚拟报告和触摸板滑动。

只在点击/原补发发生时读既有回调缓存并取一次实例，没有新常驻线程、轮询、刷新率或系统输入。复用原 screen_standalone_special_actions_selftest：旧源实际在新增游戏覆盖层断言FAIL（只切主界面）；候选完整123项通过，其中21项新增游戏路由，包含菜单原有102项。源JS与当前生成头 --check 通过，三个Raw Job均先归属后执行、自然退出且关闭前active0。无新C++业务/接口，未完整主程序link/打包/部署或在游戏中操作Steam。

来源及原件：Testing/YMCC/Evidence/Touchpad-OverlayRouting-20261110。Research/SOURCES.json记录在线地址、时间及SHA；当前客户端镜像是 Valve 交付代码内容的研究样本，不等于用户安装版/加载内存身份或未来接口保证。此修正只处理“已有游戏覆盖层却误选主界面”的路由，未挂接/被禁用/不兼容的覆盖层仍需按 Steam 的条件处理，不能承诺所有游戏强行可见。

## 2026-11-10：菜单功能键解除账户一致性门

用户明确要求移除 Steam／三个点菜单触发的账户保护。原生 `steamLiveRequest` 与 JS `checkAccount` 对固定七项菜单操作（menu.button/toggle/inspect、menu.qam.prepare/observe/retry/dispose）不再读取／要求 ActiveUser 与 UI 账户一致，原生也不向菜单脚本传账户绑定。QAM 记录器按 SteamUIStore／原接收器／原点击票据归属，不因请求账户变化重置。账户缺失、注册表/UI 冲突或账户读取不可用，不再挡住菜单键。

这项固定菜单范围不能由调用方的 skip 标记扩大；设置、鼠标配置、睡眠/输入租约等账户数据操作仍走原门。原 Steam PID/根路径前后变化检查、已有 action admission、电源／退出／取消、锁屏/VR和3秒750ms补发停止条件保留。没有添加键盘、抢焦点、Steam重启或轮询。JS源及原生成头同步。

原JS消费者旧源实际在“main functional key ignores registry account 43”失败；候选前65项通过后，一条静态片段断言因新allowlist误匹配到checkAccount而失败，精确锚定原菜单分支后只续测该项及未到达项37项，合计102个唯一通过项。覆盖菜单账户缺失/不一致/不可读、原其他账户门、取消/无Steam/上下文替换及补发边界。不是一轮完整发布门。

原生 wrapper 的私有端口消费者首次编译因未声明既有 `steamreturnevidence::bounded` 辅助而FAIL；没有执行原生业务或编译完整产品，原生通过未确认。最初两次Raw准备分别在环境Path/PATH重复、缺launcher/helper pin时拒绝，均在业务启动前；原始失败、已执行成本和900秒原预算保留，没有重开额度。generated脚本的 --check 在源码生成时通过，未完成后续Raw选择。

主线改动已落，未部署；证据 Testing/YMCC/Evidence/Touchpad-Menu-NoAccountGate-20261110，完整原生构建和Win5实机仍需单列。此前93次preflight拒绝是本轮触发依据，不倒判192710旧SD waiting-receiver已经修复。

## 2026-11-09：SD 屏幕菜单键复用无虚拟输出入口

本次用户要求将 SD 虚拟手柄档位的屏幕 Steam／三个点按键也改为直接触发。两键都在有效 UP 后进入原 `standaloneDispatch → nativeScreenStandaloneSubmit → menu.button`，分别调用 Steam 自身 `OnHomeButtonPressed()`／`OnQuickAccessButtonPressed()`；不再产生屏幕 SD 菜单位。`buttonSnapshot()` 额外过滤旧的低两位，避免与直接调用重复；实体手柄的原始报告、触摸板滑动、背键、PS5／Xbox 的虚拟按键均保持各自原链路。

SD 直接动作沿原配置代次／可用目标门准入，取消、切档、OFF／退出仍撤销 pending。丢 UP 的 SD 触点保留 3 秒期限，仅新 DOWN 时回收；期限不会发送虚拟 UP／DOWN。三个点从有效 UP 创建与无输出相同的直接票据，沿现有 3 秒、750ms 有界补发；不再在 SD DOWN 启动等待虚拟 receiver 的旧观察票据。未知结果／无 Steam 不发键盘或抢焦点。SD 设置页面也显示原直接动作失败状态。

Win5 19:27 包中，原生详情 210～330 行的 24 条 screen-qam-followup 覆盖 19:26:50.997～19:27:05.249，全部 waiting-receiver；导出时 SD active/applied 一致、按键输入及视觉 mask=7、error=0、直接动作计数为 0。这证明本段走旧 SD 观察链而未记录到接收边缘，不证明 Host／物理手柄或 Steam 全局坏死。判定位于 steam_live.js 的 entry.previous/event 与票据起始时间比较；没有将此状态直接归为驱动故障。

本轮只有源码修正及静态复核。隔离消费者准备计入原 900 秒预算，首次生产器启动前已到期并拒绝，实际编译／运行数为 0。新增未验证消费者留在私有证据，不接入默认门；原有消费者的 SD 预期随行为更新，未称其通过。未构建安装版、部署或操作 Steam／设备。证据：Testing/YMCC/Evidence/Touchpad-SD-DirectMenus-20261109。

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


## 2026-11-10：Win5覆盖层开关确认、开机启用选项及四模式触控菜单

Win5 22:00:43导出记录的最新进程10次三个点均送达主窗口，但Steam UI现有覆盖层Map为0。用户随后实际观察到Steam界面开关关闭，手动重新开启后确认“可以了”。ROG 22:20:27成功包43条新诊断行均选择已存在的游戏覆盖层（最新PID18320为20条），不是改焦点或猜游戏。关闭状态是用户实机确认，与日志吻合；未假装前面的Shift+Tab回复是实体键盘验收，也不再把未知开关值当默认开启。

开机模式通用启动中在风扇控制上方新增“游戏中启用 Steam 界面”。startupDesired.steamOverlayOnBoot在无配置/缺字段时默认true（用户随后明确要求）；明确保存false才停止自动设置，不反向关闭Steam。Windows boot identity在原SettingsFileGuard/文档写入器内先持久占用，再发本次设置；重开YMCC、renderer重建、睡眠唤醒和丢回执均不重复。YMCC授权logon启动器只在既有启动准入后调用一次可选准备，再重新核Steam存在性；准备失败不阻塞原授权Steam启动。普通主进程在UI启动前走同一启用器。Steam关闭时使用原scoped VDF解析、备份、原子写及回读；Steam运行只走既有settings.set(enable_overlay=true)，保留账户/会话/取消门且不运行时写配置。无新常驻线程、订阅、计时器或故障重试；结果随原启动偏好和生命周期日志导出。只管理总开关，不覆写每游戏AllowOverlay或重启Steam/游戏。

触控专用键四个profile（disabled/steamdeck/dualsense-edge/elite）新默认均为开启Steam全部，即Steam+三点，控制中心默认对侧呼出。PS5/Xbox保留原有专用键选项与原报告路径；显式选择Steam对时走直接菜单键，与虚拟目标是否可用无关。仅1/2两个专用键改变图标/动作，背键保持真实profile名称和映射；按下/移动不发菜单或虚拟位、有效UP只派发一次，取消/退出沿原代次清理，残留菜单位在快照再次剔除。已有配置明确的原生选择和旧mask-only选择保留，不强制覆盖用户设置。UI名称改为“呼出YeManCC控制中心”，可及名称同步。

已实跑：原生四profile消费者47项；原开机设置23项通过后，按用户新默认开启要求修正并实跑25项（含无配置、缺字段默认开启及明确关闭保留）；实际Vue与正式gamepad engine在1280×700/720/800的18项专用键检查，以及720/800开机控件检查通过。首批MSVC vctip工具残留被原Job强制清零，已保留失败；随后直接复用编译产物确认自然退出/Job0，没有重编已过的两个消费者。启动包装层临时提取器编译失败及其工具声明拒绝均保留；其六项新语义用例尚未完成实跑，不以源码审阅冒充通过。完整vue-tsc首次工具进程0xC0000005、关闭JIT后的续测在原360秒墙钟余量到限，保持未验收；普通包不自动增加完整夹具。当前未完整主程序link、打包、部署或真实开机验收。

来源：Testing/YMCC/Evidence/Touchpad-Steam-Overlay-Startup-20261110/REPORT.md、SOURCE_READY.json；原FAIL/UNKNOWN与本轮限定通过分别保留。触控按键路由没有因为开关问题改成强制焦点/创建覆盖层。


2026-11-10 默认开启补充：用户明确“无配置默认开启”，当前DEFAULTS、PowerView缺省显示和原生开机准入已同步；只把缺省解释为开启，不把字符串/损坏分区当作缺省。25项原生及720/800实际Vue/gamepad检查通过；MSVC工具遥测按原已声明限定策略回收，自有Job均归零。前轮完整类型检查/启动包装层未验收继续保留，无新包或部署。最新来源：Testing/YMCC/Evidence/Touchpad-Steam-Overlay-Startup-20261110/SOURCE_READY-DEFAULT-ON.json。


## 2026-11-10：触控专用键正反检查的两项待修

隔离生产头文件/Vue回放确认：丢UP后的期限回收仍限定SteamDeck，disabled/PS5/Xbox新点击可能被旧contact挡住，旧UP可迟触发；部分开机预设只有summon/autoHide时，子组件未补新默认，下拉显示关闭/“—”。四profile普通点击正常，回放虚拟报告位均0；capture loss/切换/退出清理仍保留。修复边界应只扩Steam直达回收并统一部分预设缺省，保留原PS/Xbox长按及旧明确选择。本轮仅检查复现，产品执行逻辑未改；前轮通过不包括新负例。证据Testing/YMCC/Evidence/Touchpad-Steam-Overlay-Startup-20261110/chain-audit/REPORT.md；两个自有Job已归零。完整类型检查/启动包装层未验收不变。


## 2026-11-11：触控专用键两项缺口已修及正反验收

接续chain-audit的两个待修：四模式Steam直达键统一在新DOWN时回收原3秒期限已过的旧contact，先释放旧反馈/自动隐藏接触，迟到UP不得作用于新触点；部分开机预设编辑器按实际ProfileBank补缺省视图，保存仍只发原部分配置和显式改动，保留false/zero/off、旧mask-only及PS/Xbox原生长按。没有新增轮询、线程、timer或MOVE工作，开机Overlay同伴代码未改。

旧原生22项反例与旧Vue缺省显示反例实际失败；候选原生118项、实际Vue/正式gamepad engine侧栏72项、原四模式界面18项及32组原生/TS逐字段对照通过。初次Vue因退场动画双listbox失败保留，只修等待后重跑受影响预设组，已通过消费者不重跑；同一原始开始/1800秒预算，自有Job均0，编译vctip按已声明限定策略收尾。前轮完整类型检查/启动包装层及完整EXE/打包/部署/实机未验收保持，不能以软件回放冒充Steam游戏画面验收。来源：Testing/YMCC/Evidence/Touchpad-Steam-Overlay-Startup-20261110/chain-fix/REPORT.md、SOURCE_READY.json；旧待修记录为历史，以上追加为修复现状。


## 2026-11-11：触控Steam/三个点临时诊断精简

用户要求只保留少量必要数据。routing.version2只记录lookup分支，删除active/selected/lookupWindow及AppID/PID/browserID明细、覆盖层清单与遍历；原完成行的接收源、窗口类型/是否游戏覆盖层、送达/响应和失败原因保留。原路由、按键及3秒补发未改，QAM区域与原件逐字一致，没有新轮询或线程。原消费者137项通过（132原功能+5调整后的诊断合同），确认额外清单/窗口metadata读取0；生成头同步检查通过，自有Job3个自然结束active0。未完整EXE编译、打包或部署，CPU百分比未测。证据Testing/YMCC/Evidence/Touchpad-Menu-Log-Trim-20261111/REPORT.md、SOURCE_READY.json；旧详细日志继续保留。


## 2026-11-11：手柄快捷动作与触摸菜单发送通道统一

手柄快捷动作的 Steam 键、Steam 三点键，现在与触摸专用键共同调用 `nativeSteamMenuButton`，经既有 `steamLiveRequest` 的 `menu.button` 进入 Steam 当前主菜单或游戏覆盖层接收器。四种虚拟手柄模式均可用，不再依赖 Steam Deck 虚拟目标或回退到 Ctrl+1，也不再为发送菜单键改变前台焦点。PS、Xbox 原生 Guide 与 Gamebar 原路径保留。

触摸三个点原有 3 秒／750ms 的有界补发及票据仍由触摸源管理；普通手柄快捷动作保持一次派发。手柄新菜单动作仅撤销派发接纳时捕获的旧触摸票据，不影响后来触摸的新票据。排队以及最终传输前均复核规则、输入来源、OEM/WMI、录制、退出和电源归属。失败、坏回执或不确定送达不改走键盘或虚拟输入。回执分别保留已发送、Steam 接收与菜单响应，不能把发送成功当成菜单画面已出现。

原 native 快捷动作消费者定向 `frontend-buttons` 235 项、Steam 接收端原消费者 137 项、现有三点映射 Vue 浏览器消费者 18 项通过；旧源在相同原生消费者中实际复现通道差异。私有完整来源类型检查通过。首次 PATH 超长、缺游标通知端口、缺前端源依赖等失败原件保留。证据：`Testing/YMCC/Evidence/ControllerSteamMenuChannel-20261111`。未完成完整主 EXE 编译、打包、部署或实物手柄／Steam 游戏画面验收。
