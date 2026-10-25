# 触摸板 CPU 调用与 SD 光标模拟修正

用户已澄清：本聊天仅负责屏幕触摸板和触摸按键，监控属于另一任务。本轮监控探索已交接给“监控”聊天；误入范围的自身代码变更按精确片段撤回，撤回当时与本轮前快照一致。后续监控代码由该任务自行维护，本轮交付不含监控修改。

## CPU 调用：前台抑制仍然执行，但不重复清理空状态

旧 handleButtonPointer 在 YMCC 自身前台且使用虚拟目标时，对每一个 MOVE 都调用 buttonsReleaseMapped。即使映射早已清空，仍取得 standaloneMutex、推进 standaloneGeneration 并清零原子状态。实际完整头文件模拟中，连续10,000次同手指MOVE产生10,000次重复撤销。

新增 buttonsHaveMappedWork 检查映射mask、standalonePending、非Idle状态、全部contact和armed（包括没有映射bit的Y/summon）。只有仍有工作时执行原撤销；无论是否执行都从原前台抑制分支返回，不能进入映射派发。触摸反馈在此分支之前处理，仍会发送按下脉冲及抬指释放。生命周期release路径保持原样。

修正后同样10,000次MOVE产生0次重复撤销。模拟还验证首次从游戏回到YMCC时立即撤销一次、pending/status仍取消、其他手指不误释放、正常键鼠变化仍输出。这里测得的是取消调用和代际变化次数，未测整机/应用CPU占用百分比。

## SD 显示：两个实际代码反例

1. 系统光标已成功接管、软件补画已隐藏后，后续 GetCursorInfo 返回 SHOWING 且形状句柄为空。旧代码重新显示缓存补画；新代码在纯Deck且补画已隐藏时保持隐藏。原本已可见的帧、SUPPRESSED时复用缓存和Mouse分支保持原行为。
2. 正常UP的 GetCursorInfo 偶发失败，旧代码直接cursorStop，清掉已有补画和恢复机会。新代码仅在纯Deck已有有效、可见、已上传的补画时进入retained，沿之前已有的24ms截止时间继续复查；失败这一刻不新显示、不上传、不移动。取消、无补画和Mouse失败路径保持原行为。

光标策略日志标记升为 cursorPolicyVersion=8。没有新增刷新频率、坐标平滑、强制系统显隐、假鼠标输入或焦点切换。持续查询失败仍可能暂时保留滞后的最后一帧；只在重新读到有效当前状态后完成交接。不能把本轮逻辑修正当成所有实机闪烁已消除。

## 模拟和回归

| 项目 | 方式 | 结果 |
| --- | --- | --- |
| SD光标 | 同一模拟程序直接包含Before/After实际cursor及visibility头文件，Windows API为替身 | 修复前49项中5项失败，分别属于上述两问题；修复后49/49通过 |
| 按键CPU | 实际完整screen_touchpads头文件，输入/前台/窗口范围API替身，无窗口创建 | Before与After各16项；重复撤销10,000→0，其他保留语义通过 |
| 事件时间 | 既有screen_touchpads_event_time_selftest，纯事件与虚拟时间 | 10/10通过 |
| 震动资源 | 既有screen_touchpads_feedback_lease_selftest，模拟资源/写入器 | 44/44通过 |
| 完整cursor上下文 | 既有cursor selftest隔离编译 | 编译通过；未运行真实窗口程序 |

SD模拟程序未导入User32/GDI32/DWM或设备输入DLL；按键程序为包含完整头文件需要链接User32等库，但实际前台/输入调用均替换，未创建真实窗口、未注入真实输入。回归不启动手柄、不访问真实震动设备。没有部署或修改用户设置。

新增模拟工具：

- tools/screen_touchpad_cursor_r1_simulation.cpp / .ps1：需要新的OutputDirectory；可通过CursorSourceDirectory指定之前头文件。
- tools/screen_touchpads_button_cpu_simulation.cpp / .ps1：需要新的OutputDirectory；可通过ButtonSource与ExpectBaseline复现旧取消次数。

## 留档

主交付目录：G:/YeManCC-Work/Build/Analysis/Touchpad-CPU-Simulation-R1。
SD模拟原始目录：G:/YeManCC-Work/Build/Analysis/Monitor-Touchpad-CPU-Simulation-R1/Touchpad。该目录名来自用户澄清前建立的工作目录，其中Touchpad部分只包含触摸板证据；监控部分已另行标记撤回并交接。

生产改动仅native/screen_touchpad_cursor.h、native/screen_button_overlay.h，以及native/screen_touchpads.h中的版本号。先前触摸事件/按键配置与命中复用、光圈查询减少、policy7显隐观察和InputHost低分配改动继续保留。新增修正不更改震动强度、间隔、触点坐标、Steam映射和鼠标穿透策略。
