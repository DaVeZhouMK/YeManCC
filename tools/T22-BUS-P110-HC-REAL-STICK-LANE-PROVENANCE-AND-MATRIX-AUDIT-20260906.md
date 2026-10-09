# T22 BUS-P110：HC real-stick lane provenance / matrix consumer / route audit

日期：2026-09-06  
状态：`HISTORICAL-AUDIT-COMPLETE / SUPERSEDED-BY-P111 / CURRENT-WORKTREE-UNFROZEN / NEW-GAPS-RECORDED / RUNTIME-BLOCKED`

> P111 在用户明确授权的 standalone 测试边界内已实现真实 gyro→DS4 左/右摇杆 lane；本页的“本轮不修源码”和“永远 outX/outY=0”只约束 P110 当时的审计快照，不得继续解释为 P111 current source 结论。P110 中关于 descriptor/readback、consumer、power rearm、proof registry 和正式产品 runtime 的缺口仍然有效。

## 1. 范围与边界

本页承接 P109，专门复核当前工作树中新出现的 `HC-REAL-STICK-TEST-v1` standalone lane，并继续按 HC 源码追踪：

```text
HC SensorsManager / MotionManager / GyroActions / AxisActions / LayoutManager
→ WinRT sensor ingress
→ selector/matrix candidate
→ GamepadMotion
→ canonical Host frame
→ UI telemetry / target stick
→ power resume / release
```

本轮只读探查：没有运行设备、InputHost、HIDMaestro、HidHide、Steam、游戏或硬件；没有修改 C++、C#、TypeScript、JSON、二进制、安装状态或正式包；没有解除 `pairProven=false` 合同安全边界。

由于主线工作树存在其他任务的未提交改动，本页不是冻结 manifest。任何后续源码写入都使本页需要重新执行：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

## 2. 本轮观察到的 source snapshot

以下 hash 只用于定位本轮观察，不代表 current-source 已冻结：

| 文件 | bytes | SHA-256 |
|---|---:|---|
| `native/main.cpp` | 1,101,060 | `BF5B9FFD0DEC0AD9915C07796ED0E4BAC56184D023C190399495A1CC7EA67985` |
| `src/bridge/motionSample.ts` | 13,547 | `B8314D6BED5852629217C098EBA386F6698FFF580BE8B9C2E94723C318EE7AE3` |
| `src/bridge/inputContracts.ts` | 16,314 | `50793CE62459FC7E25F85EBC8758B617CBA34860FEAB3BDBB80D34E5C0F54E4C` |
| `src/bridge/gyroMotionMapperMock.ts` | 7,913 | `E2F25BC842C967E2B6C875F65ECC47468B7215B39629B4403FD5D64226F3FC68` |
| `src/bridge/virtualReportAssembler.ts` | 5,446 | `B6FF222B8267F2979B6DF1745EE6C880086DE71D13DD5377E3B40A0803744EA6` |
| `InputHost/Program.cs` | 39,213 | `7C3B915B92B120FFF1D385EB286924129C3F894FEFDD8DB587F1DED56FA35239` |

锁定 HC 对照文件：

```text
MotionManager.cs       19A9C645351DC74B5A0E2A74D99C3AD95F105CF85EDF173C8AF8531713F6FC7E
SensorsManager.cs      660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3
TouchpadActions.cs     6BD14DDADAC39A871E9951CAF68CE229BC810A496554C7AE3F2F2EA3CE770420
Profile.cs             34A1F734D1C90C2D0A78DF102A306D185AD3DA128C61D3BF1ECC870BB58DC1E4
XboxROGAlly.cs         78BF90520419B63C6C8FD2BC12543B2D16280895802D8F9952849C4E030FE3D4
XboxROGAllyX.cs        404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE
```

## 3. HC 对齐结论：公式未发现新的轴向原创偏移

本轮重新对照的 HC 事实：

| HC 事实 | current YMCC | 结论 |
|---|---|---|
| `MotionManager.SetupMotion` 先取 `GamepadMotion` calibrated gyro/gravity，再应用 `GyrometerMultiplier` / `AccelerometerMultiplier` | standalone native 仍使用已计算的 `cgx/cgy/cgz`，但 lane 固定阈值/倍率，不消费完整 profile 参数 | 公式 owner 未闭合；不是新符号偏移 |
| `SteeringAxis.Roll/Yaw/Auto`，Windows/SerialUSBIMU 的 `Auto` 可解析为 `Yaw`；`SwapYawRoll` 为 `(X,-Z,-Y)` | standalone lane 硬编码 `motionX=-cgy`、`motionY=cgx` | 局部 test-lane 语义被窄化；不能泛化为 HC 全 profile |
| LocalSpace `(Default.Z, Default.X)` | mapper fixture 和 lane 注释/公式均为该方向 | 静态子公式一致 |
| PlayerSpace `(-Y,X)`，scale `1.41f`；WorldSpace `(-Y,X)`，scale `0.125f` | TypeScript fixture 保持同向，native candidate 仍暴露对应值 | 静态子公式一致；producer/active plane 未闭合 |
| `MotionMode Off/On/Toggle`、Toggle rising-edge/debounce | fixture 可读 `pressed/toggleOn`，native standalone lane 无完整 semantic trigger/toggle owner | action owner 缺失 |
| `AxisActions.ApplyGyroOutput` 使用选定 `AxisLayoutFlags`，`current + gyro × (gyroWeight - padNorm)` | TypeScript fixture 重演右摇杆子公式；native test lane blend 同向 | 右摇杆子公式相符；target route 不完整 |
| Windows sensor `TimerManager.GetPeriod()` 作为传感器周期 | native 固定请求 8 ms，再取 `max(MinimumReportInterval, 8)` | 只有锁定默认值的部分对齐，不是完整 TimerManager consumer |

因此，本页不报告新的 `Local/Player/World/Steering` 轴符号偏移；以下缺口是 admission、provenance、owner、target 和 lifecycle 缺口。

## 4. Current-source 缺口与歧义

### P110-F01：real-stick lane 的 `pairProven` 不是合同级 `SamplePairProof`

当前 `native/main.cpp:6548–6553` 的准入近似为：

```text
real-stick flag present
∧ live
∧ gyro callback has value
∧ accel callback has value
∧ normalized ACPI prefix equal
∧ selectorId == asus-rc73xa
```

这没有证明：

- 同一硬件 frame / PnP container / physical identity；
- gyro 与 accel 的 normalized monotonic timestamp、skew 上限和时间转换；
- hardware sequence 是否来自同一 provider generation；
- calibration epoch、power generation、config revision/hash；
- first paired sample receipt；
- proof 中的 matrix identity/source hash 与实际 GamepadMotion 输入一致。

随后日志会生成 `hc-test-same-provider-rc73xa-v1` 字符串，Host 只要求该字段非空。该字符串不能升级为 `SamplePairProofV1`。

分类：`SOURCE-DIVERGENCE / T11-T12-PROVENANCE-GATE / SAFE_STOP`。  
处置：standalone lane 也不得被解释为正式 HC pair closed；正式安全行为仍是 `pairProven=false → safe-zero/no-report`。

### P110-F02：HC selector/matrix candidate 没有成为 GamepadMotion active input

`inputCaptureApplyHcMatrixCandidate()` 会写出 `hcCandidateGx/Gy/Gz` 与 accel candidate，但当前 `g_gmProcess()` 路径仍以 raw `mgx/mgy/mgz` 调用。candidate 的 `resolved=true` 只进入 capture telemetry，未形成同代 matrix admission。

因此：

```text
selectorBound=true
≠ actual GamepadMotion input uses HC RC73XA matrix
```

分类：`SOURCE-DIVERGENCE / MATRIX-ADMISSION-CONFLICT / T11-T12-OWNED`。  
这不是可以直接改一个正负号的局部偏移；在 pair proof、provider ingress 和 matrix owner 没有共同合同前，不直接把 candidate 替换进 producer。

### P110-F03：standalone lane 固定 yaw/roll 选择，缺少 HC `SteeringAxis` owner

当前 lane 注释和代码固定：

```text
motionX = -cgy * 1000
motionY =  cgx * 1000
```

HC 由 profile `SteeringAxis`、`DetermineSteeringAxis`、sensor family 以及 `SwapYawRoll` 决定。YMCC 没有把 `Roll/Yaw/Auto` 的选择、sensor family 和选择结果写入 runtime proof，也没有把该决定传给 native lane。

分类：`SEMANTIC-AMBIGUITY / MAPPING-OWNER-MISSING`。  
处置：只能声称“默认 LocalSpace candidate”，不能声称所有 HC profile/SKU 已对齐。

### P110-F04：real-stick lane 是固定默认参数，不是完整 HC parameter consumer

当前 lane 固定/简化了：

```text
threshold = 2000
sensitivity curve = 默认 0.5 节点的恒等缩放
sensitivity = 1.0 × 1000
gyroWeight = 1.2
```

未证明完整消费：`GyrometerMultiplier`、`AccelerometerMultiplier`、`MotionSensitivityX/Y`、`MotionMode`、`MotionTrigger`、Toggle debounce、ADS multiplier、velocity mode/scale、invert、steering 参数、output shape/deadzone、config revision/hash。

分类：`EXPLICIT-TEST-LANE-NARROWING / T10-T15-PARAMETER-CONSUMPTION-GAP`。  
处置：该 lane 只能作为明确标记的默认诊断路径，不能作为 HC 参数 parity 闭口。

### P110-F05：`motionSample.ts` contract 没有进入 current native/InputHost production route

`src/bridge/motionSample.ts` 已定义 `SamplePairProofV1`、锁定 HC matrix registry、threshold→matrix 和 `processPairedMotion()`；但当前 native/InputHost/UI 路径没有证明 import/调用该 contract。

分类：`SOURCE-LOCATED / CONTRACT-ARTIFACT / PRODUCTION-ROUTE-MISSING`。  
处置：TypeScript contract 的存在不能提升 native real-stick lane 的 provenance 等级。

### P110-F06：persona/target 传播仍固定 DS4

当前 native 多处固定：

```text
persona = dualshock4
kInputHostTargetId = ymcc-hidmaestro-ds4
profileIdentity = dualshock-4-v2
```

而 `inputContracts.ts` schema 声明 `disabled / dualshock4 / xbox360`，且 `xbox360 + ds4-imu` 的静态校验会拒绝。当前没有证明 `outputTarget.persona` 的 revision/hash 经过 Coordinator → native → Host target 的完整动态传播；standalone native 也只把 targetStick 做成 left/right 文本。

分类：`TARGET-ROUTE-MISSING / PERSONA-PROPAGATION-GAP`。  
处置：不能把 schema 支持 Xbox 360 解释为 current runtime 已完成 persona 切换；X360 仍禁止伪造 DS4 IMU。

### P110-F07：telemetry 没有完整 active-lane provenance

当前 UI telemetry 主要传播：native loop `now` sequence、FILETIME 格式化的 `timestampUtc`、gyro/accel 数值、player/world 数值、targetStick、motionAdmission 和 Host-local frame。

仍缺少或未绑定：

- provider identity、physical identity、PnP/container；
- gyro/accel source sequence 的同帧关系与 skew；
- matrix id/hash 的 active-vs-candidate stage；
- pair proof、calibration epoch、power generation；
- raw / calibrated / GamepadMotion / candidate / submitted stage；
- `safe-zero`、真实静止、sensor absent、Host 未运行的不可混淆 reason；
- external HID/Steam/game consumer receipt。

分类：`OBSERVABILITY-GAP / PROVENANCE-PROPAGATION-MISSING / SEMANTIC-AMBIGUITY`。  
处置：UI 波形或数字变化只能证明 telemetry/display 层，不得关闭 T16/T17。

### P110-F08：power resume 后 GamepadMotion/calibration 代次未完全失效与重建

当前 suspend/resume 代码会切换 power lifecycle、递增 `g_powerGeneration`、重启 WinRT listener、清空 sensor cache，并要求 Host release；但没有完整证明：

```text
old callback rejected
∧ old GamepadMotion object/calibration epoch invalidated
∧ fresh pair proof bound to new power generation
∧ first fresh paired sample re-admitted
```

`g_gmLocked` / calibration state 的重置主要依赖 object/lane 生命周期；listener restart 不是 HC motion rearm receipt。

分类：`LIFECYCLE-GAP / CALIBRATION-EPOCH-UNKNOWN / RUNTIME-BLOCKED`。  
处置：resume 后不自动复用旧 sample/calibration；继续 safe-zero，等待显式 rearm 证据。

### P110-F09：standalone lane 启用权只有文件标记，没有 build/provenance 绑定

native 只需在 `feature-assets/gyro-motion/` 读取：

```text
real-stick-test.flag == HC-REAL-STICK-TEST-v1
real-stick-target.txt ∈ {left,right}
```

当前代码没有把 lane marker 绑定到签名 manifest、package digest、测试包身份、source digest 或 runtime dependency digest；本轮也没有重新验证 generator/updater 对该 marker 的排除/保留策略。

分类：`TEST-LANE-BOUNDARY-GAP / PACKAGE-PROVENANCE-UNKNOWN`。  
处置：不能只因“正式包通常不带 marker”就把 production unreachable 升级为可证明；正式 Release/updater 仍禁止包含该 lane 的 real-stick marker，需由 T22-RF00/package audit 重新闭口。

### P110-F10：sensor cadence/time-domain 只有部分 HC 对齐

HC `SensorsManager` 将 `TimerManager.GetPeriod()` 同时交给 Windows gyro/accel wrapper；当前 native 使用固定默认请求 `8 ms` 并记录 WinRT `Timestamp()` 的 FILETIME 100ns 值，再将最大 timestamp 格式化为 UI UTC。

当前没有证明：

- YMCC 的 8 ms 与当前 HC profile/runtime period 同 revision；
- gyro/accel 事件时间转换为同一 monotonic domain；
- UI `sequence`（native loop tick）与 sensor sequence/frame sequence 同义；
- fixed latest-value cache 不会跨 sensor generation 混帧。

分类：`HC-PARITY-PARTIAL / TIME-DOMAIN-AMBIGUITY / T11-T16-OWNED`。  
处置：不能用 UI timestamp 或 loop sequence 替代 pair proof。

### P110-F11：schema/fixture 支持 left stick，但 canonical assembler 仍只消费 right stick

`GyroMotionConfigV1.outputStick` 声明 `left/right`，native test lane 可以接收 `targetStick`；但 `virtualReportAssembler.ts` 的 `CanonicalFrame`、blend 和输出字段仍固定 `rightStickBase/rightStick/gyroContribution`，没有同等 left-stick canonical consumer。

分类：`SOURCE-DIVERGENCE / TARGET-CONSUMER-GAP / T10-T15-OWNED`。  
处置：不能把“配置可选 left”解释成 left route 已闭合；不在本轮自行补一条旁路。

### P110-F12：Host 的 `motionPairProofId` 仍是非空字符串字段，不是 proof registry

`InputHost/Program.cs` 会要求 `motionPairProofId` 为非空字符串，并校验 normalized input hash；当前没有 registry 查询、provider/physical identity、matrix hash、calibration epoch、power generation、config hash 与该 proofId 的一致性验证。

分类：`LATENT-PROVENANCE-GAP / T10-T12-OWNED`。  
处置：当前 `pair-unproven-safe-zero` 继续是唯一安全正式标记；不得把 `hc-test-same-provider-rc73xa-v1` 当作 direct IMU admission。

## 5. P109 stale correction

P109 中以下两条只属于旧 source snapshot，不能继续写作 current bug：

1. “任意 accepted frame 都直接写 host-active”：当前 source 已改为读取 receipt `firstFrame`，并以 `g_inputHostActive = firstFrame`、`lifecycle = host-active/neutralized`；旧结论降级为 `HISTORICAL / SUPERSEDED-BY-CURRENT-WORKTREE`。
2. “native 永远 outX/outY=0”：当前 source 已出现 real-stick standalone blend、`motionAdmitted` 与 `targetStick`；该结论降级为 `HISTORICAL / OLD-SNAPSHOT-ONLY`。但 P110-F01/F02/F03/F04 说明这不等于 HC runtime closed。

## 6. 证据、推断、未知分层

### 6.1 证据（可直接复核）

- current `native/main.cpp` 存在 `HC-REAL-STICK-TEST-v1` marker、`targetStick`、`motionAdmitted`、`pairProven` candidate 分支。
- current native 会记录 raw、matrix、HC candidate、selector、provider key、sensor sequence/timestamp、calibration confidence、player/world 和 Host-local frame。
- current native matrix candidate 函数的输出没有被证明替换实际 `g_gmProcess` 输入。
- HC source 明确拥有 profile multiplier、SteeringAxis/Auto、MotionMode/Toggle、AxisLayout target 和 sensor period owner。
- current native/Host 多处固定 DS4 target/persona；TypeScript schema 另有 Xbox 360 选项。
- `motionSample.ts` 的 proof/matrix contract 已存在，但没有 current production route receipt。

### 6.2 安全推断（明确标记为推断）

- 若 real-stick marker、same-provider prefix 与 RC73XA selector 同时成立，lane 可能进入 Host-local stick blend；这只能解释 standalone candidate 行为，不等于外部 consumer 可见。
- raw matrix 被送入 GamepadMotion 的可能性来自当前调用关系；需后续 instrumentation/同代 sample proof 才能升级为 runtime receipt。
- 当前固定 DS4 target 很可能解释了 schema 的 persona 变化未进入 Host，但本轮没有运行动态 persona 切换，因此保留为 route gap，不写成用户可见故障。

### 6.3 未知/缺口（本轮未闭口）

```text
same hardware frame / PnP container / provider registry
timestamp normalization and skew bound
active HC matrix at GamepadMotion ingress
profile parameter revision/hash consumer receipt
left-stick canonical consumer
persona dynamic propagation
power-resume motion epoch and first fresh pair
test marker package/updater provenance
HIDMaestro DS4 report readback
Steam/Windows/game consumer observation
```

## 7. 处置与下一步

本轮当时不修源码、不修改 T10、不解除 `pairProven=false`、不新增 DGF、不改变 `runtimeUpgrade=false`。该历史裁决已由 P111 的用户授权 standalone test-lane source fix 局部 supersede；正式产品 runtime、descriptor/readback、consumer、power rearm 和 proof registry 仍未闭口。

唯一顺序仍为：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

需要用户介入的最小集合仍是：

1. 在 P110 source snapshot 重新冻结后，提供/授权真实设备的同代 provider/container/pair/calibration receipt；
2. 在 standalone test lane 中收集 `pairProven`、matrix stage、power generation、targetStick、Host first-frame 和 external consumer 的同一 run 证据；
3. 若要关闭 T17/T18，必须提供 DS4 descriptor/report/decode/readback 以及 ROG 的 P-HID/P-XINPUT/P-OWNER/Steam/game consumer 对账。

在上述证据到达前，主线保持：

```text
MAINLINE-V1-DESIGN-FRAMEWORK-PARTIALLY-CLOSED
/ T22-CURRENT-WORKTREE-REAUDIT-REQUIRED
/ T11-T13-UNENCLOSED
/ RUNTIME-BLOCKED
/ runtimeUpgrade=false
```
