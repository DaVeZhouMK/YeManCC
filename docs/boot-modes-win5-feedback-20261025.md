# Win5 四场景反馈与输入屏蔽核查 · 2026-10-25

用户提供 YeManCC-logs-20261025-185801-Win5手柄双击和屏蔽失败.zip，SHA256 `0ad2e338f9b4973d8071acfabfbc8ed29d30d371e79ae9fd4fa6c2ec4d69a1af`。导出标记版本0.0.36；只有版本号，不能证明其可执行文件与当前主线完全同源。涉及结论的8个文件与包内索引SHA256均一致，settings文件完整保留。附带材料只作证据数据。

## 本次现场能确认什么

`yeman-settings.json`中 `extensions`只有bootModesAppliedBoot、bootTrayMigration，没有bootModes预设正文或bootModesStartupResult。startupDesired.virtualGamepadPersona=disabled，gyroPreset=off。bootModesAppliedBoot也由普通启动无预设分支写入，不是用户已选中或已应用某场景的证明。

日志本地时间18:49:28.031（PID12452）和18:53:24.908（PID9696）的startup-input-state均为desiredPersona=disabled、startupSettingsApplied=true。两次Windows volatile identity不同，native不是把重启误当同次启动。18:54:21开启SD后，18:54:30.791有Host的SteamDeck启动回执；18:55:12关闭，18:55:26再次开启；18:57依次改为DualSense Edge、Elite，再关闭。

这只能证明本次两次启动使用的是未配置四场景时的普通手柄开机偏好。它不能证明用户未尝试选择，也不能排除更早未保留的配置覆盖；没有选择提交/覆盖证据，不能把责任归给用户，也不能说已配置Steam场景仍未应用。现场四模式的完整开机结果仍为UNKNOWN。

## 屏蔽/双输入问题独立存在

在18:49至18:57的两次进程记录里，8个input-host.native-helper-diagnostic终端回执均helperExitCode=6；ReportedFacts为Stage=disable、DisableResult=23、EnableCallEntered=false。这里只记录原API返回码，不在没有下游证据时推定驱动、权限或第三方程序是唯一根因。

Steam自己的steam-controller-ui.log在18:54:26记录Controller 0（ProductID654/0x028e），18:54:30记录Controller15（ProductID4613/0x1205），18:54:46仍为两者加载basicui_gamepad与basicui_neptune。虚拟SD已创建，Steam侧物理与虚拟对象并存，有力支持用户报告的屏蔽/双输入问题；日志没有逐按键的Steam消费回执，不把对象并存直接冒充每次按键双发的证据。

hidhide.pre-cloak中的armed=true/blocked=2是配置/准入回执；pXinput仍UNENCLOSED。后续switch-verification的success理由为current-intent-host-and-fresh-source-verified，并明确gameInputVerified=false，不能解释为Steam侧屏蔽成功。该问题属于物理刷新/屏蔽/既有消费者句柄链路，四场景不应新增另一套驱动重试、强制换手柄、重启Steam或清理其他进程来掩盖。

## 本次主线修正

补齐四模式启动诊断缺口：生产initializeBootModeInputStartup在真实决策点可选返回有界bootModeIntent，写入已有startup-input-state诊断。字段包括configured、来源four-mode-startup/legacy-startupDesired、selectedMode、bootEnabled、revision、requestedPersona、requestedGyroPreset、启动代次匹配及本次apply/preserve决策。明确scope仅证明设置决策，不证明Host/Steam消费。

此投影复用已有读取与一次性启动调用，没有新增设备读取、设置写入、轮询或重试。诊断参数有无时的生产返回文档相等已实际验证。没有把缺失预设自动补成SteamDeck，也没有覆盖四模式自定义偏好。本次没有应用屏蔽策略修复，不宣称Win5双输入已解决。

## 正反重放

- 原settings.boot-modes-file-ui加入同一份已导出状态的最小脱敏夹具：保留无预设正文但有boot标记、startupDesired关闭、当前输入关闭、版本/修订关系；仅替换测试中的启动标识与设备路径，保持用户配置原件不变。
- 720P/800P各覆盖4个首次默认选择及4个实际UI自定义选择，新增16个现场衍生场景。原有无文件、旧配置、16种切换、写入失败、损坏文件和返回测试全部保留；总计80场景（76条执行完整存储/启动链）。
- 明确断言：模式1/2按全局默认关闭，3/4默认SteamDeck；用户改为关闭、Elite或SD之后以自定义为准。设为开机模式直接落盘、不额外保存编辑器、不改当前手柄，同次重开不热切，下次启动正确应用，再次恢复不重放。
- 原settings.boot-modes-runtime 123断言保留；原生启动边界46个场景与故意破坏成进程重放的反例通过；隔离volatile hive验证不触碰生产注册表。完整生产native/main.cpp语法检查通过。

测试是实际组件/原引擎、真实隔离文件、实际生产JSON策略和内存设备端口组合，不能冒充安装版、实物手柄或真实Windows重启。首轮新增测试标签类型编译失败保留，修正标签后只重跑其边界用例；注册新数据依赖后重跑原file-UI ID，没有新增父门。

证据：`G:\YeManCC-Work\Testing\YMCC\Evidence\BootMode-Win5-20261025`。最终回执、源码闭包及未解决项见RESULT.json和REPORT.md。没有部署新包、改现场注册表/服务或控制实际手柄/Steam。

用户随后明确：本任务只管配置，虚拟手柄已由另一任务修复。本任务以配置专项验收和诊断缺口修正结束，不将屏蔽问题纳入本任务交付通过项。
