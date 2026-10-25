# SteamDeck 屏幕触摸板滑动闪烁：链路核对与低 CPU 修正

日期：2026-10-14。仅源码研究与修改，未运行测试、构建、输入注入或部署。未读取/改写用户的 Steam Input 布局，未重启 Steam，未访问运行中控制器。

## 结论及证据范围

代码中确认一个调度缺陷：Deck 每个 MOVE 都先立即读系统光标，并将既有 BUS 补读期限重置为当前时刻 + 24ms。触摸更新间隔持续小于 24ms 时，独立补读机会可以一直被推迟；直接鼠标路径先提交 SendInput，而 Deck 路径此时仅更新接触状态，实际鼠标由 Host → 虚拟 HID → Steam Input 异步产生。两种路径不能用相同的“输入事件刚到就读取”假设。

本轮只修正此处：连续 Deck MOVE 合并至原有 24ms 调度，不再在每个 MOVE 先读光标并重置期限。第一次追踪、DOWN/UP/取消、直接模拟鼠标保留原处理。该修正能消除这项本地调度缺陷，但尚无当前故障现场、同版本二进制/日志或实机画面证明它是全部闪烁的根因。用户说的“闪烁”也尚未区分系统光标、软件光标或整个触摸板；本轮改动针对光标采样，不能声称修好了整个面板闪烁。

## 两条输入链路

```text
模拟鼠标：触摸 MOVE → Core 相对位移 → output / SendInput → 即时读取 CURSORINFO
SteamDeck：触摸 MOVE → Core 触点 → 既有 BUS → InputHost → 64 字节报告
                                      → USB/IP 虚拟 HID → Steam Input → Windows 鼠标
         光标绘制另行读取 CURSORINFO，并不知道 Steam 何时处理完该报告
```

即使 SendInput 返回，也不保证系统合成已完成；这里只比较 YMCC 调用顺序，不将其当作显示完成的同步栅栏。修正后的 24ms 读取同样不是 Steam 消费确认，也不保证总链路延迟不超过 24ms。

## 联网资料与固定驱动核对

1. **Valve Steam Input 模式说明**：触摸板可映射鼠标、相对鼠标、区域等；Trackball 可保留惯性。因此不能自行按触摸坐标预判屏幕光标，也不能每次手指停止就发送反向鼠标/清零 Steam 状态。本轮保留 Steam 布局语义。
2. **Valve 署名的 SDL 协议结构及 SDL Deck 解码**：触摸位、点击位、XY、压力分开。SDL 根据触摸位判定 down，压力独立上报。YMCC 的左右 XY 位于完整报告 16/18、20/22，压力 56/58，触摸位 19/20、点击位 17/18，与这些公开实现一致。不能仅凭滑动压力为 0 就断言丢失触摸，本轮没有给滑动伪造压力。
3. **Linux hid-steam（含 Valve 版权署名）**：触摸输入与 haptic feature 命令是不同方向；haptic pulse 包含 pad、duration、interval、count、gain。它不是一条“震动完成后才允许下一次鼠标移动”的确认链路。公开协议也不等于 Steam Windows 客户端完整鼠标/压感算法源码。
4. **Windows CURSORINFO**：CURSOR_SUPPRESSED 表示触摸/笔输入令系统不绘制光标；CURSOR_SHOWING 与 flags=0 有不同语义。当前代码 flags=0 仍立即隐藏，未将游戏主动隐藏光标误当闪烁强行覆盖；80ms 稳定接管策略保留。
5. **实际固定 SDK**：本仓库 HIDMaestro.Core.dll SHA256 为 `BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E`，与 dependency-lock 相符。用已有 ilspycmd 只做静态反编译，没有加载控制器、启动 SDK 或运行设备自测。不能拿联网 master README 的新版本行为代替该固定 DLL。

固定 DLL 的 `HMController.SubmitRawReport` 复制/写入共享报告，但不更新 `_lastSubmitted` 或 `_lastSubmitTicks`；`IdleFrameLoop` 使用旧 `_lastSubmitted` 调用 SubmitState。这验证了已有 `TakeScreenPadReportOwnership` 停止 idle producer 的必要性。当前代码先取消并等待线程退出，再由唯一 Host 发送包含释放在内的 raw 报告，目标重建时重置拥有状态和计数。本轮没有发现需要再加第二个保活发送者的依据；也没有以静态代码推断用户正在运行的版本已具备这些防护。

固定 DLL 的 `UsbipEmulatedDevice.InputPumpLoop` 收到输入事件后将报告发往主 HID 端点；没有把同一份 pad 坐标额外生成鼠标端点数据。SET_REPORT 经 PublishOutput 写入输出通道，再由 HMController 的 OutputReceived/OutputDecoded 暴露给调用方。InputHost 源码检索未找到这两个回调订阅；当前屏幕触摸反馈由 YMCC 本地触摸反馈链路驱动物理掌机震动。本轮不新增 Steam haptic 回传，避免将另一个功能改造混入闪烁修正。这些是固定实现的静态事实，并非对全部驱动/Steam 运行状态的动态排除。

## 明确修改

- `native/screen_touchpads.h`：只有有效 Deck WM_POINTERUPDATE 传入 deferDeckMove；取消分支不传。状态版本改为 3，增加少量采样计数。
- `native/screen_touchpad_cursor.h`：持续 Deck MOVE 不直接 cursorSync，不推迟补读期限；调用既有 cursorRequestSync，复用至多一个待处理 UI 消息。起始追踪仍即时同步。
- `tools/SCREEN-TOUCHPADS.md`：记录行为和未验收边界。

保留原 24ms 活跃/被动补读预算、80ms 稳定接管时间，以及 BUS、HID、显示各层的频率参数。没有新增线程、计时器、钩子、GDI 位图重建或逐 MOVE 日志。连续 Deck MOVE 的额外光标查询可减少；未测量整体 CPU，不能给出下降百分比。连续软件光标的位置采样改为使用原有 24ms 预算，相比旧的逐触摸即时读取可能更稀疏，流畅度仍需用户后续实机观察。

触摸板边框、十字、光点、按下亮度、自动隐藏、鼠标穿透、焦点和震动档位均未改动。InputHost、SDK/驱动和 Steam 接口没有修改，也未增加或移动 Steam 接口，因此维护索引无需新增接入项。

## 已有状态增加的诊断字段

| 字段 | 意义 |
| --- | --- |
| cursorPolicyVersion = 3 | 本轮源码策略标识，后续日志可区分运行版本 |
| cursorDeckDeferredMoves | 有效追踪时合并到既有调度的 Deck MOVE 次数，不等于少发了 HID 报告 |
| cursorFlagsObserved | 是否已有 CURSORINFO 样本 |
| cursorLastFlags | 最近已采样的 CURSORINFO.flags |
| cursorFlagTransitions | 已采样状态之间的变化次数；不能代表系统真实的全部转换 |

这些字段与既有 cursorFallbackQueries、Shows/Hides、Handoffs、Uploads、Error 一起使用。没有额外查询，也没有每次事件序列化。因用户要求不测试，本轮未为诊断主动制造输入/窗口切换。若后续仍闪烁，优先用实际版本和同一时段的这些数据区分“系统显隐/软件接管”与“整个显示窗口被隐藏”；然后才决定是否需要改接管策略。不要直接把 flags=0 全部屏蔽，也不靠提高刷新率掩盖问题。

## 留档与只读盘点

研究目录：`G:/YeManCC-Work/Build/Analysis/ScreenTouchpad-Deck-Flicker-20261014-121500`。

- `Sources/`：联网原文、URL/哈希回执、固定 DLL 的三个相关类型反编译。
- `Before/`、`After/`、`Diff/`、`source-edit-receipt.json`：本轮三个已有文件的精确增量、修改前后哈希。写入前核对原字节，保留其他任务已存在的修改。
- `steam-inventory.json`：只读维护盘点为 13 类、308 候选、0 缺失锚点、0 未归类。不是兼容、构建或实机验收。
- `completion-receipt.json`：最终源文件与研究报告回执。

本轮未执行测试、编译、部署、Steam/游戏重启、硬件输入或 CPU 基准。已人工复核差异中的分支范围和现有调度常量；不据此声称硬件问题已解决。

## 原始资料地址

以下为 2026-10-14 获取时的来源，原文与 SHA256 在 Sources 留档；main/master URL 可变，固定 SDK 分析以本地锁定 DLL 为准。

- Valve Steam Input 模式：`https://partner.steamgames.com/doc/features/steam_controller/input_source_modes`
- Valve ISteamInput：`https://partner.steamgames.com/doc/api/ISteamInput`
- Valve 署名协议结构：`https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/hidapi/steam/controller_structs.h`
- SDL Deck 解码：`https://raw.githubusercontent.com/libsdl-org/SDL/main/src/joystick/hidapi/SDL_hidapi_steamdeck.c`
- Linux 固定 v6.17 协议实现：`https://raw.githubusercontent.com/torvalds/linux/v6.17/drivers/hid/hid-steam.c`
- Microsoft CURSORINFO：`https://raw.githubusercontent.com/MicrosoftDocs/sdk-api/docs/sdk-api-src/content/winuser/ns-winuser-cursorinfo.md`
- HIDMaestro README：`https://raw.githubusercontent.com/hifihedgehog/HIDMaestro/main/README.md`（仅上游背景，未用其当前版本替代固定 DLL 证据）

## Windows 软件补充对照

根据用户后续提供的 Steam Deck Tools 与 HC 线索，已固定两套上游提交并追踪实体板、屏幕板、虚拟 Deck 与反馈回传。详细见 [Windows 源码对照](screen-touchpad-windows-reference-20261014.md)。未找到可直接移植的同类光标抑制防闪算法；保留本轮已完成的查询时序修正。
