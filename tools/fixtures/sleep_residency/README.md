# 当前策略：保守事件修复与低开销观察（2026-10-25）

当前产品沿用已核验发布版0.0.35的Windows事件修复主路径，并保留后续电源键取消、旧消息隔离、能力降级与共享三次预算等修正。DRIPS不是重睡的必需信号。完成会话的507统计仍写普通日志；缺失字段保持UNKNOWN。

持续高功耗是待补实机证据的长期目标。生产Arm/Owned入口仅按原始人类意图合并一条必要策略记录，不启动ETW、线程、定时器或30/230/330/360秒升级动作，不消耗或补回原重睡预算，不阻止已经独立获准的父请求。下面保留的分阶段引擎、实时ETW与历史日志测试属于明确隔离的历史回归，不能算当前产品启用或物理睡眠验收。

静态扫描仍在开机/确认唤醒后延迟60秒单次执行。全局软件快照失败后，本轮停止该类别并汇总失败阶段、affected/visited/unvisited及UNKNOWN，下次独立操作重试；成功观察缓存仍限1秒/64项，写前、写后与OFF读取保持新鲜，写前持久化、外部修改保护和180秒预算保留。没有周期扫描。

唯一ID仍为`sleep.residency-evidence-consumer`，入口`G:/YeManCC-Work/Testing/YMCC/Run.ps1 -Optimized -Mode run -Tests sleep.residency-evidence-consumer`。原90秒外层/85秒子预算不扩展，19次有界编译执行包含全部14个原负例，以及生产Arm/Owned倒退、全局失败重复读取和中文空设备列表回退4个新故障对照。结果单列conservativeProductionChecks与historicalStagedEngineChecks，静态策略及调度另列。不删除原断言，不用缓存冒充本轮执行；无实际Windows动作或CPU百分比结论。

2026-10-25 第二轮同版本兼容补充：新ROG/Win5/MSI普通日志的journal均出现中文“无”被当未知设备名，ROG USB4及MSI Wi-Fi读回留UNKNOWN。当前收集和读回共用严格生产解析器，仅接受单行精确NONE或无；库存中真实同名设备优先，多行混杂、空字节、未知输出仍拒绝。原生Unicode转换、trim/lines、类型、空列表谓词和parseArmed完整定义由当前源码提取到本消费者编译，保留身份依赖和全部旧断言，新增实际扫描/读回/OFF边界；中文支持被移除的编译故障必须被原断言拒绝。原90秒外层/85秒子预算不变，不增加ID。命令失败只在原普通error/journal路径附加ran、elapsedMs、timeoutMs、phase，不逐条记录成功命令，不扩大命令预算；这些字段不把1460的包装超时/原始子进程退出混为确定根因。此修复不改变30秒/120秒睡眠条件，也不修改风扇或虚拟设备策略。

## 历史设计与保留回归记录

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


2026-10-25 当前修订：沿用 `sleep.residency-evidence-consumer` 唯一ID，增加真实ROG连续日志的零驻留/正驻留对照和重睡后新观察阶段。旧 Diagnostic EvtSubscribe 来源不适用（Analytic/Debug不受支持，列出的channel16也不代表可订阅），产品改为仅sleep episode拥有的实时Kernel-Power ETW 161/522/554，TDH按字段类型解码，不写ETL、不配置全时通道。缺事件/丢失/权限及清理失败仍为UNKNOWN，不授权盲重睡。普通sleep-facts日志中的来源、区间摘要、deadline、请求结果和终止原因属于必需证据，不受高频日志开关控制。

同一消费者还执行静态屏蔽的内存journal恢复策略及实际延迟调度代码（所有Windows动作端口均mock）：原电源修复开关启用时，开机和确认唤醒后60秒各扫一次，无周期扫描；睡眠/OFF/退出和旧代次取消，手动补扫消费待执行任务。仅恢复自身仍匹配的改动，保护电源键、风扇及虚拟设备所有权。静态屏蔽不能证明全部现代待机内部激活被禁止。

适配器维护全部新增源码和工具输入闭包，在原90秒预算内保留原6个负例并增加5个新负例。执行成本、原失败和当前独立回执见Testing/YMCC/Evidence，缓存与历史结果不算本轮通过；模拟结果不替代实机ETW交付、功耗或30秒响应验收。


2026-10-25 反推补充：实际复现并修复“旧扫描开始消息越过后发取消”和“终态清理缓冲区的Windows健康证据被丢弃”。开始消息也绑定原revision，ETW先撤销动作权限、完成自有consumer drain/join后退休观察身份；成功、丢失及人工否决在最终动作前仍有效。显式关闭per-processor buffering使2–4个64KiB缓冲预算有效。增加原夹具的相应场景，并在同一ID原90秒预算内保留11个负例、增加2个故障对照，共14次有界子执行。静态journal外部改动、未知读回、断裂祖先链及文件权限的修正继续复用原静态消费者；UI迟到回执/卸载修正在原导航用例内验证。旧FAIL及全部旧断言保留，实机结论仍为UNKNOWN。

## 2026-10-25 bounded background cost follow-up

The existing consumer retains its ID and 90s budget. It now also rejects a
compiled final-query freshness bypass; the unchanged 30/230/330/360 stages,
5s evidence freshness, 10s dispatch window, final loss statistics, human wake
and terminal healthy-drain vetoes remain mandatory. Episode-local verified
schema identities avoid repeated TDH metadata discovery; every event still
reads and validates its field sizes/values. Same absolute timer targets coalesce.

Static planning observations are operation-local and bounded to 64 uses / 1s.
They cannot authorize writes: fresh prewrite, readback, device identity/protection,
write-ahead durability and OFF restoration checks remain. Existing policy/native
mocks compare snapshot loads and exercise invalidation, external edits and cancel.

The existing wake/Steam production consumers separately check worker deadlines
and the completion-before-wait race. Idle owns no recurring timeout. Evidence
reports operation counts; they do not establish physical CPU or power savings.


<!-- STATIC-SCAN-COST-RECEIPT-20261026 -->
2026-10-26：扫描/恢复只在原终态记录补operationEvidence，复用已有全局快照加载计数。loadAttempts包含调用者校验，不等于powercfg进程数或覆盖数；elapsedMs是操作运行时间，可能包含系统睡眠，不是CPU时间。取消/期限/锁拒绝也保留可用计数；无新查询、日志轮询、定时器或每项成功日志。现有消费者精确提取Native.run，配合内存端口验证实际接线、取消、180秒期限、锁拒绝及独立OFF，不新增ID或放宽90/85秒预算。
