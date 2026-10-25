# 屏幕触摸板触摸反馈（2026-10-11）

> 同日最新更新：触摸反馈现提供“最小震动反馈／中震动反馈／强震动反馈”，可调后端请求为 10%／20%／30%，新配置及缺失字段默认中档；明确保存的关闭或任意档位保留。18ms 脉冲、短边 5% 刻度与 45ms 限频不变。下文早期参数和测试结果保留为历史，三档最终说明见 `G:/YeManCC-Work/Mainline/YeManCC-source/YeManCC/docs/screen-touchpad-haptic-tiers-20261011.md`。

## 用户需求与边界

面向掌机内置手柄马达，用低强度短脉冲近似触摸板反馈；开发机没有手柄不阻塞实现。
仅复用已存在的实体输出能力（XInput、受管 MSI 主游戏 HID），不启用虚拟手柄、
不重新开放无虚拟手柄的 SteamDeck/PS5 原生触控映射，不改 BUS/InputHost 单写者、
HWND/GDI 所有权、权限或 Sleep Guard 策略。不部署安装版。

## 使用与初版参数

- 屏幕触摸板开启时增加“触摸反馈”：关闭 / 最小震动（分段滑动）；默认关闭。
- 每个现有配置档独立持久化 `touchFeedback: "off" | "minimal"`；旧配置缺省关闭。
- 初次触碰发一次轻脉冲；持续移动累计到短边的 6% 再发一次。
- 小于短边 0.6% 的触点噪声通过锚点过滤；慢速真实位移仍可累计。
- 初版线性马达请求为 8% / 18ms，触点/实体输出最短间隔 45ms。
- 大跨度移动只发一次，不积压脉冲；停手后不会靠静止 UPDATE 继续震动。
- 两个触点独立累计；松手、取消、配置/尺寸切换释放状态。
- MSI A1M 现有固件后端只有 0/193 开关量，不能承诺 8% 实际振幅；以短时长近似。
- 这是普通马达短脉冲模拟，不声称等效 Steam Deck 原生独立触觉，也未验证真实手感。

## 输出链路与保护

`Core::event -> Action.hapticContacts/hapticPulses -> output -> nativeScreenTouchpadHapticPulse`
`-> touchhaptic::Dispatch -> 现有 gamepadSerialSubmit -> nativeGamepadFeedbackPulse -> 实体写入 -> 零值停震`

触摸输入/UI 线程只提交一个有界请求，不执行设备 I/O、不为每次移动创建线程；
80ms 以上的排队请求、松手后旧请求、旧电源代际请求直接丢弃。当前没有实体源时安全返回，
不回落到虚拟/Unknown 槽。保留原来低频手动震动测试线程，但触摸短脉冲在串行工作线程中完成。
起震、脉冲代际与停震共享互斥所有权；停震绑定原槽身份代际或 MSI 主接口路径/代际，
不能重定向到替换的控制器。触摸反馈也不截短手动测试脉冲。

## 已执行验证

- `pnpm run test:screen-touchpads-haptics`：326 项通过。
  - legacy / candidate 状态夹具各 155 项。
  - 当前正式主 EXE 提前返回自测 16 项：无设备、虚拟/未知槽拒写、低幅起震/归零、旧停震拒绝、
    真实 callback -> 串行 worker -> 模拟物理 writer、1000 次请求合并与排队取消。
- 标准 native 构建成功；隔离 candidate 全主 EXE 构建及同一 16 项输出自测通过。
- candidate 主 EXE owner 提前返回自测：显式 opt-in 活跃，默认仍为 legacy，各 10 项通过。
- Vue typecheck / 前端校验 / 实际 SFC 渲染通过；浏览器 131 项通过，包含选项持久化与 persona 隔离。
- Web 构建成功；光标 104 项、视觉缓存 20 项通过（冗余上传为 0）。
- InputHost 8 个既有无设备自测和 native -> Host 接触/按钮报文夹具通过。
- **全量 WM_POINTER 回归未全绿**：435/436 通过，失败项 `overlay-does-not-steal-focus`。
  记录到运行期间前台窗口变化（包括 Steam 大屏幕窗口/空前台）；未弱化断言，也不将重跑当作完整验收。
- 所有触觉写入测试均使用模拟输出，真实硬件写入为 0；真实震感待掌机验证。
- 鼠标穿透候选仍保持显式 opt-in，未借触觉功能自动推广未验收的输入路线；其硬件验收未完成。

## 证据

- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics\haptics-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics\candidate-app-output.json`
- `G:\YeManCC-Work\Mainline\Build\Validation\ScreenTouchpads-Polish\browser-verification.json`
- `G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics\pointer-focus-first-run.json`

### 后续同日回归更新

鼠标穿透链路审计中，焦点检查改为测试 PID 范围的真实前台事件观测（包含瞬间激活），
不再要求外部应用整段保持同一 HWND。标准回归现已全过，437 项 WM_POINTER 检查通过，
ownForegroundEvents=0。另修复了延迟 DOWN/UP 被误判短点按的问题，见
`screen-touchpad-mouse-pass-delivery-audit-20261011.md`。原始失败记录保留，真实触觉及
静止双指/鼠标穿透仍未做硬件验收。


## 同日后续：触摸反馈默认开启

按用户要求，仅把触摸反馈缺省值由 `off` 改为 `minimal`：

- native Config、前端四档默认配置及界面缺失字段 fallback 全部同步。
- 开启单／双触摸板时，无虚拟手柄／SteamDeck／PS5／Xbox 档均继承最小震动反馈。
- 缺少该字段的旧配置沿用新的开启默认值；明确保存的 `off` 不被加载或布局切换覆盖。
- 触摸板关闭仍不产生触觉输出；脉冲强度、时长、分段距离和实体输出保护不变。
- 用户已验收的鼠标穿透 owner、input surface、button overlay、native 构建接线未修改，保留文件哈希对照。

验证：触觉测试 366 项通过（legacy／native 夹具各 175，主 EXE 输出 16）；
Vue typecheck、前端默认值、实际 SFC 缺失字段／显式关闭渲染通过；
浏览器 136 项通过，包含各档开启后的默认反馈和明确关闭持久化。native 与 Web 构建成功。
未部署安装版，未运行人工触觉／鼠标测试，不改写先前的硬件结果。

证据：`G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-DefaultOn`。

## 同日最终更新：低 CPU 刻度触觉与显示恢复

- 可调强度后端请求由 8% 提到 10%，时长仍为 18ms；MSI A1M 仍使用原有 0/193 开关量，
  不承诺可调振幅。复用既有实体后端与机型适配，不启用虚拟手柄。
- 分段距离由短边 6% 改为 5%；0.6% 噪声门、45ms 最短起震间隔与 80ms 请求过期保护保持。
- 每触点固定状态、O(1) 处理，DOWN 缓存几何，MOVE 先做平方噪声判断；大跨度移动一次消费余量，
  不积压补震。重复 held／idle 不重复调度，成功短脉冲日志在结束时有界汇总。
- 不增加线程、轮询或高精度计时器，不重构现有串行 worker 的 18ms 停震等待，也不改 HID 句柄生命周期。
- 触摸反馈默认开启、明确关闭持久化、普通鼠标点击穿透均保持；新增事件驱动的显示栈恢复，
  并修复延迟原生短点按发布窗。完整实现与边界见
  `G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\docs\screen-touchpad-lowcpu-and-game-visibility-20261011.md`。

最终固定基线的软件验证：触觉 400 项（legacy／native 各 190，实际主 EXE 模拟输出 20）、
鼠标穿透 64、游戏式窗口显示恢复 16、主 EXE owner 40、WM_POINTER 437、Core 106、浏览器 136 项通过。
纯 Core 微基准约 13–17ms／百万事件，比旧算法约快 8–27%；不是整个 YMCC 的 CPU 测量。

最终证据：`G:\YeManCC-Work\Build\Validation\ScreenTouchpads-Haptics-LowCpu\final-touchpad-verification.json`。
验证产物来自最后可编译的固定项目基线加本轮触摸板文件，不是所有其他任务最新改动的整包验收。
真实游戏／独占全屏和掌机震感尚未验收，未覆盖安装版。
