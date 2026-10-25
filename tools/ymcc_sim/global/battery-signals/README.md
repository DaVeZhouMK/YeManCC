# 电池充电限制全局模拟

统一入口：`pwsh -NoProfile -File tools/ymcc_sim/ymcc_sim.ps1 -Domain global -Mode run -Tests battery.charge-workflow -EvidenceDir <新目录>`。

沿用既有模拟器、`native/BatteryHostTests`、正式组件和本机已缓存工具；不生成掌机验证包、不修改安装版。构建使用独立证据目录和空本地 NuGet 源，缺依赖时保留失败日志，不自动下载或安装。

执行分为四段：

- 原测试源码保持原样编译，真实 Controller、Policy、RuntimeContract 及厂商适配代码执行；电源、存储和设备端口使用原有 Rig/Fake/MsiFixture。覆盖58组策略、身份、写入失败、归还、逻辑睡眠/恢复和参数案例。
- `NativeBoundary.cs` 只加入测试程序集，拦截测试/产品/低层库的目标 P/Invoke，拒绝反例必须实际退出2；正常组目标调用尝试必须0。临时文件只在本轮隔离 TEMP，生产 Main 未执行。
- 原浏览器用例运行正式 BatteryChargeSettings.vue、batteryCharge.ts，验证24个状态/操作场景及四个公开命令的35000ms预算。Vue/TS hash取自编译实际读取的字节；页面和自建服务器均关闭后才写最终回执。
- 新增同一 Controller 的实时链路：正式组件 → 正式 bridge → 测试 HTTP/stdio 端口 → 从当前生产 Program.cs 原样提取并编译的 dispatch/Restore → 真实 Controller。后台仍用原 Rig 的内存端口，没有第二套限充/恢复模型。6个联动场景覆盖明确应用、旧fence拒绝、无电池门、丢失确认后不自动重放、ASUS仅ACK、软件限充逻辑挂起/恢复/归还及重复restore不写。

`extract-dispatch.mjs`只提取当前正式switch和Restore方法；生成物与原始片段hash写入dispatch-source.json。发现源结构不支持或编译失败即失败；不退回手工维护的替代switch。协议仍执行正式RuntimeContract.Validate。真实命名管道、生产Main的父进程/单例/电源事件和物理EC/WMI均不在此入口验收范围。

主要回执：result.json、controller-boundary.json、dispatch-source.json、ui/result.json、ui/live-controller/result.json。后者记录请求/反馈、实际测试DLL、目标native守卫及自然退出。所有实际输入是source-derived-fixture；不是掌机物理读回。原失败保留：首次守卫在模块初始化期退出导致有界超时，改为让原Main自然结束；该工具失败未当产品通过。

对HC无运行依赖；BatteryHost源码、组件、合同或已使用库变化后由同一入口重建并回归。每轮使用新证据目录，旧源码/产物身份不能给新候选放行。
## G3E 充电卡片刷新（2026-10-22）

充电卡片在设置页当前设备分区固定显示，已知不支持/无电池给出明确提示；读取失败不能当作硬件不支持。可见时正常15秒读取一次，后台/原生窗口隐藏/逻辑睡眠暂停；shown/restored/maximized/summoned及native focus恢复时只读刷新。复用正式uiLifecycle的文档/窗口可见性，电源代次由组件检查，充电读取不绑定其他风扇/输入恢复结果。

临时读取失败的旧实现会停止15秒刷新，已由正式组件负例复现。现在每次临时故障最多按1.5/5/15秒追加三次只读尝试，耗尽后静止；前台/电源/重新进入可续开预算。后台查询不应用、不确认未决事务、不重放限充写入；未应用的下拉选择不会被刷新覆盖。软件限充的主机所有权继续由Controller处理。

同一浏览器入口新增实际组件与虚拟时钟用例，涵盖35组独立页面案例；全链路入口另含实际Controller JSON联动及原控制逻辑，当前60组控制测试、36组页面测试和6组实时联动通过。需随当前源码重跑，不将这些数量视为未来固定合同。UI隐藏、反复停用、未决读的迟到回调及完全卸载均须不恢复轮询；页面固定显示不能使不支持设备反复读取或新增设备写入。此处只验证界面订阅/定时器与既有Controller的软件边界，不宣称真实Host父进程/命名管道销毁或实物充电截止已验收。

现场证据及最终源码验证位于 `Build/Validation/Battery-Charge-G3E-20261022/`。现有全页导航器覆盖实际SettingsView -> BatteryChargeSettings、正式手柄engine，720/800两种比例；本轮测试快照228状态、18872方向、422动作检查通过。快照结束后有无关SteamAppearanceCard并行修改；充电消费者/父设置页/engine等仍匹配，但不得复用该整页回执批准整个新主线导出。未自动部署安装版。
