# CPU 计数器源头分配优化与本机验证（2026-10-16）

本轮减少 YMCC 既有 Windows CPU 采集路径的重复分配与复制。继续使用同一次 `NtQuerySystemInformation(SystemProcessorPerformanceInformation)` 的结果，不增加采集器、线程、定时器、硬件 API 或读取频率。总占用仍来自同一组核心计数器；失败时保留原有 `GetSystemTimes` 兜底。HWiNFO 上一轮策略继续生效，Presented FPS 未关闭。

## 变更

- `native/monitor_cpu_deltas.h` 新增接受已验证、定长索引读取的 `acceptIndexed`。回调在编译时要求不抛异常，每个有效计数器只读一次；在原差分循环中提交下一轮历史，省去临时 Times 向量和循环后的整组复制。原 `accept(vector, stamp)` 兼容入口保留。
- `native/main.cpp::nativeCpuUsagePct` 保留完整快照的非负校验及 48 字节 ABI/长度检查。全部校验完成后，直接将已取得的 Windows 计数器交给差分计算。1–128 个逻辑处理器的统一数据上限保持不变。
- 仅请求总占用或走总占用兜底时，不再创建无消费者的核心比例向量。
- 核心 JSON 数组按真实数量一次预留容量，避免逐项增长时重复搬运。序列化数值、四舍五入、空值与 slot 顺序不变。

无效输入、首帧、核心数量变化、计数器回退、计数器溢出、零增量、超过 6 秒的样本间隔以及 API 切换仍按原规则处理，不把未知当作 0% 占用。FPS-only 需求仍完全跳过 CPU 采样。

## 可验证收益

用真实生产函数与差分头、确定性 Windows API 替身统计分配，未测 CPU 时间或进程 CPU 百分比。每组 300 次稳定采样：

| 路径 | 优化前分配 | 优化后分配 |
| --- | ---: | ---: |
| 1 个逻辑处理器并输出核心 | 1200 | 900 |
| 16 个逻辑处理器并输出核心 | 3300 | 900 |
| 32 个逻辑处理器并输出核心 | 3900 | 900 |
| 64 个逻辑处理器并输出核心 | 4500 | 900 |
| 128 个逻辑处理器并输出核心 | 4800 | 900 |
| 原生计数器只输出总占用 | 600 | 0 |
| API 失败后的总占用兜底，传入核心接收器 | 900 | 300 |
| API 失败后的总占用兜底，不输出核心 | 600 | 0 |
| NtQuery 入口不存在，走总占用兜底 | 900 | 300 |

以上仅是被测采集函数的分配次数，不表示整个 YMCC 或 HWiNFO 的 CPU 降幅。正常输出路径仍需要创建最终 JSON 核心数组及差分结果；没有用新全局缓存改变其所有权。

## 校验

`node tools/monitor_cpu_counter_source_selftest.mjs` 比较冻结的优化前真实函数与当前函数，累计 10,785 次断言、2,763 次原生返回包及 Windows API 调用次数对拍、2,000 组纯计数器序列对拍，全部一致。覆盖了非负校验失败、长度不合法、超过上限、拓扑变化、部分核心回退、时间戳相等/回退/缺失、休眠间隔、真实零占用、总占用兜底与空输出指针。分配测量先预热，输出对象的比较与报告生成位于测量之外。

紧凑的历史生产函数/差分头位于 `tools/fixtures/monitor_cpu_counter_before_20261016.inc` 与 `tools/fixtures/monitor_cpu_counter_deltas_before_20261016.h`，在无历史 Build 目录的新输出目录运行亦通过相同对拍。

现有 `tools/monitor_cpu_measurement_selftest.mjs` 另通过 447 项原生采集/发布检查和 11 项统一数据入口、有效期与显示协议检查。上一轮停更保护另通过 22 项独立回归。以上不涉及真实 CPU 占比压测。

## 本机小更新与实测

完整原生宿主基于 249 个冻结输入编译链接成功，原 manifest 已嵌入。本轮仅更新 `C:/SOFT/YeMan/YeManCC/YeManCC.exe`，部署后 SHA-256 为 `b0ecb53f76f87a97d99819ae75e04d7d5d194dc588341cab3242c815c34ef4d8`。HWiNFO BAT、PS1、配置与游戏均未更新；HWiNFO PID 始终为 28552，INI 哈希保持一致。已备份原 EXE 和统一设置。

在已运行的 Cyberpunk 2077 下检查 40 条统一监控快照：

- 32 个逻辑处理器槽位在 40 条中全部保留。
- 第 1 条没有差分基线，因此核心为空值、总占用无效；后续 39 条总占用与全部核心有效。
- 无越界、部分核心失效却声称全局有效、时间戳错配或首帧后的持续空值。
- 40 条 HWiNFO 健康；生产者时间有 16 个不同值，继续推进。
- 帧率返回包中 36 条使用 `hwinfo-presented`，1 条为既有 RTSS 补位；其余不产生帧率包。数据证明 Presented 优先关系保留，不把未产包伪装为 FPS 0。
- 测试后恢复原 `domainLogs` 区域，主配置和备份配置均恢复为详细日志关闭。最终正常启动 PID 为 53204。

编译期间其他任务继续修改 `main.cpp` 的非监控段。冻结输入、当前监控采集函数及差分头分别校验一致；其他任务的源修改保留，没有操作暂存区。当前安装版包含本次冻结时的并行修改，不宣称包含冻结之后的全部其他任务修改。

证据目录：`G:\YeManCC-Work\Build\Tasks\YMCC-Steam-HUD-Probe\cpu-counter-source-pass-20261016`。

- `FINAL-RESULT.json`：部署、源绑定、数据与收益汇总。
- `COUNTER-SOURCE-RESULT.json` / `clean-selftest/COUNTER-SOURCE-RESULT.json`：真实函数对拍及无历史目录依赖的重跑。
- `consumer/CPU-MEASUREMENT-SELFTEST.json`：既有发布与消费检查。
- `LIVE-CPU-DATA-RESULT.json`：本机统一快照数据检查。
- `SOURCE-MANIFEST.json` / `DEPLOYMENT-MANIFEST.json` / `NATIVE-BUILD.log`：完整构建与安装哈希。
- `ROLLBACK-MANIFEST.json` / `rollback/`：本轮更新前程序和设置的备份。

更广泛的 HWiNFO 机型差异、真实 12 小时共享期限到期，仍使用上一轮报告所述证据边界；本轮没有用本机 40 条 CPU 数据宣称这些范围已经全部验收。
