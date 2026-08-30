# FanHost 剥离 LHM 初步成功记录

日期：2026-08-29

## 结论

FanHost 已完成 LibreHardwareMonitor（LHM）温度监控逻辑的初步剥离，温度源改为复用现有 HWiNFO 共享内存通道。

本阶段属于“逻辑剥离”而不是删除 HC 冻结依赖文件：`LibreHardwareMonitorLib.dll` 仍保留在 HC 运行时闭包和 payload 中，但 FanHost 不再加载它、不再调用它轮询温度，也不再因为该文件的内容哈希读取失败而阻止启动。

## 当前职责边界

- HWiNFO：提供 `CPU (Tctl/Tdie)` 温度，缺失时回退 `CPU Package`。
- FanHost：只读取 HWiNFO 共享内存，不启动第二套硬件传感器后端。
- HC：继续负责风扇曲线、占空比和设备回调规则。
- LHM：不再作为 FanHost 温度源或每秒轮询来源。

## 验证结果

- FanHost Release 构建通过，0 警告、0 错误。
- FanHost 自测通过，包括 HWiNFO 温度选择和 OpenEvents 生命周期顺序检查。
- payload 自测通过，运行时闭包审计通过。
- payload 清单验证通过，共 156 个文件。
- Windows 安装 ACL 验证通过。
- LHM 文件被占用时，FanHost 仍可完成只读启动/握手链路，证明启动不再依赖 LHM 内容哈希读取。
- 实际日志已从原先的 LHM 验证失败推进到 HC `OpenEvents` 阶段；随后已修正 ROG `IsOpen` 前置检查顺序。

## 尚待确认

目标 ROG 机器仍需实际复测：睡眠唤醒、`Open -> OpenEvents -> Device_Inserted(true)`、风扇启用以及 60 秒/30 次恢复窗口。当前记录不把未在目标设备上完成的实机睡眠唤醒验证标记为最终完成。
