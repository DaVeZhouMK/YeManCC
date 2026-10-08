# T22-BUS-P67：生命周期三方对账、当前缺口收敛与 PS4 电量问题登记

Status: `STATIC-RECONCILED / HC-SOURCE-BOUND / EVIDENCE-GAPS-EXPLICIT / NO-UNAMBIGUOUS-HC-MATH-FIX / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC source、当前 YMCC source、GyroVirtual 任务书合同的增量对账。范围包括：Host active 首帧、gyro/accel 配对证明、hash/timestamp provenance、GamepadMotion UI 数据链路、HC action admission、SensorFamily/provider、合帧时序、异常清理、RF00 覆盖，以及用户回传的 PS4“电量低 - 5%”问题。

本页只记录可归属证据与缺口，不把推断写成事实；没有修改正式 Release/updater、HIDMaestro、HidHide、系统状态或真实设备状态。

## 1. 三方 authority 与证据边界

| 平面 | 当前 authority | 已闭合事实 | 仍未闭合 |
| --- | --- | --- | --- |
| HC source | `HC-Candidate-0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103` | ROG DMI 分支、矩阵、MotionManager action gate、SensorsManager provider 选择可定位 | YMCC 是否在同一 provider/epoch/receipt 上实现这些行为 |
| YMCC source | 当前工作树 `native/main.cpp`、`InputHost/Program.cs`、`src/bridge/*` | fail-closed safe-zero、raw 与 product matrix 分离、Host tuple 字段、UI 合同字段存在 | Host/driver/Steam/game consumer 的真实回读；部分 gate 尚未执行 |
| TASK/MD | `Docs/Tasks/GyroVirtual/TASK.md` 与其引用合同 | UNKNOWN/GAP、`runtimeUpgrade=false`、T22→T20→T21→T14 顺序明确 | 运行时证据、DS4 raw report、ROG matrix receipt、外部消费者证据 |

锁定 HC 的数学、设备选择和生命周期规则优先；若当前 YMCC 仅有 fixture 或 Host-local receipt，不得提升为产品/消费者闭口。

## 2. 本轮已证实事实

1. `native/main.cpp` 的当前 Host 提交路径会读取 `firstFrame` receipt，但在 `frame-accepted` 后直接设置 `g_inputHostActive=true`；代码注释要求“只有同一 tuple 的首个 accepted canonical frame”才能 active。该差异是实现与现行合同的不一致，不是 HC 轴/符号/单位偏移。
2. `motionSample.ts` 同时要求 `proof.gyroSequence`、`proof.accelSequence` 等于单一 `input.sampleSequence`，而 native 维护独立 gyro/accel callback sequence。当前合同无法表达“独立 callback → Coordinator 配对”的正常情况，除非 provider 提供共同 hardware frame id；不得伪造相同序号。
3. `sampleHash`、`configHash` 目前只执行 `sha256:<64hex>` 形状检查，没有根据被证明字段重算并绑定；这属于 provenance gap。
4. MotionSample 的 UTC、gyro、accel timestamp 目前主要做非空/数值存在检查，尚未执行可解析 UTC、clock-domain、sequence/proof 绑定。
5. native telemetry 已发送 `gyro/playerSpace/worldSpace/accel/hostFrame`，但没有发送 `gamepadMotionPlane` 嵌套对象。当前前端零输出仍是 `pair-unproven-safe-zero`，不能误报为真实 GamepadMotion 数据链路已闭合。
6. 锁定 HC `MotionManager` 的 `MotionMapped`/`ActionType.Disabled` 清零门没有在 YMCC fixture mapper 入参中表达；当前 mapper 通过不代表 runtime action admission 已闭合。
7. 锁定 HC `SensorsManager` 支持 Windows、SerialUSBIMU、Controller 三类 provider；YMCC 当前 native 只登记 `windows-default-winrt`，没有 SerialUSBIMU/controller producer、identity、transform、receipt 的完整链路。
8. `assembleCanonicalFrame()` 只检查 run/epoch/power/target/config generation，不检查 controller 与 motion 的 sequence、clock domain、最大 age/skew、上一 accepted sequence；合帧 timestamp 使用 `Math.max` 不能证明同一硬件时刻。
9. `inputCaptureThreadProc()` 的异常路径可能在 `g_inputCaptureReady=false` 时被 `inputCaptureShutdown()` 提前返回，导致 emergency cleanup 不可达。尚未复现实际泄漏或崩溃，因此只能登记 latent lifecycle gap。
10. 旧 RF00 71-key 集合没有覆盖当前 `motionSample.ts`、`rogGyroProtocol.ts`、`specialControllerProtocols.ts` 等 source；旧 hash 不能当作当前全部 gyro source 的覆盖证明。下一次 RF00 必须使用包含这些 source 的 current key set。
11. 锁定 HC `SensorsManager.Resume()` 在 `UpdateSensor()` 后即可继续正常 Tick，而 `IMUGyrometer.StopListening()` / `IMUAccelerometer.StopListening()` 只解绑 sensor 并清空引用，没有清空 `SensorReading.reading` / `readingAxis`。在首个新 ReadingChanged 到达前，恢复/rebind 可能重新消费旧缓存；记为 `HC-RESUME-CACHED-MOTION-BEFORE-FIRST-READING / LATENT-LIFECYCLE-GAP`。
12. 锁定 HC `SensorSelection` 回调在请求 `SerialUSBIMU` 或 `Controller` 不可用时同步调用 `PickNextSensor()`；设置事件是同步重入，外层回调随后仍执行 `SetSensorFamily(sensorSelection)`。因此实际 provider 与 `sensorFamily` 可能不一致；记为 `HC-SENSOR-SELECTION-FAILOVER-REENTRANT-CLOBBER / LATENT-PROVIDER-LIFECYCLE-GAP`。

## 3. 用户 PS4 电量问题

用户观察：PS4 Controller 右下角持续显示“电量低 - 5%”。本轮只登记，不猜测写入或修复：

```text
PS4 battery 5% origin = UNKNOWN / EXTERNAL-OR-ENCODER-DEFAULT-POSSIBILITY
```

已对照锁定 HC 与当前 YMCC：

- HC `DualShock4Target.BuildReport()` 的 31-byte VIIPER report 未发现 battery 字段写入。
- HC `DS4OutDevice.bBatteryLvl` 结构字段未发现已证明的生产调用链。
- HC `DsBattery` 是 DSU 网络 metadata，不等同于 HIDMaestro DS4 report。
- YMCC `HMGamepadState`、native capture 和前端合同没有已证明的 DS4 battery assignment。
- Steam UI 的 `5%` 仅是消费者显示，不是原始 report 来源证据。

关闭该问题必须在同一 Host 生命周期取得：虚拟 DS4 descriptor、input/feature raw report、report ID/length、raw bytes、device instance/container identity，以及独立 decoder/readback。禁止猜写 `0x05`、改为 `100%`/`Full` 或把数值归因 HC/YMCC。

## 4. 错误、未知、HC 偏差、原创逻辑分类

### 4.1 已知实现/合同错误或缺口

```text
HOST-ACTIVE firstFrame check              = implementation/contract mismatch
independent callback sequence contract    = over-constrained pairing proof
sample/config hash binding                = provenance gap
timestamp parse/clock binding             = provenance gap
native -> gamepadMotionPlane              = UI-data-link gap
HC action-disabled admission              = mapper admission gap
HC provider coverage                      = Windows-only current producer gap
frame temporal admission                  = stale/mixed-frame gap
pre-ready emergency cleanup               = latent lifecycle gap
old RF00 71-key coverage                  = stale/incomplete provenance coverage
HC resume first-reading/cache invalidation = latent HC-source lifecycle gap
HC SensorSelection failover reentrancy     = latent HC-source provider gap
```

这些项目不是已证实的 HC 数学偏移。T10/Host 实现正由既有实现线维护；本 BUS 轮不抢改正在修改中的 T10 源码，只保留可归属证据并要求修改后重新 RF00。

### 4.2 UNKNOWN / runtime blocked

```text
PS4 battery 5% origin                    = UNKNOWN
ROG DMI identity -> native matrix receipt= UNENCLOSED
same-provider gyro+accel/frame proof     = UNENCLOSED
HC calibration/age/skew admission        = UNENCLOSED
GamepadMotion/Host/HID/Steam/game output  = RUNTIME-BLOCKED
DS4 descriptor/input/feature readback     = RUNTIME-BLOCKED
P-HID/P-XINPUT/P-OWNER consumer isolation = RUNTIME-BLOCKED
sleep/PnP/Host-crash restoration         = RUNTIME-BLOCKED
HC resume/rebind first-reading gate       = UNENCLOSED
HC provider failover final-family receipt  = UNENCLOSED
```

### 4.3 未发现的 HC 数学偏移与原创漂移逻辑

截至本页证据，没有找到可以无歧义按锁定 HC 直接修改的：

```text
axis swap / sign / unit / gain / deadzone / velocity decay = NOT FOUND
原创漂移算法                                              = NOT FOUND
battery assignment                                         = NOT FOUND
```

`pairProven=false`、safe-zero、未绑定身份时保持 raw、direct IMU no-report 是 fail-closed 保护，不归类为原创漂移逻辑；但它们也不能被宣称为 runtime closure。

## 5. 静态回归证据

本轮执行的是不触碰设备/系统的静态回归：

```text
vue-tsc --noEmit                         = PASS
motion sample selftest                   = PASS
gyro E-37 HC parity selftest             = PASS
provider capability matrix selftest      = PASS
special controller protocols selftest    = PASS
input contracts selftest                 = PASS
input lifecycle mock selftest            = PASS
evidence quality gate selftest           = PASS
```

上述 PASS 只证明 source/fixture/合同的静态行为，不证明 HID、Host、Steam、游戏消费者或真实设备回读。

## 6. 下一门与停止条件

文档写回后，必须重新执行：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

本地静态扫描继续到没有新的、可归属的 HC 数学偏移为止；剩余无法由本机源码/锁定 HC 关闭的项目统一停在用户介入门：

1. ROG 上回传 DMI identity receipt、同 provider/epoch 的 gyro+accel raw frame、matrix receipt、calibration/age/skew proof 和 GamepadMotion/Host/consumer readback。
2. DS4 同一 Host 生命周期回传 descriptor、input/feature raw report 与独立 battery decoder/readback。
3. 若要关闭 T10/Host firstFrame、hash binding、timestamp 和 cleanup 项，先完成 T10 owner 的源码修改，再重新 RF00；不得在静态证据不足时打开 `pairProven` 或补写 ROG matrix。

```text
BUS-P67 conclusion = no new unambiguous HC math fix; evidence gaps explicit; runtime remains blocked
runtimeUpgrade = false
formal Release/updater changed = false
system/device mutation = none
```

## 8. BUS follow-up：HC 源码级生命周期风险

两项新增条目均来自锁定 HC source 的可定位调用链，不是 YMCC 已复现错误，也不是轴/符号/单位/倍率偏移：

```text
HC-RESUME-CACHED-MOTION-BEFORE-FIRST-READING
  evidence = SensorsManager.Resume -> UpdateSensor; IMU StopListening leaves reading cache
  disposition = LATENT-LIFECYCLE-GAP / HC-SOURCE-OBSERVED / no guessed fix

HC-SENSOR-SELECTION-FAILOVER-REENTRANT-CLOBBER
  evidence = synchronous SettingsManager.SetProperty reentry -> PickNextSensor -> outer SetSensorFamily
  disposition = LATENT-PROVIDER-LIFECYCLE-GAP / HC-SOURCE-OBSERVED / no guessed fix
```

YMCC 当前 `pairProven=false`、safe-zero、epoch/power gating 仍不能替代首个新 provider reading 或最终 provider-family receipt；这些证据门必须在实现修改后以 runtime trace 关闭。BUS 不复制 HC 的潜在风险为产品行为，也不在 T10 owner 未完成前抢改源码。

## 7. P67 写回后的最终静态重审

P67 与主 `TASK.md` 写回后已完成：

```text
T22-RF00 → T20 → T21 → T14 = COMPLETED
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 138A8C0C59684761A2C9425F91D8412AC38ED00999A05F05F90B08C0E8DFAB2F
keyFiles      = 74
porcelain     = 454
HC clean      = true
runtimeUpgrade= false
T20           = 4/13/4
T21           = 9 claims
DGF           = DGF-01…DGF-15 only
```

重审报告：[T21-T14-RF00-T10-I-CURRENT-SOURCE-reaudit-20260905.md](T21-T14-RF00-T10-I-CURRENT-SOURCE-reaudit-20260905.md)。该 digest 只作为 manifest/T21/T14 产物的事实，不复制回 `TASK.md`，避免 TASK.md 作为 key file 造成自引用漂移。
