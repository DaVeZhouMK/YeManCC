# 屏幕触摸板：触摸／移动／按压分离（2026-10-13）

## 本轮边界

只做公开源码对比、当前链路静态检查和两处已明确的 SteamDeck 输入修正。不运行测试、基准、编译、掌机震动或虚拟设备，不部署 EXE，不改用户 Steam 布局、前台焦点、PS5/PS4/鼠标/WASD/方向键语义、触觉算法及最小/中/强档位。

这不是“已经实机证明解决所有边滑边点击”的结论。Steam Input 用户配置、具体游戏行为和触屏输入仍未作动态验证。

## 一手公开资料

1. Valve 官方 Steam Deck 规格写明触摸板具有可配置点击力度的压力敏感能力：
   https://www.steamdeck.com/en/tech
2. 固定版本 Linux v6.17 `drivers/hid/hid-steam.c`，文件声明 `Copyright (c) 2022 Valve Software`；包含 SteamDeck HID 输入处理。这是 Valve 参与维护的公开驱动，不是完整 Steam Input 用户态源码：
   https://github.com/torvalds/linux/blob/v6.17/drivers/hid/hid-steam.c
   https://raw.githubusercontent.com/torvalds/linux/v6.17/drivers/hid/hid-steam.c
3. Valve 官方 Steam Input 概念说明，区分硬件输入和可配置的软件输入模式；公开 API 文档不等于提供完整点击/触觉内部算法：
   https://partner.steamgames.com/doc/features/steam_controller/concepts

该驱动的 `steam_do_deck_input_event()` 独立读取 byte 10 的 bit 3/4 作为左右触摸状态；同一函数的 `BTN_THUMB` / `BTN_THUMB2` 事件读取 byte 10 bit 1/2 作为左右按压；报文说明把 offset 56/58 分别定义为左右压力。坐标为 offset 16/18 和 20/22。含义如下：

| 信号 | 左 | 右 | 含义 |
| --- | --- | --- | --- |
| Touch | 全局 bit 19（10.3） | 全局 bit 20（10.4） | 手指触摸 |
| Click | 全局 bit 17（10.1） | 全局 bit 18（10.2） | 点击/按压 |
| X/Y | 16/18 | 20/22 | 坐标 |
| Pressure | 56 | 58 | 压力，u16 |

因此“触摸存在 + XY 改变”不需要额外设置 click 或模拟较高压力。屏幕不能复刻真实力感；长按只能作为软件替代手势，不能声称它就是 Valve 的原始压感阈值算法。

## 当前 YMCC 链路证据

- 原生输入和 WM_POINTER 路径共用 `Core::event()`；普通 DOWN/MOVE 不把 native click 置为 true。
- UP 在 250ms 内且最大离起点距离小于短边 3%（最低 1px）时，把轻点转为 60ms `TapClick`。移动后回到起点仍看最大偏移，不能只看终点。
- `Core::snapshot()` 原先对普通接触生成 pressure=8192，对点击生成 pressure=32767。
- `main.cpp` 的 FocusFrameContent 将 snapshot 原样序列化为 BUS `touchpads`。
- `InputHost/ScreenTouchpadReport.cs` 的 `BuildSteamDeckScreenPadReport()` 原样写入 touch/click 位与 offset 56/58 压力，并不会因为 click=false 自动清零压力。
- `Core::event(DOWN)` 原先没有撤销同侧旧 TapClick；`snapshot()` 一旦旧 tap.active 就优先采用旧坐标与 click。这确定会把新的接触在剩余窗口内投影成旧点击，并可能暂时冻结新坐标。
- 当前 Core 中未发现把 `p.click` 置 true 的长按判定；不能把 Steam Input 布局产生的长按、系统行为或其他映射行为当成 YMCC 已有原生长按点击。

pressure=8192 是已证实的发送值，但它在用户实际 Steam 配置下是否触发 Force Press，没有在本轮被实机证明。也不推定所有“点击”都由这个值导致。若布局把 Touch 本身绑定成鼠标/游戏点击，清零压力不会擅自抹掉该用户绑定。

## 已修改的明确部分

1. SteamDeck 普通接触：touch=true，click=false，pressure=0，保留左右 XY。
2. 轻点点击：沿用 click=true/pressure=32767/60ms，尚未把轻点替换成长按。
3. 新 SteamDeck DOWN：只撤销本侧上一次 TapClick，避免继承旧坐标、click 和 pressure。已经发布和尚未发布的旧 tap 都撤销；另一侧不受影响。
4. 原始坐标投影缓存增加 Mode 作为键。否则相同坐标/点击状态在 Deck 与 Sony 模式间切换时，有机会继续复用旧 pressure。
5. 不改变 Sony、Windows 鼠标及键盘映射的手势语义。

明确取舍：如果新的手势先于 BUS 消费旧 tap 到达，不补发旧 tap，优先保证新手势不夹带旧点击。没有新增排队/重放机制。

## 长按替代下压：可行但本轮未接入

能实现软件上的类似分离，但没有公开证据证明某个长按时长/移动容差就是 Valve 硬件的实现，也没有把用户现有短点按直接替换成长按的充分动态证据。因此本轮保留现有习惯，只给出低 CPU 设计，不插入猜测的时长默认值。

建议的固定大小每指状态机：

- 未决：接触为 true，click/pressure 为 0，只记录源时间、实际接收时间、起点和最大移动距离平方。
- 滑动：超过移动容差后锁定到抬指；之后即使停住或回到原点，也不能升级成长按点击。
- 按压：只允许未决且持续静止的手势进入；原生 click/pressure 在此独立输出。
- 按压后又移动：若目标要求“纯滑动而不是拖拽”，立即释放 click/pressure 并锁定滑动，不维持边按边滑。
- 抬起/取消/配置切换/隐藏/前台许可变化：清理状态并释放输出，不把半途接触升级成游戏点击。
- 新落指：不得复用上一点击的坐标或点击脉冲。

低 CPU 调度：

- Deck 原生路径复用现有 BUS snapshot 节拍，对至多两个触点检查整数 deadline；不增加线程、定时轮询、设备探测、队列或逐样本分配。
- 不额外为长按计算 hypot/sqrt；移动判定可比较最大距离平方与容差平方。避免对现有触觉 Motion 偷换参数。
- 使用源时间区分真实手势时长，同时保留实际接收时间，防止迟到 DOWN 在落到程序的一瞬间就被当作长按。迟到/取消消息不能复活已终止手势。
- 鼠标映射没有稳定读取 native snapshot 的入口，不能声称“在 snapshot 加判定就兼容鼠标”。若后续需要鼠标长按，可用 HWND 所有者线程上的按需一次性 deadline：只有未决接触时才预约，到期即取消；移动/UP/取消提前撤销，关联指针 ID 与手势代次，拒绝旧计时消息。不引入常驻高频 timer。
- 短点按是否保留、长按到底是一次点击还是按住、长按后移动要不要拖拽，是需要明确的体验策略，不应该借“参考 V 社源码”混为一个默认动作。

## 未运行项目

没有执行自测试、动态触摸注入、CPU 基准或编译；没有发送硬件命令，没有启动/切换虚拟手柄，没有修改安装目录。旧任务的回归数字不能作为本轮修改的验证结果。

本轮原文件快照和 SHA256 收据保存在：
`G:\YeManCC-Work\Build\Analysis\ScreenTouchpad-Gesture-Separation-20261013-022330`