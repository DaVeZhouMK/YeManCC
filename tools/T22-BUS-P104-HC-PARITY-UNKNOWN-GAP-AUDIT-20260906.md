# T22 BUS-P104：HC parity unknown / gap / ambiguity audit（2026-09-06）

状态：`STATIC-AUDIT-COMPLETE / SOURCE-DIVERGENCE-LOCATED / RUNTIME-BLOCKED / NO-SOURCE-FIX-IN-THIS-PASS`

## 1. 审计边界

本轮承接 P103，目标是扩大“未知、缺口、歧义”检索，核对：

```text
WinRT Gyro/Accel
→ provider identity / pair / timestamp
→ HC matrix / threshold / GamepadMotion calibration
→ Default / PlayerSpace / WorldSpace
→ native capture / InputHost canonical frame
→ UI telemetry / preview
→ HID / Steam / game consumer
```

本轮只读。没有运行真实设备、InputHost、HIDMaestro、HidHide、Steam、游戏或硬件；没有修改 C++、C#、TypeScript、JSON、二进制或构建产物。没有解除 `pairProven=false`，没有填写 ROG matrix，没有把 raw gyro 直接送入 Host。

## 2. current-source 证据

本轮对账的源码快照（SHA-256）：

```text
native/main.cpp                                      BE5651E59B97C2BAAF8CE752047A2C088CCC44E296D7E078BBA48729E9ED2839
InputHost/Program.cs                                 7C3B915B92B120FFF1D385EB286924129C3F894FEFDD8DB587F1DED56FA35239
src/views/GyroMotionView.vue                         32BBE123B4FB80674CDE97F58DAF2DBFB5B5C5CE4C820B349EEE53E1092C5599
src/bridge/inputContracts.ts                          697600B4668CED2B67FCB9306189CB36E2FBDC6D11BFEF6D08770224CD085F02
src/bridge/gyroMotionMapperMock.ts                   E2F25BC842C967E2B6C875F65ECC47468B7215B39629B4403FD5D64226F3FC68
deps/.../source/Helpers/GamepadMotion.cs             7BA36F0DCB0E280D526E5B09E95888AF58B98927B3C83001817EC12FB23ED
deps/.../source/Managers/MotionManager.cs            19A9C645351DC74B5A0E2A74D99C3AD95F105CF85EDF173C8AF8531713F6FC7E
deps/.../source/Managers/SensorsManager.cs           660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3
deps/.../source/Sensors/IMUGyrometer.cs              BC44A51518C82BDDAF7BA2D79A37A7F57EF762E7F0D5F5B291DBB472077930E1
deps/.../source/Sensors/IMUAccelerometer.cs          F63C11C0729B013E7B03D717C09F25FC099A8433752B52DE9E3826BCDFFF7B4C
```

锁定 HC 基线仍为 `0.32.4.0 / 06c0b954`。HC 文件的哈希与已登记 frozen reference 一致；P104 没有改动这些文件。

## 3. 已证实的 HC 路线

### 3.1 provider 与采样

- HC `IMUGyrometer` / `IMUAccelerometer` 在 `SensorFamily.Windows` 使用 typed `Gyrometer.GetDefault()` / `Accelerometer.GetDefault()`。
- 两者都使用 `max(MinimumReportInterval, TimerManager.GetPeriod())`，然后订阅 `ReadingChanged`。
- `SensorsManager.UpdateReport()` 读取各 provider 的当前缓存，将 gyro/accel 写入 `ControllerState.GyroState`，再调用 `GamepadMotion.ProcessMotion()`。
- HC gyro 在 provider 层先按 `IDevice.GetCurrent().GamepadMotion.GetCalibration().GetGyroThreshold()` 做 `abs(raw) >= threshold ? 0 : raw`，然后依据 `GyroMatrix.AxisRemapIndices` 与 `GyroMatrix.Axis` 变换；accel 依据 `AcceleroMatrix` 变换。

### 3.2 GamepadMotion 与 MotionManager

- `GamepadMotion.ProcessMotion()` 接收已经经过 provider 处理的 gyro/accel 和 controller delta。
- `MotionManager.SetupMotion()` 同时写入 GamepadMotion calibrated plane、gravity plane，以及 DSU raw plane。
- `MotionManager.ProcessMotion()` 的 LocalSpace 使用 Default plane；PlayerSpace/WorldSpace 调用 GamepadMotion 的专用输出；JoystickSteering 使用 inclination/accel；随后才处理 trigger/toggle、反转、曲线、ADS、velocity、sensitivity 和 short clamp。
- HC 的 calibration 事务为 `ResetContinuousCalibration()` → `Stillness | SensorFusion` → 等待 `confidence == 1` → 读取 offset → 以 `confidence * 10` 写回 → `Manual`；这是显式用户校准路径，不是创建 DLL 后自动发生的副作用。

## 4. current YMCC 已证实的事实

| ID | 证实事实 | 源码锚点 | 分类 |
|---|---|---|---|
| `P104-F01` | native 也使用两个 typed WinRT `GetDefault()`，并把报告间隔设为 `max(MinimumReportInterval, 8ms)`；这只与锁定 HC 默认周期静态相符 | `native/main.cpp:4882–4908` | `SOURCE-FACT / PARTIAL-HC-PARITY` |
| `P104-F02` | gyro/accel 由两个独立 callback/cache 收集，各自只有 receipt tick、timestamp、sequence；没有同 provider、同 hardware frame、skew 上限或 pair 选择算法 | `native/main.cpp:4810–4852`、`6292–6314` | `SOURCE-FACT / PAIR-UNPROVEN` |
| `P104-F03` | native 将 WinRT `Timestamp().time_since_epoch().count()` 作为 `timestamp100ns`；HC sensor wrapper 保存的是 `DateTime.TimeOfDay.TotalMilliseconds`；两者没有统一时间域/转换合同 | `native/main.cpp:4833–4850`；HC `IMUGyrometer.cs:117–128`、`IMUAccelerometer.cs:114–128` | `SOURCE-FACT / TIMESTAMP-DOMAIN-AMBIGUITY` |
| `P104-F04` | native 只输出 `gyroDeviceId` 与 `accelDeviceId`，没有把它们绑定到同一 PnP container、ROG physical identity、DMI/SKU、HC device class 或 calibration key | `native/main.cpp:6275–6284`、`6323–6329` | `SOURCE-FACT / IDENTITY-UNENCLOSED` |
| `P104-F05` | native 目前把 gyro/accel 保持 raw；`matrixIdentity` 只有 `unresolved-rog-pid-family/raw` 或 `unbound-device/raw`，没有 HC `GyroMatrix` / `AcceleroMatrix` remap/sign | `native/main.cpp:6320–6330` | `SOURCE-FACT / SOURCE-DIVERGENCE-BLOCKED-BY-IDENTITY` |
| `P104-F06` | native 对 gyro 使用固定 `2000.0 dps` clip；HC 使用 physical device calibration threshold。代码注释已承认该值不能代表完整 HC parity | `native/main.cpp:6316–6321`；HC `IMUGyrometer.cs:117–120` | `SOURCE-DIVERGENCE / CALIBRATION-KEY-MISSING` |
| `P104-F07` | native 已加载 `StartContinuousCalibration`、`PauseContinuousCalibration`、`ResetContinuousCalibration`，但 current path 没有调用 Reset/Start/Pause；仅在 `pairProven` 且已执行 ProcessMotion 后尝试读取 confidence，并在成功时直接 `SetCalibrationMode(Manual)` | `native/main.cpp:5014–5025`、`6351–6395` | `SOURCE-DIVERGENCE / CALIBRATION-ARM-MISSING` |
| `P104-F08` | `pairProven` 当前硬编码 `false`；因此 GamepadMotion.ProcessMotion、confidence、cal-lock、PlayerSpace/WorldSpace 的 native 实际路径均不可达，P103 的 `rx/ry=0/589` 与该门一致 | `native/main.cpp:6334–6351`、`6440–6452`；P103 | `SOURCE-FACT / SAFE-ZERO-EXPECTED` |
| `P104-F09` | 即使未来 pair gate 放开，native 的 calibration lock 仍依赖未启动的 continuous calibration；这形成“pair gate → ProcessMotion → confidence → lock”的潜在闭环阻塞，不得用 P103 数据推断 calibration 失败原因 | `native/main.cpp:6351–6395` | `INFERENCE / NEEDS-T16-DESIGN-DECISION` |
| `P104-F10` | native Host frame 只有实体 XInput stick 与 `pair-unproven-safe-zero` proof；`cgx/cgy/cgz`、PlayerSpace、WorldSpace、calibration 参数当前不改变 Host frame。`host-active` 只表示 Host 接受 canonical frame，不表示 motion plane active | `native/main.cpp:6143–6155`、`6180–6228` | `SOURCE-FACT / SEMANTIC-AMBIGUITY` |
| `P104-F11` | native 没有发出 `gamepadMotionPlane`；UI contract 只在 `processProven/pairProven/calibrationLocked` 都为 true 时接受该 plane。raw telemetry 因此只能更新诊断轴和 Host-local dot，不能证明 HC mapper 已运行 | `native/main.cpp:6464–6477`；`inputContracts.ts:24–33, 104–135` | `SOURCE-FACT / UI-PLANE-UNENCLOSED` |
| `P104-F12` | UI preview 使用固定 `runId='ui-preview'`、`epoch=0`、`powerGeneration=0`，并把实际 telemetry 的 sequence/timestamp 塞进 fixture mapper；这不是同代 Coordinator/Host frame，也没有 configHash/calibrationEpoch/first-frame 关联 | `GyroMotionView.vue:135–149` | `SOURCE-FACT / PREVIEW-GENERATION-AMBIGUITY` |
| `P104-F13` | UI 同时显示 raw gyro、Host-local right-stick 和 fixture preview；三者视觉上都在同一页面，但没有 external HID/Steam/game receipt，且 `outputKind=native-canonical-pipe-frame` 明确只到 Host pipe | `GyroMotionView.vue:109–180`；`inputContracts.ts:34–59` | `SOURCE-FACT / PRESENTATION-BOUNDARY` |
| `P104-F14` | capture append 在文件打开失败、写入失败时静默返回；桌面 CopyFile 结果也被忽略；样本数量不能单独证明日志完整 | `native/main.cpp:4935–4962` | `SOURCE-FACT / LOG-INTEGRITY-GAP` |
| `P104-F15` | capture start 仅记录 GamepadMotion 是否加载与 locked 状态，没有 DLL SHA/MVID、export ABI digest、profile identity 或 calibration asset digest | `native/main.cpp:6275–6285` | `SOURCE-FACT / PROVENANCE-GAP` |
| `P104-F16` | power suspend/resume 会停止/重订阅 WinRT sensors 并清空缓存，但 capture 记录没有独立 source generation/calibrationEpoch/first-pair invalidation receipt | `native/main.cpp:4919–4932`、`6275–6287` | `SOURCE-FACT / REARM-GAP` |

## 5. 证据、推断、未知分开

### 5.1 可以直接作为证据的结论

1. P103 的 ROG gyro/accel 已进入 native capture；`rx/ry=0/589` 是当前安全门结果，不是 provider absence。
2. HC reference 的 provider 选择、matrix/threshold 处理、GamepadMotion/MotionManager 分层已定位；锁定 HC 文件没有发现数学公式被改写。
3. YMCC 当前 native 还没有一个可以被称作“HC calibrated/default/gamepadMotion output 已 active”的 producer receipt。
4. native calibration route 还缺 HC 的 explicit arm calls；这是源码事实，不是对 P103 运行结果的猜测。

### 5.2 只能作为安全推断的结论

- P103 `confidence=0` 很可能与 GamepadMotion 处理被 `pairProven=false` 完全阻断有关，但没有 runtime trace 证明 DLL 在未 ProcessMotion 时的 confidence 初值，因此不能写成唯一原因。
- 如果未来只把 `pairProven` 改成 true 而不补 calibration arm、identity、matrix、时间域和 lifecycle invalidation，仍可能保持 zero 或产生未对齐输出；不得把“解锁一行布尔值”当作修复。
- P103 的 raw gyro 幅度不能用来选择 ROG matrix，也不能用来证明“静态漂移”“单位错”或“传感器坏”。

### 5.3 仍未知 / 缺失的最小证据

```text
provider DeviceId ↔ PnP container ↔ ROG physical identity ↔ DMI/SKU ↔ HC device class
gyro/accel same-provider pairing rule, timestamp normalization, skew threshold, hardware sequence
HC matrix source, exact ROG model branch, axis remap/sign and matrix hash
per-device gyro threshold and calibration persistence key
calibration start/stop/abort/timeout/rearm semantics and first paired sample
GamepadMotion default plane receipt and Player/World output receipt
UI/native same runId/epoch/configRevision/calibrationEpoch linkage
InputHost applied ACK, first-frame receipt, HID raw report, Steam/game consumer receipt
capture file write/copy failure receipt and artifact completeness proof
GamepadMotion DLL hash/MVID/export ABI/profile provenance in the runtime record
```

## 6. HC 偏差与处理决定

| 项目 | 是否直接改源码 | 原因 |
|---|---|---|
| fixed `2000 dps` vs HC per-device threshold | 否 | 明确是 parity gap，但修复必须先取得 physical identity/calibration key；不能猜阈值 |
| raw gyro/accel vs HC matrix | 否 | matrix 未绑定；填 ROG PID family 或轴序会制造原创偏移 |
| missing Reset/Start/Pause calibration calls | 否（本轮） | HC 路线明确，但要定义与 pair proof、用户校准、power/rearm 的顺序；直接插入可能绕过安全门 |
| `pairProven=false` | 否 | 这是当前安全合同，不是可凭 P103 数据解除的错误 |
| UI fixture generation / plane proof | 否 | 需要 T10/T16 的同代 Host/Coordinator receipt，不能把 fixture 变成 runtime |
| log write/copy receipt | 否 | 属于测试可观测性修正，当前用户要求是先记录缺口；不影响 HC 数学 parity |

因此本轮没有无歧义、可在不碰并发/设备/consumer 语义的范围内直接落地的源码修正；保持：

```text
T11 = PAIR-UNPROVEN / SAFE-ZERO
T12 = DIRECT-IMU-NO-REPORT
T15/T16/T17/T18 = RUNTIME-BLOCKED
runtimeUpgrade = false
```

## 7. 下一次最小定向闭口（不重复通用采集）

只有获得真实运行介入后，按以下顺序采集：

1. `T16-A`：同一 run/epoch 记录 gyro/accel DeviceId、PnP container、ROG identity、DMI/SKU、HC selector、matrix/threshold hash。
2. `T16-B`：明确 pair 规则与时间域，记录 source generation、timestamp normalized value、skew、hardware sequence、firstPairReceipt。
3. `T16-C`：执行 HC calibration transaction 的 start/reset/mode/steady/confidence/offset/weight/manual lock/timeout/abort，并记录 calibrationEpoch。
4. `T16-D`：只在 A–C 闭口后验证 GamepadMotion default/Player/World → native telemetry → Host canonical frame 的同代 sequence/hash。
5. `T17/T18`：随后再做 DS4 descriptor/raw readback、P-HID/P-XINPUT/P-OWNER、Steam/game consumer 与恢复。

P104 不是 runtime closed；它的作用是防止把“有 raw 数据”“GamepadMotion DLL 已加载”“Host frame accepted”“页面图有数值”混写成 HC motion 已闭合。
