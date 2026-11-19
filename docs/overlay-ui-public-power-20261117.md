# 悬浮 UI、CPU 重复工作与公共睡眠协调

本轮接续 `overlay-write-storm-20261117.md`。产品源已改，原生完整编译链接、前端构建及类型检查通过；未部署运行新产品。

## 生产链路

悬浮展示继续使用原 UI 线程、原浏览器与原呼出/导航/隔离机制。

1. `presentation_power_scope.h` 是原 `SharedPowerV1` 的展示投影，调用原 `sleep_power_observation.h`，不创建电源状态或系统监听。展示准入要求已知 Ready 且没有待处理睡眠通知，跟设备/硬件写入权限分开。
2. `game_overlay_surface.h` 入场捕获一次公共观测；原输入绑定和可视探针都验证同一展示上下文。普通路径复用原 `gameOverlayBindingCurrent`，避免同一处重复读取公共快照。
3. 原 `projectSharedPowerLifecycle` 的一次性消费者分派增加 `ui-presentation`。同代 pending、非Ready、未知，以及睡眠 serial/通知序列改变，会撤销旧浮层；只看到合并后的最终 Ready 也不能保留旧会话。元数据 publication 更新不重建展示，Ready 不自动呼出。
4. 监控和通知仍使用原公共投递；相同准入/撤销身份的元数据更新不重复唤醒采集或刷新两个显示载体。首次未知仍投递一次，真实变化仍投递。
5. `uiLifecycle.ts` 的展示 Ready 和原生展示投影保持相同区分：设备 `inputReady` 或 `hardwareWritesAllowed` 不足不等于系统未Ready，但 `notificationPending` 必须暂停展示活动。
6. `scheduler.ts` 订阅同一个现有公共电源源。周期任务默认在系统非Ready时暂停，`pauseWhenHidden` 仍独立控制普通隐藏窗口。没有新电源读入源。已开始的事务保留原所有者/取消机制；离页或注销不会在调度器中取消它。任务恢复保留原周期相位，不补跑历史槽。

公共电源源本身已有独立的观察/恢复时钟，不依赖这些周期任务，因此不增加第二个电源读取器或任务绕过选项。

## CPU 与去重

原同进程 SHOW/REORDER 回调逐条执行遮挡扫描。现在回调只标记待检查，在原合并消息通道检查最终窗口顺序。没有新增常驻 timer、线程或轮询。

固定7批、每批5000个相同窗口事件，成功的 Windows 操作仍进入自有屏幕外窗口：

- 修前每批5000或5001次遮挡扫描，七批35001次；每批实际当前线程 CPU 约156.25–187.5ms，墙钟约159–191ms。
- 首次修后每批1次扫描，七批7次；每批墙钟约0.29–0.73ms。线程CPU增量低于该 Windows 记账粒度，记录为0，不能解释成零CPU。
- 最终25组载体回归再次验证每批最多1次扫描、事件队列合并及终态监听归零。

这些是同一个事件路径的实测，不是整应用、整个UI线程稳态或整台机器的 CPU 百分比。前端睡眠测试采用逻辑时间，证明无效周期工作消失，不能当作实机CPU计时。

## 结果和复现

证据目录：`G:\YeManCC-Work\Testing\YMCC\Evidence\OverlayUiPower-20261117`。

|内容|最终证据|
|---|---|
|25组原生载体与真实事件路径计时|`carrier-final/REPORT.json`；首次修前/修后为`peer-before`和`peer-after`|
|20组显示生命周期、原呼出入口、真实公共投递函数及失败组合|`surface-final/REPORT.json`|
|96轮四路竞争/288000次事件，另32轮四路实际消费者电源循环|`four-layers-power-final-2/REPORT.json`|
|16组实际公共源+UI生命周期+调度器：睡眠、隐藏、事务、相位、异常和释放|`ui-scheduler-final/REPORT.json`|
|原27组UI电源恢复|`ui-power-regression/ui-power-recovery.json`|
|原13组58项输入入口及3种故意破坏逻辑的负向控制|`input-entry-regression/RESULT.json`|
|实际仓库400并发写入、失败/取消/超时恢复|`settings-regression/REPORT.json`|
|完整三翻译单元原生编译/链接与私有符号|`native-final/REPORT.json`|
|完整前端构建、类型检查|`web-final.log`、`typecheck-final.log`|

主要命令，均在仓库根目录执行，输出必须为新的绝对目录：

```powershell
node tools/game_overlay_storm_win32_selftest.mjs --out G:\path\carrier
node tools/game_overlay_surface_storm_win32_selftest.mjs --out G:\path\surface
node tools/overlay_layers_storm_win32_selftest.mjs --out G:\path\layers
node tools/ui_presentation_scheduler_power_selftest.mjs --out G:\path\scheduler
node tools/ui_power_recovery_selftest.mjs --ui-consumers-only --out-dir G:\path\ui-regression
node tools/overlay_storm_native_compile.mjs --out G:\path\native
```

本轮修前失败留存：`peer-before`；`public-projector-monitor-before`中的监控元数据重放；`ui-scheduler-before-2`中的四种原周期/展示缺口；`ui-scheduler-temporal-1`中的新订阅失败残留边界。其他早期失败是夹具适配，包括显示指针排序端口补齐、私有窗口夹具需使用与主线相同的触控板编译标志。

原27组UI恢复、实际新16组调度场景和输入入口回归已执行；没有把旧 `core_scheduler_cpu_selftest.mjs` 的历史整套结果冒充成本轮执行。

四路共同测试使用真实窗口栈、监控指针接收、触控板 owner/visibility、浮层载体；公共投递函数测试中的其他业务消费者是明确的无副作用端口，浏览器挂载/COM有受控端口。没有实际系统睡眠、设备输入注入或游戏消费验收。整机CPU降幅、长期运行及独占全屏像素仍需实机确认。
