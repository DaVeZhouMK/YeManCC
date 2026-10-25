# 关闭帧时间曲线后的 RTSS FPS 兜底（2026-10-16）

本轮修复现有统一采样循环的一个接线问题：帧时间曲线关闭时，RTSS FPS 兜底也被曲线开关一起阻断。修复继续以有效 HWiNFO FPS 为主；只有它缺失、过期或低于 1 时，才在现有普通循环读取 RTSS。没有增加采集器、线程、定时器、IPC、配置项或前台开关。本轮仅修改源码并做隔离验证，未部署或操作实际 YMCC/HWiNFO/游戏。

## AMD 选项的结论

用户只要求当前数据保持可见，前台 AMD SnapshotPolling 开关已在之前轮次撤回。本轮再次核对 src 中没有该配置入口，且 PowerControl/YeManHWiNFO.ps1 与轮次起点逐字节相同；不自动开启快照模式。内部兼容读取既有 monitorAcquisition 配置的能力沿用原实现。没有以当前监控开关为理由关闭 HWiNFO FPS。

快照采样会改变采集方式，不能凭名称保证所有机型的数据一致；本轮不将其作为已经验证的数据无损优化。前轮结论和证据见 monitor-hwinfo-static-acquisition-20261016.md。

## 缺陷复现

native/main.cpp 的普通 FPS 循环原先仅在 g_monitorFastFrames=true 时调用 sampleRtss；另一处 100 毫秒读取也受同一标志控制。monitorHudHost.ts 使用 frametimeGraph 设置该标志。因此关闭曲线会让两个读取点同时停止，即使 HWiNFO FPS 不存在也不会读 RTSS。先前只向 Output 直接注入帧回执的测试不能证明生产循环真的访问过兜底源。

新测试从冻结/当前 main.cpp 提取真实普通循环条件，编译执行该分支与真实 Output；只将 RTSS 读者边界替换成无 IO 回执。相同用例在冻结代码产生 19 个失败：300 轮缺失主源且曲线关闭的循环，RTSS 读取数为 0。当前代码的 39 项均通过，同样 300 轮读取数为 300。

## 修改和负载边界

native/monitor_frame_output.h 把既有 HWiNFO FPS 原观测时间、有效期、有限数值和至少 1 FPS 的判定提为私有 const 方法。packet() 与新增 needsRtssRescue() 共用这个判定，避免读者需求和输出优先级各自定义有效条件。缺失 PID 或创建身份不发出兜底需求。

普通采样点现在接受“曲线需要读取，或当前需要 FPS 兜底”。健康 HWiNFO+关闭曲线的 300 轮，额外 RTSS 读取仍为 0；主源恢复后的 300 轮也为 0。主源缺失+关闭曲线的每轮仅读一次，复用已有 1 秒循环。曲线仍独立使用原 100 毫秒内存发布通道。

上述数字是确定性边界调用计数，不是 CPU 占比或真实时间测量。整段 nativeMonitorLoop 与冻结版本对比仅有普通采样条件变化；高频通道、采样周期、硬件读取、PDH、游戏识别、快照写入和诊断策略均逐字节保留。没有修改 HWiNFO 策略、RTSS 读者、历史容量及显示传输周期。

## 正确性和消费验证

39 项真实循环/输出检查覆盖：无游戏、正常主源、缺失主源、确切期限、重复包装旧 poll、0/低于 1/NaN 主源、无效 RTSS、主源恢复、曲线开关、相同 PID 的创建身份变化、不同进程和时钟回退。RTSS 观察重读不会更新原时刻或期限，不替旧 HWiNFO 1% LOW 续期。

三个当前生产输出包继续通过真实 monitorData 唯一 hub、HUD 模型及两个既有输出协议，6 项检查通过。双协议回归仅验证既有数据结构，没有重新开放隐藏的 Steam 输出。另运行当前生产头文件的已有帧优先级 16 项及回执接收 4 项，均通过。本轮合计 65 项正确性检查通过；冻结缺陷的预期失败单独保存。

这证明隔离条件下的真实采样接线和消费结果；没有证明已经安装到本机，或实际游戏中 RTSS/HWiNFO 的全部运行情况。本轮未进行 CPU 百分比测试，未新取实际硬件样本。持续优化任务仍 active。

## 交付和证据

生产变更仅 native/main.cpp 的普通采样条件、native/monitor_frame_output.h 的共用有效性判断。新增 tools/monitor_rtss_rescue_worker_selftest.cpp/.mjs 可重现冻结缺陷和当前结果。主线其他任务的改动保持原状；未暂存、未提交。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/rtss-rescue-without-curve-20261016/。

核心回执：BASELINE-PINS.json、worker/before/RESULT.json、worker/after/RESULT.json、WORKER-RESULT.json、CONSUMER-RESULT.json、REGRESSION-RESULT.json、INVARIANTS-RESULT.json、SCOPED-CHANGES.patch、PATCH-CHECK.json、ROUND-RESULT.json。补丁以本轮起点为基准，仅包含本轮所有权范围；不能当作从干净仓库重建此前全部监控功能的完整补丁。
