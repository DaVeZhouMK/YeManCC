# T22-BUS-P86：ROG 运行日志包复核与 PS4 电量缺口

日期：2026-09-05  
状态：`RUNTIME-LOG-INGESTED / ROG-ONLY / DS4-EVIDENCE-ABSENT / BATTERY-UNKNOWN / RUNTIME-BLOCKED`

本页复核用户提供的 `C:\Users\DaVe\Desktop\陀螺仪\YeManCC.zip`。这是一个包含真实运行日志的清理包，但不是 DS4/HID/Steam consumer evidence bundle；本轮只读读取 ZIP，没有启动程序、设备、HidHide、HIDMaestro、Steam 或游戏，也没有修改系统状态。

## 1. 包完整性与成员

```text
zip bytes   = 147060
zip SHA-256 = BB22751194710E73F8FE0D17D5D70BDDB0A4AB9508D1337B1BBA008C1B3B0DAE
```

可归属的运行成员为：

```text
native-lifecycle.log   = 1405 bytes / 6 JSON lines
recovery-service.log   = 168 bytes / 1 JSON line
webview-failures.log   = 1269 bytes / 8 JSON lines
input-capture.jsonl    = 1476 bytes / 3 JSON lines
```

包没有独立 `manifest.json`、artifact SHA-256 清单、Steam/游戏日志或 DS4 HID capture manifest；因此证据质量不升格为 T17/T18/R1 runtime closure。

## 2. 真实运行事实

`native-lifecycle.log` 证明一次 ROG 运行实例完成了：

```text
boot-single-instance-acquired
window-created
recovery-service-started
rog.hid-bound: usbProductId=6988 (0x1B4C), featureLen=64, inputLen=16
rog.xbox-face-enabled: wrote=true
```

`webview-failures.log` 是正常 environment/navigation/render-ready 链，包含 runtime/browser version `152.0.4191.62`、navigation-complete 和 render-ready-complete；该包没有 browser crash/failure 事件。

`input-capture.jsonl` 的完整采样边界为：

```text
kind=start              = 1
kind=sample-pair-unproven = 1
kind=sample             = 1
sensorFamily            = windows-default-winrt
gyro/accel provider      = ACPI BOSC0200 device IDs
requested interval      = 8 ms
observed interval       = 10 ms per sensor
locked                  = false
matrixIdentity          = unresolved-rog-pid-family/raw
```

采样记录明确写出 `provider-capability-matrix-not-bound`、`safeZero=true`，所以它证明的是 ROG 诊断数据进入了采集器，不是 HC 同 provider/epoch/matrix 已准入，更不是虚拟 DS4 输出或 Steam 读回。

## 3. PS4/DS4 电量搜索

对该包的四个运行日志成员搜索：

```text
054C / 05C4 / 09CC / PS4 / DualShock / BatteryLevel
Steam / descriptor / rawReport / featureReport / reportId / byte14 / 电量 / 5%
```

结果全部为 0 命中。`usbProductId=6988` 是 ROG 内部 HID `0x1B4C`，不是 Sony DS4 `054C:*`。

因此该包不能证明：

- 虚拟 DS4 是否被创建；
- 虚拟 DS4 的 VID/PID、container/instance 或 descriptor；
- DS4 input/feature raw report 和 byte-14；
- `BatteryLevel` 的实际发送值；
- Windows/Steam 对同一虚拟设备的电量读取。

## 4. 三方裁决

```text
ROG runtime log                     = FACT / ROG-only
ROG HID identity                    = FACT / 0x0B05:0x1B4C path
WinRT gyro/accel sample             = FACT / collector-level
same-provider/epoch/matrix proof    = UNPROVEN
DS4 identity/raw/descriptor        = ABSENT
Steam/Windows battery readback     = ABSENT
HC/YMCC/HIDMaestro battery writer  = NOT FOUND
PS4 battery 5% origin              = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade                     = false
```

这个新包没有形成可按 HC 直接修正的电量偏差；不写入 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`，不把 ROG HID 绑定误写成 DS4 证据。

## 5. 下一介入门

仍需在同一 Host 生命周期额外采集虚拟 DS4 VID/PID、descriptor、input/feature raw report、byte-14 独立解码和 Steam/Windows 电量 readback。该包的 ROG IMU 证据不关闭 P-HID/P-XINPUT/P-OWNER、DS4 consumer 或 PS4 battery 缺口。

