# T22-BUS-P65：HC motion API reachability 与 ROG / Lenovo / MSI 设备矩阵审计（2026-09-05）

Status: `STATIC-EXPANDED / HC-SOURCE-BOUND / EVIDENCE-UNKNOWN-SEPARATED / NO-SOURCE-MUTATION / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC candidate、当前 YMCC source 与任务书合同的只读增量审计。范围扩大到：`motion`、`gyro`、`gyrometer`、`accelerometer`、`acceleration`、`gravity`、`raw`、`calibrated`、`processed`、`orientation`、`Madgwick`、`PlayerSpace`、`WorldSpace`、`LocalSpace`、`JoystickSteering`、`SteeringAxis`、`GyrometerMultiplier`、`AccelerometerMultiplier`、`ADS`、`velocity`、`decay`、`threshold`、`offset`、`Stillness`、`SensorFusion`、`confidence`、`ReportInterval`、`ReadingChanged`、`GetCurrentReading`、`SensorFamily`、`MotionSensor`、`GetDefault`、`DeviceId`、`SMBIOS`、`DMI`、`ProductName`、`SystemModel`、`RC71L`、`RC72LA`、`RC73YA`、`RC73XA`、`Legion`、`Claw`、`XInput`、`DInput`、`HID`、`report ID`、`report length`、`provider`、`epoch`、`rebind`、`resume`、`release`。

本轮没有启动 YeManCC、InputHost、GamepadMotion、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实硬件；没有执行 PnP、电源、拔插、睡眠/唤醒或外部消费者回读；本页 motion 审计没有修改产品程序源码、JSON、配置、驱动或正式包。BUS-P65 companion run 对审计 probe/selftest 的工具修正另记于 runtime supplement。

## 1. 证据对象与 source identity

### 1.1 锁定 HC

根目录：`G:\YeManCC-Work\Isolated\HC-Candidate-0.32.4.0-06c0b954-20260902`

| 文件 | bytes | SHA-256 |
| --- | ---: | --- |
| `HandheldCompanion/Devices/IDevice.cs` | 48018 | `FBAED1DFD329E3328376488B8EC70C3A7FDCF97D70DCB37F1525EE33AA169C20` |
| `HandheldCompanion/Devices/ASUS/ROGAlly.cs` | 25435 | `04097428F3D62DE75F8160EB47398E1C61606F9D6681EB605D100B384814AA00` |
| `HandheldCompanion/Devices/ASUS/ROGAllyX.cs` | 1137 | `8907D79FEB28D2C76C6EB3D0F20E798C0D91CC5E0D7A84616B1D2C7064ECECBD` |
| `HandheldCompanion/Devices/ASUS/XboxROGAlly.cs` | 1815 | `78BF90520419B63C6C8FD2BC12543B2D16280895802D8F9952849C4E030FE3D4` |
| `HandheldCompanion/Devices/ASUS/XboxROGAllyX.cs` | 1826 | `404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE` |
| `HandheldCompanion/Devices/Lenovo/LegionGoTablet.cs` | 14058 | `A05910FDDD410A68E00F028B3760C784A6AA5A8BC0FCA27BEE96ED20D59F3414` |
| `HandheldCompanion/Devices/Lenovo/LegionGoTablet2.cs` | 5646 | `823586B37D8580AE5545B2B1D5EA091F7CD066037B78C4E04184E77729F804B2` |
| `HandheldCompanion/Devices/Lenovo/LegionGoSZ1.cs` | 8890 | `176C4AEE0153AAA9956837B95B044E493C90AE0C5CD86EAAA1AC2754CA54CF66` |
| `HandheldCompanion/Devices/MSI/ClawA1M.cs` | 41825 | `FA7E4A2C2F6B56BA019689361769127875BE4DE0985351001839A48614D08748` |
| `HandheldCompanion/Devices/MSI/ClawA2VM.cs` | 1569 | `AA54CA29A82E9DBBE0FE23B98CB766E81A5E6558FD7C1221DCC8B8E900020D5E` |
| `HandheldCompanion/Devices/MSI/ClawBZ2EM.cs` | 1469 | `644E95EF21A6F191E92976026595ECF1E13153F7BCC877757BAF2911079FB726` |
| `HandheldCompanion/Devices/MSI/ClawCG3EM.cs` | 1934 | `45518A1EF85F40D2427CA5259D1B1EB8538883618F99C6B2D39DBC4567E0BFBD` |
| `HandheldCompanion/Managers/SensorsManager.cs` | 16413 | `660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3` |
| `HandheldCompanion/Managers/MotionManager.cs` | 15971 | `19A9C645351DC74B5A0E2A74D99C3AD95F105CF85EDF173C8AF8531713F6FC7E` |
| `HandheldCompanion/Sensors/IMUGyrometer.cs` | 5084 | `BC44A51518C82BDDAF7BA2D79A37A7F57EF762E7F0D5F5B291DBB472077930E1` |
| `HandheldCompanion/Sensors/IMUAccelerometer.cs` | 5012 | `F63C11C0729B013E7B03D717C09F25FC099A8433752B52DE9E3826BCDFFF7B4C` |
| `HandheldCompanion/Controllers/Lenovo/LegionController.cs` | 20075 | `F22DA7BE57ADFD962176CB2E58A2C33259658D700C0CDA2975204CDD5A1E5F77` |
| `HandheldCompanion/Controllers/Lenovo/LegionControllerS.cs` | 5059 | `46DC972BF2FA09B710D52FE472469A316DB85775C70CB52C81F8CBBCCFBB13B2` |
| `HandheldCompanion/Controllers/MSI/DClawController.cs` | 8942 | `FE9F0909538E2AF4AD3B2E67CE6FBBBE1CD3D46D8C1273D9E9F211B8FB812035` |
| `HandheldCompanion/Helpers/GamepadMotion.cs` | 12816 | `7BA36F0DCB0E280D526E5B09E95888AF58B98927B3C83001858817EC12FB23ED` |

### 1.2 当前 YMCC

| 文件 | bytes | SHA-256 |
| --- | ---: | --- |
| `native/main.cpp` | 1081061 | `0246970E4EFFFA2F1DD0C8583987E25A7E758A3538ABE96A9015DD6E672828D3` |
| `InputHost/Program.cs` | 见当前工作树；本页不重冻全量 manifest | — |
| `src/bridge/motionSample.ts` | 13547 | `B8314D6BED5852629217C098EBA386F6698FFF580BE8B9C2E94723C318EE7AE3` |
| `src/bridge/specialControllerProtocols.ts` | 2736 | `145CC60A6BCA581300E2CCC9FA52FD88664A6EF5CD8AE90073FF88F8F709014D` |
| `src/bridge/rogGyroProtocol.ts` | 2584 | `1F2972AE0BAD99B2F78B634E5F3DC7FCC3D7FE4510C5A4FEEE235875ECC9A335` |
| `src/bridge/rogInputAdapter.ts` | 3633 | `D1EC2EB58D22F80EF38D47F017809E57844264B2FC96C238E21252E5BBDAF634` |

上述是本轮局部 static identity，不是新的 RF00 manifest。因为本轮新增证据页，后续任何 current-source 声称仍须走 `T22-RF00 → T20 → T21 → T14`。

## 2. HC 事实

### 2.1 设备身份先于矩阵

HC `Devices/IDevice.cs` 使用主板/系统信息选择设备类型，而不是用共享 HID VID/PID 直接决定矩阵：

- ASUS `ProductName`：`RC71L → ROGAlly`、`RC72LA → ROGAllyX`、`RC73YA → XboxROGAlly`、`RC73XA → XboxROGAllyX`（825–835 行）。
- Lenovo `SystemModel`：`83E1 → LegionGoTablet`、`83N0/83N1 → LegionGoTablet2`、`83L3 → LegionGoSZ2`、`83N6/83Q2/83Q3 → LegionGoSZ1`（856–875 行）。
- MSI `ProductName`：`MS-1T41 → ClawA1M`、`MS-1T42/MS-1T52 → ClawA2VM`、`MS-1T8K → ClawBZ2EM`、`MS-1T91 → ClawCG3EM`（879–895 行）。

HC `MotherboardInfo.cs` 分别从 `Win32_BaseBoard` 与 `Win32_ComputerSystem` 读取 `Product`、`Model`、`Version`、`SystemModel` 等字段；因此 `VID/PID` 只作为设备控制器/HID 接口识别的一部分，不能代替 DMI/SKU 身份。

### 2.2 ROG 矩阵不是一个共用策略

锁定 HC 记录的有效矩阵如下（`AxisSwap Y↔Z` 代表先将输入轴送到对应输出轴，再乘 `Axis` 符号）：

| HC device | Gyro | Accel | 证据 |
| --- | --- | --- | --- |
| `ROGAlly` / RC71L | `(-1,-1,1)` + `X→X,Y→Z,Z→Y` | `(-1,-1,1)` + `X→X,Y→Z,Z→Y` | `ROGAlly.cs:89–109` |
| `ROGAllyX` / RC72LA | `(1,1,-1)`，继承 `ROGAlly` 的 accel | 继承 `ROGAlly` accel | `ROGAllyX.cs:16–20` |
| `XboxROGAlly` / RC73YA | `(1,1,-1)` + `Y↔Z` | `(-1,-1,1)` + `Y↔Z` | `XboxROGAlly.cs:22–42` |
| `XboxROGAllyX` / RC73XA | `(1,1,-1)` + `Y↔Z` | `(-1,-1,1)` + `Y↔Z` | `XboxROGAllyX.cs:22–42` |

因此 `0B05:1ABE/1B4C` 不能单独选择 ROG 矩阵；同一 HID family 对应多个 DMI 设备类。当前 YMCC `rogGyroProtocol.ts` 只保留 Xbox ROG Ally X 矩阵 fixture，`classifyRogGyroProvider()` 固定返回 `none`，native 也输出 `unresolved-rog-pid-family/raw` 或 `unbound-device/raw`。这属于正确的 fail-closed 身份保护，不是可以直接替换成某个 ROG 矩阵的错误。

### 2.3 Lenovo 是控制器内置 IMU 的另一种 provider

HC `LegionController` 与 `LegionControllerS` 都声明 `ControllerCapabilities.MotionSensor`。HC 不是从 Windows `Gyrometer.GetDefault()` 读取它们：

- `LegionController` 为右 Joy-Con 建立独立 `GamepadMotion` 实例（`gamepadMotions[1]`），从 HID report 读取左右控制器的加速度与陀螺仪字段，并按各自 report layout 解出 `±4 g` 与 `±2000 deg/s` 后调用 `GamepadMotion.ProcessMotion(..., delta)`（`LegionController.cs:208–209,255–295`）。
- 左右 Joy-Con 的字段位置和符号不同；右控制器的加速度/陀螺仪并非简单复用左控制器的字节偏移（`LegionController.cs:260–281`）。
- `LegionControllerS` 使用另一套 report layout，从 `data[14..24]` 解出 `±4 g` / `±2000 deg/s`，然后调用对应 `GamepadMotion`（`LegionControllerS.cs:90–104`）。
- `LegionGoTablet`、`LegionGoTablet2`、`LegionGoSZ1` 的设备矩阵仍存在，但 controller report 的 provider 已经做了独立字段解释；未来不能把 controller-pretransformed 输入再次套用 Windows default-sensor 的轴矩阵，否则可能发生 double transform。

### 2.4 MSI Claw 不能从“有矩阵定义”推导“有控制器 IMU 运输

HC `ClawA1M` 定义了 `GyroMatrix=(1,1,-1)+Y↔Z`、`AcceleroMatrix=(-1,-1,1)+Y↔Z`（`ClawA1M.cs:192–212`），并为 `0DB0:1901/1902/1903` 区分 XInput、DInput、Testing 模式（`ClawA1M.cs:127–135,172–184`）。但锁定 `DClawController.cs`：

- 声明的是 `DInputController`，只把 DirectInput `JoystickState` 转成普通按键、摇杆和扳机（`DClawController.cs:9–20,108–174`）；
- 未发现 `Capabilities |= ControllerCapabilities.MotionSensor`；
- 未发现 `GamepadMotion.ProcessMotion`、gyro/accel report 字段或六轴 HID motion decoder；
- `XClawController` 只是 `XInputController` ready 检查（`XClawController.cs:5–23`）。

因此 MSI Claw 的矩阵定义不能单独证明当前 controller report 提供 IMU；若未来采用 HC Windows sensor family，仍须经过设备身份、sensor family 和矩阵绑定；若采用 controller provider，则需要新的、独立的 HC source evidence，不能由 Claw 的普通 XInput/DInput 轴猜测。

### 2.5 HC GamepadMotion API reachability

锁定 `GamepadMotion.cs` 对 `GamepadMotion.dll` 的 P/Invoke/API 包括：

`CreateGamepadMotion`、`DeleteGamepadMotion`、`ResetGamepadMotion`、`ProcessMotion`、`GetRawGyro`、`GetRawAcceleration`、`GetCalibratedGyro`、`GetGravity`、`GetProcessedAcceleration`、`GetOrientation`、`GetPlayerSpaceGyro`、`GetWorldSpaceGyro`、`StartContinuousCalibration`、`PauseContinuousCalibration`、`ResetContinuousCalibration`、`GetCalibrationOffset`、`SetCalibrationOffset`、`GetAutoCalibrationConfidence`、`GetAutoCalibrationIsSteady`、`GetCalibrationMode`、`SetCalibrationMode`。

HC `MotionManager.SetupMotion()` 读取：

- GamepadMotion plane：`GetCalibratedGyro` + `GetGravity`；
- DSU/raw plane：`GetRawGyro` + `GetRawAcceleration`；
- Default plane：GamepadMotion 输出乘 profile 的 `GyrometerMultiplier` / `AccelerometerMultiplier`；
- steering：依据 `SteeringAxis`、`JoystickSteering` 和加速度倾角；
- Player/World：直接调用 `GetPlayerSpaceGyro(..., 1.41f)` 与 `GetWorldSpaceGyro(..., 0.125f)`；
- motion enable：由 `MotionMode` + `MotionTrigger` + toggle release-edge/debounce 决定，未激活时清零 GyroX/GyroY。

当前 YMCC native 动态加载了 create/process/calibrated/player/world/offset/confidence/steady/mode/calibration-control，但没有加载 `GetGravity`、`GetProcessedAcceleration`、`GetOrientation`；当前 InputHost 也不把 direct IMU 写入 `HMGamepadState`。这目前是 **能力覆盖缺口**，不是已证实的产品偏差，因为 `pairProven=false` 仍阻断 GamepadMotion 与 direct IMU 输出；不得因这些函数未加载而直接打开准入，也不得在前端 mapper 复制 HC 的 Default/Player/World 计算。

## 3. YMCC 当前 source 对账

### 已对齐/安全的部分（Fact）

1. `inputCaptureStartWinRtSensors()` 选择 typed WinRT `Gyrometer.GetDefault()` / `Accelerometer.GetDefault()`，使用 `max(MinimumReportInterval, 8 ms)` 并订阅 `ReadingChanged`；方向与 HC Windows sensor branch 相容。
2. `native/main.cpp:6292–6297` 在 Windows gyro ingress 做 `abs(value) >= 2000 → 0`，与 HC `IMUGyrometer` 的阈值形状一致；但当前固定 2000 不是 per-device calibration identity，已正确标成参考片段而非完整 HC parity。
3. `native/main.cpp:6300–6314` 不使用共享 ROG PID 直接套矩阵；`pairProven=false` 维持 `GamepadMotion`、virtual-stick 和 direct IMU safe-zero。
4. `motionSample.ts` 的 `ingressTransform` 已区分 `hc-windows-threshold-then-matrix` 与 `provider-pretransformed`；这是正确的合同方向，当前尚未给 Lenovo controller provider 绑定实现。
5. `specialControllerProtocols.ts` 已按 HC VID/PID 记录 MSI Claw、Legion Tablet、Legion S 的分类；注释和代码没有声明 fallback poller、HID report 或 OEM writer，未把分类记录冒充输入运输。

### 能力覆盖缺口（Gap）

```text
HC DMI/SKU -> exact YMCC device identity receipt              = UNENCLOSED
ROG RC71L/RC72LA/RC73YA/RC73XA matrix registry               = PARTIAL (only Xbox ROG Ally X fixture)
ROG PID-only provider admission                               = correctly NONE / fail-closed
Lenovo controller HID motion provider                         = CLASSIFICATION-ONLY / NO PRODUCT TRANSPORT
Lenovo left/right Joy-Con report identity and epoch           = UNENCLOSED
Lenovo controller-pretransformed vs Windows matrix separation  = CONTRACT-ONLY
MSI Claw controller IMU transport                             = UNENCLOSED; no HC DClaw ProcessMotion found
GamepadMotion GetGravity/ProcessedAcceleration/Orientation     = NOT LOADED in native
HC Default/Player/World production plane                       = UNENCLOSED
per-device threshold/calibration key                          = UNENCLOSED
same-provider/same-generation pair proof                       = UNENCLOSED
timestamp skew/age/rebind invalidation                         = UNENCLOSED / latent
descriptor/report/readback/Host first-frame                   = UNENCLOSED
P-HID/P-XINPUT/P-OWNER + Steam/game consumer                   = RUNTIME-BLOCKED
```

## 4. 错误、未知、HC 偏离、原创逻辑

### 已证实错误

本轮没有发现生产路径中新的、可依据锁定 HC source 无歧义直接修正的轴交换、符号、单位、倍率、deadzone、velocity decay、ROG 矩阵或 MSI/Lenovo report layout 偏移。

### 已知未知

- MSI `ClawCG3EM` 自身对 gyro matrix 留有 `todo: figure me`，不能从它推导额外的轴策略。
- ROG PID family 对应的当前机器 DMI/SKU 未在 YMCC source 中形成 receipt；不能根据“ROG”或 `0B05:1ABE/1B4C` 选择 RC71/RC72/RC73 的矩阵。
- Lenovo `LegionController` 的报告字段、左右控制器与 controller instance identity 在 YMCC 没有完整采集/回读链；不能把 `specialControllerProtocols.ts` 的分类当作已支持。
- GamepadMotion 三个未加载 API 的产品必要性尚未由当前 UI/Host contract 关闭；不能把“API 存在”推断为必须立即接线。

### 当前保留的原创/项目安全逻辑

`pairProven=false`、`safe-zero`、direct IMU no-report、未绑定 ROG matrix、`provider-pretransformed` 与 Windows matrix 分支分离，属于项目的 fail-closed 安全合同，不应被错误标为 HC 漂移。它们比 HC 原始调用面更严格，但不能据此声称 runtime 已闭环。

### 本轮 BUS 增量：合同、时间与覆盖缺口

以下项目已与当前 source 逐项对账，均属于“证据/合同缺口”，不是可按 HC 数学偏移直接修改的项：

1. native 在 Host 返回 `frame-accepted` 后无条件设置 `g_inputHostActive=true`，没有检查同一 tuple 的 `firstFrame=true`；`HOST-ACTIVE-INVARIANT` 当前为 `UNENFORCED / SEMANTIC-AMBIGUITY`。
2. `SamplePairProof` 要求 gyro/accel sequence 都等于单一 `sampleSequence`，而 native 两个 callback 维护独立 hardware sequence；这可能过度收紧正常 Coordinator 配对，记为 `PAIR-PROOF-DESIGN-GAP`，不得伪造相同序号。
3. `motionSample.ts` 的 `sampleHash`、`configHash` 目前只检查 `sha256:<64hex>` 格式，没有按字段重算并绑定 source；记为 `PROVENANCE-GAP`。
4. `motionSample.ts` 对 UTC/gyro/accel timestamp 只做非空检查，没有解析、时钟域、sequence 或 proof 绑定；记为 `TIMESTAMP-PROVENANCE-GAP`。
5. native telemetry 发送 `gyro/playerSpace/worldSpace/accel/hostFrame`，未发送 UI contract 要求的嵌套 `gamepadMotionPlane`；当前 preview 的零值仍是 `pair-unproven-safe-zero`，不能误报为已闭合的真实链路。
6. HC `MotionManager` 有 `MotionMapped`/`ActionType.Disabled` 清零门，YMCC fixture mapper 尚未表达 action-disabled/layout mapping 输入；记为 `HC-ACTION-ADMISSION-GAP`，不能在 fixture 通过时推导 runtime。
7. HC `SensorsManager` 的 Windows/SerialUSBIMU/Controller `SensorFamily` 选择尚未形成 YMCC producer/identity/transform/receipt 全链；记为 `HC-PROVIDER-COVERAGE-GAP`。
8. `assembleCanonicalFrame()` 只检查 generation/epoch 等身份，不检查 input/sample sequence、clock domain、age/skew 或已接收序列；记为 `FRAME-TEMPORAL-ADMISSION-UNENCLOSED`。
9. native worker 在 `g_inputCaptureReady=false` 时，异常路径可能由 `inputCaptureShutdown()` 提前返回并跳过 emergency cleanup；目前未复现，记为 `LATENT-LIFECYCLE-CLEANUP-GAP`。
10. 旧 RF00 71-key 集合没有覆盖当前 `src/bridge/motionSample.ts`、`rogGyroProtocol.ts`、`specialControllerProtocols.ts`；因此覆盖范围本身为 `INCOMPLETE / REFREEZE-REQUIRED`，不能把 71-key hash 当作全量 gyro source 证明。

上述项目不授权打开 `pairProven`、补写 ROG matrix、接入未证明的 GamepadMotion plane、伪造 battery/hash/sequence 或把 fixture 提升为 runtime。若后续要修复其中任一项，必须先在 TASK.md 明确合同版本和可归属 HC 证据，再单独改动并重新 RF00。

### 没有执行的源码修正

本轮没有修改 `native/main.cpp`、`InputHost/Program.cs`、前端桥接、JSON、HIDMaestro、HidHide、正式包或系统状态；只修正了 net10/native-aware 探针工具链。没有新建 DGF；`runtimeUpgrade=false`。

## 5. 后续门与用户介入

静态审计目前已经推进到“不靠猜测不能再闭合”的边界。需要用户介入的下一项是运行时证据，而不是继续扩大词汇：

1. 若要关闭 ROG/Lenovo/MSI provider 缺口，需要在对应设备上回传同一生命周期的设备身份（DMI/SKU、VID/PID、container/instance ID）、传感器/控制器 report identity、raw report/sequence/timestamp、矩阵 receipt 和独立 decoder/readback。
2. 若要关闭 GamepadMotion reachability 缺口，需要明确 UI 要求 `Gravity`、`ProcessedAcceleration` 或 `Orientation` 哪一个进入产品合同；在没有该授权前，保持未加载与 safe-stop。
3. RF00 producer 已补入 `motionSample.ts`、`rogGyroProtocol.ts`、`specialControllerProtocols.ts`；重冻后使用新的 74-key manifest，不能沿用此前 71-key 或 P64 snapshot 的 hash。

## 6. 结论

```text
HC vocabulary/source comparison       = EXPANDED / EVIDENCE-ALIGNED
ROG DMI matrix distinction            = CONFIRMED; PID-only admission remains NONE
Lenovo controller IMU route           = HC FACT / YMCC TRANSPORT GAP
MSI Claw controller IMU route         = HC matrix exists; controller motion not evidenced
GamepadMotion API reachability        = PARTIAL; 3 APIs not loaded, no current runtime error proven
Current YMCC HC drift                 = no new unambiguous direct fix found
Evidence / inference / unknown split  = RECORDED
runtimeUpgrade                        = false
runtime/device/system mutation        = none
next governance gate                  = T22-RF00 → T20 → T21 → T14
```
