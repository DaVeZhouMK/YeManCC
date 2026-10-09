# T22-BUS-P68：历史 A3 DS4 电量 raw contract 取证与当前 5% 问题边界

Status: `HISTORICAL-EVIDENCE-LOCATED / NON-HC-ORIGINAL-LOGIC / CURRENT-REACHABILITY-UNPROVEN / PS4-BATTERY-UNKNOWN / RUNTIME-BLOCKED`

本页只读审计历史测试包，目的为解释用户观察到的 PS4 虚拟手柄“电量低 - 5%”。没有修改当前 YMCC 产品源码、正式 Release/updater、HIDMaestro、HidHide、系统状态或真实设备。

## 1. 审计对象与完整性

```text
package = C:\Users\DaVe\Desktop\Test\A3-R2-ROG-GYRO-DS4-20260829-r3.zip
bytes   = 41,943,299
sha256 = 7DC3C07F9340C25DED3351BD987B55D0F149631DC114CC6CEEE41EAC52BC1C86
member  = bridge/publish-fd/A3RogDs4Bridge.dll
dll bytes = 48,640
dll sha256 = 361DA3366E25DE4C38D3595BA1B0EAB3B23B3BF7CAEAD55E60D7E6ADD3577B98
```

包内同时包含 `HIDMaestro.Core.dll`、`Microsoft.Windows.SDK.NET.dll`、`WinRT.Runtime.dll`、`README-ISOLATED-YMCC.md` 和 `manifest.json`。README 声明该 A3 包“虚拟 DS4 电量固定为满电语义”，但这只是包内说明，不能替代运行时 raw report 证据。

## 2. 可重复的 binary evidence

在隔离临时目录加载 `A3RogDs4Bridge.dll`，仅通过 .NET reflection 调用静态 self-test `Program.RunRawBatteryContract()`，得到：

```text
status                = RAW_BATTERY_CONTRACT_PASS
profileId             = dualshock-4-v2
reportId              = 1
reportSize            = 64
dataLength            = 63
batteryByteOffset    = 14
requestedBatteryLevel= 10
encodedBatteryLevel  = 10
reportHex             = 01 80 80 80 80 08 00 00 00 00 00 00 00 00 0A ...
errors                = (empty)
```

这证明历史 A3 binary 内部存在一个把 DS4 battery level 写到 report byte 14 的 raw packing contract，并且该 self-test 使用 `0x0A`（十进制 10）作为测试值。它不是锁定 HC 的 `DualShock4Target.BuildReport()` 路径，也不是当前 YMCC `InputHost` 的已证明生产调用链。

`RunRawBatteryContract()` 本身只加载 profile、读取 extended-report metadata、编码并校验内存中的数组，然后返回 self-test 结果；它不调用 `InstallDriver`、`CreateController`、`Start` 或 `SubmitRawReport`，所以该方法单独不能证明真实 HID 写入。

BUS 对同一 binary 的静态 IL 继续追踪后，确认 `Program.Run(...)` 另有一条实际提交尝试：

```text
LoadDefaultProfiles
→ GetProfile("dualshock-4-v2")
→ InstallDriver
→ CreateController(profile)
→ controller.Start()
→ PackDs4UsbReport(...)
→ HMController.SubmitRawReport(...)
```

`PackDs4UsbReport(...)` 的构造数组中固定写入 `fullReport[0]=0x01`、`fullReport[14]=0x0A`、`fullReport[38]=0x80`、`fullReport[42]=0x80`；随后将 `fullReport.AsSpan(1)` 交给 `SubmitRawReport`。因此历史 A3 确实存在过一次非 HC 的 DS4 raw battery assignment 尝试，但 report ID 的最终拼接仍由 HIDMaestro 内部实现决定，且没有 OS/Steam readback，不能把该数组直接等同于最终 USB 报文。

历史包的 `HidReader` 只具备 HID input-report 枚举/读取与 byte[14] 统计能力；未发现 `HidD_GetFeatureReport`、`HidD_SetFeature`、`HidD_GetInputReport` 或独立 descriptor/feature-report dump。ZIP 也没有附带 descriptor、raw report、feature report 或 Steam/游戏消费者日志。

同一 binary 的 `RunRawPackerSelfTest()` 返回：

```text
status             = RAW_PACKER_SELFTEST_PASS
frameCount         = 4
matchingFrameCount = 4
mismatchCount      = 0
```

## 3. 与 HC、当前 YMCC 三方对账

### 锁定 HC

- `DualShock4Target.BuildReport()` 的 31-byte VIIPER report 代码未发现 battery assignment。
- `DS4OutDevice.bBatteryLvl` 是结构字段，但当前锁定 HC source 未发现可归属的生产写入链。
- HC `DsBattery` 属于 DSU 网络 metadata，不能推导为 HIDMaestro DS4 report byte 14。

### 当前 YMCC

- `InputHost/Program.cs:471-479` 的 `HMGamepadState` 只设置 `Buttons`、`Hat`、`Axes`，没有设置 `BatteryLevel`、`BatteryCharging` 或 `BatteryFull`。
- 当前 `HIDMaestro.Core.dll` 的 `dualshock-4-v2` extended report metadata 明确声明 `Byte=14 / Type=uint8-battery / Semantic=batteryLevel`。
- 反射创建的默认 `HMGamepadState` 的字段值为 `BatteryLevel=0`、`BatteryCharging=false`、`BatteryFull=false`；这只是默认状态事实，尚不能证明 HIDMaestro 或 Steam 将其显示为 5%。
- 当前主线没有 `RunRawBatteryContract`、`A3RogDs4Bridge.dll` 或同名 production import/IPC/writer route 的已证明可达性。

### 历史 A3

- A3 bridge 的 raw battery contract/`PackDs4UsbReport` 是额外的、非 HC 的 encoder 与 raw-submit 逻辑。
- README 的“固定满电”与 self-test 的 `requested/encoded=10` 存在包内语义歧义；没有生产运行 trace，不能判定哪一个值曾被真实送达 Steam。
- README 声称的 ZIP SHA-256 `E70F8BEC414A0AB0DC6671E6624A72EC69ACA13D1DDCD8F5BBC8D29D013F38F4` 与实际包 SHA-256 `7DC3C07F9340C25DED3351BD987B55D0F149631DC114CC6CEEE41EAC52BC1C86` 不一致；包名为 `r3`，随附 PDB 的编译源路径含 `...20260829-r1...`。这两项是历史 provenance 异常，不是当前产品行为证据。

## 4. 裁决：证据、猜测、缺口分开

### 已证实

```text
历史 A3 binary 存在 DS4 report byte 14 的 battery raw contract
历史 A3 `Run(...)` 路径存在固定 byte[14]=0x0A 的 raw-submit 尝试
历史 A3 self-test 可重复通过，测试值为 0x0A
当前 YMCC InputHost 未显式赋 battery 字段
当前 HIDMaestro dualshock-4-v2 metadata 将 byte 14 标为 batteryLevel
```

### 未证实 / 不得猜测

```text
PS4 Steam “5%” 是否来自当前 HMGamepadState 默认值       = UNKNOWN
当前 HIDMaestro encoder 对 null/0/default 的最终编码值     = UNPROVEN
当前虚拟 DS4 实际 input/feature raw report byte 14         = RUNTIME-BLOCKED
当前 report 是否由 A3 bridge 或其他旧组件写入             = CURRENT-REACHABILITY-UNPROVEN
Steam 5% 是否来自 report、缓存、显示策略或其他设备        = UNKNOWN
```

禁止据此猜写 `0x05`、`0x0A`、`100%`、`Full`，也禁止把历史 A3 contract 接回当前产品。按 HC 对齐原则，只有在同一 Host 生命周期取得 descriptor、input/feature raw report、report ID/length、raw bytes、instance/container identity 和独立 decoder/readback 后，才能决定是否存在明确的非 HC 偏差。

## 5. 下一门

本地静态探查在该线索后没有发现新的 HC 轴/符号/单位/倍率/deadzone/漂移算法偏差。要关闭 PS4 5% 缺口，用户介入材料仍为：

1. 当前实际包启动期间的虚拟 DS4 descriptor 与 report ID/length；
2. 同一 Host epoch 的 input/feature raw report（含 byte 14）；
3. 设备 instance/container identity；
4. 独立 battery decoder/readback 与 Steam 显示对应关系。

当前状态继续保持：

```text
PS4 battery 5% origin = UNKNOWN
historical A3 battery writer = NON-HC-ORIGINAL / RAW-SUBMIT-ATTEMPT / CURRENT-UNREACHABLE-UNPROVEN
runtimeUpgrade = false
GYRO VIRTUAL MAINLINE STATUS = RUNTIME_BLOCKED
```
