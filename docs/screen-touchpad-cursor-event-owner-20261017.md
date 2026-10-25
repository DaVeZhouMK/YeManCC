# 触摸板光标事件来源线程：近时只读观测

2026-10-17。触摸板闪烁取证补充，不是显隐策略或闪烁修复。来源代码：`native/screen_touchpad_cursor_visibility.h`。

旧归档只有WinEvent sourceThreadId，导出时再OpenThread常因线程退出而失败。新增 `cursorVisibilityObserver.sourceOwnerObservations` 在现有WinEvent消息被UI线程处理时，读取首次来源TID当时的PID及线程创建/退出时间。记录的是近时线程归属，**不是隐藏光标API调用者、不是事发时历史PID的确定证明**。

## 触发与CPU边界

- WinEvent回调仅记录固定大小POD并复用既有PostMessage；不在回调或普通MOVE中打开线程句柄。
- 每个hook generation最多8个不同非零来源TID，每TID至多查询一次；失败不重试，超限只计数。
- 同一来源后续事件只复用首条观测引用；不意味着线程归属在后来所有事件中保持不变。
- UI消费既有通知时才解析待查项，原有合并通知策略保留。无新hook、线程、timer、轮询、磁盘写入、进程路径查询或Steam接口。
- 来源观测固定16项环，eventTrace固定64项环。stop使旧pending失效，旧通知不会触发新一代查询。查询保存/恢复调用前LastError。

## 字段

`eventTrace.version=2`：在原事件字段上增加 `sourceOwnerObservationSequence`。0表示来源TID为0或该代来源数超限；非0与来源观测的sequence连接。环覆盖后观测可能已过期，不能把当前同号TID补配给它。

`sourceOwnerObservations.version=1` 包含capacity、total、perGenerationThreadLimit、limitHits、queriesPending及entries。每项保留generation、sourceThreadId、首次event/eventTick、回调观察tick、是否已查询、查询tick、PID、OpenThread/PID与thread-times错误、线程创建/退出FILETIME。新观测中的两个FILETIME以十进制字符串输出，避免前端JSON数字精度损失；旧lateResolution字段不改类型。

`eventToObservationMs`和`observationToQueryMs`用于看投递/派发延迟，DWORD差值按现有32位tick约定。未查询时后者为null。TID可以在事件产生与查询之间退出或复用，墙钟也可能调整；不据这些字段自动修改输入或宣称历史身份绑定。

原 `cursorEventThreadOwnersAtExport` 仍明确属于lateResolution，不能覆盖或反向认证这些近时观测。`historicalEventProcessProven=false`、`cursorApiCallerProven=false`是有意保留的证据边界。

## 本轮验证

MSVC C++20对包含实际生产header的既有离线替代宿主完成 `/Zs` 编译检查。另以实际生产header配假API执行10,003次回调的有界检查：回调OpenThread次数为0；同代查询上限8；旧代/stop后通知不查询；新代允许同号TID重新查询；错误与LastError保留、FILETIME字符串精度通过。该检查程序没有User32/GDI32及线程查询API导入，未使用真实窗口、hook、线程句柄或输入，不是实机功能/CPU测量。没有调整触摸报告、鼠标输出、刷新率、焦点、穿透或光标显隐决策。尚未部署，旧日志不会自动出现新字段。
