# T22-BUS-P73：PS4 电量低 5% 运行时复现登记与三方边界

日期：2026-09-06
状态：`USER-OBSERVED / STATIC-RECONCILED / UNKNOWN / RUNTIME-BLOCKED`

本页登记用户在另一台测试设备上再次观察到的现象：Steam/系统右下角持续显示 `PS4 Controller`，并提示“电量低 - 5%”。本页只记录复现现象和已完成的 HC/YMCC/HIDMaestro 静态边界，不把 UI 数字提升为 raw HID 或产品源码事实。

## 1. 用户运行时观察（非本机独立回读）

```text
observed consumer = Steam/系统右下角
persona           = PS4 Controller
displayed battery = 5%
repeatability     = 用户报告为持续出现
capture           = 未提供同一 Host 生命周期的 descriptor/raw/feature readback
```

这条记录改变的是问题的复现状态，不改变电量来源裁决：它仍然不能单独证明是 YMCC、HIDMaestro、HC、Windows PnP、Steam 缓存或其它 producer 写入。

## 2. 三方静态事实

### HC locked source

- `DualShock4Target.BuildReport()` 未发现 battery assignment。
- `DS4OutDevice.bBatteryLvl` 只是结构字段；未发现可归属的生产写入链。
- HC `DsBattery` 属于 DSU metadata，不等于 HIDMaestro DS4 report。

### Current YMCC source

- `InputHost/Program.cs` 的 `TrySubmitFrame()` 与 neutral 路径只设置 `Buttons`、`Hat`、`Axes`。
- 当前 Host 没有已证明的 `BatteryLevel`、`BatteryCharging`、`BatteryFull` writer。
- 当前 direct DS4 IMU 仍为 no-report/safe-stop；不得把旧文档中的 `GyroDps` 叙述当成 current route。

### Locked HIDMaestro static route

- `HMGamepadState.BatteryLevel` 默认值为 `0`，充电/满电布尔值默认均为 `false`。
- `dualshock-4-v2` extended metadata 具有 report `0x01`、64 bytes、byte `14` 的 `uint8-battery` 字段，但 `AlwaysArmed=false`、`ArmOn=null`。
- `VendorBlobCodec.EncodeInput` 对 `BatteryLevel` 做原值编码；受控静态结果为 `0→00、5→05、10→0A、100→64、255→FF`，未发现 `0→5` 隐式转换。
- 当前 standard `SubmitState` 路径没有已证明的 DS4 battery report 写入。

## 3. 历史非-HC线索隔离

历史 A3 bridge 曾在 `PackDs4UsbReport()` 中固定构造 `fullReport[14]=0x0A`，并存在 `SubmitRawReport` 尝试；该路径属于历史、非 HC、无最终 OS/Steam readback 的静态线索。它不能迁移为当前 YMCC 行为，也不能解释本次 5% 现象。

```text
historical A3 fixed battery writer = NON-HC-ORIGINAL / HISTORICAL / NOT-CURRENT
current 5% source                 = not established by that artifact
```

## 4. 裁决：Fact / Inference / Gap

```text
FACT
  用户重复观察到 Steam/系统显示 PS4 5%。
  HC locked source 未找到 DS4 battery assignment。
  current YMCC InputHost 未找到 battery assignment。
  locked HIDMaestro encoder 未找到 0→5 隐式转换。

INFERENCE (not closure)
  5% 更可能来自未覆盖的 producer/consumer/cache/descriptor route，
  但不能在没有同生命周期 raw evidence 时指定来源。

GAP
  缺少同一 Host epoch 的虚拟 DS4 device identity、descriptor bytes、
  input report 与 feature report 原始十六进制、report ID/length、
  byte 14 独立解码、OS/Steam readback 和时间关联。
```

## 5. HC 偏差与原创逻辑检查

```text
new HC axis/sign/unit/gain/deadzone/velocity-decay drift = NOT FOUND
new original drift algorithm                               = NOT FOUND
new current YMCC battery writer                            = NOT FOUND
safe action                                                = do not write 0x05/0x0A/100/Full
```

没有发现可以按锁定 HC 无歧义直接修改的产品源码偏差。本轮不修改 `InputHost`、HIDMaestro、HidHide、系统状态、正式包或真实设备。

## 6. 关闭条件与介入门

要关闭此问题，必须在一次 Host 启停周期内同时取得：

1. 虚拟 DS4 的 instance/container identity 与 descriptor 原始字节；
2. input report 和 feature report 的 report ID、长度、原始十六进制及 byte 14 解码；
3. 最终 OS/Steam 显示的同一 device identity、时间戳和电量值；
4. 若 byte 14 已为 5，再继续沿 Host→HIDMaestro→driver→consumer 链定位 writer；若 raw 不含 5，则转查 OS/Steam consumer/cache。

在上述证据回来前，保持：

```text
PS4 battery 5% origin = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade       = false
```
