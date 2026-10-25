# 监控核心图块结果复用优化（2026-10-16）

核心数组和标签已经可以共享，但 MonitorHudTemplate 每次数据更新仍会调用 coreTileUsage 并重新创建最多 16 个图块值。

## 修正

对解析器拥有的冻结核心数组增加最近一次输入/stale/result 复用。相同输入和 stale 状态直接返回已有图块数组；新数组、stale 变化或可变 fixture 继续重新计算，避免缓存外部可变数据。

## 验证
- 重复载荷及 HUD 模型：8 项通过，包含 tilesShared。
- 核心图块回归：12 项通过。
- 统一消费生命周期：13 项通过，最终定时器数为 0。
- 全仓 vue-tsc --noEmit 通过。

没有运行整机 CPU 百分比压测，没有修改硬件采集或安装文件。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-core-tile-share-20261016/FINAL-CORE-TILE-RESULT.json。