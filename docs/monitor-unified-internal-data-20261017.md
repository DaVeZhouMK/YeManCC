# YMCC 内部统一监控数据层优化（2026-10-17）

本轮为主线源码改动，尚未打包或部署。

- 顶部与 HUD 复用 monitorSnapshotSignals.ts 的纯数据有效性校验。顶部取数不再构建完整 HUD、遍历核心或帧时间曲线，返回结构保持一致。
- 顶部采集需求随既有 UI 可见性释放和恢复；HUD、独立预览及持续控制保留各自需求。
- 连续性能依赖 Windows CPU 压力数据，autofloat 现在明确登记 top + fps，避免依赖顶部是否显示。控制算法和写入链路未改。
- 验证通过：2,489 组旧新输出对照、58 次真实 UI/hub/当前内部消费者观察，以及统一 hub、数据链路、会话、隔离、native FileSystem 和浏览器 HUD 回归；vue-tsc --noEmit 通过。
- 首次中间对象方案在轻负载 HUD 计时中退步，已否决；最终使用共享小型谓词。计时仅为隔离函数耗时，不能当作整机 CPU 百分比。未进行真实游戏、睡眠、硬件读写或 HWiNFO 恢复验收。

完整报告：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/unified-internal-data-20261017/REPORT.md

结果索引为同目录 ROUND-RESULT.json，审阅差异为 SCOPED-CHANGES.patch。
