# 监控新鲜度判断重复计算优化（2026-10-16）

普通 HUD feed 和曲线 feed 会对同一份 MonitorSnapshotView、同一时间连续调用 monitorSnapshotHasFreshData。该函数会检查 HWiNFO、FPS、CPU、核心数组、Global FPS 上限、磁盘、风扇和电池等多个数据族。

## 修正

现在保留最近一次非空快照对象、时间和判断结果。同一对象在同一时间再次判断直接复用；新快照或时间变化完整重新检查。没有长期缓存，不改变任何有效期或自动隐藏规则。

## 验证
- 统一消费生命周期：13 项通过，最终定时器数为 0。
- 数据链路健壮性：31 项通过。
- 全仓 vue-tsc --noEmit 通过。

FPS 上限旧回归夹具即使换回本轮前的 freshness 文件仍有 3 个相同失败：当前 production freshness family 是 13 个，夹具仍期望 9 个，并包含旧的零值/截止时间期望；这些不属于本轮改动，未作为本轮验证依据。

没有运行整机 CPU 百分比压测，没有修改硬件采集或安装文件。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-freshness-key-cache-20261016/FINAL-FRESHNESS-KEY-CACHE-RESULT.json。