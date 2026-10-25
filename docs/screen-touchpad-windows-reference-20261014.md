# Steam Deck Tools / Handheld Companion 的 Windows 触摸板链路对照

2026-10-14，接续 `screen-touchpad-deck-flicker-20261014.md`。仅阅读源码与联网资料，没有运行两套软件、测试、构建、真实输入或部署；“具备触摸板功能”不等于已证明解决相同屏幕触摸闪烁。

## 固定来源

- Steam Deck Tools：`ayufan/steam-deck-tools`，抓取 `main` 后固定提交 `deee7fa887c2864ee3b54eba2c32b2f58b547349`。
- Handheld Companion：`Valkirie/HandheldCompanion`，固定提交 `a250b19a4b7e29dac3b383f2d1ac517d1af43e4e`。
- YMCC 内置 HC 源基线仍为 `deps/handheldcompanion-runtime`，声明 `HC-BINARY-BATCH12-20260831` / `0.32.3.2`；没有以本轮联网版本覆盖它。核对的四个相关文件与联网版本有差异，详见 `Sources/hc-local-baseline-comparison.json`；差异既包括设置生命周期，也包括鼠标动作和报告头，不将两者混称同一版本。
- 全部读取文件、提交树、原始 URL、SHA256 存在研究目录的 `Sources/SDT`、`Sources/HC` 与 `windows-upstreams-receipt*.json`。

## 关键链路与结论

| 路径 | 上游实际实现 | 对本问题的意义 |
| --- | --- | --- |
| SDT 实体 Deck → 桌面鼠标 | HID 读实体 28DE:1205；按下触摸位时把上一坐标设为当前坐标；随后用相邻坐标差输出鼠标 | 避免重新落指跳跃。实体 HID 触摸本身不是 Windows 屏幕触摸事件，不能据此证明屏幕触摸光标显隐已解决 |
| SDT 位移提交 | `MouseController.Accum` 累积小数部分；统一 Update 后仅在非零整数位移时 MoveMouseBy | 减少零位移调用、保持慢滑精度；YMCC 已有 remainderX/Y 和非零 SendInput 门限 |
| SDT Steam Input 模式 | `SteamProfile.Run` 把 LizardButtons / LizardMouse 设为 false；Lizard 控制以 HID feature 报告执行 | 避免实体设备固件的鼠标映射与 Steam Input 重复处理；不能向虚拟设备盲发同样命令并假设其实现了实体固件行为 |
| HC 实体 Deck → 鼠标 | `NeptuneController` 根据 touch 位保留 XY，未触摸时清零；`MouseActions` 首帧只建立基准，随后算 delta，保留小数 | 与 SDT 类似。明确区分“零坐标是板中心”和“未触摸”；YMCC 的接触有效位与坐标已经分开 |
| HC 平滑 | `MouseActions.Filtering=false`，开启后才使用 OneEuroFilterPair | 是位移噪声滤波，不是 Windows 光标显示权处理。本轮不叠加滤波到 Steam Input 前，以免改变手感/增加延迟 |
| HC 屏幕触摸板 | `OverlayTrackpad` WPF TouchDown/Move/Up → `DS4Touch.OnMouse*`；触摸 MOVE 只提交坐标，亮度只在边沿变化 | 该入口写 DS4Touch；`DualShock4Target` / `DualSenseTarget` 消费这些状态。已读调用链没有证据说明同一个屏幕入口直接成为 SteamDeckTarget 的左右 Deck 触点 |
| HC 虚拟 SteamDeck | `SteamDeckTarget : VIIPERTarget`，输入来自映射后的 ControllerState；写触摸位/XY/压力，声明 250Hz 主间隔 | 和屏幕 DS4Touch 入口应分开看。触摸/点击独立，未点击压力为零；不能因 YMCC 普通滑动压力为零就断定闪烁来自报告格式 |
| HC Steam 反馈回传 | VIIPER FeedbackReceived → SteamDeckTarget.HandleOutput → SendVibrate，识别 0x8F/0xEA/0xEB | 证明虚拟驱动的反馈是回传马达命令，并不是光标更新完成信号 |

具体代码位置均在固定提交原文中：

- SDT `SteamController/Devices/SteamAction.cs::SteamAxis.SetValue/GetDeltaValue`、`MouseController.cs::Accum/Update`。
- SDT `SteamController/Profiles/Default/GuideShortcutsProfile.cs::EmulateMouseOnRPad`、`Profiles/Predefined/SteamProfile.cs::Run`、`Devices/SteamControllerLizard.cs`。
- HC `Actions/MouseActions.cs::ExecuteAxisContinuous`、`Controllers/Steam/NeptuneController.cs`。
- HC `Views/Windows/OverlayTrackpad.xaml.cs`、`Misc/DS4Touch.cs`、`Managers/ControllerManager.cs::UpdateInputs`、`Targets/SteamDeckTarget.cs::BuildReport`、`Targets/DualShock4Target.cs`、`Targets/DualSenseTarget.cs`。
- HC `Targets/VIIPERTarget.cs` 与 `Targets/Viiper/ViiperService.cs` 的回传订阅，以及 `SteamDeckTarget.HandleOutput`。

## 没有发现可直接移植的“Windows 光标防闪烁 API”

在已读取的 SDT 鼠标/实体触摸/Steam 模式，以及 HC 实体鼠标/屏幕触摸板/基类窗口/虚拟 Deck 路径中，没有找到 GetCursorInfo / CURSOR_SUPPRESSED / ShowCursor 的相应接管算法。HC 基类窗口使用 WS_EX_NOACTIVATE 和 WM_MOUSEACTIVATE，这解决激活窗口问题，不等于触摸造成的系统光标抑制。该结论仅限列出的源码审查范围，并不是对两个项目所有版本、所有模块的全局不存在证明。

Microsoft 对 CURSOR_SUPPRESSED 的定义明确涉及触摸/笔输入。由此推断：YMCC 的“屏幕触摸仍持续输入，Steam 稍后产生鼠标”更容易形成异步显示状态交错；两套工具的实体板桌面控制不具备完全相同的前端输入条件。这是可解释现象的机制候选，不是本机根因已经证实。

## 本轮实际采用与保留

1. **保留已经完成的 Deck 光标查询合并**：连续 MOVE 不先同步读取再重置 24ms 期限；复用原有至多一个 UI 待处理消息。Steam/HID/BUS/显示刷新参数不提高，直接鼠标与 DOWN/UP 仍即时。
2. **保留唯一鼠标输出者**：Deck 接触由 Steam 解释，YMCC 不再旁路发一份鼠标位移。核对固定 HIDMaestro USB/IP 主端点实现，没有发现将 Deck XY 再转换成鼠标端点数据的同一发送路径。
3. **保留已有首帧基准与小数积累**：YMCC Core DOWN 已把 x/lastX/startX 设为同一点；Mouse MOVE 保留小数余数，零位移不发鼠标报告。没有为模仿上游重复增加另一层计算。
4. **不移植 HC 的逐脉冲 Task.Delay 回零**：其 0x8F 分支每次建立延后回零 continuation。对连续小脉冲，需要额外解决旧回零截断新脉冲及任务数量问题；本轮闪烁修复不扩展为反馈转发工程。若将来接 Steam haptic 输出，应并入 YMCC 既有马达仲裁/截止时间调度，而不是每脉冲增加延时任务。
5. **不改 HC 主间隔或复制其整段报告**：250Hz 是 HC 该目标的输入节奏，不是屏幕光标刷新。另发现该上游报告头第 3 字节为 payload 长度 56，而本地 HC 冻结基线/固定 HIDMaestro 路径使用 64；本轮没有证据把该差异关联到当前闪烁，也没有核实两套虚拟设备后端的完整消费者契约，因此未改 YMCC 已锁定报告头。

HC 的反馈转发是独立可借鉴的功能能力；当前工作聚焦闪烁与 CPU，尚未取得其能解决光标闪烁的证据，故保留本地触摸反馈算法与档位。无额外源码修正由本次 SDT/HC 对照触发。

## 剩余限制

- 不能证明两套上游在用户同一游戏/屏幕触摸/虚拟 Deck 情况下完全无闪烁；本轮只读源码。
- 本轮对光标查询的修改可减少连续 Deck MOVE 的 GetCursorInfo/GDI 路径工作，但没有 CPU 实测百分比。
- 如果用户所指的是整个边框/触摸光点而不是鼠标光标，本次调度修正并不覆盖那类显示隐藏问题；新增状态计数也只观测鼠标光标。
- 在相同刷新预算下，软件光标的连续采样使用 24ms 调度。查询数量减少并不自动保证更平滑；效果留待后续实机反馈，不能将这次源代码审查写为“修复通过”。

## 主要在线来源

- `https://github.com/ayufan/steam-deck-tools/tree/deee7fa887c2864ee3b54eba2c32b2f58b547349`
- `https://github.com/Valkirie/HandheldCompanion/tree/a250b19a4b7e29dac3b383f2d1ac517d1af43e4e`
- `https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-cursorinfo`
- `https://partner.steamgames.com/doc/features/steam_controller/input_source_modes`

报告与原始证据目录：`G:/YeManCC-Work/Build/Analysis/ScreenTouchpad-Deck-Flicker-20261014-121500`。