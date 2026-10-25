# 统一监控快照启停一致性（2026-10-17）

主线源码已修复 monitorSnapshotServeFn 和 monitorSnapshotRecordForDetail 的两阶段取数：通道标志与缓存现在在既有 native→snapshot 锁重叠期间捕获，模式锁在字符串复制/JSON 构造前释放。仅改这两个生产函数，未部署。

确定性重现中，30 组交错时序的旧函数均混合开关和代际，生产修复为 0；16,000 次并发读取及 4,000 次模式切换无混合状态。12 种状态的前后堆分配、分配字节及两把锁的获取次数一致。回包、HUD pulse、GPU 诊断和高频日志接线回归通过，308 输入冻结的完整 main.cpp 编译通过。

这证明普通 monitor.start/stop 模式转换的一致取数，不证明旧游戏闪烁的根因，也不是实机睡眠或 HWiNFO 恢复验收。未增加产品辅助 EXE 或轮询，没有整机 CPU 百分比测试。

完整报告：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/snapshot-mode-coherence-20261017/REPORT.md

结果索引为同目录 ROUND-RESULT.json，审阅差异为 SCOPED-CHANGES.patch。
