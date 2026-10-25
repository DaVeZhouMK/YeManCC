# 鼠标穿透送达链路审计（2026-10-11）

## 原始目标仍未宣称完成

监控 HUD 的整窗透明样式只适合纯显示；触摸板必须同时保留 Touch/Pen、放行 Mouse。
当前候选仍使用现有 Windows controlled-input 分流，不转发鼠标点击、不伪造按住状态，
不安装/启用虚拟手柄，不改变 BUS/InputHost 单写者或 HWND/GDI UI-thread 所有权。
默认构建/启动仍为 legacy；候选仅通过现有明确 opt-in 使用。

## 本轮实际修复

Windows 延迟送达双指静止模拟帧时，原先用 UI 回调执行时刻记录 DOWN/UP，
会把约 780ms 的长按误判成短点按。独立重现中共有 **13** 个生产路径样本出现误判。

现已在 legacy WM_POINTER 和候选 controlled-source 两条路径保留
`POINTER_INFO.dwTime`，展开为当前 64-bit uptime epoch 后用于手势时长。
未来/不确定时间戳降级到 dispatch time。已送达的延迟长按不再点击；
真正的延迟短点按仍保留，不伪造缺失的 DOWN 或 MOVE。

- 当前四种注入组合下的误判计数：**0**。
- 源时间、32-bit tick 回绕、合法零值、无效未来值、Mouse/Deck/PS 的长/短按：10 项纯状态测试通过。

另将测试中“不得抢焦点”改为注册一个 **仅观察测试进程 PID** 的临时 foreground 事件观察器：
既记录瞬间抢焦点，也检查末态前台 PID。外部应用合法切换前台不再被误判为 overlay 抢焦点。
没有在产品运行路径增加输入 hook、前台操控或持续观察器。

## 静止双指证据边界

新增 `screen_touchpads_static_delivery_probe.cpp`，比较四条接收路径：
裸 WM_POINTER、真正的注册 WM_TOUCH compatibility delivery、生产 legacy、生产候选。
单/双指、近/远间距，共 12 个情形；分别使用 InjectTouchInput 与
InjectSyntheticPointerInput，以及默认/EnableMouseInPointer(TRUE) 策略，共 48 个情形。
所有注入目的地均复检为本测试自有窗口；使用被动、不可自动激活的下层窗口，
不注入鼠标或绑定、不抢用户前台，不将其当作 joint-mouse 或真实硬件验收。

本机单指有正常 held 采样；双指静止模拟帧在多个裸接收路径中也会延迟至 UP 后同批送达，
候选没有提前收到可用于构造“按住”的数据。WM_TOUCH compatibility 下也观察到同样延迟。
因此不能仅凭这个合成输入结果，把候选定性为坏实现；也不能据此宣称真实双指可用。
**真实双指静止 + 鼠标同时点击/拖动/滚轮**仍须人手对照。

## 人手工具

`verify-screen-touchpads-native-physical.ps1` 已重新构建，但没有执行 `-Manual`。
手动模式现在与正式产品采用同一 mouse-in-pointer 策略；启动时允许最多 10 秒，
让操作者点击自有白色验证窗口取得真实前台，不用注入/线程附着绕过 Windows 前台锁。
随后 legacy / candidate 各 10 秒。辅助窗口 TTL 同步延长，但仍有界。
仍保持：无手柄要求、无虚拟目标、无输入注入、无真实绑定输出、需硬件来源证明。

## 可复现命令（不自动启动产品/游戏）

- `tools/verify-screen-touchpads-static-delivery.ps1`：合成输入/时钟回归，不等价于硬件验收。
- `tools/verify-screen-touchpads.ps1 -WorkspaceRoot G:\YeManCC-Work\Mainline`：现有 native/Host/UI 回归。
- `tools/verify-screen-touchpads-native-physical.ps1`：只构建人手工具。
- `tools/verify-screen-touchpads-native-physical.ps1 -Manual`：仅操作者确认准备好后执行。

证据：`G:\YeManCC-Work\Build\Validation\ScreenMousePass-Goal\static-delivery\verification.json`。
历史未修复重现位于同目录 `before-event-time`，不要覆盖或用新的绿灯替代原始失败记录。

## 最终软件回归

本轮事件时间修复后，标准 native 构建和隔离 candidate 全主 EXE 构建成功。
`verify-screen-touchpads.ps1` 完整通过：WM_POINTER 437 项（ownForegroundEvents=0）、
既有 Core/wire、InputHost 8 个自测与 native->Host fixture、触觉 326 项、
光标 104 项、图层缓存 20 项、Vue 校验与浏览器 131 项。
当前 candidate 主 EXE owner 提前返回自测各 10 项通过：明确 opt-in 激活，默认仍不激活。
这些软件结果不替代真实双指/鼠标对照，未部署安装版、未启动真实产品或游戏。


## 后续同日用户澄清与普通点击交付

用户随后明确只要求触摸板覆盖区域内的普通鼠标点击能到达下层窗口，不以静止双指并行鼠标为此次前置。
正常构建和 owner 启动现已默认启用原生输入，64 项跨进程点击对照、40 项实际主 EXE owner 自测、
70 项 owner 触摸/生命周期检查及完整软件回归通过。此前未运行的硬件 gate 仍保留原记录，不改写为通过。
以上默认 legacy / opt-in 状态为历史阶段；当前交付见 `screen-touchpad-mouse-clickthrough-20261011.md`。
