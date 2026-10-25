# HUD 已限幅数据重复复制优化（2026-10-16）

本轮继续优化监控消费端的 CPU 工作。共享数据协议入口已经对 HUD 数据执行一次完整边界检查和深拷贝；进入实际渲染器后，之前又对同一份已拥有数据执行 boundedHudData，重复复制最多 128 个核心值、120 个帧时间点、字段值和 freshness 元数据。

## 修正

在 src/bridge/monitorHudModel.ts 增加弱引用的 owned 标记。boundedHudData 只有在对象已经由同一函数完整生成、且 stale 状态没有变化时才直接返回原对象。

- 原始输入对象仍会重新读取；原始对象被修改不会命中 owned 快速路径。
- boundedHudData(owned, true) 等 stale 覆盖仍重新生成。
- WeakSet 不持有历史数据，旧快照可正常回收。
- 没有放宽任何数值、时间戳、图片、核心数组或帧时间边界。

上一轮 monitorHudSampleSource 的同一心跳投影缓存继续生效，Steam 和桌面承载在同一快照、回执、毫秒时间下共享一次统一数据投影。

## 验证

- 已限幅对象身份及原始对象隔离：4 项通过。
- 统一消费生命周期：topbar plus two carriers use one existing hub, one shared source binding/TDP binding/read, independent top/FPS demand,Steam/desktop sequential consumers reuse one projection for the same snapshot, TDP receipt and clock tick,projection cache expires when the clock advances so field freshness is never prolonged,a newly published unified snapshot invalidates the projection cache and subsequent consumers share the new result,51 fast native publications respect 1Hz topbar/200ms HUD lanes without another read/start or fabricated observations,HWiNFO outage retains independent Windows CPU/cores and RTSS FPS; selected GPU never borrows FPS GPU usage,new game lifetime replaces target proof, keeping only the current publisher history,live DC write receipt and pending AC transition use shared TDP binding without collection or saved-value fallback,real FPS zero stays a zero measurement while the complete FPS family hides, including cap,authoritative unavailable publication clears both displays immediately without invented zero/read fallback,complete producer silence ages real readings to unknown without poll/start/read or observation renewal,one carrier release keeps the other alive; final carrier releases FPS while topbar demand remains,last topbar release converges with no remaining retry/display/age timers or in-flight read 项通过，51 次发布、一次 read、最终定时器数为 0。
- 数据链路健壮性：31 项通过。
- 输出模板和字段边界：68 项通过。
- 生产模块的独立 esbuild bundle 与运行检查通过。

全仓 vue-tsc --noEmit 仍被并行源码中的 src/bridge/deckyFanActions.ts:242 报错阻断：返回对象类型没有 ownerRevision。该错误不在本轮改动文件中；本轮不能把全仓类型检查报告为通过。

没有运行 CPU 百分比压测，没有修改 native 采集，不增加硬件读取、线程、定时器、Steam 协议或安装文件。持续优化目标保持进行中。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-bounded-identity-20261016/FINAL-BOUNDED-IDENTITY-RESULT.json。