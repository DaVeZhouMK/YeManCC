# WinRT子窗口WM_SETCURSOR原生反证 R11（2026-10-15）

## 结论和主线决策

上一轮policy12现场数据证明系统光标0/1快速交替，而该保留片段的YMCC helper始终隐藏；来源尚未明确。本轮优先核验自有WinRT子窗口的NULL class cursor候选，**实际原生复现不支持“子窗口默认处理会清空当前光标”的解释，因此未修改生产childProc、WM_SETCURSOR或光标默认值。**主线仍policy13，保留R10的低CPU和来源取证。

这个结论只针对下面实际执行的消息路径。没有物理Touch/Pen或Steam Input消费，不能扩大成“WinRT/Windows全部路径已排除”或“闪烁已解决”。

## 原生路径和正对照

使用tools/screen_touchpad_setcursor_route_selftest.cpp，在专用STA线程绑定从未显示的私有桌面；真实创建当前生产的WinRT controlled-input root/child、Composition与下层自有进程。基于原R7实际几何夹具保留142项，再加入60项光标路由检查，共202项。

测试临时对子/父窗口加fixture-only subclass，记录WM_SETCURSOR后转发到原来的真实窗口过程；生产默认消息处理没有被替身模拟。通过SendMessage显式分派WM_SETCURSOR（HTCLIENT，触发消息字段WM_MOUSEMOVE），只在该私有UI线程用SetCursor设置测试形状/NULL并随后恢复，不注入鼠标/触摸事件、不移动系统指针、不切换输入桌面、不操作真实Steam或用户应用。

| 路径，四种尺寸均执行 | 结果 | 可得结论 |
| --- | --- | --- |
| 原生产root箭头class + 原child NULL class | 子/父各收到一次消息，已有HAND形状保持 | 没有复现NULL child清空或替换光标 |
| fixture父窗对WM_SETCURSOR直接返回FALSE + 原child NULL | HAND仍保持 | 候选并未在放行父窗后出现 |
| fixture-only child直接返回TRUE | HAND保持，不再通知父窗 | 与原路径相比没有修复已复现的形状损坏 |
| 正对照：仅在私有测试类临时设置child class为ARROW | 同一路径子/父都收到消息，HAND确实变成ARROW | 消息和默认类设置机制确实在运行，不是未投递造成假阴性 |
| 恢复原NULL class，初始光标本来为NULL | 仍为NULL | 原路径保持主动隐藏；候选也保持NULL |

具体运行中的HAND句柄为65567、ARROW为65539；这些仅是本次会话数值，不是永久身份或生产判断条件。正对照恢复class cursor后继续所有原触点/几何/UP/cancel/穿透断言，且配置、触点状态、原前台与私有线程cursor都保持/恢复。

Microsoft的WM_SETCURSOR合同说明DefWindowProc先向父窗转交，父窗返回TRUE可终止进一步处理；NULL class cursor本身并不提供“必然调用SetCursor(NULL)”的证据。官方原文：https://learn.microsoft.com/en-us/windows/win32/menurc/wm-setcursor 。当前源码事实“child class cursor为NULL”不能单独升级为故障。

## 保留失败与证据范围

初始Native-Current夹具有190项，其中8项失败：错误预期原root默认应先强设ARROW、拒绝的父窗之后NULL child应清空形状。实际8组均保持HAND。失败是诊断假设被实测否定，生产未为测试通过改动。

Native-Verified加入正对照，改为断言直接观测的保持行为并验证消息链可设置非空class；202/202。Native-Delivery修正测试元数据，明确确实调用了私有线程SetCursor，不把它描述成“所有cursor API都未操作”。Native-Final为最终精简runner（移除复制来的无关geometry baseline选项），202/202。所有早期数据原样保留，最终以Native-Final和hypothesis-result.json为准。

真实API与窗口线程在本机Windows上执行；光标fallback的GetCursorInfo、前台/输入事件仍沿原夹具适配器，现场可见输出、真实鼠标消息自动命中、Touch/Pen内部默认处理及Steam代码不在覆盖范围。SendMessage能证明指定窗口过程接收和处理指定消息，不能证明现场系统一定会向这个child自动派发相同消息。

## 生命周期、来源与复现

最终报告确认runtimeShutdownCompleted、queueShutdownObservedCompleted、uiWorkerTerminated、privateDesktopClosed、ownedTargetsTerminal、inputDesktopUnchanged均为true。报告systemCursorManipulated=true是为了如实记录私有线程形状测试；privateThreadCursorShapeTests=true、cursorPositionChangedByFixture=false、visibleDesktopSwitched=false同时限制其作用范围。runner严格拒绝私有桌面/运行库/进程未回收或实际输入注入。没有ShowCursor计数操作、指针位置注入、部署、设置/设备变更。

生产8个相关头文件哈希与Native-Final/FrozenNative完全一致，并与上一轮R10中已有的3个生产头文件哈希一致；无本轮生产修改。新增仅tools中的独立研究fixture/runner、本文与tools/SCREEN-TOUCHPADS.md的维护说明。

证据目录：G:/YeManCC-Work/Build/Analysis/Touchpad-Cursor-Route-R11。
复现：tools/screen_touchpad_setcursor_route_selftest.ps1 -OutputDirectory <fresh-dir>。只用新目录，保留已有证据。

下一步仍需policy13现场事件sourceThreadId/observerThreadId及导出时有限PID归属，先判断SHOW/HIDE事件来源，再决定Steam导航路径或自有其他消息路径的局部处理。晚解析线程身份不是历史API调用方证明；若TID已退出/复用仍保留unknown。日志目录最新仍为19:35的policy12包，暂无policy13来源证据。目标仍未完成。
