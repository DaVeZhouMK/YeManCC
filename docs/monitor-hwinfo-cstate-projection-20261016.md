# HWiNFO C-state 驻留率：裁剪未消费的逐项生成

日期：2026-10-16。继续全 YMCC 唯一数据层与 HWiNFO 上下游优化，保留当前可见读数和 HWiNFO FPS。

## 最终源码行为

`PowerControl/YeManHWiNFO.ps1` 原有磁盘活动组静态裁剪扩展到两个固定家族：磁盘 `F0000101_0..31`、C-state 驻留率 `F0000302_0..31`，总数上限 64。仍在同一个停止/INI 合并/备份/验证/启动事务内执行，没有新增监控入口、采集器、定时器或展示开关。

内部函数统一改名为 SensorBudget，以覆盖两个明确允许的家族；公开 BAT/PowerShell 参数不变。仍以已验证的 HWiNFO 8.54-6090 原始镜像哈希 `e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3` 为门，未知镜像保持原采集。健康运行的 start 仍是无写入、无哈希、无重启的返回路径。

C-state 百分比表示驻留状态，并非当前监控中的 CPU 使用率或核心负载图块；这些项目继续使用现有 Windows CPU 数据。功耗、频率、温度、内存/显存、网络、电池和帧率来源未替换。PresentMon=1、NvClockMeasuring=0、USBC_PD=0 和 AMD SnapshotPolling 不默认启用的既有策略保留，没有关闭 HWiNFO FPS。

## 上游证据的实际范围

固定哈希研究镜像的 RTTI/虚表、初始化返回值和构造循环将 CSensorCState 对应到 F0000302、实例 0..31。测试直接校验初始化返回该标识的原始字节。

隔离模拟器执行原始组设置读取、Enabled 轮询门、C-state 项目方法，以及实际缓存 CPU 状态访问器。访问器仅从已有 CPU 对象取指针；外部字符串格式化/注册表/时钟边界使用桩，不启动研究 EXE、不访问真实硬件。

4 组实例 0、1、7、31 的用例：开启时，12 项驻留率均取出夹具中 43% 的值，发生 12 次缓存对象访问及 12 次标签生成入口调用；关闭后均为 0。该结果证明跳过 HWiNFO 的这一组逐项生成工作。

不能据此声称 HWiNFO 底层 CPU 寄存器采集也被停掉，也不将投影计数换算成 CPU 百分比。源码前轮曾因缺少底层阻断证据保留 C-state，本轮只把已验证的逐项工作收益纳入，不推翻该未证实边界。

## 依赖和数据对照

三个 RTSS 模板逐项检查非空 ReadingName，没有驻留率依赖。只读核查当前 FanHost 的 CPU 温度标签分类器，也没有使用 C-state 百分比；未修改 FanHost。WHEA、内存时序等仍有既存依赖的组保留。

10 组真实 parser/publisher 夹具覆盖目标组位于最前/中间/最后、32 个 C-state 实例、与 32 个磁盘组同时裁剪、零值、NaN、重命名、相邻 System 家族和未验证实例。移除组后重排传感器索引，通过同一缓存所有者再解析，比较所有发布字段和 FPS/LOW/帧时间的源设备身份。

64 项原生检查通过；其中 64 个管理组确实从 71 个输入组中移除、留下 7 个真实消费组，CPU/GPU、内存、网络、Windows 文件读写、电池和 FPS 输入保持原值。25 项实际唯一 hub/model/两个现有输出协议检查验证完整输出及失效规则；Windows 使用率 25% 和核心图块 20/30/40/50 等独立输入保留。两个协议测试不表示重新开放隐藏的 Steam 输出。

## 配置升级和回滚

继续使用原有有界注册表读写和回滚逻辑，只接受两个固定家族/0..31 的完整身份；相邻 F0000301 System 家族、FPS/GPU、路径注入和越界实例均被拒绝。意外注册表类型、进程出现、写前并发修改或写后回读失败都不能伪报成功；可选优化失败仍继续原必要数据/FPS 恢复。

为兼容已部署的磁盘裁剪版本：

- `.ymcc-sensor-groups-original.json` 继续只保存磁盘家族的 32 个首值，旧文件逐字节保留。
- `.ymcc-sensor-groups-cstate-original.json` 保存 C-state 首次实际接管前的 32 个值，后续修复不覆盖。
- `.ymcc-sensor-groups-last.json` 保存本次两个家族的完整原值，供当前事务和调查使用。

这是恢复凭据，不是新的运行配置数据库或监控数据源。每份准备数据最多 16 KiB；没有增加常驻读取。新家族的原始值备份完成前不会写任一个传感器键。45 项策略/完整恢复检查包含旧备份升级、仅新增 C-state 的写入、跨家族失败回滚和新备份失败；7 项实际 Windows 注册表适配器检查只在随机 GUID 根下运行，真实 HWiNFO 设置没有写入，沙箱已清理。

## 未采用的 FPS 进程过滤

官方 HWiNFO 论坛命令行主题把 pm_proc_name、pm_proc_ex 列在 HWiNFO64 Pro 参数中。本轮未将它们加到免费版兼容的默认启动，没有新增授权要求、关闭 FPS 或绕过许可检查。官方页面和决策记录在 `PRO-FILTER-DECISION.json`。这不是对所有免费版运行行为的实际测试结论。

## 验证与交付状态

当前检查：上游逐项生成 4、原生数据 64、消费链 25、固定策略 45、隔离注册表 7、静态采集配置 32、模板/就绪 44、INI 合并 26、周期确认 48、恢复 29、BAT 入口 41，共 365 项；另核查 3 个 RTSS 模板。旧缺陷基线沿用各原轮留存版本。全部相关 PowerShell 回归已对应当前最终脚本。

实际安装 EXE 的只读指纹门验证通过，但这不是实际加载裁剪设置的证明。两个安装 INI 仍各 567 字节、SHA-256 `48cdaf01460262e51a7a01020c72d8e070762af615911790ab2c1fbec6fb40d8`；真实 Sensors 注册表仍为 0 个子键、14 个值。

未部署、未重启 HWiNFO/YMCC/游戏、未新增真实传感器采样、未测 CPU 百分比。没有写 native/main.cpp、两个 GPU 分类优化头或 FanHost，并行修改保留。补丁只包含本轮 PowerShell、相应测试和报告，在隔离目录检查应用与逐字节一致性，不暂存/提交主线。

证据目录：`G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-cstate-projection-20261016/`。主要文件：`CSTATE-EMULATION-RESULT.json`、`CPU-STATE-ACCESSOR.txt`、`CSTATE-DATA-EQUIVALENCE-RESULT.json`、`CSTATE-CONSUMER-RESULT.json`、`DEPENDENCY-RESULT.json`、`regressions/`、`IMAGE-VERIFIER-RESULT.json`、`LOCAL-STATE-UNCHANGED.json`、`PATCH-CHECK.json`、`ROUND-RESULT.json`。

实际上游设置采纳、跨机器完整性及继续减少正常采集/处理工作仍需推进，持续优化目标保持 active。
