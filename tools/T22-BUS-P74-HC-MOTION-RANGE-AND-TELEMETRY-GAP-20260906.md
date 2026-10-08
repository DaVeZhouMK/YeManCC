# T22-BUS-P74：HC 参数范围纠偏与 telemetry provenance 缺口

日期：2026-09-06

状态：`HC-PARITY-CORRECTED / STATIC-VERIFIED / TELEMETRY-GAPS-UNENCLOSED / RUNTIME-BLOCKED`

本页记录 BUS 对锁定 HC candidate、当前 YMCC UI/schema 和 telemetry 链路的三方复核。范围只包括可以从锁定 HC 无歧义确定的参数范围，以及本轮继续发现但不能猜改的 provenance/同帧缺口。

## 1. 明确 HC 范围偏差（已修正）

锁定 HC 证据：

```text
HC ProfilesPage.xaml:1295-1327
  GyroMultiplier       = 0.1..3.0
  AcceleroMultiplier   = 0.1..3.0

HC SettingsMode1.xaml:161-168
  SteeringMaxAngle     = 10..80 degrees

HC SettingsMode1.xaml:217-225
  SteeringPower        = 0.2..5

HC SettingsMode1.xaml:273-279
  SteeringDeadzone     = 0..5 degrees

HC TemplatesDictionary.xaml:1481-1529
  Axis2AxisInnerDeadzone / OuterDeadzone / AntiDeadzone = 0..25%
```

BUS 发现原 YMCC durable/UI 边界存在下列漂移：

```text
gyroMultiplier / accelerometerMultiplier  = 0..10 (旧 schema/UI 约束)
antiDeadzone                              = 0..100% (旧 durable 约束)
SteeringMaxAngle                          = 10..60 degrees (旧 UI/schema)
SteeringPower                             = 0.5..2 (旧 UI/schema)
SteeringDeadzone                          = 0..15 degrees (旧 UI/schema)
```

已按锁定 HC 直接纠偏：

- `src/bridge/settingsRepository.ts` durable normalizer 现在使用 `0.1..3`、`0..25`、`10..80`、`0.2..5`、`0..5`；
- `src/views/GyroMotionView.vue` 的陀螺仪倍率 slider 现在使用 `0.1..3`，方向盘三个 slider 使用 HC 范围；
- `tools/settings_cas_selftest.ts` 的边界断言同步为 HC 结果 `gyroMultiplier=3`、`accelerometerMultiplier=0.1`。

这属于已证明的 HC 参数范围偏差，不是原创算法；没有改变单位、轴顺序、矩阵、倍率公式或 runtime admission。

## 2. 静态验证

```text
pnpm run test:settings-cas       = PASS
pnpm run type-check              = PASS
pnpm run test:gyro-config-activation = PASS
```

这些 PASS 只证明 schema/UI 静态边界和 mock 合同，不证明 Host 已消费参数、GamepadMotion 已产生真实输出或 HID/Steam/游戏回读已闭合。

## 3. BUS 继续发现但未授权猜改的 telemetry 缺口

本轮 HC/source 对照还发现以下 current-source/contract 风险；它们均属于既有运行时缺口的细化，不是可以直接填入常量的 HC 数学偏差：

```text
GamepadMotionPlaneTelemetryV1 provenance      = 缺少完整 provider/identity/epoch/pair/hash 绑定
UI sequence + max(timestamp)                 = 不能证明 HC 同硬件帧配对
非法 telemetry 被归零                       = 可能与合法静态 zero 混淆，缺少明确 invalid/no-report reason
motion config 结构与真实消费链               = source/fixture 有字段，Host/GamepadMotion 消费未闭口
HC SteeringAxisIndex (Roll/Yaw/Auto)          = HC 有明确 UI 选择，YMCC 当前页面未提供等价设置绑定
HCParityLedger 部分 exact/verified 叙述      = 可能超过当前 runtime 证据，应继续保持保守 tier
```

上述项目保持 `UNENCLOSED / RUNTIME-BLOCKED`；不伪造 `pairProven`、不把 `Math.max(timestamp)` 当作同帧证明、不把零值当成有效输出、不把静态 ledger 当作消费者闭口，也不新造 DGF。

## 4. 当前裁决

```text
明确 HC 参数范围偏差                         = CORRECTED / STATIC-VERIFIED
new axis/sign/unit/matrix/gain drift          = NOT FOUND
new original drift algorithm                  = NOT FOUND
telemetry provenance / same-frame proof       = UNENCLOSED
SteeringAxis UI/config/consumer parity         = UNENCLOSED
Host/GamepadMotion/HID/Steam/game closure     = RUNTIME-BLOCKED
PS4 battery 5% origin                         = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade                                = false
```

后续仍需同一 Host 生命周期的 provider/identity/epoch/pair receipt、真实 GamepadMotion/Host/HID report、OS/Steam/game consumer 观察，以及 DS4 battery raw readback；在这些数据回来前不进行新的猜测性源码接入。
