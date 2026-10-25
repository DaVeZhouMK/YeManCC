# 有效低功耗驻留证据夹具

目标：Windows 已记录 Modern Standby，但未达到有效低功耗。陀螺仪模拟鼠标导致立即退出属于其他修复，本目录不复用该问题的日志。

`cases.json` 保留30组基于本机 Microsoft Kernel-Power 507 事件定义构造的**合成边界输入**，并非真实硬件驻留记录。测试直接编译 `native/sleep_residency_evidence.h`，并从现行 `native/main.cpp` 提取实际507采集代码，验证普通 `sleep-facts.log` 的 `s0-low-power-residency` 事件载荷。

覆盖：无驻留、低比例驻留、老版本缺字段、64位长时间计数、UINT64_MAX哨兵、溢出/负数/尾随垃圾、重复字段、独立Sleep阶段、未知硬件0值及短会话不报长时异常。另有两个编译故障对照，确保“硬件0直接当失败”和“缺字段补0”会被拒绝。

运行入口：`tools/sleep_residency_evidence_selftest.py --output-dir <独立证据目录>`；可选 `--mutation hardware-zero-confirmed` 或 `--mutation missing-as-zero`。复用既有校准工具链，不启动YMCC、ETW、睡眠或设备。

新采集项在退出待机后的507结算到达时产生；120秒为诊断观察长度、80%为质量提示阈值，均不授予自动再次睡眠。`SleepEntered=false`、软件DRIPS、硬件DRIPS分别保留；硬件字段为0但无有效性依据时，必须输出未知，不能宣布硬件从未进入低功耗。

本轮报告：`G:/YeManCC-Work/Build/Tasks/GyroSleep-20261021/SleepEntryAnalysis/LowPowerResidency-20261022/REPORT.md`。

## 2026-10-24 异常睡眠主线消费者

原30组合成输入保留，增加1条真实 ROG RC73XA 记录（唯一场景 ID：`sleep.abnormal.amd-rog-delayed-drips-20261024`）。分类为异常睡眠类，用户标记 AMD常见问题；这一条现场样本不代表 AMD 平台发生率。归档 SHA256 与 sleep-facts.log 第169行来源写在 cases.json；XML 仅由导出 raw 字段重建，并非原始事件 XML。首次 DRIPS 203.714124秒，总时长217.893329秒；当时第30秒没有实时失败证据，回放必须不发睡眠请求。

同一 CPP 直接编译生产 `sleep_abnormal_resleep_native.inc` 和策略头，事件、计时器、权限和电源 API 全部替换为内存边界。验证电源键起止、Windows成功抢占未处理的界面消息、队列缺口、时钟漂移、定时器申请失败、共享三次预算、过期阶段不补发、S4未确认不关机，以及关机成功/失败/权限拒绝均终止。原30个会计断言和两项故障对照保留；另外4项编译故障检查成功取消、S4条件、终态清理和证据时效性，均须实际编译并被原断言拒绝。

核心层唯一消费 ID 为 `sleep.residency-evidence-consumer`，登记于现有 global/sleep.mjs。最小入口：`G:/YeManCC-Work/Testing/YMCC/Run.ps1 -Optimized -Mode plan -Tests sleep.residency-evidence-consumer`，执行时改为 `-Mode run`。不扩大全睡眠模块的默认最小集。执行器为同一 Python 测试器，7个有界编译运行；外层90秒，单轮总子预算85秒。源码、原生头、JSON、工具链选择器及既有原生恢复工具清单声明闭包；工具实文件在执行前后校验。它不消费父门、也不复制测试模型。结果为 `residency/result.json` 及各个子回执；实际耗时随结果记录。

本夹具能证明上述软件边界。Diagnostic通道是否可读、161是否在阶段期限内实时产生，以及再次睡眠是否改善物理 DRIPS，仍须真实机器证据。通道关闭/不可读/订阅失败时，主线仅记普通日志并终止此流程，不开启诊断通道，不把缺证认作失败。新流程不依赖YMCC高频日志。
