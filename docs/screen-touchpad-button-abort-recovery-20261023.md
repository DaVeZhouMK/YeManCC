# 专用按键同步中止后不显示（2026-10-23）

## 现场事实

样本：`YeManCC-logs-20261023-021009-显示监控开启还是卡鼠标-回补Steam失败.zip`。只分析触摸板/专用键，样本文件名中的监控、Steam 回补等由原任务负责。

| 本地时间（2026-10-23） | 选择/许可 | 输入和图案可见掩码 | 其它状态 |
| --- | --- | --- | --- |
| 01:55:34.909 | Steam Deck 专用键全开，permittedMask=6 | 6 / 6 | error=0 |
| 02:01:52.373 | specialMask=3、permittedMask=6 | 4 / 4 | error=2147500036 (0x80004004)、autoHidden=false、inputTeardown=false |
| 02:01:57.398 | 用户关闭专用键 | 0 / 0 | error=0 |
| 02:01:59.740 | 用户重新开启专用键 | 6 / 6 | error=0 |
| 02:05:30.469 及 02:10:09 导出 | 仍全开 | 6 / 0 | autoHidden=true、error=0：与上一行相隔超过一分钟，是另一种自动隐藏状态 |

位 1 对应 Steam 键，位 2 对应三个点。失败记录表明 Steam 键的输入和图案窗口当时都不可见，三个点仍显示；不是单看旧 visible 字段推断。现场已包含上轮 buttonVisibility、activePersona、appliedPersona 诊断，不以版本旧解释。activePersona=appliedPersona=steamdeck、controlTargetEnabled=true，未观察到终态退出门被错误开启。

该日志不记录引发 E_ABORT 的内部回调，也不能据 inputTeardown=false 反推出历次所有修改都无影响。本轮确认并修复的是明确可复现的失败处理缺口，不宣称已锁定最初中止发生在哪一条 OS 回调或哪一轮提交。

## 已修复的正式消费者

旧 buttonNativeInputSync 对所有同步失败都关闭当前输入源并返回 false，buttonsRefresh 将对应输入/图案 HWND 隐藏；之后没有针对临时 E_ABORT 的恢复。上轮同配置重选只唤醒自动隐藏图案，未检查丢失的原生输入根窗口，因此不能覆盖这条路径。

本轮只改 `native/screen_button_overlay.h` 与 `native/screen_touchpads.h`：

1. 正常仍开启时，E_ABORT 最多额外重建同一按钮输入源一次。每次操作后重读原窗口、当前档位、许可、原生模式及退出状态；其它错误继续保留原失败策略，持续中止也保留错误，不轮询。
2. 新一轮按钮刷新取代旧刷新时，旧轮不关闭后来建立的输入源、不继续显示旧选择。按钮刷新返回完成/失效结果，失效后不继续后半段触摸板显示。
3. 用户重选同一配置时，检查所选按钮输入窗口、图案和原生输入源是否缺失；必要时执行现有刷新，成功后再沿用原有自动隐藏唤醒。健康同配置不增加原生重绑定。
4. buttonVisibility 版本升到 2，按需输出 inputRecovery 的 aborts/retries/recovered/failed/lastError/lastIndex，普通状态查询继续区分输入窗口与图案窗口。计数不代表驱动或游戏消费证明。

未修改共享 Surface、quiesce/shutdown 退出释放、信号多轮确认、监控、虚拟手柄驱动或 Steam 接口。未增加线程、持续扫描、刷新率、MOVE 绘图或 MOVE 定时器重建；额外原生重绑定只发生在本轮处理的 E_ABORT 上，且有一次上限。

## 执行验证及限制

- 47 项内存边界检查：实际提取并运行正式 configure、buttonNativeInputSync、buttonsRefresh、buttonPermitted、buttonsNeedRefresh、buttonVisibilityState 及完整自动隐藏消费者。覆盖临时/持续 E_ABORT、其它错误、重选修复、退出中断、换档、关闭、重入取代、旧计时回调及 10,000 次 MOVE 不重绘/重建计时器。
- 3 组单一负例分别移除中止重试、同配置缺窗修复、上轮显式唤醒：精确命中 3 / 3 / 5 项对应原失败。原失败输出保留，没有将负例改成应通过。
- 23 项真实 WinRT/隐藏桌面检查：在真实 CoreDispatcher.ProcessEvents 中安排普通源关闭，使同步真实返回中止；3 轮恢复、另一按钮持续可用、原鼠标穿透、同配置缺窗修复、退出回调不复活、原句柄销毁、3 轮释放确认及 DispatcherQueue 关闭通过。该回调是受控复现，不冒充原现场回调身份。
- 原退出回归 53 项执行及 5 项源码检查通过。
- 577 个冻结原生输入完整编译通过。编译后并行任务修改了监控头、Decky 所有权/运行时头及 main.cpp（清单在 source-drift.json），两个触摸板产品头及共享 Surface/owner 仍与相应受测输入一致；该编译回执针对冻结候选，不能当作整个并行变化后的当前主线全部验收。
- 持续功能分类：触摸板属于持续功能；本轮只验收上述有界中止恢复和退出用例。完整真实睡眠、强行挂起 COM Close、物理触摸/手柄和游戏消费仍待实机，未进行 CPU 百分比测量。

## 复用入口与证据

已登记 `touch.button-abort-recovery`，模块在 `tools/ymcc_sim/global/touchpad-button-recovery/`，复用上述正式夹具；现场状态摘要在同目录 field-record.json。

定向消费者可直接执行：

```
node tools/ymcc_sim/global/touchpad-button-recovery/run.mjs <全新证据目录>
```

全局登记调用：`tools/ymcc_sim/global/cli.mjs --mode run --test touch.button-abort-recovery --out-dir <目录>`。本轮全局 CLI 在开始该消费者前进行全库登记/盘点，长时间未产出目录内容；已仅停止本任务拥有的该 Node 进程，改为直接执行同一个已登记模块。不将这次全局盘点当作通过或消费者失败。

证据根：`G:/YeManCC-Work/Testing/YMCC/Evidence/Touchpad-Buttons-Abort-Recovery-20261023`。早期编译夹具失败、负例结果不匹配以及修正后的证据分别保留，不覆盖。最终以 RESULT.json 与其引用的回执为准。

交付：修改已在主线；候选仅编译，未运行或部署到安装版，未注入系统输入、未写设备、未切换可见桌面。现场专用键偶发不显示是否完全消除，仍以新候选使用反馈为准。
