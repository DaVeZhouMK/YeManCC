# 原生帧数据对象复用与无数据返回优化（2026-10-16）

本轮在 YMCC 现有唯一原生数据层中减少持续的 JSON 堆分配：HWiNFO FPS 记录在首次有效输入时创建，之后复用同一对象的节点更新动态数值；没有可用 FPS 来源时，先判失效再复制对象。源时间、有效期、采样周期和已发布内容均保留。仅源码修改与隔离验证，未部署或重启程序，没有做 CPU 百分比测试。

上一轮 rtss-rescue-without-curve-20261016 的当前 ROUND-RESULT 已核验：它改变源码并复现/修复真实普通循环的救援接线，属于 progress。本轮不重复该实现；重跑对应真实分支测试作为回归。持续优化目标仍 active，尤其不能把这里的分配减少当成已解决整个 HWiNFO CPU 占用或完成跨机实测。

## 两处持续工作

旧 Output::hardware() 每接纳一份输入都会从 initializer_list 重建整个 hardware_。固定来源字符串和进程身份也反复创建、分配和释放。即使同一个 3 秒 HWiNFO poll 在 1 秒消费循环被多次读取，也会重建；但不能简单按相同时间戳跳过，因为同一秒内原始数据、LOW、周期或标题仍可能修正。

现在首次构建仍使用原 initializer_list，后续使用真实 JSON 依赖的异构 find 查找既有键，原位赋值动态数字/空值。game 复用对象中的 std::string，source、PID 和创建身份由原有 bind/reset 生命周期约束。相同 poll 的修正继续生效，没有新增第二份值缓存、成员字段、身份表或采集状态。标题增长仍可能分配；“0 次”仅对应下表已经初始化、标题容量足够的固定测试。

旧 Output::packet() 在确认全部来源过期之前，会先复制 hardware_，随后把这份副本丢弃并返回 null。现在把相同的不可用判定前移，避免无数据时创建这个临时树。低于 1 的主源、真实零值、RTSS 兜底及来源优先级沿用原规则；没有借优化改变展示/控制策略。

## 确定性分配计数

使用真正生产 Output 与当前 deps/json/json.hpp，分别编译冻结版本和当前版本。在合成回执上重载 operator new，仅统计表中目标函数调用，排除外部 packet 序列化、夹具和结果记录。全程不调用 HWiNFO、RTSS、进程或硬件读取。

| 调用情形 | 次数 | 旧分配次数 | 新分配次数 | 旧累计申请字节 | 新累计申请字节 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 秒生产周期下的硬件输入更新 | 300 | 27,300 | 0 | 1,152,000 | 0 |
| 3 秒生产周期下的硬件输入更新，含相同 poll 修正 | 300 | 27,300 | 0 | 1,152,000 | 0 |
| 6 秒生产周期下的硬件输入更新，含相同 poll 修正 | 300 | 27,300 | 0 | 1,152,000 | 0 |
| 全部来源已失效的 packet 调用 | 300 | 8,100 | 0 | 576,000 | 0 |

这是固定编译器/标准库/回执下的堆申请总量，不是常驻内存、CPU 占比或吞吐量。首次创建、标题增长、正常输出包副本和曲线序列化仍有原有成本，没有声称整个监控零分配。Output 的 sizeof 前后均 2,296 字节，无新增成员；既有标题字符串可复用已申请容量，原 bind/reset 释放该记录。

## 数据正确性证据

两份真实生产头各生成 1,942 份包；C++ 在 Node 解码前记录每包原始 json.dump() 字节，最终逐字节对比全部相等，包含整数/浮点类型的原始 JSON 表达。这比仅比较 JS 解码后的数值更严格。

34 项独立断言覆盖：未绑定游戏、初始有效包、相同 poll 修正、重复 poll 不续期、LOW 缺失与真实零值、GPU 空值/整数转换、周期元数据变化、返回包被外部修改后不污染内部状态、旧 poll/NaN/无穷/越界 FPS 拒绝、原期限失效、RTSS 救援及 HWiNFO 恢复、同 PID 不同创建身份/切换游戏/显式 reset。

序列对照另包括 360 轮混合值与缺失值、0/0.5/高 FPS、0/1/1.5/2/3/6/60 秒生产周期、Unicode/引号/反斜杠/换行/4096 字符标题变化和三组各 300 次的正常输入。标题增长会分配，但所有字节结果保持相同。

新增测试最初误把“零 HWiNFO FPS + 仍有效 RTSS”预期为零，冻结源码正确输出 RTSS 兜底。生产修改前已纠正该测试，并另加无 RTSS 的零值用例；原失败回执保留在 test-calibration/，没有将测试预期错误冒充产品缺陷。

当前最终头文件还通过真实普通循环分支/Output 的 39 项、真实唯一 hub/model/两个既有输出协议的 6 项、原帧优先级 16 项及回执接收 4 项，共 99 项断言。前轮冻结缺陷仍保留其预期 19 失败，本轮没有改写其历史证据。协议回归不重新开放隐藏的 Steam 输出。

## 修改范围及未验证范围

生产文件只有 native/monitor_frame_output.h。没有修改 native/main.cpp 的监控循环、HWiNFO 配置脚本、RTSS reader、历史算法、100 毫秒快速通道、1 秒硬件/文件/游戏识别周期、唯一接口、快照大小及日志上限。实际主源优先和曲线关闭后 RTSS 救援继续保持。原有其他任务修改保留，未暂存或提交。

已编译真实相关生产头文件及提取的普通采样分支；未进行完整应用重新构建、实机运行验收或新硬件采样。上游 HWiNFO 实际采纳配置、跨机数据完整性和整体 CPU 成本继续需要推进，不将本轮有限证据扩大为全部完成。

新增回归：tools/monitor_frame_hardware_reuse_selftest.cpp/.mjs。可在主线目录运行 node tools/monitor_frame_hardware_reuse_selftest.mjs；默认与本轮冻结 baseline 比较，输出位于以下证据目录，也可使用 YMCC_FRAME_REUSE_OUT 指定包含冻结 baseline 的独立目录。

证据目录：G:/YeManCC-Work/Build/Tasks/YMCC-Steam-HUD-Probe/frame-hardware-object-reuse-20261016/。

主要文件：BASELINE-PINS.json、native/before/RESULT.json、native/after/RESULT.json、REUSE-RESULT.json、REGRESSION-RESULT.json、regressions/rtss-rescue/WORKER-RESULT.json、regressions/rtss-rescue/CONSUMER-RESULT.json、INVARIANTS-RESULT.json、SCOPED-CHANGES.patch、PATCH-CHECK.json、ROUND-RESULT.json。补丁仅包含本轮变更，使用原冻结头文件作为修改基准，不代表从干净仓库重建此前整个监控功能。
