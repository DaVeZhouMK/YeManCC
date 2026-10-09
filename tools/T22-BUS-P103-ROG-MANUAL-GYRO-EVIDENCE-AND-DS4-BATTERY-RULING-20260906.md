# T22 BUS-P103：ROG 手动陀螺仪证据与 DS4 电量产品裁决（2026-09-06）

状态：`GYRO-DATA-RECEIVED / PAIR-SAFE-ZERO / BATTERY-PRESENTATION-USER-APPROVED / TARGETED-GAP-OPEN`

## 1. 用户证据包

```text
source = C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-151549.zip
bytes  = 121,715
sha256 = 9A9972519FCBB26DF63E6073FB5F1A385448392F607F6C10E963C733DBCE7F62
capture = 2026-09-06T07:15:49Z → 07:16:26Z
duration = 30 seconds
launchRequested = false
startsYeManCC = false
```

本包是手动 YMCC + 后台 sampler 模式。采集器未调用 HidHide、未发 OEM feature report、未创建虚拟控制器、未修改 Steam/游戏；HID raw report 与 Steam readback 均未开启。

## 2. 陀螺仪数据结果

从 `imu-ymcc-input-capture.jsonl` 的 589 个 `sample` 行复核：

```text
gyro magnitude min/avg/max = 0.3287 / 369.3067 / 2996.3751
gyro magnitude <= 5 deg/s    = 396 samples
accel magnitude min/avg/max  = 0.7009 / 1.0063 / 1.5703 g
gyro=true / accel=true       = present on sample rows
confidence                   = 0
matrixIdentity               = unresolved-rog-pid-family/raw
sample-pair-unproven         = 1 gate record
playerSpace non-zero         = 0
worldSpace non-zero          = 0
right-stick rx/ry non-zero   = 0 / 589 samples
```

可确认：

- ROG WinRT gyro/accel 数据真实进入 YMCC 采集链；
- 运动幅度明显非零，且同时存在低角速度样本；
- 采集器和用户控制的 YMCC 进程同时存在于同一窗口。

不能确认：

- same-provider/same-epoch pair；
- HC selector → matrix → calibration → first paired sample；
- GamepadMotion 处理后输出；
- 右摇杆 HID/Steam/游戏 consumer receipt。

当前 `rx/ry` 全为零与任务书的安全门一致：`confidence=0`、pair 未证明时保持 `safe-zero`。这不是采集器丢失 gyro 数据，也不是电量问题。

## 3. DS4 电量三方裁决

### 既有事实

- 旧 P93/P97 曾撤回 `BatteryLevel=10 / BatteryCharging=false / BatteryFull=true`，原因是它不是 HC DS4 source-level writer 事实。
- 当前 HIDMaestro 使用虚拟 DS4 state contract；该 state contract 允许 `BatteryLevel 0..10`、`BatteryCharging` 和 `BatteryFull` 字段。
- 用户本次明确裁决：YMCC 使用 HIDMaestro 虚拟 DS4，保持满电 presentation 是产品选择偏差，不属于 HC 的 gyro/accel/matrix/lifecycle 偏移。

### 当前源码处置

`InputHost/Program.cs` 的 `TrySubmitFrame()` 与 `TrySubmitNeutral()` 已恢复：

```csharp
BatteryLevel = 10;
BatteryCharging = false;
BatteryFull = true;
```

并在源码中标注为：

```text
USER-APPROVED YMCC PRODUCT PRESENTATION DELTA
HC motion/accel/matrix/lifecycle parity unaffected
```

这不是 DGF 新编号，也不改变 T11/T12/T16/T17 的 HC motion/descriptor 状态。

### 电量优先级与证据边界

本机/ROG “未出现 5%”不能单独证明旧补丁仍在，因为本次包：

- `rawReports=DISABLED_SAFE_DEFAULT`；
- `steamConsumerReadback=ABSENT_NOT_PERFORMED`；
- 未记录 DS4 descriptor、raw battery byte 或独立 Windows/Steam readback。

因此产品层处置为：

```text
虚拟 DS4 电量 presentation = USER-APPROVED / RESTORED
HC battery wire parity      = UNKNOWN / NOT CLAIMED
实际陀螺仪主线             = independent / higher priority
```

## 4. 构建回执

```text
dotnet build InputHost/YeManInputHost.csproj -c Release --nologo
exitCode = 0
warnings = 0
errors = 0
YeManInputHost.dll sha256 = 0C4DC714C8AF3468FBA10D509FB8EE4A4836F303AF407FED431EB6C15E3CDE51
```

protocol selftest 进程正常退出；本轮未执行真实 HID/Steam/game readback。

## 5. 下一步

陀螺仪主线继续优先处理：

1. same-provider pair / HC matrix / calibration / first paired sample；
2. pair gate 解除后确认 `rx/ry` 是否从 safe-zero 进入真实 Host frame；
3. 再做 UI 轴线图、虚拟右摇杆和 Steam/game consumer 对账。

DS4 电量只作为低优先级 presentation 记录，不再阻塞上述运动链路。
