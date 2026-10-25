# HUD 统一数据投影缓存优化（2026-10-16）

本轮继续优化监控数据消费端的重复工作。Steam 承载和无边框桌面承载在同一个 HUD 心跳中按顺序调用同一个 sample source；此前两次调用都会重新运行 hudFromUniformSnapshot，把同一个 YMCC 统一快照、TDP 回执和游戏图片转换成两份等价 HudData。

## 修正

在 src/bridge/monitorHudSampleSource.ts 增加进程内短生命周期缓存。缓存键包含：已接收的统一 MonitorSnapshotView 对象、TDP 回执对象、当前时间毫秒、游戏图片字符串和图片开关状态。只有这些输入完全相同才返回上一份投影；时间变化会重新计算，因此不会延长任何硬件、FPS、帧时间或电池字段的有效期。

新快照、TDP AC/DC 回执变化、游戏图片变化以及最后一个消费者释放时都会自然失效或清空。没有新增硬件读取、轮询、线程、定时器或长期历史缓存。

## 验证

实际打包当前生产 TypeScript 模块的消费生命周期测试通过 13 项：

- Steam 与桌面两个顺序消费者在同一快照、回执和时钟下复用同一个投影对象；
- 时间前进后对象更换，字段新鲜度仍按新时间判断；
- 新统一快照发布后投影失效，后续调用共享新结果；
- 原有 51 次 native 发布、1 次 read、共享需求、FPS 零值隐藏、HWiNFO 断开降级、TDP AC/DC 切换和最终定时器清理继续通过。

vue-tsc --noEmit 通过。没有执行 CPU 百分比压测，不能把对象复用次数直接宣称为整机 CPU 降幅。没有改 native 采集链、Steam 协议、硬件读数或本机安装文件。

结果：Build/Tasks/YMCC-Steam-HUD-Probe/monitor-sample-cache-20261016/FINAL-SAMPLE-CACHE-RESULT.json。
