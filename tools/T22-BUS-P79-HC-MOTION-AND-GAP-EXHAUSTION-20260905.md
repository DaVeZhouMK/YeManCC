# T22-BUS-P78：HC Motion/action 三方回归与已知缺口耗尽扫描

日期：2026-09-05

状态：`STATIC-REGRESSION-PASS / NO-NEW-HC-DRIFT / RUNTIME-BLOCKED / USER-RUNTIME-EVIDENCE-REQUIRED`

本页是 BUS 在 P73–P77 之后的继续只读扫描。范围覆盖锁定 HC source、当前 YMCC source/fixture、任务书与 parity ledger；没有启动真实 YeManCC/InputHost/HIDMaestro/HidHide、Steam、游戏或真实设备，没有写入系统状态，也没有修改 T10 owner 区源码。

## 1. 三方核对范围

### HC source

- `deps/handheldcompanion-runtime/source/Managers/MotionManager.cs`
  - `SetupMotion`：GamepadMotion calibrated gyro/gravity 与 DSU raw plane 分离；Default multiplier 与 steering-axis 选择发生在该层。
  - `ProcessMotion`：Local/Player/World/JoystickSteering 选择、H/V invert、custom sensitivity、ADS、velocity、X/Y sensitivity、short clamp 与 disabled 清零顺序。
- `Helpers/GamepadMotion.cs`
  - `ProcessMotion` 的 delta 保留语义、calibrated/default/raw 输出与 Player/World native getter。
- `Actions/AxisActions.cs`、`Utils/InputUtils.cs`
  - radial inner/outer deadzone → anti-deadzone → response curve → output shape → action-local inversion；custom sensitivity、steering 和 short clamp 公式。
- `Misc/Inclination.cs`
  - HC JoystickSteering 的三轴加速度倾角公式。
- `Targets/DualShock4Target.cs`、`Misc/DS4OutDevice.cs`、`DSU/DSUServer.cs`
  - DS4 raw transport、电量遗留字段和 DSU battery metadata 的分离边界。

### current YMCC source

- `src/bridge/gyroMotionMapperMock.ts`、`hcInputUtils.ts`、`virtualReportAssembler.ts`
  - 仅为 HC-aligned fixture/assembler；没有被提升为真实 HID/Steam/game consumer 证据。
- `src/bridge/inputContracts.ts`、`motionSample.ts`
  - GamepadMotion plane、pair/calibration/config admission 合同；仍要求完整 provenance 才能准入。
- `native/main.cpp`
  - `pairProven=false` 的 safe-stop、WinRT sensor capture、GamepadMotion symbol load/readback 与 Host-local telemetry。
- `InputHost/Program.cs`
  - 当前标准提交只写 Buttons/Hat/Axes；没有 DS4 battery 或 IMU writer。

## 2. 只读回归结果

以下命令均在 `Mainline/YeManCC-source/YeManCC` 执行：

```text
pnpm run type-check
pnpm run test:gyro-motion-mapper-mock
pnpm run test:gyro-e37-hc-parity
pnpm run test:gyro-config-activation
pnpm run test:gyro-calibration-session
pnpm run test:input-contracts
pnpm run test:input-t8-host-loop
pnpm run test:input-lifecycle-mock
pnpm run test:input-runtime-admission
pnpm run test:hc-parity-drift-ledger
pnpm run test:hc-input-order-audit
pnpm run test:hc-input-dependency-audit
pnpm run test:rog-gyro-protocol
pnpm run test:rog-input-adapter
pnpm run test:rog-hidhide-contract
pnpm run test:rog-visibility-topology
pnpm run test:gyro-virtual-mainline-status
pnpm run test:a1-gyro-release-boundary
```

结果：除最后一项发布边界审计外全部 `PASS`；`gyro-virtual-mainline-status` 保持 `RUNTIME_BLOCKED`。本轮生成的 fixture/audit 输出位于 `Mainline/Build/Validation/`，它们只证明静态/模拟层，不升级 runtime 闭口。

## 3. HC parity 裁决

| 对账面 | 结论 | 说明 |
| --- | --- | --- |
| LocalSpace `(DefaultGyro.Z, DefaultGyro.X)` | 静态通过 | current mapper 只接受已声明的 HC Default plane；raw diagnostic 不能替代该 plane。 |
| PlayerSpace / WorldSpace | 静态公式通过；runtime 未闭 | 方向为 HC `(-y,x)`，参数分别为 `1.41` 与 `0.125`；仍没有同一 Host epoch 的真实 GamepadMotion receipt。 |
| JoystickSteering | 静态公式通过；runtime 未闭 | 使用 admitted `DefaultAccel` 的 HC Inclination/Steering；没有用 raw accel 冒充 Default plane。 |
| custom sensitivity / velocity / short clamp / gyroWeight blend | fixture 通过 | 不代表 UI→CAS→Host→HID consumer 已消费。 |
| AxisActions response curve / action-local inversion / ADS / real multiplier consumer | 已知缺口 | 任务书已登记；不能由 fixture 或页面 preview 宣称 full HC parity。 |
| timing / resume / `g_gmLastTick` | 已知偏差 | P77 已登记为 `T10-OWNER-BOUND / UNENCLOSED`；本轮不抢改 T10。 |
| DS4 battery 5% | `UNKNOWN / RUNTIME-BLOCKED` | HC、YMCC、HIDMaestro 均未找到可归因的当前 writer；禁止猜写 `0x05`、`0x0A`、`100%` 或 `Full`。 |
| P-HID / P-XINPUT / P-OWNER | `RUNTIME-BLOCKED` | HidHide 不能证明 XInput/game consumer 阻断；需要同场景消费者与恢复证据。 |

## 4. “错误 / 未知 / HC 偏离 / 原创逻辑 / 缺口”分类

```text
new axis/sign/unit/gain/deadzone drift         = NOT FOUND
new autonomous drift algorithm                 = NOT FOUND
HC Local/Player/World/Steering formula drift   = NOT FOUND in this scan
YMCC timing delta path                         = ORIGINAL LOGIC / P77 KNOWN
response curve/ADS/multiplier consumer         = KNOWN GAP / UNENCLOSED
GamepadMotion provider/epoch/pair/matrix       = UNENCLOSED
InputHost ACK + first matching frame           = UNENCLOSED
DS4 descriptor/raw report/readback             = UNENCLOSED
PS4 battery 5% producer                        = UNKNOWN / RUNTIME-BLOCKED
Steam/game consumer isolation/recovery         = RUNTIME-BLOCKED
formal Release HidHide entry                   = KNOWN POLICY FAIL
```

本轮没有发现满足“明确不是 HC、且属于新增偏移代码”的局部问题，因此没有直接改源码、JSON、HIDMaestro、HidHide、正式包或系统状态。已知的 `GetTickCount64`/首帧 `0.016`/`0.001..0.1` clamp/未清 `g_gmLastTick` 仍受 T10 owner 边界约束；不能把它改成已经解释 ROG 静止漂移。

## 5. 关闭条件和介入点

本轮 Motion/action 检索范围已无新的可闭合证据；全局已知缺口仍以 P78/P79 的分类为准。剩余介入点主要是真实运行时证据：

1. ROG 同一 Host epoch 的 provider identity、sensor family、matrix、calibration、pair、GamepadMotion input/output、Host ACK/first-frame、HID readback 与 Steam/game consumer/recovery。
2. PS4 5% 的同一 Host 生命周期 DS4 identity、descriptor 原始字节、input/feature report、byte-14 独立 decoder 与 OS/Steam 对应设备 readback。
3. 若继续核验正式发布边界，需要对 `PowerControl/redist/HidHide_1.5.230_x64.exe` 的测试包携带策略作单独裁决；这不是 HC motion 数学问题。

```text
BUS-P79 = STATIC-SCAN-EXHAUSTED
runtimeUpgrade = false
device/system mutation = none
next gate = user-provided runtime evidence or explicit release-policy decision
```
