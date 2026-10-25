# Legion Tablet/GoS pure logical IMU decoder

父线程 main/settings 使用 `#include "legion_input_protocol.h"`，namespace `ymcc::legion_input`：

```cpp
const auto decoded = ymcc::legion_input::decodeLogical(
    ymcc::legion_input::Protocol::GoS, logical, capacity, actualBytes, 0);
// Tablet: Protocol::Tablet, sensorIndex=0 左 / 1 右。
// decoded->{gx,gy,gz,ax,ay,az} 可直接赋给 controller Session 的六轴。
```

`decodeLogical` 承诺输入是一个已归一的 logical report。不猜/剥除 prefix，不按 byte0 猜 Report ID；父线程负责 exact model+VID/PID+usage+单个实际 report 及 zeroReportID 一次归一。capacity 是可访问存储，actualBytes 是归一后的真实返回计数；actual>capacity、空指针、未知 protocol/sensorIndex 拒绝，不能用填零容量伪造完整报告。

Tablet 按固定 primary C# Tick 原两侧 switch：左 accel35..40、gyro41..46，所以最小47；右 accel48..53、gyro54..59，所以最小60。仅检查所选侧完整 core，不再沿旧全60门槛拒绝完整左侧47字节。**不读取 byte1 推移 IMU**，byte1=0/非0均按同一固定索引解码。

固定历史 LegionController AttachDetails 的 -2 只改 STATE/TOUCH/FRONT/BACK/EXTRA，没有修改 L/R ACCE/GYRO。旧 YMCC legionImuParse 将 IMU 也 -2 是缺乏固定 source 支撑的实现偏差，本模块予以纠正；没有实机证据证明特定固件必须 shift，若后续出现，须另取真实报文/固件日志核实，不能在此猜测。测试直接编译、执行从实际 C# Tick 提取的原两侧 switch，不加入 -2 adapter。

GoS 仅 sensorIndex=0；little-endian IMU 最后 i16 位于24/25，因此 minimum actualBytes=26。HC LegionControllerS reader 构造容量33不是 actual count 白名单，26、27、33及更长完整报告均可解码；Tablet 同样不加 exact length 白名单。GoS 对应的是 HandheldCompanion 源 wrapper LegionControllerS.cs，不得混同 controller-hidapiDLL 中的 decoder；本验证不执行任何 shipped HC wrapper/native DLL。

i16 缩放4/32767 g、2000/32767 dps，包含 -32768，不 clamp 成对称端点、不加轴阈值。Tablet 两侧大端；GoS 小端，不猜 endian。全零静止样本合法。模式、设备选择、物理身份和生命周期不属于本模块。

`node tools/legion_input_protocol_selftest.mjs` 使用已有 MSVC Release/Checked 编译、执行新头。已有本地 .NET SDK/compiler/reference pack 直接编译提取的纯 C# switch/body，实际执行得到765个向量的 float bit oracle；不 restore package，不下载/安装，不加载 HC/GamepadMotion/hidapi DLL，不访问 HID。覆盖手工 pins、signed extremes、两侧、byte1不偏移、swapped endian、每个short actual count、capacity/actual分离和有行为价值的变异体。

所有证据在 workspace `Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/LegionFamilies/Protocol/<fresh>/`。logical->physical transport framing、实际 shipped wrapper/DLL 与 source 对应、真实设备/报文、RawInput/settings 接线、wire/Host 和完整硬件支持各须独立证明；单模块通过不等于硬件验收。
