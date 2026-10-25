# 监控快照高频通道与体积上限（2026-10-16）

本轮针对“监控快照进入低频区域会卡住”的风险做了边界收敛。监控显示继续消费 YMCC 统一快照；没有新增 HWiNFO/Windows 读取、采集线程、定时器或硬件轮询。

## 路由

- `monitor.snapshot` 的内存发布和现有 `monitor.hudPulse` 继续作为高频监控快照通道。
- 诊断快照仍复用已有异步池和 `monitor-detail.log`，但记录明确标记 `lane=high-frequency`，不进入 `native-lifecycle.log` 或低频 `native-detail` 流。
- RTSS 100ms 快速区仍只在共享内存标记推进时发布；没有新帧时不构造 JSON、不发布重复快照。
- 诊断记录仍按已有 1 秒预算取样，因此不会把日志频率误当成硬件采集频率；它只消费已经发布的统一缓存。

## 体积边界

- top 文本最多 16 KiB。
- FPS 文本最多 16 KiB。
- 单个内存快照的 top+FPS 文本最多 24 KiB。
- 兼容文件写入前也经过相同的单路上限检查，超限时整路删除/不写入，避免留下半个 JSON。
- 组合超限时优先保留高频 FPS 文本，放弃较低频的 top 文本。
- 诊断快照原始嵌套 payload 仍限制为 12 KiB；单条诊断记录 32 KiB，队列 256 KiB/16 条，日志 2 MiB 加一份轮转文件。
- 超限次数进入现有监控诊断计数 `snapshotOversizeDrops`，不做截断、不制造部分读数。

## 验证

- `monitor_snapshot_bounds_selftest.mjs`：12 项静态接线与上限检查通过。
- `monitor_detail_bounds_selftest.mjs`：48 项通过。
- `monitor_detail_routing_selftest.mjs`：8 项 IPC 路由与 12 项接线检查通过。
- `monitor_fast_publish_gate_selftest.mjs`：8 项通过。
- `monitor_native_consumption_selftest.mjs`：8 组原生合成场景通过，含 2,500 次快照响应、24 次脉冲路径和当前/基线分配对照。
- `monitor_data_link_robustness_selftest.mjs`：31 项通过。
- `monitor_consumer_lifecycle_selftest.mjs`：13 项通过，剩余定时器 0。
- `monitor_export_state_selftest.mjs`：15 项通过。
- 当前 native `main.cpp` x64 编译通过。

验证范围是源码、合成快照、原生编译和现有回归夹具；本轮没有安装候选程序、重启 YMCC/Steam/游戏，也没有做 2077 实机验收或整机 CPU 百分比压测。


## 后续更正（同日）

本页最初的静态检查没有覆盖实际兼容文件提交、快速发布组合上限和字符串 reserve。后续已用修改前负例复现并修复，详见 [监控快照边界修复与百分比计算复用](monitor-snapshot-boundary-repair-20261016.md)。本页原先的“静态检查通过”不能作为这些边界在修复前已正确执行的证明。
