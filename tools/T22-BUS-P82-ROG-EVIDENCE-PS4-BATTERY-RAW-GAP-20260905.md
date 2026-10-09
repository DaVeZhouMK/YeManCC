# T22-BUS-P82：ROG 证据包复核、PS4 5% 与 DS4 raw/consumer 缺口

日期：2026-09-05
状态：`EVIDENCE-INGESTED / MOTION-NONZERO / IDENTITY-PAIR-UNPROVEN / DS4-BATTERY-RAW-GAP / RUNTIME-BLOCKED`

本页是 BUS 对用户已回传的五个 ROG 证据包进行只读复核后的增量回执。范围是：现有采集包是否能够关闭 ROG provider/epoch/matrix，以及是否能够解释 Steam/系统右下角 `PS4 Controller / 电量低 - 5%`。本轮未启动 YMCC、InputHost、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实硬件；未修改系统状态、正式包、升级器或运行时源码。

## 1. 输入证据与完整性

复核的输入文件：

```text
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-IMU-Evidence-20260905-221742.zip
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-IMU-Evidence-20260905-232115.zip
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-IMU-Evidence-20260906-004029.zip
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-IMU-Evidence-20260906-010005.zip
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-IMU-Evidence-20260906-011852.zip
```

最新包 `YeManCC-ROG-IMU-Evidence-20260906-011852.zip` 的证据如下：

```text
zip bytes       = 37266
zip SHA-256     = 5E59FF7C9416F9926BFF67ED83B4BA86E4F96FDF40321EAF02835E942C5E8AAD
manifest files  = 4
capture UTC     = 2026-09-05T17:19:46.7857365Z
```

manifest 内四个成员的 bytes/SHA-256 均已通过包内声明复核；成员为 `rog-imu-evidence.json`、`ymcc-input-capture.jsonl`、`ymcc-YeManCC-input-capture.jsonl` 与 `ymcc-native-lifecycle.log`。

## 2. ROG motion 事实

最新包的输入采样计数：

```text
ymcc-input-capture.jsonl          = 554 lines / 552 samples
ymcc-YeManCC-input-capture.jsonl  = 521 lines / 519 samples
gyro=true                         = all samples
accel=true                        = all samples
non-zero gyro/accel               = all samples
sample-pair-unproven              = 1 marker per file
```

包中记录的边界仍是：

```text
pnpToWinRtIdentity = UNPROVEN
gyroAccelPair       = UNPROVEN
reportInterval      = READ-ONLY observation; no assignment
```

这证明采集器收到了非零 WinRT motion 数据，但没有证明 HC 所要求的同 provider、同 physical identity、同 epoch、同 calibration/matrix 的 pair admission。不能把 `gyro=true/accel=true` 或连续非零值提升为 `pairProven`，也不能据此补套 ROG 矩阵、漂移修正或直接关闭 T11/T12。

最新包的 lifecycle 只有启动、ROG HID bind、WebView 崩溃与恢复事件；其中 `webview-browser-process-failed` 的 `exitCode=-1073741819` 后有 `webview-recovery-complete`。该事件不携带 DS4 身份或电量字段，不能解释 PS4 5%。

## 3. PS4/DS4 电量与 raw report 搜索结果

对五个 ZIP 的全部成员文本逐包搜索以下 token：

```text
054C  05C4  09CC  PS4  DualShock  battery  BatteryLevel  电量
Steam  descriptor  featureReport  rawReport
```

五个包均为 `0 hit`。现有包没有：

- 虚拟 DS4 的实际 VID/PID、container/instance identity；
- DS4 input report 或 feature report 原始字节；
- report ID、报告长度、byte-14 独立解码；
- Windows/Steam 对同一虚拟设备的电量 readback；
- `BatteryLevel`、charging/full 或其他电量 writer 的运行时 receipt。

因此用户观察到的 `PS4 Controller / 电量低 - 5%` 继续登记为：

```text
PS4 battery 5% producer       = UNKNOWN
HC/YMCC/HIDMaestro attribution = UNPROVEN
DS4 raw/feature provenance      = RUNTIME-BLOCKED
```

这轮没有出现可按锁定 HC 无歧义直接修复的局部偏移。禁止猜写 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`；禁止因为 HC 使用 `054C:05C4` 就把 YMCC 当前 profile 猜切为 v1，也禁止把 HC 31-byte VIIPER report 复制到 HIDMaestro 64-byte profile。

## 4. 三方裁决

```text
锁定 HC DS4 identity                 = 054C:05C4 / CUH-ZCT1x / VIIPER 31-byte
当前 YMCC HIDMaestro profile         = 054C:09CC / CUH-ZCT2 / 64-byte
HC 当前 DS4 battery writer           = NOT FOUND
YMCC 当前 battery writer             = NOT FOUND
HIDMaestro 0→5 implicit conversion   = NOT FOUND
ROG non-zero motion samples          = PROVEN (collector-level only)
same-provider/epoch/matrix admission = UNPROVEN
DS4 raw/descriptor/Steam readback    = RUNTIME-BLOCKED
PS4 5% origin                        = UNKNOWN / RUNTIME-BLOCKED
unambiguous HC source fix            = NOT FOUND
runtimeUpgrade                       = false
```

证据、推测和缺口分开：ROG motion 非零是事实；“身份/profile 差异可能影响 Steam 电量解释”仍只是可能解释，不是归因；PS4 5% 的实际生产者必须由同一 Host epoch 的 DS4 raw/consumer 证据关闭。

## 5. 后续门与用户介入边界

静态侧本轮可继续推进的工作已完成；剩余不是新的猜测空间，而是运行时数据缺口。关闭该缺口需要用户在 ROG 上回传同一 Host epoch 的：

1. 虚拟 DS4 VID/PID、container/instance identity 和 descriptor 原始字节；
2. input/feature report 的 report ID、长度、原始字节及 byte-14 独立 decode；
3. Steam/Windows 对同一 identity 的电量 readback；
4. 若继续关闭 motion，则 provider identity、gyro/accel pair、epoch、calibration/matrix receipt。

在这些资料回来前，主线保持：

```text
T11/T12/T13/T15-T18 = UNENCLOSED / RUNTIME-BLOCKED
PS4 battery 5%       = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade       = false
```
