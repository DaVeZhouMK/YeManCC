# 帧时间曲线输出按实际容量构建（2026-10-16）

本轮减少已有原生帧时间曲线的反复数组扩容：创建 JSON 样本数组时，按已选中历史的实际 size 一次 reserve，再逐项写入完全相同的真实点。没有降低采样、抽点、动画补帧或增加缓存。Presented FPS、RTSS 救援和数据失效规则保持原代码。

上一轮 source-copy-reuse-20261016 的 ROUND-RESULT 及当前生产源码已核对，属于 progress。本轮继续优化现有唯一数据链路，不将局部操作计数当成总体 CPU 降幅或安装版效果。持续目标 active。

## 持续开销检查的结果

先读当前 nativeMonitorLoop 和 monitorWriteSnapshotGuarded：正常前端接管标志会阻止兼容文件中转；top/fps 都无需求的分支使用既有条件变量谓词等待。这两处没有发现需要重做的机制，不改写文件耐久性、心跳或接管锁域。这里是当前源码路径核对，不是对正在运行的安装版进行采样。

随后确认既有 100 毫秒曲线通道调用 Output::packet；它对最多 120 点的历史从空 JSON 数组逐个 push，触发多次容量增长。历史本来就知道准确 size，因此不需要扩容猜测。

生产修改只有 native/monitor_frame_output.h 的这处数组构建：在原 push 循环前对真实 json::array_t 调用 reserve(history->size())。History 的上限和生命周期、每个 double、顺序、源标识、观测时刻、有效期、返回文档结构与序列化代码全部未改。没有加入新的持久成员；Output 的 sizeof 仍为 2,296 字节。

## 原始字节一致性

分别编译冻结/当前真实 Output 和原 deps/json/json.hpp，Windows/RTSS reader 未调用，输入只使用确定性观察回执。

968 份结果在 C++ 内记录原始 json.dump() 字节，Node 逐条严格比较完全相同。它覆盖：3 种来源、20 个容量增长边界（0/1/2/…/119/120 点）、每种来源连续 300 次、120 点环形历史绕回、Unicode/引号/反斜杠标题、重复/无效/缺失观察、来源失效、RTSS 恢复、LOW 过期、相同 PID 的新创建身份和新进程重建历史。

所有点均来自原 observe/hardware 路径，不用 FPS 倒算帧时间；没有为了界面连续而插值或续期。191 项曲线专项断言通过，原始包字节对照另记录 968 份，不把后者重复计入断言数。

## 分配工作量

同一编译器和 JSON 依赖下，全局 new 计数只在真实 packet() 调用内开启；输入构建、序列化、记录和结果输出不计入窗口。每种模式 300 份合成包，包含历史从 1 点增长到 120 点和多次绕回。

| 合成输出模式 | 旧分配次数 | 新分配次数 | 旧累计申请字节 | 新累计申请字节 |
| --- | ---: | ---: | ---: | ---: |
| HWiNFO 普通帧包 | 14,716 | 11,400 | 2,335,328 | 1,140,960 |
| RTSS 快速帧包 | 17,116 | 13,800 | 2,354,528 | 1,160,160 |
| HWiNFO FPS 优先 + RTSS 快速曲线 | 14,716 | 11,400 | 2,335,328 | 1,140,960 |

每组减少 3,316 次堆申请和 1,194,368 累计申请字节。这些是对应固定夹具的调用次数/总申请量，不是峰值或常驻内存，不是实机 CPU 百分比或采集耗时。正常帧包副本、元数据和序列化仍有开销，没有声称整个监控零分配。

历史容量没有扩大，改为一次预留实际点数；没有在栈上再复制一份完整历史，也没有保留已发送包作为新缓存。

## 接线与消费回归

当前最终头文件重新通过真实普通采样分支/Output 的 39 项，继续验证关闭曲线时的 RTSS 低频救援；HWiNFO FPS 有效时不做不必要的救援读取。最初曲线开关连带阻断救援的冻结历史仍产生其预期 19 失败，未改写该证据，也不计为本轮失败。

三个生产输出包经过实际统一 hub、显示模型及两个既有协议，6 项通过；协议回归没有重开隐藏的 Steam 输出。原有帧来源优先级 16 项和 RTSS 回执接收 4 项通过。合计 256 项当前正确性检查通过。

当前 sole worker、原始 parser（含上轮 copy-reuse）、relay 提交区域与起点逐字节相同。帧历史头、RTSS reader 和 HWiNFO PowerShell 策略也逐字节相同。其他任务对共享 main.cpp 的修改不纳入本轮补丁。

## 交付与边界

生产变更只 native/monitor_frame_output.h。新增 tools/monitor_curve_array_budget_selftest.cpp/.mjs 可重现真实前后包字节和函数内分配计数。补丁在隔离目录应用并验证逐字节一致，未暂存/提交。

仅源码与隔离检查：没有部署、重启 HWiNFO/YMCC/游戏、真实映射/传感器读取或 CPU 百分比测试，没有完整应用重新构建。已经编译相关真实生产头文件及抽取的普通采样分支，仍不能当作安装版/所有机器的完整运行验收。总体 CPU、实际 HWiNFO 上游策略采用和长期数据完整性仍需继续核验，持续目标未完成。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/frame-curve-array-budget-20261016/。

关键文件：BASELINE-PINS.json、native/before/RESULT.json、native/after/RESULT.json、CURVE-ARRAY-RESULT.json、REGRESSION-RESULT.json、regressions/rtss-rescue/WORKER-RESULT.json、regressions/rtss-rescue/CONSUMER-RESULT.json、INVARIANTS-RESULT.json、SCOPED-CHANGES.patch、PATCH-CHECK.json、ROUND-RESULT.json。补丁以本轮冻结起点为基准，只含本轮范围，不是从干净仓库重建全部既有监控功能的完整补丁。
