# T22 BUS-P112：P111 后 HC matrix active / first-frame telemetry gate 审计

日期：2026-09-06  
状态：`SOURCE-FIXED-LOCALLY / BUILD-AND-PACKAGE-OK / ROG-RUNTIME-INTERVENTION-REQUIRED / PRODUCT-RUNTIME-BLOCKED`

## 1. 范围

P111 已在用户授权的 standalone lane 内接通真实 gyro→DS4 左/右摇杆测试入口。本页只复核 P111 当前 source 的 HC 语义，并收口两个确定性 source 偏差：

```text
HC ROG matrix candidate → GamepadMotion.ProcessMotion ingress
motionAdmission → UI actual-lane/first-frame presentation
```

本页不解除正式产品 `pairProven=false`、不启用 direct DS4 IMU、不改变正式 Release/updater、不执行 ROG/Steam/game/HID 实机测试。

## 2. Current source observation point

P112 修正后观察到的文件 hash（工作树仍未冻结）：

| 文件 | SHA-256 |
|---|---|
| `native/main.cpp` | `03A4F73C7AA4EF4C1321E9CDE90B74EA0212A42F5D34DD18AB225C2ECF7970DA` |
| `src/views/GyroMotionView.vue` | `0D6187D0D6ABBBB15BDA99CC0DA73AAAC395104A22DB5095C7633C2B6E4530E5` |
| `src/bridge/inputContracts.ts` | `50793CE62459FC7E25F85EBC8758B617CBA34860FEAB3BDBB80D34E5C0F54E4C` |
| `tools/package-gyro-input-test.ps1` | `2C32967B2D0F15132DAADCC986990FB2199FD14689A377680E8D80C8715F3751` |
| `InputHost/Program.cs` | `7C3B915B92B120FFF1D385EB286924129C3F894FEFDD8DB587F1DED56FA35239` |

P111 的旧 ZIP 只对应 P111 source，不能覆盖本页 native/UI 变更；重新 build/package 后才是可测试包。

## 3. HC 对照与本轮明确修正

### P112-F01（已修正）：HC ROG matrix candidate 原先未进入 GamepadMotion

锁定 HC `IMUGyrometer.cs` / `IMUAccelerometer.cs` 的 Windows ReadingChanged 路径先执行：

```text
per-device threshold
→ AxisRemapIndices
→ device GyroMatrix / AcceleroMatrix
→ SensorsManager.UpdateReport
→ GamepadMotion.ProcessMotion
```

P111 source 在调用 `g_gmProcess()` 前仍使用 `mgx/mgy/mgz` raw 值，虽然已计算 `hcCandidate*`。这属于明确的 HC ingress 偏差，已修正为：

```text
hcMatrixAdmitted = pairProven && resolved RC73XA selector
→ mg* / ma* = locked HC RC73XA candidate
→ g_gmProcess(hc-remapped gyro, hc-remapped accel, dt)
```

正式包没有 marker 时不会进入 `pairProven` 分支，仍保持 raw diagnostic + safe-zero/no-report 边界。

### P112-F02（已修正）：UI 在 first-frame 前提前标称 actual lane

P111 UI 只判断 `motionAdmission=hc-test-lane-admitted` 与 target stick object 存在，未同时要求 Host receipt 的 `firstFrame=true` 和 `hostActive=true`。这会把已计算但尚未同代 active 的 Host-local frame 展示为“实际 lane 已准入”。

已修正为：

```text
actualLane = motionAdmission == hc-test-lane-admitted
             ∧ hostFrame.firstFrame == true
             ∧ hostFrame.hostActive == true
             ∧ targetStick value present
```

这只收紧 presentation/observability，不把 UI label 升级为 HID/Steam/game consumer receipt。

### P112-F03（已补充）：active/candidate matrix stage 现在可区分

native capture 现在同时记录：

```text
matrixIdentity = hc-active-asus-rc73xa | unresolved-provider-binding | raw-unbound-device
matrixStage    = active-test-lane | candidate-unbound-provider | raw-unbound-device
matrixAdmission = true | false
```

`hcMatrixCandidate.providerBindingProven=false` 仍保留为 provenance 事实；`matrixAdmission=true` 只表示 marker-gated standalone lane 的局部测试准入，不是正式合同级 physical/provider proof。

## 4. P112 residual gaps（未修正、需要实机或 T10/T16/T17/T18）

### P112-G01：standalone `pairProven` 仍是窄化的 provider-key candidate

当前仍只验证 marker、live、gyro/accel present、normalized ACPI provider key 相等和 RC73XA selector。它没有 `SamplePairProofV1` 所需的 hardware frame、PnP/container/physical identity、normalized monotonic skew、calibration/power/config epoch 和 first-pair receipt。

分类：`TEST-LANE-NARROWING / T11-T12-PROVENANCE-GAP`。  
含义：允许作为用户授权的 standalone 探针，不得迁移为正式 product admission。

### P112-G02：HC per-device threshold 与完整 profile 参数仍未接通

test lane 仍使用固定 `2000 dps` clip、默认 curve、`1.0 × 1000` sensitivity、`gyroWeight=1.2` 和默认 LocalSpace。HC profile multiplier、SteeringAxis/Auto、MotionMode/Toggle、ADS、velocity、invert、deadzone、output shape、config revision/hash 尚未形成同代 consumer receipt。

分类：`HC-PARITY-PARTIAL / T10-T15-OWNED`。

### P112-G03：power resume/断开/Host crash 后 motion epoch 未闭口

当前有 power generation、listener stop/start、cache reset 和 Host release，但仍没有实机证明旧 callback/GamepadMotion/calibration epoch 被拒绝、fresh pair 被重新准入、first-frame 和 external target 在新代次恢复。

分类：`LIFECYCLE-GAP / RUNTIME-BLOCKED`。

### P112-G04：Host proof registry 与 external readback 仍缺失

`motionPairProofId` 仍主要是非空字段；Host-local `frame-accepted`/`firstFrame` 仍不等于 HIDMaestro DS4 report、Windows/Steam enumeration 或游戏 consumer observation。需要 DS4 descriptor/report/decoder/readback、P-HID/P-XINPUT/P-OWNER 与独立 Steam/game 结果。

分类：`T10-T12 / T17-T18 / RUNTIME-BLOCKED`。

### P112-G05：P111 包在 P112 source fix 后已过期

P111 的 ZIP hash 仍是旧 source/UI 的包证据；P112 已重新 build/package 并完成包内文件 hash/ZIP entry 自校验。当前 P112 包为：

| target | package path | bytes | SHA-256 |
|---|---|---:|---|
| right | `Mainline/Build/TestPackages/GyroInput-ROG-20260906-174811/YeManCC-GyroInput-ROG-Test.zip` | 88,591,414 | `007004A35974549D7BB1BE4E5F08E5E2310A0AC5488D6355AD88A10361D4F9C3` |
| left | `Mainline/Build/TestPackages/GyroInput-ROG-20260906-174900-Left/YeManCC-GyroInput-ROG-Test.zip` | 88,591,420 | `BB2A7D74ABB63EA78061D36076750402ED15135EB9140BAB80EC65D414AD93AD` |

两包内 YeManCC.exe SHA-256：`EB344A2FABB6EA28317A5B0C498C39DA97B40AE51271CC73B07EA312347D4650`；两个 package script 均返回 `GYRO_INPUT_ROG_TEST_PACKAGE_OK`，并明确 `Formal Release package untouched: true`。

## 5. 证据、推断、未知

### 已证实

- HC Windows sensor source 在 `GamepadMotion.ProcessMotion()` 前执行 matrix/threshold ingress。
- P111 source 已有 RC73XA candidate、calibration order、LocalSpace blend、left/right target 和 first-frame state。
- P112 已让 standalone lane 在 admission 条件成立时实际使用锁定 RC73XA gyro/accel candidate，并让 UI 只在 first-frame active 后显示 actual lane。
- 正式包的 marker-free safe-zero 边界未被改变。

### 推断（不升级为事实）

- ROG 实机若返回 `matrixStage=active-test-lane`、`cal-lock`、`motionAdmission=hc-test-lane-admitted`，可证明 standalone native/Host-local 链路进入测试 lane；仍不能证明外部 consumer。
- 若外部游戏轴不动，优先区分 Host-local frame、HID report、Steam enumeration、游戏读取四个层级，不直接归因于 matrix 或 drift。

### 未知/需要用户介入

```text
ROG RC73XA real provider/pair/calibration receipt
firstFrame/hostActive same-run receipt after P112 package launch
DS4 HID descriptor/report/raw readback
Steam/Windows/game target-axis observation
close/focus-loss/sleep/disconnect/Host-crash recovery
```

## 6. 当前处置与下一步

本页已完成两个明确 source/observability 偏差的局部修正；native/UI build 与 standalone package rebuild 已通过，未改 T10 合同、未解除正式 pair gate、未改 direct IMU、未新增 DGF。现在需要用户在 ROG RC73XA 上运行新包并回传 `YeManCC-ROG-Input-Gap-Evidence-*.zip`。回传重点：

```text
hc-motion-selector-receipt
hc-real-stick-test-lane
hc-calibration-start / cal-lock or calibrate-timeout
matrixStage / matrixAdmission / matrixIdentity
motionAdmission / firstFrame / hostActive / targetStick
leftStick / rightStick
DS4 + Steam + game independent observation
```

主线仍保持：

```text
T22-CURRENT-WORKTREE-REAUDIT-REQUIRED
/ T11-T13-UNENCLOSED
/ PRODUCT-RUNTIME-BLOCKED
/ runtimeUpgrade=false
```
