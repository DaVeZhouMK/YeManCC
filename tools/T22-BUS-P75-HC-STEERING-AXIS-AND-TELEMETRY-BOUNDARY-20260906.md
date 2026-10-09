# T22-BUS-P75：HC SteeringAxis 与非法 telemetry 语义边界

日期：2026-09-06

状态：`HC-SOURCE-RECONCILED / NO-DIRECT-FIX / UNKNOWN-GAPS-EXPLICIT / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC source、当前 YMCC source/contract 和任务书的窄范围三方对账。重点是 `SteeringAxisIndex` 是否可以直接接入，以及 HC 对非法 motion telemetry 是否定义了可迁移的 finite→zero 语义。

## 1. 锁定 HC SteeringAxis 事实

锁定 HC `Misc/Profile.cs:48-53,215` 定义：

```text
Roll = 0
Yaw  = 1
Auto = 2
default = Roll
```

`Views/Pages/ProfilesPage.xaml:1332-1365` 的 ComboBox 顺序固定为 `Roll / Yaw / Auto`；`ProfilesPageViewModel.cs:943-955` 负责 profile 读写和更新。

锁定 HC `MotionManager.cs:115-123,139-161` 的实际分支为：

```text
Roll:
  保持 gyro/accel 原方向

Yaw:
  对 Default gyro/accel 和 DSU gyro/accel 使用
  SwapYawRoll(v) = (v.X, -v.Z, -v.Y)

Auto:
  Windows/SerialUSBIMU 直接按 Yaw；
  Controller 根据 abs(DefaultAccel.Z) > abs(DefaultAccel.Y)
  选择 Yaw，否则保持原方向。
```

HC `Profile.cs` 中的 `Auto // unused` 注释与 `DetermineSteeringAxis()` 的实际 Auto 分支矛盾；这是 HC 内部注释歧义，不能按注释把 Auto 当作未实现。

## 2. 当前 YMCC 对账事实

当前 YMCC：

- `inputContracts.ts` 的 `axisOrder` 是 `XYZ/XZY/YXZ/YZX/ZXY/ZYX` 轴排列字段，不是 HC 的 `SteeringAxisIndex`。
- `GamepadMotionPlaneTelemetryV1.steeringAxis` 只是 producer claim，当前允许 `roll/yaw`，不是用户 profile 设置，也没有 `auto`。
- `GyroMotionView.vue` 未提供 HC `Roll/Yaw/Auto` 选择控件。
- `settingsRepository.ts` 没有 `steeringAxis` schema/normalizer/binding。
- `gyroMotionMapperMock.ts` 未消费 `axisOrder`，也没有 HC `DetermineSteeringAxis()` / `SwapYawRoll()` 的等价实现。

因此：

```text
axisOrder ≠ HC SteeringAxisIndex
producer steeringAxis claim ≠ user setting
UI/schema-only addition would not prove motion/Host consumption
```

不能把 `axisOrder` 重命名为 `steeringAxis`，也不能仅新增一个 UI 下拉框就宣称 HC parity；那会制造伪闭口并绕过 provider/sensor-family/epoch/config receipt。

## 3. HC 非法 telemetry 语义边界

锁定 HC source 的可定位行为：

- `IMUGyrometer.cs:117-131` 只对 threshold 超限执行 `abs(value) >= threshold → 0`；没有发现统一 `IsFinite/NaN/Infinity` 合同。
- `IMUAccelerometer.cs:114-128` 读取并重排数据，没有发现统一非法值归零或 invalid-reason 输出。
- `GamepadMotion.cs:91-133` 直接保存并送入 native，没有统一 finite→zero policy。
- `InputUtils.cs:137-157,304-341` 没有定义全局“非法 telemetry = 0”语义。

当前 YMCC 的 finite→0 只能被视为本地诊断归一化策略，不是 HC 证据；合法静止 zero 与非法 zero 仍可能混淆，必须有显式 invalid/no-report reason 才能作为运行时闭口的一部分。

## 4. 裁决

```text
HC SteeringAxis parity                 = UNENCLOSED
Auto sensor-family parity              = UNKNOWN / RUNTIME-BLOCKED
HC invalid→zero semantic parity        = UNKNOWN
YMCC finite→0 as HC behavior           = NOT PROVEN
invalid zero vs true stationary zero   = DIAGNOSTIC GAP / AMBIGUOUS
new axis/sign/unit/gain drift           = NOT FOUND
new original drift algorithm           = NOT FOUND
PS4 battery 5% origin                  = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade                         = false
```

本轮没有修改源码或 JSON。关闭条件是同一 provider/sensor-family/epoch 的原始 gyro/accel、HC 选择 receipt、config revision/hash、重排后 frame 和 Host/HID/consumer readback；在此之前保持 `UNENCLOSED / RUNTIME-BLOCKED`，不猜接入 Auto、矩阵、zero reason 或电量值。
