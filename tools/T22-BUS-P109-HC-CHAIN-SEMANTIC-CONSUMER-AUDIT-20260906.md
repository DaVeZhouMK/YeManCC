# T22 BUS-P109：HC motion 链路语义 consumer / telemetry 传播 / 恢复代次审计

日期：2026-09-06  
状态：`STATIC-AUDIT-COMPLETE / NEW-GAPS-RECORDED / NO-SOURCE-FIX-IN-THIS-PASS / RUNTIME-BLOCKED`

## 1. 范围与边界

本轮承接 P104–P108，继续扩大“未知、缺口、歧义”检索。审计对象是：

```text
锁定 HC MotionManager / SensorsManager / TouchpadActions
→ YMCC durable config / mapper / fixture assembler
→ native capture / GamepadMotion candidate
→ native gyro.telemetry → UI contract / preview
→ power suspend / resume
```

本轮为只读 source audit：没有运行真实设备、InputHost、HIDMaestro、HidHide、Steam、游戏或硬件；没有修改 C++、C#、TypeScript、JSON、二进制或系统状态；没有解除 `pairProven=false`，没有把 candidate matrix 送入 Host。

## 2. 当前 source 快照

本页结论仅属于下列 current-source 文件快照；任何后续源码写入都使本页需要重新冻结：

```text
native/main.cpp                                      A3399E6B26679B93765195A7CE5E3221017387836851EDEDB720520FC8074428
src/bridge/gyroMotionMapperMock.ts                   E2F25BC842C967E2B6C875F65ECC47468B7215B39629B4403FD5D64226F3FC68
src/bridge/virtualReportAssembler.ts                  B6FF222B8267F2979B6DF1745EE6C880086DE71D13DD5377E3B40A0803744EA6
src/bridge/inputContracts.ts                           697600B4668CED2B67FCB9306189CB36E2FBDC6D11BFEF6D08770224CD085F02
src/views/GyroMotionView.vue                          430D5BEACE9411DE8CE76CFD9C351DCAFA8CCE161590AF75CDFCA17D8362DA0D
InputHost/Program.cs                                  7C3B915B92B120FFF1D385EB286924129C3F894FEFDD8DB587F1DED56FA35239
HC source/Managers/MotionManager.cs                   19A9C645351DC74B5A0E2A74D99C3AD95F105CF85EDF173C8AF8531713F6FC7E
HC source/Actions/TouchpadActions.cs                  6BD14DDADAC39A871E9951CAF68CE229BC810A496554C7AE3F2F2EA3CE770420
HC source/Managers/SensorsManager.cs                  660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3
```

## 3. 已证实：HC 数学/动作骨架没有发现新的轴符号偏移

锁定 HC 的当前事实：

| HC 阶段 | HC source 锚点 | current YMCC 对照 | 结论 |
|---|---|---|---|
| producer multiplier | `MotionManager.cs:92–113`：`GamepadMotion` calibrated gyro/gravity 先乘 `GyrometerMultiplier` / `AccelerometerMultiplier` | 当前 mapper 只接收 `gamepadMotionPlane`，没有在 producer 或 mapper 消费这两个字段 | 公式方向未漂移，但参数 consumer 缺口仍存在 |
| LocalSpace | `MotionManager.cs:265–268`：`(Default.Z, Default.X)` | `gyroMotionMapperMock.ts:117–121`：`(defaultGyro.z, defaultGyro.x)` | 静态公式相符 |
| PlayerSpace | `MotionManager.cs:269–272`：native `GetPlayerSpaceGyro(..., 1.41f)` 后 `(-Y, X)` | `gyroMotionMapperMock.ts:109–112`：`(-plane.playerSpace.y, plane.playerSpace.x)`；native candidate 调用仍为 `1.41f` | 静态公式相符；producer plane 未证明 |
| WorldSpace | `MotionManager.cs:273–275`：native `GetWorldSpaceGyro(..., 0.125f)` 后 `(-Y, X)` | `gyroMotionMapperMock.ts:113–116`；native candidate 调用仍为 `0.125f` | 静态公式相符；producer plane 未证明 |
| JoystickSteering | `MotionManager.cs:236–238,277–279`：`DefaultAccel → Inclination.Angles.Y → Steering` | `gyroMotionMapperMock.ts:99–108`：同样从 admitted `defaultAccel` 计算 `Angles.Y` 后 `hcSteering` | 静态公式相符；倍率和 plane 未闭合 |
| gyro blend | `TouchpadActions.cs:327–338`：选定 `AxisLayoutFlags` 的 `current + gyro × (gyroWeight - padNorm)` | `virtualReportAssembler.ts:69–105` 只对 `rightStick` 重演该公式 | 数值子公式相符；目标选择路由不相符/不完整 |
| mode/trigger/toggle | `MotionManager.cs:203–231`：Off/On/Toggle，Toggle 有 rising-edge/debounce 状态 | mapper 只有外部传入 `pressed` / `toggleOn`；页面没有真实 semantic button/toggle producer | 不能宣称 HC action parity |

因此本轮没有发现新的 `Local/Player/World/Steering` 轴线、符号、单位或衰减公式原创偏移。发现的是参数和目标在链路中的 consumer/owner 缺口。

## 4. 新发现的明确缺口与歧义

### P109-F01：HC gyro/accel multiplier 只保存，不进入当前 consumer

已证实：

```text
UI 保存：GyroMotionView.vue:243–248,270–279
durable normalize：settingsRepository.ts:319–320
schema 字段：inputContracts.ts:14–15
mapper 消费：无 gyroMultiplier / accelerometerMultiplier
native telemetry/Host transport：无对应字段消费
```

HC 明确在 `MotionManager.SetupMotion` 先把 calibrated gyro/gravity 乘 profile multiplier，再进入 Default/Player/World/Steering 映射。当前 YMCC 的两个滑块可以被保存，但没有 current-source consumer receipt；因此不能把控件值写作“已应用”，也不能在 mapper 内自行补一层乘法，因为 producer plane 的 owner、revision、pair/calibration 仍由 T10/T11 持有。

分类：`SOURCE-DIVERGENCE / PARAMETER-CONSUMPTION-GAP / T10-OWNED`。  
安全行为：保留 config intent；不伪造 applied/active，不放开 pair gate。

### P109-F02：outputStick/left-stick 目标在 current canonical route 中没有 HC 等价路由

已证实：

```text
HC：GyroActions.DefaultAxisLayoutFlags = RightStick；LayoutManager/TouchpadActions
    通过 AxisLayoutFlags 可路由 LeftStick、RightStick 等目标。
YMCC：GyroMotionView.vue:37,94–102,127–128,154
     暴露 outputStick=left/right；赛车预设写入 left；left preview 明确 zero。
YMCC：virtualReportAssembler.ts:15–20,38–46,69–105
     CanonicalFrame 只有 rightStickBase/rightStick，没有 leftStick 或 AxisLayoutFlags。
```

当前做法把 left 请求 fail-close 为 `left-stick-preview-route-unresolved`，这是避免把右摇杆 fixture 冒充左摇杆，属于安全行为；但它也证明“保存了 left 请求”不等于 HC 左摇杆动作已实现。该缺口不能在 preview 层通过复制 right-stick 字段解决，需要 T10 Config→Host→target layout 的 owner/ACK/first-frame 设计。

分类：`SOURCE-DIVERGENCE / TARGET-ROUTE-MISSING / T10-T15-OWNED`。  
安全行为：left route 零输出；不伪造右摇杆结果。

### P109-F03：P106 selector/matrix receipt 未传播到 native→UI telemetry

已证实：

```text
native capture JSONL：main.cpp:6419–6434、6554–6572
  写入 hc-motion-selector-receipt、hcMatrixCandidate、event sequence/timestamp、confidence/steady。
native UI event：main.cpp:6610–6623
  只发送 gyro/playerSpace/worldSpace/accel/output/hostFrame。
UI normalize：inputContracts.ts:82–164
  未定义/保留 selector、matrix candidate、provider binding、confidence、steady、event sequence。
```

所以 P107 即使在 ROG 上产生了 native selector receipt，当前陀螺仪页面也无法显示或把它与轴线图的同一 telemetry frame 关联。JSONL receipt 和 UI receipt 是两个不同的观察层；不能用页面看到的 raw axis/Host dot 推导 P106 selector 已进入 UI 或已绑定 provider。

分类：`OBSERVABILITY-GAP / PROVENANCE-PROPAGATION-MISSING`。  
安全行为：UI 继续只显示诊断 raw/calibrated-unknown、Host-local receipt 和 fixture 状态；不显示 applied/active。

### P109-F04：传感器缺失、非法值、合法静止零值在 UI 边界存在同值歧义

已证实：

```text
native 在无 pair/无 sensor 时仍可发送 gyro/output 结构；Host frame 的 gyroDps 由当前安全路径固定为 0。
normalizeGyroTelemetry：inputContracts.ts:86–90 将缺失/非法数字归一为 0；
gyro/accel presence、sample validity、zero reason 没有同代字段。
GyroMotionView.vue:333–339 只能显示数值与文字标签，不能区分
  sensor-absent / invalid / safe-zero / real-rest / calibrated-zero。
```

这不是把零值改成非零的理由。它是数据合同歧义：同一个 `0.0` 不能证明传感器静止、pair gate safe-zero、Host 未运行或 producer 缺失。下一次若扩合同，必须使用显式 presence/validity/reason，并保留 source generation；不能靠前端数值阈值猜测。

分类：`SEMANTIC-AMBIGUITY / TELEMETRY-CONTRACT-GAP`。

### P109-F05：power suspend/resume 只重订阅传感器，GamepadMotion/calibration 代次没有闭合

已证实：

```text
main.cpp:5021–5034
  suspend：StopWinRtSensors + ResetSamples；resume：StartWinRtSensors。
main.cpp:5104–5137
  g_gmLocked/g_gmCalStart/g_gmPairUnprovenLogged 只在 DLL create 时初始化。
main.cpp:6466
  pairProven 仍硬编码 false；当前没有 real motion admission，故本缺口尚未造成实际输出。
```

HC 的 sensor lifecycle 有 `StopListening → UpdateSensor → StartListening`，但项目合同还要求 power generation/calibration epoch、旧 callback 拒绝、首个 fresh paired sample 和 explicit rearm。当前代码没有证明 resume 后 GamepadMotion calibration epoch、source generation、provider identity 与 Host epoch 的绑定，也没有把旧 `g_gm` 状态显式标记为 invalid。不能把“WinRT listener 重启成功”写成 HC motion rearm 完成。

分类：`LIFECYCLE-GAP / CALIBRATION-EPOCH-UNKNOWN / RUNTIME-BLOCKED`。  
安全行为：继续 `pairProven=false / safe-zero / no-report`；不直接加 reset 或自动 rearm。

### P109-F06：InputHost 接受 motionPairProofId，但没有 proof registry / source binding

已证实：

```text
InputHost/Program.cs:27–29、631–655
  SUBMIT_FRAME 只要求 motionPairProofId 是非空字符串；
  没有校验 proofId 是否登记、是否绑定 provider/physical identity、pair epoch、calibration epoch、configHash 或 source generation。
InputHost/Program.cs:484–485
  当前 SubmitState 不写 direct IMU 字段，所以该问题尚未形成真实 DS4 IMU 输出。
native/main.cpp:6285–6289
  当前 native 固定发送 pair-unproven-safe-zero。
```

分类：`LATENT-PROVENANCE-GAP / T10-T12-OWNED`。这是未来 direct-IMU/paired motion 开启前必须关闭的 proof binding 缺口，不是当前把字符串改成非空即可的修复；当前安全门保持 no-report。

### P109-F07：native 将任意已接受 frame 直接标为 host-active，没有检查 firstFrame

已证实：

```text
InputHost/Program.cs:334–336
  Host 返回 firstFrame = (_phase == Neutralized)，随后进入 Active。
native/main.cpp:6291–6298
  读取 receipt.firstFrame，但只要 accepted 就写 g_inputHostActive=true、lifecycle=host-active；
  实际没有 `accepted && firstFrame` 条件。
native/main.cpp:6323–6326
  对 UI 返回 hostActive=true 与 firstFrame=false 的组合是可构造的。
```

这与当前任务书要求的“同代 first-frame receipt 才能 active”存在明确局部 source divergence。由于 T10 正在由用户持有、且本轮授权的是 BUS 审计而不是 T10 实现，本轮只记录，不直接改 native；不能把 `host-active` label 当作 first-frame active proof。

分类：`SOURCE-DIVERGENCE / HOST-ACTIVE-GATE / T10-OWNED`。  
安全行为：在真实修正和重新 RF00 前，外部 consumer 仍视为未证明。

## 5. 未发现 / 不应误报为偏差的项目

本轮明确没有发现：

```text
新的 LocalSpace (Z,X) 符号偏移
新的 PlayerSpace/WorldSpace (-Y,X) 偏移
新的 JoystickSteering Angle.Y 方向偏移
新的 velocity 60 FPS / 0.90 decay 偏移
新的 gyroWeight blend 数学偏移
新的 ROG RC73XA HC matrix 事实冲突
```

以上“未发现”只限静态 source comparison；不代表 runtime provider/pair/calibration/Host/HID/Steam/game 已闭合。

## 6. 当前处置与后续门

本轮不改源码，不修改 T10，不解除 `pairProven`，不创建 DGF，不改变 `runtimeUpgrade=false`。新增缺口回链：

```text
P109-F01 → G-T15 / G-P104-PLANE-PROVENANCE
P109-F02 → G-T10 / G-T15 / G-P104-PLANE-PROVENANCE
P109-F03 → G-P104-LOG-INTEGRITY / G-P106-CANDIDATE-TELEMETRY
P109-F04 → G-P104-LOG-INTEGRITY / G-P104-PAIR-TIMESTAMP
P109-F05 → G-T16 / G-LISTENER / G-P104-CALIBRATION-ARM
P109-F06 → G-T10 / G-T12 / G-P104-PLANE-PROVENANCE
P109-F07 → G-T10 / G-P104-PLANE-PROVENANCE / G-TARGET-RELEASE
```

唯一安全顺序保持：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

需要真实设备/Host/消费者时，仍只请求 P107 ROG selector receipt 和 T15–T18 定向证据；本页不会把源码缺口自行变成 runtime 实现。

## 7. 可复核命令

```text
rg -n -S "gyroMultiplier|accelerometerMultiplier|outputStick|outputAxis|motionTrigger|toggleOn" src/bridge src/views InputHost native
rg -n -S "GyrometerMultiplier|AccelerometerMultiplier|MotionTriggered|MotionToggle|ApplyGyroOutput|DefaultAxisLayoutFlags" deps/handheldcompanion-runtime/source
rg -n -S "hc-motion-selector-receipt|hcMatrixCandidate|gamepadSerialPostEvent(\"gyro.telemetry\"|normalizeGyroTelemetry" native src
Get-FileHash -Algorithm SHA256 <the eight files listed in §2>
```

上述命令为静态定位命令；本轮未运行设备或系统状态测试。
