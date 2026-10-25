# 模拟鼠标直接控制与 SteamDeck 准入回归

现场反馈（2026-10-21）：用户在启用 SteamDeck 虚拟手柄后仍能点击模拟鼠标。旧原生入口没有人格检查，前端还有独立 VBS/BAT 翻转和清理路径。

登记场景：`controller.mouse-native`。执行正式 main.cpp 的鼠标状态、明确设置、翻转、后端选择、只关闭 JoyXoff 的生命周期清理、原生快捷键、IPC 回调与通知消费者；JS 桥接通过真实 gameproc.ts 编译执行。系统和组件边界采用内存实现。另以输出目录内的惰性测试 EXE 验证真实 Win32 进程启动、状态确认、同名不同路径隔离和立即退出拒绝；不会启动安装的 JoyXoff、Steam 或真实输入设备。

```powershell
pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests controller.mouse-native -EvidenceDir D:/Codex/mouse-mode-evidence
```

入口 `tools/mouse_mode_native_selftest.mjs` 也接受一个证据目录参数。Checked 与 Optimized 均执行原生产消费者；Checked 另运行删掉 SD 门和恢复提前成功回执的故障版本，必须分别在原问题断言失败。旧失败回执不得覆盖或改签。

`mouseMode.releaseJoyxoff` 是只关闭、不可开启的旧后端清理入口，允许控制目标切换时释放旧 JoyXoff；它不会改变 Steam 配置、切换手柄人格或启动任何鼠标后端。SD 下所有用户鼠标开关与后端选择仍须在实际 IO 前拒绝。

界面定向回归复用现有 Inner-Pages-Import 夹具编译的真实组件和 src/gamepad/engine.ts，通过 tools/mouse_mode_menu_browser_selftest.mjs <界面证据目录> 执行 720P/800P 的 A/B、禁用项排除、合成点击、过期状态与落后界面测试。该入口不替代完整 gamepad_ui_release_gate，也不替代安装版和实物手柄验收。

JoyXoff 进程存活的 verification 为 process-liveness，mappingVerified 保持 false。不得把本场景通过写成真实鼠标映射或桌面/游戏识别命中率已经验收。


## 2026-10-21 自动挡与 A 图标

`controller.mouse-auto` 执行正式 `mouse_mode_policy::Runtime`，覆盖桌面、窗口化/全屏、YMCC 暂停、返回场景、手动优先、SD、未知前台及晚到开启撤回；删除未知前台门的变体必须失败。`controller.mouse-native` 继续执行正式后端消费者、前台开启前后 guard 与失败读回，并明确隔离策略调度边界。

界面专项复用 `ui-navigation/templates`：复制到新的源码外证据目录，将 `acceptance-hook.ts` 追加到该轮 entry.ts，将 verify-production-ui.mjs 复制为该轮 verify-mouse.mjs，沿用现有固定工具和 build/serve，执行后保留 mouse-production-ui-result.json 并归还自建 server。钩子只暴露正式状态消费者和 MonitorHudRenderer，不注入产品源码。手柄导航仍用正式 engine。SD 管理真值优先于旧 Xbox 模拟快照。

完整发布前继续执行 controller.ui-navigation 的 720P/800P 和原精确源码门；专项图标截图不能代替完整门，软件回执不能代替物理设备或安装验收。

## 自动鼠标状态提示（2026-10-21）

监控全局偏好 `mouseStatusEnabled`（鼠标状态）独立于模板外观及通用弹出提示。自动挡的实际状态变化显示右下角「鼠标模式 / 手柄模式」2.5 秒；只显示对应图标和名称，无副文字。YMCC 呼出、退出/最小化及其延迟完成回执静默；首次挂载、重复帧、手动模式和 SD 均不提示，重新打开设置不回放。`mouse_status_hint_selftest.mjs` 已接入 controller.mouse-native，以注入时钟运行正式控制器，覆盖过期、重复、落后序列、pending、设置、失败及销毁；专项实际组件回放同时检查新开关的 A/左右导航、同外观仅设置变更生效和旧大鼠标通知被拒绝，亮度通知保留。

YMCC 往返修订：正式状态消费者保留 `ymccVisitRevision`，随现有 HUD 帧传递；即使监控采样跳过中间 YMCC 帧，返回状态也静默。专项覆盖 1,176 种前台对、挡位、人格、设置、pending 组合，并执行真实 HUD wire admission 保留标记的断言。

## G3E 托盘/开始弹窗现场回归（2026-10-21 20:03 导出）

121 条 mouse-mode-context 中，Explorer PID 5948 的桌面和弹窗 HWND 被旧类名名单分成 desktop/external，导致真实 backend_started/backend_stopped 往返。候选改为完整系统 Explorer 路径、当前会话与进程寿命核验后覆盖其交互窗口，不靠固定类名排除弹层；同名外部 explorer.exe 仍拒绝。只对前台 NULL 过渡保留最多 250ms 已准入状态，不从未知状态开启；真实窗口、手动关闭与 SD 不等待该宽限，持续 NULL 和事件风暴到期后关闭。正式发布事件增加 foregroundImage/foregroundClass，后续普通日志可直接判断新 Shell 窗口。

controller.mouse-auto 增加执行正式分类函数的弹窗与伪装路径负例；恢复旧类名过滤的变体必须在 logged-explorer-popup-remains-desktop 失败。普通应用、浏览器、启动器的扩大桌面范围仍需与游戏身份及前台证据结合；本轮不把“没匹配到游戏”当作任意前台的桌面证明。


## 2026-10-21 Steam 桌面鼠标预设

`controller.steam-desktop-mouse` 执行新增正式 `steam_live.js` 分支与完整 `steam_desktop_mouse_runtime.h`，包括虚拟线已选物理源和已接受虚拟端点的环境叶子、当前账号/slot/serial约束、仅此设备的选择/保存、OFF空布局、100%预设、滑块保存及同类型外接手柄保留。物理与Steam边界为内存；不操作当前掌机或安装版。原 `controller.mouse-native` 继续执行正式后端交接，新增Steam选择前释放旧后端、切回前关闭Steam桌面鼠标、失败不提交选择等断言。SD受管虚拟人格的旧后端禁止门保留。

真实UI场景固定在 `ui-navigation/templates/steamDesktopMouseCases.mjs`，由现有Run-Ui入口执行720/800正式组件与engine；`controller/steam-mouse` 已加入发布门必需状态。该结果不代表真实Steam接受此布局、跨机覆盖或实物鼠标移动已验收。旧 `steamdeck_mouse_native_selftest.ps1` 综合夹具仍含已退役Overlay/旧observer边界，本轮调用保留其过期提取失败，未降低断言或把该旧入口签成通过。

## 2026-10-22 JoyXoff 设置窗口首次显示被隐藏

现场观测：安装版 JoyXoff 进程由 YMCC 启动，SettingsChildWndJoyx 已创建但不可见，GUI线程的WM_NULL探测正常响应，无持续死锁证据。源码启动参数 STARTF_USESHOWWINDOW + SW_HIDE 可在隔离 Win32 GUI 子程序中重现首次 ShowWindow 仍不可见。正式后端启动改为让GUI应用自行管理托盘和设置窗口，保留 CREATE_NO_WINDOW，未改变输入映射或后端准入。

controller.mouse-native 现执行 mouse_backend_settings_show_selftest：正式进程启动消费者创建唯一测试路径下的GUI子程序，使用未展示的独立Desktop，不显示用户桌面、不操作安装JoyXoff。当前首次显示必须可见；恢复旧SW_HIDE启动参数的反例必须在同一显示断言失败。由自建事件通知子程序自然退出，保留输入与同名外部进程隔离检查。源码回归不能写成当前安装版已经恢复；本轮恢复现有设置窗口的实际请求被Windows错误5拒绝，不更改权限或强杀。


## 2026-10-22 普通手柄 Steam 鼠标失败退出

普通模式继续使用 controller_generic 的内置桌面预设，111%识别标记已归一为100%；不借用 SteamDeck 虚拟人格，也不覆盖同类型外接手柄。全局 XInput topology 变化不能单独证明当前设备更换；每次仍重查实际 slot、PnP/root/path、虚拟 Host runId/epoch 和 power，任一实际身份变化继续拒绝。

切换先只读预检 Steam 与目标，失败保留原后端。发送后明确 mutated=false 的拒绝，可在原模式、前台及电源代次未改变时恢复原 JoyXoff/微软鼠标；不确定写入保存释放责任，未确认关闭前不允许另一个注入器自动开启。Steam/runtime/缓存争用立即返回忙碌，不排队卡住鼠标界面；前端后端选择及灵敏度读写都有30秒等待上限，晚到回执不能使旧Promise复活。页面挂载/激活合并未完成读取，失败写入不重放排队数值；错误提示支持B返回，按钮finally释放。超时仅结束前端等待，不代表取消已经提交的Steam写入。

现有 controller.mouse-native 增加正式消费者预检、回退、显式OFF及不确定状态门，删除预检或恢复动作的负例必须在各自原断言失败；真实IPC以受控时钟验证请求超时、晚到响应丢弃和后续请求可用。controller.steam-desktop-mouse 执行正式runtime与Steam transport争用、相同设备的无关枚举变化、真实路径/槽位/电源变化拒绝及100%布局。UI保留720/800全页正式engine，加入连接拒绝、超时、B关闭、切回原方案与首次读取合并。控制器分区导航与内容共用正式pane，避免方向导航被内容区限制住；旧跨分区滑块样本按现有三个分区更新，仍验证可达和返回。

现场ZIP为2026-10-22 17:42:43的G3E导出，其保存输出为SteamDeck虚拟人格、JoyXoff/off，没有这次Steam桌面鼠标错误回执；截图为用户提供的症状。不能把源码复现写成截图现场的唯一根因或真实Steam输入已恢复。本轮不写安装目录，不启停Steam、游戏或控制器。证据位于 D:/Codex/2026-10-21/new-chat-12/work/steam-mouse-error-20261022。


## 2026-10-22 取消普通手柄 Steam 桌面鼠标

用户最终选择取消普通/非SteamDeck手柄套用Steam桌面配置；本节取代上文普通Steam预设/故障修复方案，历史事实与失败证据保留。普通模式仅微软鼠标、JoyXoff，自动/开启/关闭由原有YMCC实时策略控制；SteamDeck仍由Steam管理，现有灵敏度设置保留。

移除steam_desktop_mouse_runtime.h、steam_desktop_mouse_preset.h及主线全部普通导入、slot-pin和转发路径；旧desktopMouse.get/set在真实Steam JS访问编辑器/Input/ControllerStore前返回退役拒绝。mouseMode.setBackend与旧summon.set不能重新保存steam。summonLoad把历史steam选择迁移为joyxoff/off，其他开关保持，重复读取不重复迁移，不在迁移时调用输入或Steam。

controller.mouse-native执行正式summonLoad和summon.set，覆盖旧ON/AUTO/OFF迁移、既有后端保留、旧选择拒绝；删除迁移关闭动作必须在指定断言失败。保留原Win32后端启停、SteamDeck禁止门和后台策略回归。controller.steam-desktop-mouse沿用ID改为退役能力的零IO回归，不再把删除的功能测试签成可用。

完整UI仍执行720/800正式组件与engine，controller/standard-mouse必须只出现两个后端且不查询Steam灵敏度；旧配置回执不能挂载普通Steam滑块；controller/steam-mouse改为仅SteamDeck受管状态和100%滑块，A/方向调节保留；旧可控制Steam回执不能使游戏菜单开放鼠标控制。超时B关闭及普通后端切换仍须通过。所有测试只替代硬件/系统边界，不操作安装版、真实Steam/游戏/手柄。

证据：D:/Codex/2026-10-21/new-chat-12/work/retire-standard-steam-mouse-20261022。安装版尚未替换，实机验收另记。
