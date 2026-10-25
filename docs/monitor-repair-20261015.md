# 监控修复交付（2026-10-15）

本轮依据 G3E 日志及本机新版的 105 份统一快照，完成主线监控修复与隔离构建。证据目录：`Build/Tasks/YMCC-Steam-HUD-Probe/monitor-repair-20261015`。

已纳入本轮构建的修复：

1. `src/bridge/monitorAppliedTdp.ts`：沿用上一轮已完成的 TDP 消费修正。同一物理电源模式下，新的写入 pending 最长保留上一条成功 ACK 5 秒；成功后更新为实际接受值，重复请求不续命，失败/unknown/AC-DC/睡眠边界立即失效。控制状态仓仍真实呈现 pending/null，没有显示请求目标或存储配置。共享一个原有订阅和一个有界到期计时器。
2. `native/main.cpp / nativeMonitorExcluded`：在唯一游戏识别入口补充编译/构建工具，以及带版本号的 Python 3 解释器规则。已观察到的 `cl`、`python3.13` 不再靠大工作集成为默认游戏候选。显式白名单仍保持原有优先级；正常游戏名和相近前缀名称不受规则命中。监控继续消费同一 `nativeDetectGame` 结果，没有另起进程扫描或 HWiNFO 读取。

这一规则修复覆盖已确认的工具进程误关联；它不把通用大进程启发式变成所有应用的完整游戏分类，也不证明 HWiNFO 全局 Presented 统计具备精确进程归属。

G3E 的 49W 超时、代次失效和 backoff 属于真实控制结果，本轮保留其失效语义；23W/30W 的配置保存记录仍不能作为已生效功耗。监控修复没有修改 G3E 的硬件控制/降压路径。台式机的 AC 120W ACK 也没有用来填补 G3E 数据。

验证已通过：

- `monitor_process_exclusions_selftest.mjs`：抽取真实原生匹配器、排除规则、原有准入表达式，验证 25 个工具/解释器名称、正常及相似游戏名、白名单及用户排除，并接入真实 FPS `Output`，确认排除后清除旧 FPS、真实游戏随后可再次产生输出。共 5 组用例。
- `monitor_tdp_continuity_selftest.mjs`：10 组状态/时间/释放用例。
- 正式 Vue HUD：13 种外观 × 100%/140%/200% × 修正前后，共 78 个组合。每组合 3 次 pending/ACK，旧逻辑隐藏 117 次，新逻辑隐藏和尺寸变化均为 0。
- 45 份本机真实统一快照再次经过正式 HUD 投影，22 个有源标量持续有效，缺失电池及未启用的风扇仍保持未知。
- TDP runtime、消费生命周期、FPS 家族进入门槛、显示需求、游戏策略隔离、Vue 类型检查均通过。旧游戏策略夹具因页面新增 `onInputSettingsChanged` 导入而缺少惰性订阅替身，本轮只补齐该 mock，未修改生产游戏策略。
- 原生完整 x64 编译/链接通过；Web 正式生产构建通过。原生 258 份构建输入已冻结并逐一校验，候选二进制确认包含新的排除规则，候选载荷已记录哈希。
- Steam 只读维护盘点：0 缺失锚点、0 未归类候选。没有修改 Steam IPC、设置、现有游戏会话或手柄控制接口。

交付状态：

- 主线源码路径：`Mainline/YeManCC-source/YeManCC`，常规主线构建会包含本轮修改。
- `candidate/Native` 与 `candidate/Web` 为本轮隔离编译验证产物，属于 Native/Web 候选，不是完整签名发布包或安装器。
- 未覆盖本机安装文件、未重启 YMCC/Steam/游戏、未写硬件功耗。本轮没有新增采集、轮询或 CPU 占用基准测试。
- 实机证据仍以前一轮为准：45 秒真实监控开启期间没有持续跳动；本轮修正的真实游戏与 G3E 验收尚未进行，不宣称已全部实机根治。

主要证据：`PROCESS-EXCLUSIONS-RESULT.json`、`TDP-CONTINUITY-RESULT.json`、`BROWSER-REPLAY-RESULT.json`、`REGRESSION-RESULT.json`、`CANDIDATE-RESULT.json`、`FROZEN-NATIVE-SOURCE.json`、`NATIVE-BUILD.log`、`WEB-BUILD.log`。
