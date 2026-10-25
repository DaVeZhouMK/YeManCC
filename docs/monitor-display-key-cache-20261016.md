# 监控显示 key 重复计算优化（2026-10-16）

普通 HUD feed 和曲线 HUD feed 会连续接收同一份 MonitorSnapshotView。此前两个 feed 都会重新遍历约 50 个字段、创建数组并 join 成显示 key。

## 修正

monitorSnapshotDisplayKey 现在只保留最近一次快照对象和已生成 key。若下一次调用仍是同一个对象，直接复用结果；只要对象变化，就完整执行原有 key 计算。这样不会改变 seq、会话、健康状态、FPS、内存、电池、风扇、文件系统或 seq=0 兼容字段的去重语义。

## 验证
- 显示 feed 回归：16 项通过。
- 统一消费生命周期：13 项通过，最终定时器数为 0。
- 全仓 vue-tsc --noEmit 通过。

没有运行整机 CPU 百分比压测，也没有修改硬件采集或安装文件。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-display-key-cache-20261016/FINAL-DISPLAY-KEY-CACHE-RESULT.json。