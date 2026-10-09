# T22 BUS-P91：PS4 5% 本机 HID 身份与源码对账（2026-09-06）

## 结论先行

本机真实设备快照证明：当前系统同时存在一个物理 Xbox 360 和一个 HIDMaestro 虚拟 DualShock 4 v2；但当前 YMCC/HC 源码没有可归属的 battery writer，也没有源码证据把默认值自动转换成 5%。因此：

```text
virtual DS4 identity             = PROVEN / HIDMaestro 054C:09CC
physical Xbox 360                = PROVEN / 045E:028E
current YMCC battery assignment  = NOT FOUND
HC DualShock4 battery assignment = NOT FOUND
HIDMaestro 0 -> 5 conversion     = NOT FOUND
PS4 5% producer                  = UNKNOWN / RUNTIME-BLOCKED
```

## 本机只读证据

采集文件：

```text
Mainline/Build/Validation/Physical-ROG-Input-Gap-20260906-8/rog-input-gap-evidence.json
```

### 虚拟 DS4

```text
FriendlyName = Wireless Controller
InstanceId   = HID\HIDCLASS\1&4784345&10&0000
HardwareIds  = HID\VID_054C&PID_09CC, HID\HIDMaestro, ...
ContainerId  = 00000000-0000-0000-FFFF-FFFFFFFFFFFF
```

该结果与当前 `InputHost/Program.cs` 固定选择 `dualshock-4-v2` 一致；它不是物理 Sony 手柄，而是 HIDMaestro root HID device。

### 物理 Xbox 360

```text
FriendlyName = 支持 Windows 的 XBOX 360 手柄
HardwareIds  = USB\VID_045E&PID_028E
ContainerId  = E69E2004-8DA6-11F1-BCD9-8C688B206ADB
```

这两条 identity 分属不同设备；不能把 Xbox 360 的输入或电量信息当成 DS4 的 battery report。

### 生命周期异常

本机当时存在 7 个 `YeManInputHost.exe` 实例，分别带不同的 pipe/nonce。该事实证明存在多 Host 生命周期残留或并发实例，但当前快照没有显示其中任何一个写入 `BatteryLevel=5`；它只能登记为 lifecycle/cleanup 缺口，不能被提升为 5% 归因。

## YMCC / HIDMaestro 源码事实

### 当前 InputHost

`InputHost/Program.cs` 的 `TrySubmitFrame()` 与 `TrySubmitNeutral()` 只构造：

```text
HMGamepadState.Buttons
HMGamepadState.Hat
HMGamepadState.Axes
```

没有写入：

```text
BatteryLevel
BatteryCharging
BatteryFull
```

### 锁定 HIDMaestro 1.7.0 静态证据

`T22-BUS-P71-HIDMAESTRO-DS4-BATTERY-ROUTE-STATIC-PROBE-20260905.md` 已记录：

```text
dualshock-4-v2 battery metadata = byte 14 / uint8-battery
HMGamepadState default           = BatteryLevel 0, Charging false, Full false
profile AlwaysArmed              = false
profile ArmOn                    = null
standard SubmitState             = Buttons/Hat/Axes route
```

受控 encoder probe 的原值编码为：

```text
0   -> 0x00
5   -> 0x05
10  -> 0x0A
100 -> 0x64
255 -> 0xFF
```

所以静态源码不能支持“默认 0 被 HIDMaestro 自动转成 5%”这一说法。

### HC 源码事实

锁定 `HandheldCompanion/Targets/DualShock4Target.cs` 的 `BuildReport()` 只写摇杆、按钮、触摸和 raw gyro/accel；没有 battery percentage、charging 或 full flag assignment。

## 事实、推断、未知分离

### FACT

- 当前虚拟 PS4/DS4 identity 是 HIDMaestro `054C:09CC`；
- 物理 Xbox 360 identity 是 `045E:028E`；
- 当前 YMCC `SubmitState` 没有 battery writer；
- HC `DualShock4Target` 没有 battery assignment；
- HIDMaestro 已知 encoder 是原值写入，不存在静态 `0→5` 转换证据；
- 当前机器存在 7 个 InputHost 实例。

### UNKNOWN

```text
Steam/Windows/GameInput 是否把某个空值、0、无效 report 或缓存状态规范化显示为 5%
是否存在另一个未被当前源码快照覆盖的 HID/consumer route
当前虚拟 DS4 的最终 input/feature report 原始字节和 OS/Steam 同设备 readback
```

### 禁止推断

不因观察到 5% 就写 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`；不把 HC `054C:05C4` 直接替换成 HIDMaestro v2 的 `054C:09CC` wire contract；不把 7 个 Host 进程直接认定为 battery producer。

## 采集器修正

ROG gap collector 另有两个兼容性修正：

1. C# 代码改掉旧 Windows PowerShell 不支持的字典索引初始化语法；
2. SetupAPI/HidD snapshot 和 raw report 读取改为显式 opt-in，默认只采集 targeted PnP identity，避免真实 HID 同步调用阻塞整个采集器；`-ReadRawReports` 仍不会自动启用。

本机用真实手柄执行了安全 identity-only 采集，退出成功；该文件只证明本机设备身份和生命周期观察，不关闭 Steam battery consumer 缺口。

最终裁决：

```text
PS4 battery 5% origin = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade       = false
```
