# Steam integration maintenance / Steam 更新入口



当用户要求适配 Steam 更新、修复 Steam 接口、设置、大屏、侧边栏或睡眠恢复时，先读 [STEAM-UPGRADE.md](STEAM-UPGRADE.md) 和 [docs/steam-integration-map.json](docs/steam-integration-map.json)。

- 用 `node tools/steam-integration-audit.mjs` 只读盘点全部接入点、原生 IPC、JS operation 和未归类候选。不要只看 SteamView 或某一个修复文件。
- 重新核验官方渠道/版本和实际接口；索引内日期与版本是历史快照，不保证永远最新。逐项记录受影响、兼容保留和待实测范围。
- `native/steam_live.js` 为源，修改后用 `node tools/steam_live_script_embed.mjs` 生成头文件；不要单改生成物。
- 保留并行任务、用户设置、原实例校验和各模块边界。此维护索引不授予部署、重启 Steam/游戏、切分支、真实睡眠或输入设备变更权限。
- 新增或移动 Steam 接口时同步更新总览/JSON。只有相关回归与实机证据才能支撑对应结论；盘点、编译和接口回执不是完整兼容验收。

## Steam 睡眠修复项目的唯一入口

本聊天用 [项目总记录](../../../Docs/Research/Sleep/SteamNonSteamRecovery-20261011/STEAM-SLEEP-EXECUTION-BASELINE.md) 和同名JSON持续更新一个项目，旧修复整体保留。普通YMCC日志直接分析，CMD可选；用户手动控制现场，助手不改YMCC设置/界面、不启停或睡眠。旧入口仅跳转；旧修复代码待最终验证后再统一查依赖、合并精简。不要重复搭环境或重问既有授权，当前模式限制优先。

## 性能页面 UI 快速验收

用户于 2026-10-17 约定：只想快速查看和纠正性能页面界面时，优先向实际安装目录作最小必要导出，不默认等待完整主线压缩包。沿用当前会话授权继续执行，不为常规可逆步骤重复暂停确认。保留用户设置，保持前端与原生接口匹配；完整包仍在用户明确要求主线完整导出时制作。

用户同日补充：界面迭代交付优先提供可直接运行的主线测试版启动入口（Start-Mainline-Test.cmd）即可，不必每次附完整ZIP。性能页按钮比例对齐YMCC常用控件，采用更低、更横向的按钮；不修改全局缩放来压缩界面。


## UI 手柄验收门（2026-10-18 用户要求）

适用范围：真实 UI 导入主线、导出安装测试版及正式交付。纯方案、模拟布局和 H5 预览阶段可以先不做完整手柄检测，不应因此拖延预览；一旦导入真实主线，必须完成检测后再放行。本次 2026-10-18 已进行真实主线导入，因此完整检测为必需项。

验收须使用已导入主线的实际组件与正式 `src/gamepad/engine.ts` 检测 1280×720、1280×800 两种比例。左右沿视觉同行并在边缘停止，上下按相邻视觉行；启用控件可到达、可返回，滚动后焦点完整可见；隐藏/禁用控件和左侧导航不得进入普通方向候选。覆盖 A、B、下拉、滑块编辑与取消、弹窗所有权及既有 LB/RB 切页、LT/RT 调节，不得仅靠静态标记或编译放行。

本轮真实组件回归入口位于 `G:/YeManCC-Work/Build/Tasks/Inner-Pages-Import-20261018/verify.mjs`（内存 IPC，禁止当作实机输入验收）。检测通过后运行 `node tools/gamepad_ui_release_gate.mjs <本轮证据目录>` 核对所有被测源码和预览哈希；源码变化须重新构建并检测，失败或旧回执不得放行。浏览器方向与动作回放、实物手柄/安装版验收须分别说明。

性能调度首个手柄焦点为 TDP 滑块；初始仅聚焦，所有滑块须按 A 后才能调节。返回或重新进入页面不得沿用旧的滑块编辑态。模拟测试每次新开默认轻度 2D，第一人称镜头/持枪/载具晃动幅度为此前的 25%，保留行进速度与动作内容。

## HC 精简与运行依赖验收（2026-10-18 真机回归）

用户要求把精简故障反馈到本机现有 AI 模拟器；不能依靠掌机日志逐库试错。HC runtime/profile/第三方依赖改动，至少运行 `pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain fan -Mode connect -EvidenceDir <新目录>`，以实际待交付 RuntimeRoot、SharedRuntimeRoot、HostPayloadRoot 覆盖默认路径。这个门五阶段已包含精确运行库正反校验、profile声明的实际交付库及COM ABI正反检查、SharpDX托管callback/反射/shadow与9种破坏反例、真实13个Manager构造、旧坏NAudio必须失败的对照，以及公开mock API开启、控制、关闭、归还和自然退出。仅类型/签名加载或绕过HC初始化的null mock不可代替。

COM、native callback、接口继承及实现类的裁剪须先过 `tools/hc-application-dependency-trim` 的原始输入ABI审计和正反门，不能按托管MemberRef可达性删除COM槽位。interopAbiRepair 中34项恢复仅为历史记录，不得据此强制恢复上游大文件。软件模拟不证明掌机EC/固件/物理转速；未知或缺少回执必须保持未验收。
## Decky 双通道更新发布必检（2026-10-18 用户要求）

每次 YMCC 发布，必须通过 tools/decky_update_release_gate.ps1 的 Build / Package 两阶段，并在导出安装前 Verify 对应回执。Build 验证 Decky 自身更新的支持平台、明确确认、失败备用、不重复安装、重启后的实际版本；YMCC 通道验证原更新管理器、认证回环、下载校验、游戏/电源及断线恢复。Package 在发布候选 ZIP 前检查 Loader/插件/主程序/网页匹配，并在临时目录验证完整安装、启动握手失败及子程序失败恢复。缺少回执、源码或产物变更、任一用例失败均阻断发布，不允许用版本查询或旧回执代替。Windows 的锁定兼容 Loader 不直接覆盖成上游 Linux 产物；原生通道不支持时明确走 YMCC 整包。真实 Steam/SteamOS 与物理手柄验收单独记录。

## 全页导航模拟器维护入口（2026-10-19）

上述真实组件/正式engine回归现已复用并固化到 `tools/ymcc_sim/global/ui-navigation/`，统一运行 `tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests controller.ui-navigation`。1280×720/800、原 `tools/gamepad_ui_release_gate.mjs` 精确源码门及物理验收区分要求保持；夹具与每轮产物分开，新增产品接口须同步内存合同。原Build目录保留为历史来源，不是今后唯一可运行入口。

## Decky 故障恢复边界（2026-10-19）

连续短时崩溃达到 LoaderCrashBudget 上限时必须停止自动启动；只有明确重新开启或已验证的新 Steam 会话可更新预算。不能用连接恢复、窗口事件、unknown 状态或循环重装绕过。不自动重放未确认的设置/安装，不清空用户配置、不杀非自己拥有的进程；校验/来源/权限异常应停止并保留具体错误。新增崩溃及稳定性检查由 Decky 更新发布门强制执行。社区问题覆盖与真实 SteamOS/实物手柄验收分别记录。
## 功能修复与模拟环境同步维护（2026-10-19）

用户要求缺口同时补正式源码和现有测试环境。修复风扇、输入、灯光、运行依赖或睡眠软件链路时，同步维护 `tools/ymcc_sim` 中直接执行正式消费者的场景、错误回执和能抓住原问题的负例；共同流程见 [HC 升级与双向验收合同](tools/ymcc_sim/HC-UPGRADE-REGRESSION.md)。已有调用链正确时以实际执行证据说明，无须为凑修改数改产品。找不到真实消费者、缺少实际执行回执或只有独立模型时保持未验收。

保留基线、原失败和候选身份；升级 HC 复用现有环境与场景，实际 ABI/协议差异须修适配并定向重验。旧轮回执用 global `-Mode audit -Receipts <旧轮>,<后续轮>` 核对源码和明确载荷，不能凭历史 PASS 放行新源码。软件模拟、Host 启动集成、安装与物理设备验收分别记录；本规则不授权真实系统睡眠或硬件操作。

2026-10-23 用户明确撤销“普通 DLL 只允许上游原件、仅五个例外”的限制。生成器、校验器、ABI 门和导出器都允许经过审计的精简导数；逐文件按照当前主线 profile/manifest 的大小与 SHA256 核对，不按 DLL 名称或历史34项恢复记录拒绝精简、覆盖当前已认可的较小文件。ABI 门检查实际 COM 槽位、继承、封送、callback 与实现兼容性；保留真正损坏反例，不能用“字节不同”代替兼容性失败。下次完整导出须将同一冻结源码、受管载荷、打包模板、ZIP 和部署清单关联，并对已认可精简基准作逐项差异核对；发现无明确变更依据的大文件回归即停止交付并修复来源漂移。不能把旧构建、旧修复包或上游默认值当作当前主线载荷。

## Decky 官方依赖升级（2026-10-19）

后续任务直接从 tools/decky-loader-upgrade.ps1 的内置帮助和源码开始：Check / Prepare / Apply / Verify / ReleaseGate。身份唯一来源为 decky-plugin/vendor/decky-loader.lock.json，native/decky_loader_identity.h 由工具生成。只消费官方 Windows 公开版原字节，不再维护或重新启用 windows-loader-policy / patch-decky-loader.py 的历史补丁。YMCC 保留自己的插件、认证桥接、进程隔离和既有整包更新，禁止新增专用更新器 EXE。GitHub 发布前在线最新稳定版、官方 SHA/asset id/source commit 检查不得跳过；准备并提交新底座后再打 tag，CI 不修改不可变 tag 的源码。

## Decky 更新与故障关闭（2026-10-20 用户决定）

产品只走 YMCC 整包更新；公开 Loader 保持官方字节，不另做更新服务或 EXE。维护入口为 `tools/decky-loader-upgrade.ps1`，源码含 Prepare/Apply/Verify/ReleaseGate 示例。每次升级须复核 `native/decky_sidebar_health.js` 读取的官方错误页和生命周期接口，生成头后运行 `tools/decky_update_release_gate.ps1`；不得以普通网络、商店、单插件失败或旧上下文自动关闭整个 Decky。明确致命故障由 YMCC owner thread 走正常关闭并保留原因，不自动重启 Steam/游戏或重新启用。完整 Build/Package/Verify 回执与已测源码匹配后才可发布；离线测试不能写作安装版/实物手柄验收。

## Decky 更新重连回归（2026-10-20）

用户要求把本机更新后断连录入现有夹具并修复。场景 `plugins.decky-update-recovery` 已注册到全局模拟器，生产更新门调用同一消费者；现场样本在 `tools/ymcc_sim/global/decky-update-recovery/field-record.json`。修改更新器、启动准入、模式读取、bootstrap或插件重载时必须回放该场景，旧策略负例必须在原断连点失败。更新接续仅Decky、同一已验证Steam会话；保存ON不等于普通启动许可，用户/故障OFF优先。不可删除启动门或自动重启Steam来使回归通过。


Decky 同模块重开 YMCC 的传输恢复见上述场景 README：retainedBinding 只恢复认证桥接，必须严格核对同一 Steam 身份、模块 build ID 和官方 Loader 版本，实际目标再次核对；禁止借机启动 Loader、页面重建、重放写请求或绕过 OFF/崩溃预算。修改时保留删除 retained 准入的指定失败反例、连接退避和健康零轮询检查。模块不一致须分开记录，不能宣称已恢复。

## 退出销毁与持续类自愈验收（2026-10-22 用户要求）

维护功能或夹具时，AI须依据实际资源所有权、订阅/定时任务/会话及跨事件存活行为，自行判断其为持续类、一次性或尚未判明；未判断不能默认为一次性。退出必须有界销毁该功能拥有的窗口、定时器、订阅、线程/子进程等资源，正常关闭失败或挂起时也要证明自有资源被兜底销毁；仅发出stop/close、隐藏窗口或退出测试进程不能放行。停止后晚到回调、重复关闭和旧电源/连接代次不得使功能复活。

持续类在用户仍希望ON时，须证明睡眠与暂时故障后的实际健康恢复，不能以requested、connected、保存ON或旧ready回执代替。显式OFF、致命故障关闭、Decky崩溃预算、当前会话身份及官方资源校验仍优先，不允许无限重试或重放未确认写入。缺失运行证据保持未验收。首批必须覆盖监控通知退出残留和Decky睡眠不自愈。

公共规则及合同见 [生命周期验收门](tools/ymcc_sim/global/capability-lifecycle/README.md)，随现有全局模拟器执行并输出lifecycle-acceptance.json；保留原有场景、预算、失败与UNKNOWN。桌面复用已有环境，现场用普通日志；此要求不授权操作安装版、真实睡眠、设备写入或终止非自有进程。
## Steam 自定义外观的资源优先级（2026-10-22 用户约定）

- YMCC 管理的八个主题以 `PowerControl/steam-appearance/manifest.json` 和随包资源为准。用户从插件商店下载、更新同名主题后，即使商店版本更高、YMCC 包版本未变，也应按文件内容校验并用 YMCC 版本覆盖有差异的托管资源，先沿用既有备份/事务。此规则取代过去“同包资源差异不自动覆盖”的策略。
- 资源覆盖包括 CSS、theme.json 等清单文件；保留 `config_USER.json` 中的开关、亮度、饱和度和个人参数。只有用户明确恢复预设才恢复默认8/8，不把资源同步当恢复默认。
- 通过现有外观应用及已确认 Steam 大屏会话的授权流程核对，不增加定时扫描或常驻搜索。Status/前端挂载只读；不因覆盖资源启动 Steam、切换 Steam 模式或调用启动 BAT/VBS。
- 只修复已有 YMCC 接管记录的托管目录；未接管目录、无关主题及原有外部主题冲突准入继续保留。CSSLoader Desktop 正在编辑时暂缓覆盖，等待后续授权使用；不终止编辑器。

## Steam 不得自动重建界面或抢前台（2026-10-22 本机复现）

- 用户反复要求：普通启动/重开YMCC、恢复设置、默认外观全开、自动恢复和唤醒，均不授权启动Steam、切入/退出大屏或使Steam抢前台。用户主动点击与明确设置的开机Steam授权入口独立保留。
- 除直接启动/模式URI外，也必须禁止自动调用 `SteamClient.Browser.RestartJSContext` 等整个Steam页面重建操作：本机20:16:55自动Decky连接恢复后，Steam20:16:57重建BPM窗口并到了前台，Steam进程未重启。不能因进程没重开就认为没有违背用户意图。
- `decky.recoverConnection` 自动来源在原生与实际嵌入JS两层拒绝整页重建；需要此操作时交给显式手动修复。已存在的认证transport接续、后端重连和CSS应用不得借机调用整页重建。禁止用伪造userInitiated绕过。
- 回归保留普通初次启动、重复事件、换文档、睡眠/Ready、替换Steam身份均为零自动UI重建，以及用户主动手动修复仍可工作。现场和证据见 `Build/V/SteamNoAutoFront22/REPORT.md`。

## Steam CSS 唯一常驻策略（2026-10-23 用户取代旧策略）

- 用户明确要求确认唯一 CSSLoader 执行程序后常驻。本条取代旧的120秒退出、自动Idle杀进程、secondary cleanup和关闭YMCC时销毁CSS的规则。CSS是独立常驻服务；一次性PowerShell助手、订阅与等待句柄仍须有界退出，不能把助手一起保留。
- 验证当前用户/会话、精确路径及35821监听者；PyInstaller父子进程算一份实例。启动互斥期间只在缺失时创建一次，后续复用；不就绪、其他路径、重复独立根、编辑器或身份未知时不新建、不杀旧进程、不重启CSS。YMCC关闭、Steam退出/换代均只释放请求。
- CSS专用CreateProcess明确脱离助手Job，外层Job拒绝时失败返回，不降级启动第二份。原生Group只持CSS观察句柄，禁止将CSS放入kill-on-close Job或对其TerminateProcess。Idle兼容接口只确认常驻，不写退出回执。原生idlePolicy=3、自动32字节回执protocol=2须随源码和脚本一起交付。
- 配置异常通过既有读取/已确认Steam模式事件合并一次核对，仅现有后台、已接管目标允许；Status本身只读，草稿与用户新点击优先。不增加巡检timer；同一异常不自动重放。健康DOM/配置零主题写入，差异只写对应主题；新文档缺样式才一次完整补注入。资源散列仅助手内按元数据复用，配置始终重新读取。
- 保留Desktop编辑保护、8/8出厂预设、个人开关/参数及商店资源覆盖优先级。绝不因此启动Steam、改变模式、调用BAT/VBS、RestartJSContext或抢前台。证据统一位于Testing/YMCC/Evidence/SteamThemeReconcile-20261023；软件回归不等于安装版/真机验收。

## Steam 主题实际注入回执（2026-10-23 持续稳定性）

- 不能用任意一个style.css-loader-style标签或get_themes.enabled=true认定八个主题均已注入。八个托管主题的shared.css各含一个--ymcc-receipt-…:1根变量；同一只读CDP表达式核对八个变量与当前配置，所有已验证大屏目标均须一致，OFF主题不得残留自己的回执。
- Reconcile一次只重提已知缺失/残留回执的主题及实际配置差异，保留全部主题的失败恢复合同。未知/报错/未就绪DOM不授权盲目重写；沿用0/2/7/15/24秒有限窗口，外层观察最多5轮，纠偏最多1次（纠偏前后另各1次核对、成功后常驻确认另1次）；纠偏后配置/目标改变则停止，不自动重放。手动Apply/Refresh也须区分已保存和实际回执确认，前端拒绝ok=true却renderConfirmed=false的矛盾结果。
- 只读回执验证覆盖shared.css实际到达页面；单独patch CSS丢失、真实Steam选择器/GPU、真实睡眠仍需独立验收，不将这次通过写成主题效果全覆盖或目标完成。新CSS字节及manifest校验须一同更新，导出门拒绝缺回执的主题或旧检查模块。
- 社区#126链接暂列待核线索（本轮抓取没有可存档正文），不作为Windows根因证据；本地旧实现的1/8+32style假成功已由真实PS5.1、HTTP/WebSocket及浏览器表达式独立复现。证据继续位于Testing/YMCC/Evidence/SteamThemeReconcile-20261023/render-receipts，不操作安装版/真实Steam/CSS。

- 本机只读确认.NET Process.Path为空但QueryFullProcessImageName可读，不能把空Path当未启动或永久未知。Get-AppearanceProcessPath保留可读Path快路径；空/抛异常时以PROCESS_QUERY_LIMITED_INFORMATION读取同一handle的出生时间和路径，出生不符立即拒绝，所有handle关闭；不借此提权/终止/新建进程。resident模块须在监听者发现之前载入，CSS/Steam/编辑器/CDP路径统一使用。PS5.1和7自有进程与控制器夹具各10项，以及本机CSS零控制只读回执留存。
- 外观模拟器的OS互斥边界New-AppearanceMutex在产品中仍限定Launch/ThemeControl固定名称；夹具替换成每次随机独立命名空间，避免与现场CSS控制争用。正式进程/端口/父链不得泄漏进模拟值；异常收尾仅精确路径、当前用户/会话的自有惰性进程。

## 可选CSS文件与唤醒事件复查（2026-10-23 持续稳定性）

- render协议2取代共享文件回执协议1：44个受管静态CSS各含稳定文件回执，当前选中项须存在，未选项/OFF须不残留。Get-ThemeRows复用已经读入的theme.json/config构建renderPlan；不重新扫描CSS文件。只读CDP仍一次取全部值，Reconcile按有问题的主题去重定向重提。核对后提交前再次检查Desktop编辑和原配置revision/target；变化就停止旧纠偏。
- 这覆盖shared及可选静态文件，不等于已证明动态变量事务、所有独立MainMenu/QuickAccess目标、Steam真机选择器/GPU完全正确。所有CSS和manifest的SHA/bytes须随协议一起更新，导出门拒绝可选文件缺回执。
- CSS恢复只接已有WM_POWER_RESUME_COMMIT且Ready、当前power generation的事件，Runtime.requestResumeRecovery按generation去重，OFF/退出/隔离/旧Steam身份不准入。worker先通过实际refreshMode回调作废睡前临时助手/旧成功确认（CSS保留），再使共享模式缓存失效并请求既有读线程；新确认大屏后才做一次有限CSS核对。相同PID/相同文档也不能跳过，重复Ready不得重开助手或后台。
- 新增测试已接既有plugins.decky-update-recovery生产者。Decky专属共享Ready夹具对CSS定义明确的惰性端口，CSS实际回调/实际guard另由steam_cssloader_resume_selftest执行；不能以Decky夹具端口代替CSS验收。正例、禁用恢复调用的语义坏例、原生常驻进程及旧确认作废回执分开留存。
- 本轮没有安装/真实睡眠/Steam模式操作。持续目标仍ACTIVE，证据在Testing/YMCC/Evidence/SteamThemeReconcile-20261023/patch-receipts；真实睡眠和完整匹配发布仍须后续独立验收。

<!-- STEAM-APPEARANCE-VARIABLE-RECEIPTS-20261023 -->

## Steam 外观变量核对（2026-10-23）

- render 协议3取代2：同一次有界DOM查询覆盖44个静态文件与18个受管SP根变量，核对选中值、无覆盖选项的静态默认和OFF残留。计划仅使用已解析配置及源码默认表，不重读全部CSS。默认表须与源CSS一致，导出门在原SHA/bytes校验之外检查静态默认；浏览器测试独立验证实际值和轮播transform。
- 原生创建助手前须核对idle.ps1的render3/变量模块，typed bridge拒绝旧计划与不安全/不完整变量字段；原生常驻生命周期策略仍3、32字节结果协议仍2，不能混淆。临时助手退出不销毁CSS，现有实例不重复创建。
- 变量缺失或旧值按主题合并一次定向纠偏，沿用Desktop/revision/target围栏和有限窗口；没有新增timer或Steam启动/模式/前台/JS-context权限。
- 这证明受控浏览器的当前SP变量值和静态文件；独立MainMenu/QuickAccess、异常退出完整自愈、真实Steam/GPU/睡眠和匹配安装版仍需各自证据。持续目标ACTIVE，详见G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/variable-receipts/REPORT.md。

<!-- STEAM-CSS-ABNORMAL-EXIT-RECOVERY-20261023 -->

## CSS后台异常退出的事件恢复（2026-10-23）

- 对已确认的resident后台退出HANDLE读取真实退出码；仅已知非零异常、原Steam身份/当前ON及旧确认撤销成功可准入。正常0、console-close、未知/仍活状态不重开；所有退出均撤销旧确认、清理自有临时助手，绝不杀CSS。
- ExitRecovery仅由真实退出事件启动30秒一次性期限；同一已验证Steam进程最多2次恢复。模式、账号、Ready、目录写入与OFF/ON不重置该上限；仅新Steam出生身份或明确requestManualRecovery可重置。现有同族幸存进程优先复用，身份不明/冲突/编辑器保护不授权创建或不确定重放。
- 期限到达才经原refreshMode撤销缓存、请求共享模式读；重新确认大屏再调用原Ensure/唯一实例互斥/临时助手。睡眠未Ready时只等已提交Ready事件接续同一已计数恢复，不启动Steam、不改模式/前台/整个JS上下文，不引入常驻搜索timer。
- 原启动attempts与automaticRecoveries分开记录，guard诊断policy3。旧event-handle夹具是非进程退出，只更新其unknown诊断，保留其不重试/预算/CPU原断言；新增真实惰性进程退出和禁用分派语义负例接原plugins.decky-update-recovery。
- 软件回归不等于安装版/真实CSS崩溃/物理睡眠验收；当前目标仍ACTIVE，证据G:/YeManCC-Work/Testing/YMCC/Evidence/SteamThemeReconcile-20261023/crash-recovery/REPORT.md。

<!-- STEAM-APPEARANCE-MANUAL-RESIDENT-20261023 -->

## Steam 外观手动完成接续与本机候选（2026-10-23）

手动 Apply/Refresh/Reset 的成功和失败都结束唯一临时租约，再按当前 Steam PID/birth、generation 和 Ready 发出一次只读接续。VerifyOnly 不 Ensure、不修复资源、不重放配置，硬期限最多30秒。OFF、目录变化、编辑器优先、退出和旧会话会取消本功能助手，保留独立 CSS 后台。健康监控只等待进程/目录/共享模式事件。

本机证实 elevated Steam 的 webhelper SID 查询会在普通权限下失败。按已合入的 CSS 单一身份策略，Ensure 与 Automatic 均通过 steam_cssloader_launch.h 保留 YMCC 当前用户、会话、logon、完整性和 elevation；Decky 继续使用自己的降权创建器。初始化完成后才接续核对脚本，失败与手动接管不允许晚到的自动确认；第三方后台仍唯一常驻。

本机导出已验证自动修复一份资源差异、手动重新应用和只读接续 ready；Steam PID63648、birth和大屏文档代次保持不变，CSS45500→11240同一父子实例组在YMCC正常退出后仍保留。个人7/8和Footer Default保留；默认新预设仍8/8。

用户随后反馈画面仍抽动，视觉验收仍开放；用户随后明确授权先提交并部署候选以便测试。Round候选取消焦点框扩张/20次闪烁；Game Cover Reflections候选固定倒影transform，仅过渡opacity。新版Edge CSS回放5项通过、旧CSS负例被原断言拒绝；本机套用renderConfirmed=true，但不能以回执代替真实Steam/GPU视觉确认。

新增manual native64项（普通/管理员）、只读PS5.1/Core各24项、service33项和stable-focus独立浏览器门。旧idle杀进程策略入口转接常驻合同。证据：G:/YeManCC-Work/Build/Tasks/CssLive-20261023-1455；全局门注册不代表其他任务的完整联合父门已通过。

<!-- STEAM-APPEARANCE-CONTENT-VERSION-20261023 -->

render4内容标记/重复注入和一次定向纠偏已部署并本机前后验证；普通/管理员同令牌路线沿用当前主线。每助手只缓存一份manifest，无新增扫描timer；PS5.1/PS7有界整数兼容。主题资源改动须重建内容标记并通过导出门。详见docs/steam-appearance-content-recovery-20261023.md；视觉抽动全消除与完整正式发布尚未验收，目标ACTIVE。

## 2026-10-23 普通主线导出默认不运行测试夹具（用户最新要求）

用户明确要求：以后普通导出默认不测试夹具，只有用户明确要求测试时才运行。主线快速测试导出只执行必要编译、打包、HC精简逐文件大小/SHA256与成品配套、配置保留检查；不要为普通导出自动展开Decky/CSS/生命周期/升级循环夹具。使用显式TestExport测试模式并清楚标记完整正式发布门未验收，不得将跳过的夹具伪装为PASS；保留已有实际失败。唯一主线、固定快照、完整包与默认位置部署规则继续适用。



## 夹具成本与新增边界（2026-10-23 用户新要求）

当前旧模拟器扩建/自动整轮重跑已暂停，状态在 `G:/YeManCC-Work/Testing/YMCC/State`。普通导出默认不跑夹具，完整验收独立显式要求；日常按模块最小入口及源码/依赖/工具身份变化选择。不得无限增加父门或复制轮次实现；复用原测试，保留有效断言、负例及FAIL/UNKNOWN。缓存、共享和跳过不能算本轮实际通过。新增或修改夹具先读主线 `tools/ymcc_sim/FIXTURE-POLICY.md` 与 `tools/ymcc_sim/AGENTS.md`，入口为核心层 `Run.ps1 -Optimized`。本要求覆盖旧文档中自动触发整轮测试的默认安排；产品/安装/实机授权仍分别核对。

## 2026-10-23 普通导出工具默认合同
普通导出直接使用 tools/publish.ps1 -WorkspaceRoot <独立工作区>，默认不运行测试夹具；旧 -TestExport 参数仍兼容。tools/build-workspace.ps1 和 tools/package-release.ps1 直接调用时也默认不跑夹具。只有用户明确要求完整验收时使用 -FullAcceptance；发布工作流显式选择完整验收。普通包标记 fixtureValidation=NOT_RUN、formalReleaseAccepted=false；不能把跳过的门写成通过。

<!-- FXSOUND-SILENT-RULE-20261024 -->
## FxSound 的新增静默规则（2026-10-24 用户要求）

Decky/Xbox 全屏音质开关全程后台，禁止通过 UIA、菜单、模拟鼠标/键盘或前台窗口/焦点操作控制 FxSound。无直接 API 时允许后台正常退出后，只改预设选择、音效开关和必要静默配置再系统启动。独立“移动端通用音频优化.fac”存在不动、缺失才补，内部参数不管理；随包携带本机备用文件。退出复用原 Settings Recovery 并验证真实输出；不能用杀进程代替正常退出。详见 docs/fxsound-silent-control.md。

<!-- FXSOUND-NATIVE-WIRE-20261024 -->
FxSound 接线验证必须包含 native/decky_sidebar_broker.h 的真实命令准入；单用内存 client/host 无法发现漏白名单。复用 host-loopback 的显式 --fxsound-native 实机模式，默认不得操作真实音频。现有配置空/损坏/占用与文件不存在分开；ON 失败退出旧实例后也要恢复孤立虚拟默认输出。
