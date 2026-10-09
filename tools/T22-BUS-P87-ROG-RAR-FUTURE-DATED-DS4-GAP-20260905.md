# T22-BUS-P87：ROG RAR 快照的当前日期与 DS4 缺口

日期：2026-09-05
状态：`RAR-INGESTED / CURRENT-DATED-EVENTS / ROG-ONLY / DS4-BATTERY-ABSENT`

本页复核 `C:\Users\DaVe\Desktop\陀螺仪\YeManCC.rar`。读取仅使用本机 7z 的列表和标准输出解压，没有写出归档内容、启动产品或修改系统状态。

## 1. 归档身份

```text
format      = RAR5
archive SHA = 74EDE9B3B0A141D6323AEDCF3E9B7AE98BA483B4A58ABE51D0A85C50CD84BB05
selected members:
  YeManCC/native-lifecycle.log
  YeManCC/recovery-service.log
  YeManCC/webview-failures.log
  YeManCC/input-capture.jsonl
```

该 RAR 没有单独 DS4/HID/Steam manifest；本页只把四个日志成员作为有限运行证据处理。

## 2. 日期边界

RAR 内 native lifecycle 事件时间为 `2026-09-06T00:40:29Z`。当前适用日期为 `2026-09-06`，所以这些事件是 `CURRENT-DATED`，可以纳入当前 ROG runtime snapshot；但它们仍然不能被写成 DS4/Steam 电量闭口，也不能覆盖缺失的 DS4 原始证据。

## 3. 内容事实

日志显示：

```text
rog.hid-bound       = usbProductId=6988 (0x1B4C), stable input=true
featureLen          = 0 → 64 during binding retries
inputLen            = 16
rog.xbox-face-enabled = wrote=true
input capture       = start + sample-pair-unproven + sample
provider            = ACPI BOSC0200 / Windows default WinRT
matrixIdentity      = unresolved-rog-pid-family/raw
safeZero            = true on pair-unproven marker
```

四个成员中没有 `054C/05C4/09CC`、PS4、DualShock、BatteryLevel、Steam、descriptor、raw/feature report、report ID、byte-14 或电量文本。`0x0B05:0x1B4C` 仍是 ROG 内部 HID，不是 Sony DS4。

## 4. 裁决

```text
RAR runtime content          = ROG-only / current-dated
DS4 identity/raw evidence    = ABSENT
Steam/Windows battery readback = ABSENT
PS4 battery 5% origin       = UNKNOWN / RUNTIME-BLOCKED
new HC source fix            = NONE
runtimeUpgrade              = false
```

该 RAR 没有新增可按 HC 直接修复的电量或 persona 偏差；当前日期的 ROG 事件仍不等于 DS4/Steam 电量闭口，不写任何猜测性的 battery 值。
