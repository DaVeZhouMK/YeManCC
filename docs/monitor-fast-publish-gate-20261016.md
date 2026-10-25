# RTSS 高频快照发布门控优化（2026-10-16）

本轮继续优化监控数据和 CPU 工作，目标是处理 RTSS 高频区的重复发布。原有 100 ms 快速循环每次调用 packet()、dump() 并发布 monitorSnapshotFrames，即使 RTSS 共享内存的帧标记没有变化，也会把同一份 FPS/帧时间数据重新送给两个 HUD 承载。

## 修正

native/monitor_frame_output.h 中 Output::sampleRtss 现在返回本次是否接受了新的 RTSS 观察。RtssReader 对首次预热、写入 busy、无效布局、进程退出和完全不变的生产者标记返回无效观察；这些情况不会推进 FPS 或帧时间证明。

native/main.cpp 的快速区只在接受到新的 RTSS 帧时构造 packet JSON 并发布。没有新帧时直接回到下一次已有 100 ms 等待，不新增轮询；进程退出仍 reset 并发布一次空 FPS 载荷，清理旧游戏数据。HWiNFO、Windows CPU、磁盘和电池数据仍走原有 1 秒采集区。

跳过的快速 tick 不会修改 collectedAt、heartbeatAt 或任何有效期，因此不会用重复读取延长旧数据寿命。旧的 1% LOW、TDP、内存、GPU 和电池独立时间戳规则保持不变。

## 验证

- 实际 RTSS 共享内存夹具：3 项通过；首次预热、真正推进和不变读数均覆盖。
- Output 接受回执：4 项通过；无效、有效、旧时间戳和重复时间戳均覆盖。
- 生产接线：8 项通过，包括快速区门控、退出清空、无详细日志记录和 1 秒完整采集保留。
- 上一轮快速发布回归：15 项及 8 项接线检查通过。
- 当前 native/deps 冻结后完整 x64 原生编译、链接和 manifest 嵌入通过。

这些证据证明重复 RTSS 读数不会再进入 JSON 和快照发布路径，但没有运行整机 CPU 百分比压测，不能把发布次数减少直接换算成整机 CPU 降幅。

本轮没有安装候选、重启 YMCC/Steam、操作游戏或读取新的硬件源。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-fast-publish-gate-20261016/FINAL-FAST-PUBLISH-GATE-RESULT.json。