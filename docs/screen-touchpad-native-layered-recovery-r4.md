# 原生光标分层表面恢复：policy 9

本轮仅触摸板显示层及对应验证工具；没有修改监控、虚拟手柄报告、触摸输入面、坐标、触觉或Steam配置。用户已允许模拟。本轮进一步使用真实User32/GDI/WinRT，在从未切换为输入桌面的私有测试桌面上操作自有窗口，不注入输入、不启动Steam或游戏、不部署应用。

## 原生验证发现

离线API替身能验证我们的状态机，但不能完整模拟User32内部的分层图像状态。本轮对软件光标窗口实际移除WS_EX_LAYERED及穿透相关样式，生产WM_STYLECHANGED回调会立即补回所需样式并隐藏失效图像。然而在本机这个真实Win32序列里，后续UpdateLayeredWindow持续失败，返回ERROR_INVALID_PARAMETER（87）；单独SWP_FRAMECHANGED也无法恢复。

探针在原样式通知调用链结束后，对同一自有窗口执行一次layered off/on，恢复成功。这个事实只证明可复现的异常样式恢复边界；并没有证明用户原先滑动闪烁一定由样式损坏导致。正常移动和显隐交接仍沿原policy8处理。

## 最小修正

native/screen_touchpad_cursor.h在真正丢失WS_EX_LAYERED时设置cursorLayeredResetPending。即时穿透/不激活保护与隐藏仍保留。下次原有cursorUpload在原样式回调外修复一次分层状态，用UI线程局部busy guard避免自己的样式消息再次递归修复，完成后重新ensureDisplay，再按原路径上传位图。

保持同一个HWND；不重建触摸源、不更改系统光标计数、不注入鼠标、不切焦点。off/on或最终策略校验失败时保留pending及active/retained，沿原补读期限重试，不增加timer、worker或刷新频率。成功后清pending；后续如果只是位图上传失败，不重复重置layered。窗口销毁清掉该窗口的pending。

native/screen_touchpads.h标记cursorPolicyVersion=9，既有状态导出新增cursorLayeredResetPending、cursorLayeredResets、cursorLayeredResetFailures。计数仅在恢复动作发生时变化，没有逐MOVE日志。

## 同一原生测试的Before/After

新增tools/screen_touchpad_cursor_native_surface_selftest.cpp/.ps1。实际cursorSyncInfo接收合成CURSORINFO，窗口、GDI位图、UpdateLayeredWindow、样式消息、WindowFromPoint均为真实Windows调用。没有真实触摸、键鼠或设备输入，也没有系统光标移动/ShowCursor。测试线程恢复原桌面并关闭私有桌面，输入桌面名称前后相同。

同一最终测试程序对旧/新生产头文件：

- Before：25项中的4项恢复相关检查失败，第一次恢复上传error87。
- After：25/25通过；同一HWND成功恢复；1,000次位置提交尝试中996次实际位置-only更新（坐标未变时复用），该阶段0次位图重传；1,000次命中均落到下层自有窗口。
- 两轮各128次形状切换、12轮完整创建/坏样式/恢复/销毁通过；稳态GDI计数19→19，没有观察到随周期持续增长。冷进程计数0、首次完整流程19是保留的原始观察，不用冷基线直接断言泄漏，也不将稳态结果夸成所有资源绝不泄漏。
- 原生控制输入面回归：既有screen_touchpads_mouse_pass_game_switch_selftest，在私有桌面使用两个自有下层进程，64轮、1,038断言、1,600命中点通过。既有输入源不重建、视觉保持禁用/穿透、配置与模式切换正常；两个目标进程最终退出。该用例不是真实游戏/物理触摸验收。

初版测试把冷GDI计数当成稳态基线，已经保留该失败记录并修正为等价完整生命周期比较；原生Before最终仍能复现四项error87恢复失败，未用修改测试掩盖产品问题。第一份Before整合编译因仅回退cursor头而保留新日志字段未通过，随后按匹配的两份头文件重新构建；编译失败不是运行验收。

复跑当前版本：

```powershell
& tools/screen_touchpad_cursor_native_surface_selftest.ps1 -OutputDirectory <新的证据目录>
```

复现旧版可附加-BaselineDirectory指向本轮Before/native并传-ExpectBaselineFailure。runner只接受明确的4项既知失败和error87；不会把任意失败视作成功。

## 限制与证据

本轮未取得实际产品policy9活动手势的现场证据。私有桌面检查原生窗口/图像API及命中行为，合成状态不代表Steam输出；没有验证显示器逐帧扫描、Windows通知覆盖率或掌机实机闪烁变化。CPU恢复路径只在样式损坏后触发；没有声称应用CPU百分比下降。

证据根：G:/YeManCC-Work/Build/Analysis/Touchpad-Native-Display-R4。Native-Final-Before-2、Native-Final-After保存同一测试的源码哈希/退出码/结果，fixed-native-hit.json保存64轮原生回归，native-cursor-repair-probes.json保存FRAMECHANGED无效与回调外reset有效的对照。生产Before及FrozenNative保存原字节。Sources保存微软SetThreadDesktop与UpdateLayeredWindow官方文档，用于核对API边界；实际error87结论来自本地实验，不能外推为所有Windows系统相同。

## 错误注入回归补充

原55项加3个恢复错误场景，共58/58通过。每个新场景分别覆盖active与retained：off失败、on失败保留pending和所有者，下个原24ms预算slot重试；重置成功后上传失败只重试上传，不重复off/on。busy退出正确，恢复内样式写调用深度最大1，后续32帧不再重置。原55项源码与结果保持一致。该模拟不能替代User32内部状态；它与上述真实Windows的成功/失败对照互补。结果位于Simulation/simulation.json及Simulation/verification-summary.json。
