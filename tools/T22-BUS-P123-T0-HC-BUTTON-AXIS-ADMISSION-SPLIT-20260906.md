# T22 BUS-P123：HC ButtonState/AxisState 准入拆分修正

日期：2026-09-06
状态：SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / NEUTRAL-ADMITTED / PHYSICAL-SEMANTIC-RECEIPT-PENDING

## 1. Git v0.0.28 对比结论

与 Git v0.0.28（commit 0274412）对比，当前新增的连接中性代码把 g_inputReleaseRequired 同时用于按钮、扳机和摇杆。由于实体 XInput 曾报告 leftY 约为 -32503，gamepadEval 在进入 LB+RB、A/B、LB/RB、页面导航和 gamepadProcessUiInput 之前直接返回。

这解释了“控制器页面显示连接，但全部 YMCC 手柄操作无效”。该统一 barrier 不是 HC 原样行为。

HC 的输入层分开维护 ButtonState 与 AxisState；本修正按该边界拆分，不删除生命周期安全闸门。

## 2. 当前修正

文件：native/main.cpp。

- g_inputReleaseRequired 不再由新连接的漂移轴设置；它继续保留给睡眠/恢复/释放事务。
- 新增 g_gamepadAxisNeutralAdmissionRequired，只阻止模拟摇杆驱动的 YMCC 页面导航。
- A/B/X/Y、LB/RB、Start/Back、D-pad、LT/RT 的 semantic 路径不再被漂移模拟轴挡住。
- 新连接若当前摇杆已经中性，立即记录 admitted-neutral；不需要等待下一次输入事件。
- 若连接时摇杆非中性，只阻止模拟摇杆导航；检测到摇杆回中后记录 axis-neutral-cleared。
- 不添加任意死区，不改写 HC 轴值，不创建虚拟手柄，不启用 HidHide。

## 3. 构建与 runtime 证据

native/build_native.bat → BUILD_OK。
tools/deploy-installed.ps1 → DEPLOY_OK。

新实例 PID 13504 记录：

- gamepad-xinput-state: connected=true, connectedMask=1, primarySlot=0, source=xinput
- gamepad-neutral-admission: status=admitted-neutral, buttonNeutral=true, axisNeutral=true
- gamepad-state-replay: reason=webview-render-ready

这证明当前实例不再停留在统一 awaiting-neutral 阻断。

## 4. 当前仍需真实设备证据

本机没有权限替用户生成真实 A/B/LB/RB 物理边沿；因此尚未把 gamepad-ui-input-emitted 或 YMCC 页面动作标记为 runtime PASS。

当前实例已部署并保持可测试。只需要用户在 YMCC 控制器页实际按一次 A、B 或 LB+RB，回传 native lifecycle 中对应的 gamepad-ui-input-emitted 或 gamepad-ui-input-dropped 记录，即可闭合 T0 semantic receipt。

