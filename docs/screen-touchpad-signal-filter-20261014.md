# SteamDeck 滑动：不连续光标信号防护、低 CPU 与可选滤波

> 历史方案，滤波已撤销：2026-10-14 用户反馈开启后大幅移动更闪，同日删除该功能。当前状态见 `screen-touchpad-no-filter-cursor-ownership-20261014.md`；下文只保留原决策与失败反例的追溯依据。

日期 2026-10-14，接续同日 `screen-touchpad-deck-flicker-20261014.md` 和 `screen-touchpad-windows-reference-20261014.md`。用户授权综合优化并直接修改。本轮仍仅源码与静态复核，不运行测试、编译、部署、实机输入或 CPU 基准。

## 处理决定

采用两条互相独立的优化：默认修正系统光标显隐判定的采样连续性与重复窗口查询；增加可选的 Deck 坐标轻度平滑，默认关闭。窗口显隐闪烁与坐标噪声没有同一根因保证；不能把滤波写成“修复闪烁已通过”。Valve 的私有处理与用户现场仍不明确，维持报告格式、触摸/点击/压力位和 Steam Input 布局解释，不伪造鼠标移动或加大压力。

## 光标链路（默认生效于新源码）

上一轮已经让 Deck 连续 MOVE 合并至既有 24ms 补读，避免每个事件重置期限。本轮补充：

- 旧逻辑在 GetCursorInfo 失败时保留上次画面，这是合理的；但也保留了“系统光标稳定显示”的起始时间。现在查询失败保留画面、清除稳定计时，后续 SHOWING 必须重新确认。
- 两次有效 SHOWING 之间如果间隔大于 72ms（原 24ms 补读预算的三倍）或时间回退，重新开始 80ms 的稳定接管窗口。未采样的一段时间不算已确认连续显示。72ms 是 YMCC 容错选择，不是 Valve 或 Windows 的保证值。
- 不改原有 flags=0 主动隐藏分支；SHOWING 但没有可用形状继续保留已知画面；真实抬指后的系统光标接管维持原策略。
- 缓存软件光标已提交的位置，WM_WINDOWPOSCHANGED 使缓存失效，成功上传/定位后恢复；省掉每次位置比较的 GetWindowRect，同一位置不重复 SetWindowPos。显示层仍归 UI 线程，样式/启用修复和不激活、穿透约束保留。
- cursorPolicyVersion=4；增加 cursorQueryFailures、cursorHandoffGapResets、cursorMaxHandoffSampleGapMs。只记录现有采样，不新增采样或逐帧日志。

现有自测夹具两处表示“连续稳定 80ms”却只有 79ms 间隔的采样序列补上中间观察，与新规则保持一致；仅维护源码，没有执行这些自测。当前改动不是实机验收结论，也没有证明窗口显隐切换就是全部闪烁来源。

## 可选“滑动平滑”

SteamDeck 档的双板布局中，只要左右任一板映射为 Deck，就显示“滑动平滑：关闭 / 轻度”。保存字段为 `deckSmoothing: "off" | "light"`，native 内部用 bool，旧配置缺字段默认 off。复用现有配置保存/四档隔离，不新增 IPC。直接模拟鼠标、PS、方向键路径不运行这个滤波；切换该配置时走原有释放流程，下一次落指重新建立状态。

轻度方案参考 One Euro 的速度自适应低通思路，但独立实现了固定大小的两轴状态及本任务所需的保护，并非移植 HC 的整段代码或复刻 Valve 私有算法。

| 项目 | 本轮做法 |
| --- | --- |
| 输入 | 使用原始触摸位置除以板宽/高后的 0–1 坐标；时间来自 Core 原有触摸源时间 |
| 平滑强度 | 最低截止频率 30Hz，按低通后的速度提高，最高 120Hz；这是算法参数，不是 HID/显示采样频率 |
| 时间计算 | 按事件实际间隔计算 alpha；没有硬编码假设每次事件为 4ms，也不以 BUS 频率反复推进 |
| 两轴一致 | 用两轴速度绝对值的最大值计算同一个 alpha，避免左右/上下使用不同时间响应；无需 sqrt |
| 初始状态 | DOWN 首点直通；Pad 清零/取消/故障/配置切换会同时清除滤波状态 |
| 静止 | 相同原始坐标保留输出并清速度；没有新事件就不运算，不在 BUS 追赶历史目标 |
| 异常时间 | 改变的坐标遇到同毫秒、回退或超过 100ms 间隔时重新建立基准，避免旧速度跨异常间隔传播 |
| 点击与震动 | 点击分类、移动距离、震动触发、手指光点使用原始坐标；点击报告沿用末次滤波输出坐标，避免点击瞬间向原始点跳一下 |
| 运算/内存 | 每有效改变事件仅固定数量四则运算、abs/min/max；每板固定内联状态，无堆分配、样本队列、后台线程或计时器 |

滤波参数是偏轻的初始选择，没有在用户硬件调参验收。关闭时原始坐标投影保持原路径，仅多一个配置判断。开启后低速噪声可被压低，但也会增加少量位置滞后、改变微小操作手感；抬指不会补发追赶移动，因此末端滤波位置可能与最后原始位置有小差距。这些权衡是默认关闭的原因。

## 为什么不直接上其他滤波

- 多帧均值/FIR：需要历史窗口，并在快速转向时引入明显的样本延迟；没有本机噪声证据支持该成本。
- 固定死区/中值：可能把有意的精细慢滑当噪声；不适合先当默认策略。
- 对已被 Steam 映射后的软件光标再做平滑：系统光标与软件光标可能处于不同位置，容易在交接时错位。本轮软件光标仍使用 CURSORINFO 的真实位置。
- 直接改变 Steam 触摸/点击/压力语义：公开协议与 SDK 审查没有支持其可修复本次闪烁，不根据猜测修改。

## 证据来源与限制

已联网取得 One Euro 作者说明及参考 C++ 实现。作者明确说明抖动与延迟的权衡、低速调 cutoff/高速调 beta；本轮只借鉴算法思路，不声称参数来自官方 Deck。Microsoft CURSORINFO 对触摸抑制与应用隐藏状态有不同定义，继续保留该边界。

- One Euro 作者资料：`https://gery.casiez.net/1euro/`
- 作者参考实现：`https://raw.githubusercontent.com/casiez/OneEuroFilter/main/cpp/OneEuroFilter.cpp`
- Microsoft 原文：`https://raw.githubusercontent.com/MicrosoftDocs/sdk-api/docs/sdk-api-src/content/winuser/ns-winuser-cursorinfo.md`
- Valve/SDL、固定 HIDMaestro DLL、SDT/HC 两个固定提交：见前两份研究报告及 `G:/YeManCC-Work/Build/Analysis/ScreenTouchpad-Deck-Flicker-20261014-121500/Sources`。本轮没有将网络上游覆盖本地冻结依赖。

源码阅读能确认旧代码在“采样缺失后继续计算稳定时间”及“重复窗口查询”上的行为；不能确认用户当前运行二进制、实际闪烁对象、全部根因、实际 CPU 百分比或滤波手感。软件光标补读仍为 24ms，系统/虚拟驱动/显示刷新率未增加，80ms 接管基准未提高。

## 文件与留档

- `native/screen_touchpad_smoothing.h`：独立、固定大小、可选的滤波状态。
- `native/screen_touchpads.h`：配置读写、Pad 状态、原始/滤波坐标分离、生命周期释放、诊断字段。
- `native/screen_touchpad_cursor.h`：显隐采样连续性与位置缓存。
- `src/bridge/screenTouchpads.ts`、`src/components/ScreenTouchpadsSettings.vue`：默认值、严格枚举校验、设置入口。
- `tools/screen_touchpads_cursor_selftest.cpp`：只同步旧夹具的中间采样，没有运行。
- `tools/SCREEN-TOUCHPADS.md`：维护说明。

留档：`G:/YeManCC-Work/Build/Analysis/ScreenTouchpad-Signal-Filter-20261014-124400`，包含 Before/After/Diff、联网原文与哈希、初始源码修改回执和最终 completion-receipt。只改列出的文件，写入前核对字节，保留其他任务的修改。维护只读盘点 13 类、308 候选、0 缺失锚点、0 未归类，仅作为变更前接入索引检查。