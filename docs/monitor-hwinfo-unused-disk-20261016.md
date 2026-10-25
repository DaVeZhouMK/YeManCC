# HWiNFO 未消费磁盘活动组：减少上游重复查询

日期：2026-10-16。承接 `monitor-hwinfo-unused-clock-20261016.md`。本轮以已验证的实际数据依赖裁剪上游工作，保持 HWiNFO FPS、普通 CPU/GPU 和原有 Windows 文件系统读写来源。

## 结果与适用范围

`PowerControl/YeManHWiNFO.ps1` 已在原有恢复事务中加入磁盘活动组静态配置：确认停进程、完成 INI 合并后，备份并设置对应组的 `Enabled` DWORD 为 0，回读验证，再进入原来的单实例检查和启动/新鲜度确认。健康运行的 `start` 仍直接返回，不做镜像哈希、注册表准备或重启；监控刷新和玩家显示开关不触发这条配置路径。

目标严格限定为当前核验 HWiNFO 镜像的 `F0000101_0` 至 `F0000101_31`。本机 8.54-6090 原始 EXE 与前一轮取得的官方压缩包 EXE 同哈希：`e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3`。版本号相同但内容不同也不应用；未知镜像保留原采集。原有 HWiNFO 路径适配和其他数据恢复流程不受此版本限制。

没有公开新增 AMD 或磁盘采集技术开关，没有默认强开 SnapshotPolling，没有关闭 PresentMon。此前已修正的 HWiNFO FPS 优先、RTSS 缺失救援逻辑没有改变。未新增采集器、传感器读取、线程、刷新定时器或高频日志。

这会停用 HWiNFO 自己的这组磁盘活动读数。当前 YMCC 监控读写输出来自现有 Windows FileSystem 计数器，两个 RTSS 模板的读写项也直接绑定该计数器，第三个模板没有对应 HWiNFO 磁盘依赖；因此当前消费项不需要该组。

## 上游证据

核验使用原始镜像同源的解压研究副本，未执行研究 EXE 或改动安装 EXE。RTTI/虚表与初始化代码将 `CSensorDiskActivity` 对应到 `F0000101`；构造循环覆盖 0 至 31 实例。实际偏好读取器用 `%08X_%d` 形成组键、从 `Enabled` 读取 DWORD，并将值装入组的采集状态；`Show` 是另一条显示设置。

在隔离指令模拟器中执行原始组键构造/读取、组轮询门和实际磁盘轮询函数，所有注册表、格式化、时钟与设备访问边界都是桩。开启时每轮走到一次控制码 `0x70020` 的查询，关闭时该次查询为 0；C++ 夹具另用 Windows SDK `IOCTL_DISK_PERFORMANCE` 常量编译断言相等。8 组用例覆盖开启、关闭、旧非零值、缺失项、查询错误，以及实例 1、7、31。

这证明受测稳态轮询分支被跳过，不代表启动时不再探测磁盘，也不代表整个进程的 CPU 降幅。实际安装生产者加载新设置后的执行及跨版本支持仍未实测。本轮网页查询未返回可引用正文，主要证据为已固定哈希的本机/官方同源镜像和 Windows SDK。

## 数据与配置检查

9 组原生用例重建真实 SM2 传感器表，移除目标组并重新编号，再经过当前实际解析器、同一缓存所有者、GPU 选择和发布函数。覆盖磁盘组位于最前/中间/最后、32 个实例、有效零值、NaN、用户重命名、相邻家族和未核验实例。逐项比较所有发布字段及 FPS/LOW/帧时间的来源设备身份，避免把索引重排误判为来源改变。55 项检查通过。

输入的 HWiNFO 磁盘读数为 999/888，Windows 文件系统输入为 123.5/45.25；输出断言仍为后者，CPU/GPU、内存和网络也断言已知非空值。21 项实际唯一监控 hub、HUD model 和两个现有协议检查验证完整输出一致及原有失效时间。两个协议的隔离检查不表示重新开放隐藏的 Steam 输出选项。

三个 RTSS 模板逐项检查 ReadingName 与 Windows 计数器绑定，没有该 HWiNFO 组依赖。另只读核对现有 FanHost 温度分类器，仅消费 CPU 温度标签；未修改或重建其他任务维护的 FanHost，不将源码核对说成风扇实机验收。

35 项实际 PowerShell 策略/完整恢复检查覆盖固定家族边界、未知/同版本不同镜像、真实大写带连字符哈希格式、关闭/非规范 RememberPreferences、备份、回读、写前/写后异常、并发设置、新实例出现和可选优化失败时继续恢复必要数据。6 项注册表适配器检查只将固定根替换成随机 GUID 沙箱，其余适配器代码保持原样；实际 Windows 64 位注册表 API 验证 DWORD 零值/负值、缺失值、错误类型和标签/子键保留。沙箱清理完成，未写真实 HWiNFO 注册表。

备份使用现有原子备份函数，保存原始和最近一次值到 INI 同目录的 `.ymcc-sensor-groups-original.json` / `.ymcc-sensor-groups-last.json` 后缀文件，每份准备数据限制为 16 KiB。它们是恢复凭据，不是新增监控数据源或运行时配置数据库。读取只访问固定组的 `Enabled`，不枚举整棵注册表或该组其他值。并发修改不会在回滚中被覆盖；生产者已重新运行时不再写设置。失败状态明确为未确认，不伪报已优化。

## 回归与交付状态

当前检查：指令 8、配置策略 35、隔离注册表 6、原生数据 55、消费链 21、采集配置 31、模板/就绪 44、合并 26、周期确认 48、恢复 29、BAT 入口 41，合计 344 项；另核查 3 个 RTSS 模板。旧缺陷基线来自原始留存版本，当前回归结果已对应本轮 PowerShell 哈希。

实际镜像的只读验证发现并修正了开发中的哈希格式不一致；随后补上正例和同版本不同内容反例。并发隔离测试还暴露了测试报告依赖 Get-FileHash 自动加载的问题，报告已改用 .NET 哈希并补跑成功；初始失败记录保留。上述均已修正，不是已部署产品故障。

本机两个安装 INI 各 567 字节，哈希仍为 `48cdaf01460262e51a7a01020c72d8e070762af615911790ab2c1fbec6fb40d8`；真实 Sensors 注册表仍为 0 个子键、14 个值。未部署，未重启 HWiNFO/YMCC/游戏，未新取真实传感器样本，未做 CPU 百分比测量。本轮只完成源码与隔离验证，不能声称本机已降低占用。

未写 `native/main.cpp`、Windows FileSystem 读取器、FanHost 或 RTSS 模板；它们作为依赖核对。补丁仅包含本轮 PowerShell、针对性测试和本报告，在隔离目录检查应用与逐字节一致性，不暂存/提交主线。

证据目录：`G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-group-plan-20261016/`。主要文件：`DISK-GROUP-EMULATION-RESULT.json`、`DISK-GROUP-RESULT.json`、`ISOLATED-REGISTRY-RESULT.json`、`DISK-DATA-EQUIVALENCE-RESULT.json`、`DISK-CONSUMER-RESULT.json`、`DEPENDENCY-RESULT.json`、`regressions/`、`IMAGE-VERIFIER-RESULT.json`、`LOCAL-STATE-UNCHANGED.json`、`PATCH-CHECK.json`、`ROUND-RESULT.json`。

实际生产者采纳设置、其他版本以及继续按全 YMCC 依赖裁剪上游传感器组尚未闭合，持续优化目标保持 active。
