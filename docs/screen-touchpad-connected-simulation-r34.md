# 屏幕触摸板 R34：隔离连通模拟

2026-10-16 用户新增授权：“你要么做一个模拟连通环节 用模拟来测试算了 这样猜测搞不定”。此前不做测试的限制对这轮隔离模拟解除；R33拟置blocked已被该指令替代，整体闪烁目标仍active且未完成。

## 实际执行范围

生产 native 触摸板 Core/event、外层 snapshot/normalize/snapshotJson 及机械提取原文的帧装配，生成2,140帧；真实快速序列化函数原文随后生成完整requestText，逐字节与通用JSON路径2,140行相等，再原样进入Host，未由夹具修正请求。冻结 InputHost 的真实 JSON parser、HostSession.Handle 的 tuple/sequence/phase 准入、TrySubmitFrame、单写者接管和64B编码器接收这些数据；SHA锁定 SDK 的 SubmitRawReport、SharedMemoryIO、InputPumpLoop、USBIP CommandLoop/HandleSubmit/RET_SUBMIT及socket发送均实际执行。模拟客户端从独立本地TCP连接接收回复，保存请求/回复原始头及完整payload。

SDK对象及prepared Host上下文仅用私有内存、无名事件、元数据profile与本地socket初始化，未调用会创建设备/解压驱动的生命周期构造器。目标创建、真实进程管道、Windows VHCI/HID completion、Steam鼠标转换和显示不在覆盖范围。不能把这个接收端称为Steam或已证明完整实机链路。

## 结果与判断

- 12组native→wire场景共2,140帧，覆盖左右/双侧持续滑动、中心零值、边界夹取、静止、正常UP、快速重触、epoch/ready变化、tap及延迟首发。选定触摸/点击/坐标/压力/counter契约通过；源生命周期和raw.Hex另由独立脚本验证，不用Host解析结果反填期望。
- 独立生产者按局部高精度约8ms等待提交，分别保持1和4个USB读请求；各181帧（含最终释放）实际收齐，无意外松触/错坐标。记录实际提交间隔，不把Thread.Sleep(8)当作精确8ms；未调用timeBeginPeriod或调整系统时钟分辨率。
- 无新输入时，真实pump保持读请求等待，100ms内没有自行制造松触；显式UP完成该请求。提交后的复用buffer改写与32bit报文/共享序号环回均通过。
- 反例真实复现：绕过现有接管、保留原SDK idle时，源触摸保持却会收到松触帧。现有主线接管可阻止该写者；这不是已接管后的现场根因证明。
- 当前链路边界真实复现：pump停顿时，单槽共享状态可合并touch→UP→新touch；消费者不读时，SDK FIFO最多保留8帧，较早的真实UP也会被挤掉。该行为需要停顿或读取落后，不能直接解释同ID持续MOVE的闪烁。
- 源侧快速非tap UP+新DOWN均发生在相邻快照间时，两次wire均touch=true且XY可大跳。普通Mouse新DOWN重建位移基准。该条件已从静态候选变成隔离复现，仍不是现场持续滑动归因。

## 修改与CPU边界

本轮增加的是研究目录中的夹具、回放脚本与证据，以及维护文档/索引。没有改生产手势、SDK、刷新率、报告格式、过滤器、运行时线程或逐帧日志；本轮运行时CPU路径无新增工作。模拟使用的高精度等待、逐帧追踪和故障注入仅存在于隔离夹具。没有据此声称实机CPU实测或闪烁改善。

当前没有满足“导致用户持续滑动闪烁”的新增行为修补证据，因此不把UP合并边界强行改成固定插帧、延迟重触或提高频率。现有R28单写者修补继续保留。

完整可复查交付：[研究目录报告](../../../../Build/Analysis/Touchpad-Producer-R34-20261016/REPORT.md)、[隔离回放入口](../../../../Build/Analysis/Touchpad-Producer-R34-20261016/RUN-SIMULATION.ps1)。原始构建和运行结果在该目录HostTransport；native独立源/协议校验在NativeProducer。生产文件与固定DLL身份见SOURCE-MANIFEST.json及SOURCE-CURRENT.json。
