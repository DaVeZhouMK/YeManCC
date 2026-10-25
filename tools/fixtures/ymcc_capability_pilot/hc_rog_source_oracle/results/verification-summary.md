# 源码级 oracle 验证结果

- 结果：70/70 通过；失败 0。独立 C# 构建 0 警告、0 错误。
- 完成时间：10/18/2026 15:40:10。运行时：.NET 10.0.11，X64。
- Oracle 命名空间：Ymcc.CapabilityPilot.HcReference；类型：HcRogSourceOracle。
- HC 源码：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\deps/handheldcompanion-runtime/source/Devices/ASUS/ROGAlly.cs
- HC 源 SHA-256：04097428F3D62DE75F8160EB47398E1C61606F9D6681EB605D100B384814AA00
- 生成源码：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\fixtures\ymcc_capability_pilot\hc_rog_source_oracle\Generated/HcRogSourceOracle.g.cs
- 生成源码 SHA-256：7105DB427328251FD89A52C08356C3434057C7D34FA6F090D958E8303550844C
- 实际自主 writer：G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\FanLab/fan-backend/Asus/AsusFanCurveWriter.cs
- 本次实际 writer SHA-256：14B09F20EE7EA896AD825AB9FA3C06B135CD24673C07AE15608A50FB4BB03F56
- 捕获层：AsusACPI.SetFanCurve / SetFanCurveObserved 的曲线入口参数，未执行下游 ACPI 钳制或封包。
- 范围：正常 11 点、非法长度/null、数值边界、Software 三写、CPU/GPU/Mid 顺序和数组身份、默认表交还及原方法守卫。
- 隔离：未启动产品、未加载真实 HC 二进制、未调用设备/HC 工厂、没有 native import；未更新依赖。
- 合成值 1001/1002/1003 仅标识调用序号；观察型收据为空的编译占位类型。
- 本次不覆盖主控的能力目录、会话消费链、真实硬件或真实 HC 二进制。完整逐条数据见 oracle-parity-results.json。

主控可在能力释放测试后把 HcRogSourceOracle.ApplySoftware(...) 或 RestoreDefaultCurves(...) 当作 HC source reference 回调；不能把这种内存对照命名为真实 HC 验证。