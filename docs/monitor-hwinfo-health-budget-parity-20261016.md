# HWiNFO 健康检查存储边界对齐（2026-10-16）

本轮确认并修复恢复助手与 YMCC 唯一原生解析器在存储布局准入上的不一致。产品只改 PowerControl/YeManHWiNFO.ps1 的 Test-HWiNFOSharedMemory；原生解析器、显示数据、时钟兼容策略、采样频率和 HWiNFO 上游项目均未调整。

## 复现与修复

使用实际生产 C++ 解析函数，以及实际 PowerShell 探测函数读取隔离命名映射，旧助手有 5 组误确认健康的布局：

- 传感器区偏移为 0。
- 传感器区偏移小于基本头的 44 字节。
- 传感器区偏移超出映射视图。
- 单行读数超过原生既有的 16384 字节上限。
- 总读数区超过原生既有的 32 MiB 上限。

这些输入原生层已经拒绝，助手却可能返回健康。修复只是让助手遵守同一存储边界，避免确认消费者无法接收的数据；没有增加更严格的完整传感器表假设或关闭任何硬件项目。

## 测试

新增 tools/monitor_hwinfo_header_budget_selftest.mjs 与 tools/hwinfo_header_budget_selftest.ps1，直接编译实际解析函数并执行实际助手函数。16 个布局用例中，7 个合法布局保留，9 个非法布局拒绝，修复后的结果全部一致。旧助手的上述 5 个差异可重复复现。

恰好 32 MiB 的读数区继续通过，超过上限才拒绝。合法旧基本头、未知格式的原有兼容路径、空传感器表和映射末端的原有准入也保留。每个通过的 C++ 用例都解析出真实夹具 CPU 功耗 42W；9 个拒绝用例均未执行读数区复制。该 42W 仅为隔离测试输入，从未接入本机监控。

夹具与旧实际探测函数保存在 tools/fixtures/，新输出目录独立运行同样通过，不依赖历史 Build 目录。共享恢复旧测试中遗漏基本头传感器偏移的有效夹具也已补为 44，不再把原生层已拒绝的布局当作有效旧格式。

其他现有检查通过：进程/UAC 29 项、连续生产者 44 项、周期与一致性 48 项、配置合并 26 项、采集策略 32 项、只读共享 46 项、精确子进程等待 34 项。本轮不进行 CPU 占比或耗时压测。

## 本机小更新

仅替换 C:/SOFT/YeMan/PowerControl/YeManHWiNFO.ps1，安装 SHA-256 为 93F0B1ED04D6B5F3742387179E09857B8EBF81EC6CD56F4E7BBA683F9A70947E。源文件、冻结候选和安装文件哈希一致，旧助手已备份。

普通权限现场探测中，旧/新助手都确认当前共享数据健康。生产者时间由 1792159830 推进至 1792159839，布局保持 416 行、3000ms 周期。连续 3 次正常 start 均返回 0；HWiNFO PID 保持 21512，YMCC PID 保持 14824，INI 与原生程序哈希不变。没有在本机注入坏布局、重启 HWiNFO 或关闭游戏；详细日志门保持关闭。

此次只对齐健康检查的存储边界，不将其扩大解释为全部协议差异、其他机器现场或真实 12 小时到期已经验收，也不据此报告 CPU 降幅。

## 证据与回退

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/hwinfo-health-budget-parity-20261016。

- FINAL-RESULT.json：最终汇总和源/安装绑定。
- HEADER-PARITY-RESULT.json：实际原生解析器与助手的逐例对照。
- NATIVE-HEADER-RESULT.json：原生输出及复制次数。
- BEFORE-HELPER-RESULT.json、AFTER-HELPER-RESULT.json：前后健康判断。
- clean-selftest/：无历史目录依赖的独立运行。
- REGRESSIONS.json：既有恢复回归。
- LIVE-FIRST-PROBE.json、LIVE-LAST-PROBE.json：本机真实生产者推进。
- REAL-HEALTHY-START-RESULT.json：三次正常启动。
- DEPLOYMENT-RESULT.json、rollback/YeManHWiNFO.ps1：只更新助手的部署与原文件备份。
