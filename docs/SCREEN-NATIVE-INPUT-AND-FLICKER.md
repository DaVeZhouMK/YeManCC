# 屏幕触摸板：原生输入分流与视觉保留层

2026-10-16 当前状态补注：本文2026-10-11章节中的“默认关闭／双重opt-in”描述为当时历史状态。当前native/build_native.bat默认编入独立Touch/Pen source，正常owner初始化默认请求该route，显式legacy/conflict可关闭。18:47微星归档的导出状态报告nativeMousePassCompiled/Requested/Active均true、runtimeError0；仅为导出时点，不是故障同段或闪烁验收。当前证据见[原生入口隔离测试R36](screen-touchpad-native-ingress-simulation-r36.md)。

## 完整目标与当前结果

原目标仍是：借鉴监控窗口，实现真实跨进程鼠标穿透，同时触摸板继续消费触摸，且不把触摸送给下层游戏。附加任务是检查原生能力与 `C:\SOFT\VirtualGamepad\Beta` 是否有能改善闪烁的做法。不能把绘制透明、仅同线程透明、点击转发或单次碰巧通过当作目标完成。

**完整鼠标穿透目标未完成。** 当前安装版未被替换、未开启 UIAccess、未创建虚拟设备、未执行 Beta 的 EXE/DLL。

## 输入实测：不要误报原生 API 已解决整窗穿透

`tools/screen_touchpads_mouse_pass_acceptance.cpp` / `tools/verify-screen-touchpads-mouse-pass.ps1` 使用实际 `SendInput` / `InjectTouchInput`，下层窗口属于独立的自有进程。每种变体创建全新的生产输入窗口，避免切换窗口样式留下的缓存影响结论；校验正常和鼠标/触摸未泵消息的重叠输入两组情况。

- 正常生产输入窗口：接到触摸，下层接不到鼠标。
- 监控式 `WS_EX_LAYERED | WS_EX_TRANSPARENT`：下层接到鼠标和滚轮，但也接到触摸；不满足目标。
- 完全零 Alpha 输入图层：同样让触摸一起漏到下层。
- `CoreIndependentInputSourceController`（受控 Touch/Pen + `IsTransparentForUncontrolledInput`）：确实能通过独立输入源接到触摸，但现有 Win32 顶层输入 HWND 仍挡住跨进程鼠标。
- 控制器叠加监控式窗口样式：鼠标通过，但独立输入源也收不到触摸。
- 单纯 HTTRANSPARENT：不能用作跨进程鼠标穿透的证明。

当前 16 个样本的正反对照有效，全部不满足完整双向要求。这个脚本默认是**失败闭合的功能门**：`-DiagnoseOnly` 仅允许诊断运行成功，并在结果中明确 `featureAcceptancePassed=false`；默认运行不能把这个状态判为功能 PASS。

另保留了仅在自有辅助进程安装、不吞输入的临时鼠标观察钩子实验：顺序输入曾出现“鼠标通过且触摸捕获”，但未泵消息的重叠输入 12/12 漏触摸。该方案已否决，未接入产品，不采用按鼠标事件反复切换透明样式的竞态方案。

## Beta 静态读取

只读取 PE、.NET reflection-only 元数据与 IL，未运行、加载执行或修改安装目录；未复制其控件资源或代码进入产品。

- `VirtualGamepad.exe`：SHA256 `DFB8F0166107E03650F913F788E358BA600983C11DAA43DDD16F793E6E0443A3`。
- `VirtualGamepad.dll`：SHA256 `1228EB09B4A87CF472FCC2A994558BEB51A5DA754E1A0F91446F34444DD8F4FE`。
- 32 位 .NET / WinForms，文件版本 1.6.0.0。
- `frmAbstractTouch.HandleShown` 中的 `SetStyle(139266, true)` 对应 `UserPaint | AllPaintingInWmPaint | OptimizedDoubleBuffer`；还有 `WS_EX_NOACTIVATE`，这部分我方已有等价处理。
- `timerRefresh_Tick` 只在 `RefreshLayer` 为 true 时 `Refresh()`，然后清除该标记；值得借鉴的是“内容没变就保留上一帧”，而不是照搬其常驻计时器。
- WndProc 处理 WM_POINTER / GetPointerInfo；读取证据不能据此证明它已解决我们的跨进程鼠标/触摸双向要求。

元数据与 IL 证据在 `G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\beta-audit`。

## 已实现的原生视觉改进

本批只对生产视觉路径做了一个可验证改进：保留 Windows 已上传的分层位图，不为相同画面重新分配 GDI 或再次 UpdateLayeredWindow。

- 触摸板轮廓缓存按尺寸/透明度失效；按钮还按图标档位失效，独立 Steam→PS 切换不会沿用旧图标。
- 仅坐标变化时移动已有图层，不先清空画面或替换 bitmap。
- 缩放、透明度、图标变化按需上传完整帧；显示/DPI 变化显式强制重传，不能被普通缓存吞掉。
- 避免 owner 的同一次配置切换再次调用重复 refresh。
- 不新增 hook、渲染定时器、工作线程、输入替代、功能 EXE 或交付 DLL。
- 保留原有输入许可、按键释放、100% 透明时触摸可达、鼠标/原生触摸输出链路。

## 证据与边界

`tools/verify-screen-touchpads-visual-cache.ps1` 包含 20 个检查；旧源同样内容 64 次 × 两块触摸板产生 128 次重新上传，当前为 0。验证缓存失效、位置移动、强制重建、图标切换、透明度不取消现有触点、缩放仍取消触点，以及 GDI 生命周期。

真实桌面合成像素也已检查：辅助进程自己的白色背景窗口内能看到蓝色轮廓，中心仍透明；128 次保留层调用前后像素一致。仅裁剪自有窗口区域，不保存用户全屏截图。

**这证明消除了重复上传并保留了实际合成帧，不等于所有真实触屏闪烁已彻底解决。** 触摸与系统/软件光标交替显示、游戏全屏及真实掌机动态画面仍要独立验收。原鼠标穿透目标继续保持未完成，不能借本项改进缩小原目标或提前标记完成。

## 可复现命令

```powershell
.\tools\verify-screen-touchpads-mouse-pass.ps1 -DiagnoseOnly
.\tools\verify-screen-touchpads-visual-cache.ps1
.\tools\verify-screen-touchpads-standalone-special.ps1
.\tools\verify-screen-touchpads-cursor.ps1
```

主 `verify-screen-touchpads.ps1` 已接入视觉保留层回归。功能鼠标穿透门应在真正满足双向要求并接入生产路径后再放行，不能改为仅看 API 成功或 diagnosticValid。

## 2026-10-11：自有子窗口原生输入候选（仍不是生产实现）

新增 `tools/screen_touchpads_native_topology_probe.cpp`、共享测试宿主、`tools/screen_touchpads_native_topology_stress.cpp` 和 `tools/verify-screen-touchpads-native-topology.ps1`。与原 16 样本的失败矩阵分开保存，不能覆盖旧结果或把诊断成功当成功能通过。

### 已证实的候选路径

父输入 HWND 保留完整区域，使用 **全零 per-pixel Alpha**，但不加 `WS_EX_TRANSPARENT`；其自有、非分层子 HWND 承载 Composition visual 和受控 Touch/Pen 独立输入源。视觉轮廓仍走原来的独立绘制窗口。不能把这些子窗口挂到游戏/其它应用，不能把鼠标点击转发到下面。

- 新鲜 HWND 的 11 个拓扑 × 顺序/重叠两组，共 22 样本，诊断对照有效。只有上述零 Alpha 父窗口 + 自有受控子窗口的两个初始样本同时满足点击/滚轮到下层进程、触摸留在上层。
- 空区域、小角区域、空心区域会把触摸也放走；透明分层父窗口（`WS_EX_TRANSPARENT`）下的子窗口也没有救回触摸。
- 独立候选动态压测 72 样本通过：实际跨进程点击、移动、按住拖拽、滚轮，单指/双指、笔、两个生命周期/设备、三种位置/尺寸状态；框外触摸仍交给下面的自有目标。
- 同步调用现有生产 `windowProc` 的隔离核心适配实验，动态 72 样本也通过，包含实际坐标匹配、Mouse/Single DualSense 接收与释放、UI 线程回调、不向下面泄漏原始 touch/pen。只模拟能力上下文，不启动虚拟控制器或 InputHost；触摸板自身键鼠输出用 dryRun 抑制。
- 核心适配实验使用正常 Win32 泵；不额外泵 CoreDispatcher，不需要实验中的手势禁用属性。独立输入源不是一个常驻鼠标观察 hook。
- 鼠标注入点经过自有窗口身份核验；目标前台属于自有目标进程，滚轮还校验目标前台，避免滚动其它用户应用。所有下层 helper 已正常退出。

### 未解决/不能放行的部分

单指同坐标帧保持 500 ms、取消、按住关闭和重新开启通过；Single DualSense 两指静止保持/按住关闭**未验收通过**。同一注入方案在原正常 Win32 输入窗口的额外双指静止正对照里，也没有在 UP 之前持续看到两指按住，但 UP 后能看到 DOWN/UPDATE/UP。这与既有 pointer 自测对同 HWND 微小合成多指更新被合并的记录一致，不能将它直接归因于新拓扑，亦不能拿动态滑动成功代替静止验收。

早期试验中多指 ID 超界（后改为与生产相同的 0/1）、固定过短等待，以及静止 DOWN 后没有后续报告帧，造成过无效正对照。这些失败报告已保留。最终脚本继续要求正常 Win32/独立源的输入正对照，发现无效正对照就失败；双指静止额外正对照单独记录，不会悄悄删除候选的静止失败项。

**最终完整核心候选仍为 false，生产功能门仍为 `featureAcceptancePassed=false`。** 尚未把该输入拓扑接入生产初始化/刷新/关闭入口，未部署安装版。下一步需用有效的双指静止输入（包括真实触屏对照）验证，并完成生产的档位/许可切换、DPI/显示切换、捕获丢失及绘制/光标回归，然后才可能放行。无虚拟手柄的 SD/PS5 触控板映射限制不改，功能键独立动作不改。

### 本轮保留层与功能键复核

- 当前视觉缓存重新验证：20 项、重复上传 0、自有窗口合成像素保留通过；不等于真实触屏/全屏游戏闪烁彻底解决。
- 独立功能键/核心重新验证：184 项通过，Steam 脚本嵌入检查通过；不是实际 Steam/麦克风消费行为的验收。
- Beta EXE/DLL 哈希仍与静态审计一致，未执行或修改 Beta。
- 共享 `native/main.cpp` 有其它并行工作更新；旧 immutable snapshot 仍只代表旧的固定源码，不代表最新 main 已构建。未覆盖其它改动。

本轮证据：`G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\native-topology`，复现：

```powershell
.\tools\verify-screen-touchpads-native-topology.ps1 -DiagnoseOnly
```

默认不带 `-DiagnoseOnly` 仍失败闭合；诊断中 72+72 动态样本通过，也不会改为生产功能 PASS。
## 2026-10-11 接续：候选组件与生产生命周期接线

本轮不是仅重复拓扑实验：新增 `native/screen_touchpad_input_surface.h`，并把受控 Touch/Pen 源的归一化事件接到 `screen_touchpads.h` 中共用的 `handlePointer` / `handleCaptureLoss`。原 WM_POINTER 入口复用同一许可/Persona/Core/输出处理；原 BUS 和 InputHost 所有权不改，没有伪造窗口消息或重放鼠标。

- 组件只接受本进程、当前 UI 线程所属 HWND；自己的子 HWND 承载 Composition 输入，禁止其它应用 HWND。STA/DispatcherQueue 懒初始化，事件驱动，无常驻 hook、计时器或工作线程。
- 生产 refresh 的几何同步、force 重建、Single/Dual 销毁、release/cancel、shutdown 已有候选分支接线。关闭撤销事件订阅并关闭 controller/target，销毁子窗口，恢复原 HWND 样式；部分初始化失败也清理。
- 缓存全零输入帧，只在尺寸变化/force 时重传；位置变化复用。已测试重复同步不上传和 force 强制上传。
- 编译开关 `YMCC_SCREEN_NATIVE_INPUT_CANDIDATE` 和运行时请求开关都默认关闭。普通产品不会自动切到新输入路径；未部署安装版。状态中的 `nativeMousePassCompiled/Requested/Active` 只报告实际候选情况，不是功能验收 PASS。

### 当前组件证据（自有下层进程、实际 Windows 输入、dryRun 输出）

- 单面板动态 72/72：真实跨进程点击/滚轮/移动/拖拽 + 单指/双指/Pen，坐标 72/72，UI 线程回调和无重复交付 72/72；输入帧缓存/force 四组通过，关闭四组通过。
- 双面板 24/24：Deck/Deck、WASD/Mouse、Arrows/Mouse、DualSense/DualSense，包含真实移动后保持 500 ms、透明度只改不释放、取消、单侧 capture loss、关闭/恢复。普通 Win32 移动正对照有效。早期小位移/静止对照无效的失败报告保留，没有把它们删掉冒充全面 PASS。
- 五种生产模式映射/许可切换通过：输出仍由原核心产生（测试中 dryRun，不操作用户游戏或虚拟目标），persona 改变通过真实 configure 路径释放，旧 UP 不重新发键或泄漏触摸。
- 所有权/线程/恢复 12 项通过：拒绝其它进程 HWND，拒绝 MTA，拒绝非 UI 线程 sync/close，UI 线程可恢复，禁用恢复样式/销毁子窗口，可重挂，shutdown 无残留。
- 全主 EXE 的固定源码候选已编译/链接（候选开关编译存在但默认未启用）。原核心 97、原真实 WM_POINTER 427、wire 自测通过；视觉缓存 20、独立功能键/核心 184 + Steam 动作脚本 22、光标模型 104 复核通过。主 EXE 在 RunAsInvoker 下只运行原早返回、无设备自测，未修改产品 manifest。

### 静止双指仍未验收完成

额外专用对照不移动坐标，分别试了相同报告、压力、接触面积、方向、PerformanceCount，以及先后落下两指；旧 Win32 和新组件共 14 个样本，API 调用/释放成功，但没有 UP 前持续的双指状态，故全部静止验收 false。它们不能用来否定新组件，也不能证明静止双指正确；必须获得有效的同范围输入正对照，包括真实触屏。

单指静止/取消/关闭恢复成立；双面板已经移动建立触点后的保持成立。**这些都不能替代“直接从静止开始的双指按住”**。完整组件候选仍 `candidateCompletePassed=false`，默认验证明确拒绝 `NATIVE_COMPONENT_NOT_ACCEPTED`，生产 `featureAcceptancePassed=false`。

候选还需有效静止双指验收、实际生产默认入口/UI 启用决策、真实显示/DPI/全屏/光标场景回归。按钮/功能键现有输入路由未换成该候选；无虚拟手柄 SD/PS5 触控板限制不改。

复现（隔离组件验证，不运行安装版）：

```powershell
.\tools\verify-screen-touchpads-native-component.ps1 -DiagnoseOnly
```

证据：`G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\native-component`。完整候选主 EXE / 固定源码清单位于该目录的 `candidate-snapshot`；早先 `ScreenMousePass-Goal\candidate-snapshot` 保留不覆盖，仍是旧版本证据。
## 2026-10-11 接续：静止输入选项诊断与人工硬件对照入口

本轮生产原生 header/main 没有再改；没有启用或部署候选。新增的是两组隔离选项探针、人工输入工具及来源检查。不能把探针 API 调用成功或 helper 编译成功当成实际硬件验收。

### 新的静止选项证据

- `RegisterTouchWindow` 的 0 / `TWF_FINETOUCH` / `TWF_WANTPALM` / 两者组合，在旧 Win32 和候选子窗口各测一遍，共 8 个样本。注册与注入/释放成功，但持续双指样本仍为 0，没有 WM_TOUCH 兼容消息补救；生产未添加触摸窗口注册。
- 独立源默认、关闭掌心拒绝、Touch-only、提前 SetPointerCapture 共 4 个样本。当前机器读回默认 `IsPalmRejectionEnabled=false`；关闭它不产生新行为，提前捕获成功也没有让静止双指持续成立。鼠标/滚轮仍到自有下层，触摸 DOWN/UP 未泄漏；静止联合验收全部 false。生产未添加预捕获或更改掌心策略。
- 两组证据和源码 hash pin 位于 `ScreenMousePass-Goal\native-stationary`，入口 `verify-screen-touchpads-native-stationary-options.ps1 -DiagnoseOnly`。默认仍拒绝把这个诊断当成静止功能验收。

### 合成输入来源不能冒充硬件

`screen_touchpads_native_component_acceptance.cpp` 现在记录回调中的 `GetCurrentInputMessageSource`。本轮 24 个来源样本全部 `originId=IMO_INJECTED`（Touch/Pen），增加了不误标硬件的断言。完整组件重新跑过：动态 72/72、坐标/无重复 72/72、双面板 24、映射 5、所有权 12、缓存/关闭各 4 仍通过；直接静止的 14 个样本仍未通过。它们不是硬件证明。

### 人工对照工具（交互路径尚未执行）

新增 `tools/screen_touchpads_native_physical_acceptance.cpp` / `tools/verify-screen-touchpads-native-physical.ps1`。默认仅构建；已经编译并核对 SHA256，但未执行人工输入路径。单独检查了未传 `--manual` 时退出 code 4、不创建窗口/报告的入口保护。

人在真实触屏和真实鼠标前准备好后，才运行：

```powershell
& 'G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\verify-screen-touchpads-native-physical.ps1' -Manual
```

工具特点和验收边界：

1. 同一自有、独立下层进程，先原 Win32、后候选，共约 20 秒；窗口内有阶段/倒计时说明，不启动游戏或安装版。
2. 每阶段先在蓝框内保持两指静止 1 秒，再保持手指按住，用真实鼠标在蓝框内点击、拖动、滚轮。静止容差 4 屏幕像素；下层联合鼠标计数只在 UI 观察到两指持续静止至少 800 ms 且鼠标消息位置在蓝框矩形内时累积，不能在框外点击冒充穿透。
3. 不调用输入注入；触摸板输出 dryRun，禁用所有临时功能键/背键/呼出图标，无虚拟目标、hook、设备/配置写入。下层 HWND 和坐标属于 helper，所有消息记录只在自有区域。
4. 阶段切换要求实际松手；未松手/提前退出/来源不足/触摸 DOWN、UPDATE、UP 泄漏均不能通过。原 Win32 对照的鼠标可能被上层挡住，硬件鼠标来源允许从其上层真实窗口记录，不能错误要求原对照已经穿透。
5. 记录输入来源标志并要求人工阶段的触摸/鼠标来源有效。`IMO_HARDWARE` 是 Windows 的来源标签，不是防伪鉴权（官方定义也允许 UIAccess 注入归入该标签）；必须由人在场实际操作，不能把另一个注入器当成真人。
6. 即使人工对照通过，它只补足该范围的硬件证据，完整生产 `featureAcceptancePassed` 仍保持 false，直到真实生产启用与其余完整回归成立。

`physical-touch-verification.json` 当前为 `compiled=true, manualRunExecuted=false, hardwareAcceptancePassed=false`。没有人在场输入就不能继续用合成成功推导硬件成功；下一步需要用户配合人工对照。整个原目标继续未完成，默认候选仍关闭。
## 2026-10-11 接续：真实映射输出、按钮全链路、冷启动/重建缺口修复

本轮继续推进原目标，不仅重跑旧诊断：生产候选现在也覆盖所有可见功能键/背键/呼出键输入 HWND，同时修复两个由新冷启动验证暴露的生命周期问题。候选编译/运行时开关仍默认关闭，安装版未替换。

### 真实键鼠输出不是“转发鼠标穿透”

新增 `screen_touchpads_native_real_output_acceptance.cpp`：不用 dryRun 代替这项证据，实际调用现有 Core/output 的 SendInput，把映射键鼠送到单独的、身份核验过的自有前台进程。

- WASD / 方向键 / Mouse，各测正常 UP、取消、禁用、销毁，共 12 组；自有接收进程实际收到预期按下/释放、位移，未留下按键或触点、未泄漏原始触摸。
- 相同位置的重复更新不重复键 DOWN；每个已按键只有一次 UP。原触摸轻点 → 鼠标点击映射单独验证一次 DOWN/UP。
- 这项明确属于保留现有“触摸映射输出”，不能把其 SendInput 点击当成真实鼠标穿透的证据；真实鼠标穿透仍用独立的鼠标输入样本验证。
- 只在自有前台/自有坐标许可下送新输入，开始前拒绝已按住的相关键/修饰键，触摸回调再查前台与 Shift/Ctrl/Alt/Win；丢失许可只做补偿释放，不再发送新 DOWN/点击。UIAccess token 只读核验为 false，未修改权限。

### 功能键也接到同一原生组件

`screen_button_overlay.h` 抽出共用的指针/捕获丢失处理，候选路径增加七个自有 Surface，接到原 `buttonEvent` / BUS mask / Y owner 消息 / standaloneDispatch。生产按钮 refresh/cancel/shutdown 同步候选，禁用候选恢复原样式。`nativeMousePassActive` 现在同时核对可见按钮，不能只有触摸板通过就误标全浮层 active。

88 个按钮样本通过：SteamDeck、DualSense Edge、Elite、无虚拟手柄的独立 Steam/PS5，所有许可按钮 × 正常 UP/取消/关闭选择/销毁。实际跨进程鼠标点击和滚轮通过，Touch DOWN/UPDATE/UP 不泄漏，无重复交付；虚拟 Persona mask 不改，无虚拟 Persona 仍只提交独立动作；Y 到自有消息接收者恰好一次。Standalone submit 在此是隔离假接收器，**没有打开真实 Steam 菜单/静音真实麦克风**，不冒充这些动作的消费验收。

前一版 40 ms 固定短观察出现部分未交付样本，保留失败报告；最终测试等待实际按下状态且有界，所有 88 项均实际交付。不是把未收到 DOWN 的样本忽略或作为成功。

### 新找到并修复的生命周期缺口

1. `shutdown` 保留配置，`configure` 对相同配置不触发 refresh；旧 initialize 因此在相同配置重建时没有 HWND。initialize 现在只在 configure 没有 refresh 时自行 refresh，既确保重建，也不造成同一次重置的重复刷新。
2. 原候选立即注入首笔触摸时，0 ms 样本丢失，而 16/100 ms 对照成立；原 WinRT `IsInputEnabled` 已是 true，没有 InputEnabled 事件，所以不能把该属性当作真正就绪证据。
3. 显式处理独立通道及显式 commit 两个对照均成立。最终在 Surface 同步中、透明输入帧上传前，调用独立源 dispatcher 的 `ProcessAllIfPresent`，处理其排队的注册/几何工作；不加固定 Sleep、不加常驻轮询/计时器/worker，不替换为普通 Win32 窗口消息泵。
4. 通道处理可能交付回调，代码随后核验 generation/HWND/当前尺寸，已关闭/替换或尺寸已改变的请求以 E_ABORT 失败闭合，不能用旧尺寸继续上传或复活旧窗口。

新 `screen_touchpads_native_cold_acceptance.cpp` 的 24 项通过：触摸板/按钮 × 0/16/100 ms × 四次新窗口生命周期；包含不手动额外 refresh 的相同配置 reinitialize、首笔 DOWN/UPDATE/UP、真实下层点击/滚轮与无原始触摸泄漏。零等待不是“改成一帧后才测试”。失败及 IsInputEnabled 不足、commit/dispatcher 独立对照报告都保留。

### 当前验证与仍未完成的范围

- 最终按钮/真实输出/冷启动 verifier 默认运行已执行并拒绝完整功能放行，`featureAcceptancePassed=false`；只有 `-DiagnoseOnly` 允许这些局部证据成功。
- 组件动态 72、双面板 24、映射 5、所有权 12、来源 24 回归通过，直接静止双指 14 个合成样本仍不足。旧功能键/核心 184 + Steam 动作脚本 22、视觉缓存 20、光标模型 104 回归通过。
- 完整主 EXE 固定源码构建在 `native-button-input\candidate-snapshot-v2`；此前 `candidate-snapshot` 保留为冷启动修复前证据，不覆盖或冒称最新。源/EXE pin 与原核心/wire/WM_POINTER 自测分别记录。
- 人工工具已按新 header 重新构建，仍没有执行 `-Manual` 路径。不得把自动目标续跑当作人已在屏幕前操作的确认。
- 仍需真实静止双指 + 鼠标对照、实际产品默认入口启用/同会话回归、真实显示/DPI/全屏/光标场景。没有改无虚拟手柄 SD/PS5 触控板映射限制，也没有以真实输出或首笔触摸通过替代整个目标。

复现：

```powershell
.\tools\verify-screen-touchpads-native-button-input.ps1 -DiagnoseOnly
```

证据根：`G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\native-button-input`。
## 2026-10-11 接续：真实 App owner、标准构建选项与恢复接线

本轮查到了候选只能由测试请求的实际入口缺口：之前正式主程序从不调用 `setNativeInputCandidate(true)`。现在新增 `native/screen_touchpad_owner.h`，正式 main 的正常初始化改为同一个 owner 适配器；正常构建和正常启动都仍默认旧路径，不部署安装版。

### 精确双重 opt-in，不改持久配置/Persona

- 标准 `native/build_native.bat` 只在显式 `YEMAN_SCREEN_NATIVE_INPUT_CANDIDATE=1` 时给 main 加候选宏；未设/0 保留旧构建，其它值在编译前拒绝。
- 已含候选的构建只在完整 argv 参数 `--screen-touchpads-native-input-candidate` 请求时使用新输入；路径名含此字符串、前缀、后缀、拼接参数不激活。
- 明确 `--screen-touchpads-native-input-legacy` 或两参数冲突时保持旧路径；不能改变默认 SD/PS5 虚拟目标能力、配置档位、BUS/InputHost 所有者或功能键动作。
- owner 必须是当前 UI 线程/当前进程所属 HWND，非法 owner 失败闭合。这个 opt-in 是候选验证入口，不代表适合在现有安装实例旁直接启动第二个正常产品实例。

### 真实主程序电源/恢复入口

原 PBT suspend 的 release 接到 owner epoch 失效 + release，仍不做设备 IO。resume 只在候选已请求时向原 owner 的 `WM_APP+176` 消息排队，重复通知合并。owner 消费时释放旧触点、关闭受控源/子窗口，再强制 refresh 重建；重复、旧 epoch、关闭后的消息拒绝。

正式 main 的 suspend/resume/rebind/startup 和三个 shutdown 出口都接到这些入口。没有改变 Sleep Guard 的用户/自动唤醒判定、设备写入门、暂停 lease、InputHost 电源或驱动策略；恢复广播回调不阻塞等待初始化或硬件 IO。

自有 owner 检查候选和 legacy 两种构建各 34 项通过：精确参数、权限/线程所属、默认和冲突回退、恢复消息合并/延后、旧消息拒绝、跨恢复重建的旧触摸 UPDATE/UP 不复活或泄漏、配置不变、同配置重启、DPI/display 消息后强制画面刷新，并在恢复/DPI/display 后重新验证实际 Windows 触摸与不同进程鼠标路由。这里的显示/电源消息是自有 owner 控制试验，不是让用户机器真实睡眠/改变分辨率，也不是全屏游戏验收。

### 标准配方与实际主 EXE 证据

不是临时拼出的链接命令：固定 100 个 native/dependency/bat/def 源文件，分别通过真实 `build_native.bat` 的默认和 opt-in 分支构建。主 EXE、原 recovery service、原 RTSS helper/bridge 均编译成功，未运行服务或加载 bridge。

增加主 EXE 的早返回 `--screen-touchpads-owner-selftest` 精确参数入口，在设置/会话/WebView/硬件初始化之前返回。实际两种主 EXE 共 14 次测试：每种 default / opt-in / conflict / explicit-legacy 的 owner 启动/重启，以及原 core / wire / WM_POINTER 自测。owner 自测每次 10 项，含真实 pad 和 standalone 按钮 Surface 的 active 状态，配置写入/设备操作/输出注入为 0。只有 opt-in 构建 + opt-in 请求显示 active；legacy 构建即使收到候选参数也不假报 active。

原真实 WM_POINTER 427 项在两种主 EXE 通过，原核心/wire 通过。通过嵌入 manifest 读回确认仍 requireAdministrator/uiAccess=false；只对原早返回自测用 RunAsInvoker，不改 manifest/UAC/UIAccess/用户安装文件。

固定最终源码：`ScreenMousePass-Goal\native-owner\candidate-snapshot-v2`；标准构建输出：`standard-build-v2-candidate` / `standard-build-v2-legacy`，与旧快照分开，不覆盖历史失败/通过证据。

### 防闪烁范围进一步核对

视觉 verifier 新增可选 `-NativeCandidate`，先确认候选 pads/按钮输入源确实 active，再验证相同画面保留、位置/尺寸/force、透明度和自有区域实际合成像素。候选路径 23 项通过（重复上传 0、图层中心透明、重复 128 次合成像素一致、GDI 无增长），旧路径 20 项通过。这仍不是实际手指/全屏游戏动态闪烁验收。

```powershell
.\tools\verify-screen-touchpads-owner-lifecycle.ps1 -DiagnoseOnly
.\tools\verify-screen-touchpads-owner-app.ps1
.\tools\verify-screen-touchpads-visual-cache.ps1 -NativeCandidate
```

`owner-app` verifier 只消费指定固定构建的早返回自测，不启动正常产品，`featureAcceptancePassed` 一直 false。owner lifecycle 默认 verifier 也实际执行并拒绝当作完整功能 PASS。

### 下一步依赖（目标未完成）

人工静止双指 + 框内鼠标对照需要用户在触屏前配合，已经多轮询问但没有收到准备确认。人工工具重新构建，`manualRunExecuted=false`；没有以自动 goal 续跑当作手指/鼠标已在场，也没有启动无人值守交互测试。

整个目标仍缺真实静止硬件对照、正常产品同会话启用/实际输入消费及全屏/显示/光标真实场景验收。完成这些之前不部署、不默认为新路由，不能用 34/14/23 个局部检查缩小或宣布整个目标完成。当前独立可执行接线/构建/来源/局部链路工作已完成，下一步需要人机配合而不是继续重复合成对照。
**状态审计：blocked，非完成。** 真实人工静止双指/鼠标对照的同一外部依赖已经连续多轮存在，现已核对没有测试进程仍活着、没有人工硬件报告，也没有新的独立必要接线工作可用来替代它；目标正式标记为等待用户配合。完整范围和失败闭合功能门保持不变。准备好后显式确认，再执行已构建的人工对照；不自动启动人工窗口或切换安装版。