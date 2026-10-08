# T22-BUS-P78：HC 校准会话与特殊手柄生命周期三方对账

日期：2026-09-05

状态：`HC-SOURCE-BOUND / CALIBRATION-SESSION-GAP-EXPLICIT / SPECIAL-PROTOCOL-TRANSPORT-UNENCLOSED / NO-RUNTIME-CLOSURE`

本页是 BUS 对锁定 HC source、当前 YMCC source/native 与 `Docs/Tasks/GyroVirtual/TASK.md` 的增量只读对账。扩大检索词覆盖：

```text
Calibrate / ResetContinuousCalibration / StartContinuousCalibration /
PauseContinuousCalibration / SetCalibrationMode / CalibrationMode /
Stillness / SensorFusion / Manual / confidence / GetAutoCalibrationConfidence /
GetAutoCalibrationIsSteady / GetCalibrationOffset / SetCalibrationOffset /
IMUCalibration / calibration.json / deviceInstanceId / StoreCalibration /
SensorFamily / Windows / Controller / SerialUSBIMU / UpdateSensor /
StartListening / StopListening / ReadingChanged / GetCurrentReading /
GamepadMode / SwitchMode / ReadGamepadMode / GamepadModeAck /
SetMotionStatus / MotionDataAck / CalibrationControl / CalibrationAck /
MS-1T41 / MS-1T42 / MS-1T52 / MS-1T8K / MS-1T91 /
LegionGoTablet / LegionGoSZ1 / DInput / XInput / HIDAPI / report decoder /
ROGAlly / XboxROGAlly / DMI / SMBIOS / ProductName / SystemModel /
epoch / generation / first reading / stale cache / provider / matrix identity
```

本页不启动 YeManCC、InputHost、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实设备；不执行构建、运行时测试或 OEM HID/WMI 写入；不修改 T10/native/source JSON。本页只写入证据页和 TASK.md 汇总。

## 1. 证据来源与当前快照边界

### 1.1 锁定 HC source

本轮使用主线随附的锁定 HC source tree，而不是仅凭 DLL 名称推断：

```text
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\deps\handheldcompanion-runtime\source
```

关键 source 文件的只读摘要：

| 文件 | bytes | SHA-256（本轮读取） | 关键范围 |
| --- | ---: | --- | --- |
| `Helpers/GamepadMotion.cs` | 12,816 | `7BA36F0DCB0E280D526E5B09E95888AF58B98927B3C83001858817EC12FB23ED` | 构造、ProcessMotion、校准 API |
| `Sensors/IMUCalibration.cs` | 3,395 | `320BC0E671B4B77D39F3471D98B9CC684F92F4B784B119FDD416C42F4302998B` | calibration.json、identity、Store/Get |
| `Sensors/IMUSensor.cs` | source-located | source tree | reading/cache、Start/StopListening |
| `Managers/SensorsManager.cs` | 16,413 | `660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3` | Suspend/Resume、UpdateReport、Calibrate |
| `Devices/MSI/ClawA1M.cs` | 41,825 | `FA7E4A2C2F6B56BA019689361769127875BE4DE0985351001839A48614D08748` | Claw mode、HID/WMI、motion command |
| `Controllers/MSI/DClawController.cs` | 8,942 | `FE9F0909538E2AF4AD3B2E67CE6FBBBE1CD3D46D8C1273D9E9F211B8FB812035` | DirectInput 专用消费面 |
| `Devices/Lenovo/LegionGoTablet.cs` | 14,058 | `A05910FDDD410A68E00F028B3760C784A6AA5A8BC0FCA27BEE96ED20D59F3414` | VID/PID、matrix、gyro enable |
| `Devices/Lenovo/LegionGoSZ1.cs` | 8,890 | `176C4AEE0153AAA9956837B95B044E493C90AE0C5CD86EAAA1AC2754CA54CF66` | SZ1 VID/PID、matrix |
| `Devices/ASUS/ROGAlly.cs` | 25,435 | `04097428F3D62DE75F8160EB47398E1C61606F9D6681EB605D100B384814AA00` | ROG PID、base matrix、HID readback |

当前 YMCC 对照面：

| 文件 | bytes | SHA-256（本轮读取） | 作用 |
| --- | ---: | --- | --- |
| `native/main.cpp` | 1,081,061 | `0246970E4EFFFA2F1DD0C8583987E25A7E758A3538ABE96A9015DD6E672828D3` | WinRT capture / GamepadMotion future admission |
| `src/bridge/specialControllerProtocols.ts` | 2,736 | `145CC60A6BCA581300E2CCC9FA52FD88664A6EF5CD8AE90073FF88F8F709014D` | MSI/Lenovo 静态分类 |
| `src/bridge/gyroCalibrationSession.ts` | 11,630 | `43DE784AA3E0B1A0076831902096EE50B44EB02D3AEA1A4F48B19FF82FBF9641` | UI/合同层校准会话模型 |

这些 hash 是本轮文件读取证据，不是新的 RF00 manifest。由于 TASK、P77、P78 和 native 仍属于 current-source key surface，后续必须重新执行：

```text
T22-RF00 → T20 → T21 → T14
```

## 2. HC 校准会话：源码事实

### 2.1 GamepadMotion 的校准 API 与 identity

锁定 `Helpers/GamepadMotion.cs`：

```text
53-72   GamepadMotion(string deviceInstanceId, CalibrationMode ...)
60-67   按 deviceInstanceId 读取既有 IMUCalibration；存在时 SetCalibrationOffset + SetCalibrationMode
79-83   Reset() 只重置 native GamepadMotion 与 Madgwick overlay
91-133  ProcessMotion 写入 raw gyro/accel，接收 delta；小于 0.00001 秒保留上一 delta
194-210 Start/Pause/ResetContinuousCalibration wrapper
212-226 Get/SetCalibrationOffset，Set 同步更新本地 calibration
229+    GetAutoCalibrationConfidence / GetAutoCalibrationIsSteady wrapper
```

这里的 `deviceInstanceId` 不是 UI label，也不是 VID/PID 的简写；它是 HC 用于恢复校准的 key。`IMUCalibration` 进一步在 `Sensors/IMUCalibration.cs` 固定：

```text
11      calibration.json 位于 App.SettingsPath
61-74   Dictionary<string, IMUCalibration> 序列化/反序列化
76-90   HasCalibration/GetCalibration(path)
93-104 StoreCalibration(path, calibration)，path 转大写后写回 calibration.json
```

结论：HC 的校准不是仅在当前 motion handle 内存里暂存；它包含“物理设备实例 identity → offset/weight/threshold”的持久化路径。

### 2.2 HC 显式校准流程

锁定 `Managers/SensorsManager.cs:340-406`：

```text
340-345  Calibrate(GamepadMotion) 进入可见校准会话
347-355  显示不可关闭的校准对话框
361-365  4 秒倒计时，要求设备稳定、接近水平放置
367-372  ResetContinuousCalibration()
          SetCalibrationMode(Stillness | SensorFusion)
374-385  最长 5 秒轮询 confidence，confidence == 1 时提前结束
387-390  GetCalibrationOffset()，以 confidence * 10 计算 weight 并 SetCalibrationOffset
391-392  IMUCalibration.StoreCalibration(deviceInstanceId, GetCalibration())
394-395  SetCalibrationMode(Manual)
400-401  成功提示后停留 2 秒
```

这是一个“用户显式开始 → 复位连续校准 → Stillness|SensorFusion → confidence → offset/weight 持久化 → Manual”的会话，而不是 DLL 加载即自动锁定。

### 2.3 HC provider 与 report interval 的边界

锁定 `SensorsManager.UpdateReport()` 与 `SetSensorFamily()`：

```text
318-329  GetCurrentReading() 读取 gyro/accel，写 ControllerState.GyroState，随后同一个 UpdateReport 调用 GamepadMotion.ProcessMotion
331-338  TimerManager.GetPeriod() 作为 IMUGyrometer/IMUAccelerometer 的 updateInterval
```

`SensorFamily` 仍分为 Windows、Controller、SerialUSBIMU 等选择面；当前 source 没有把“WinRT 默认传感器”和“手柄 motion provider”证明为同一物理 provider。report interval 是采样/传感器更新请求，不等于 consumer receipt，也不能替代 provider identity、same-frame pair 或 epoch proof。

### 2.4 HC Resume / stale reading 边界

锁定 `SensorsManager.cs:140-170` 与 `IMUSensor.cs:28-73`：

```text
Suspend -> StopListening()
Resume  -> UpdateSensor()
IMUSensor.StopListening() 解绑由子类实现，但基类 reading、timestamp、readingAxis 没有统一清零
```

因此“恢复后首个新事件前可能保留旧 reading/timestamp”是 HC source latent lifecycle gap；它不能被写成 HC 正确性合同，也不能直接解释 YMCC 设备静态乱动，直到有同一 epoch 的 runtime trace。

## 3. 当前 YMCC 校准路径：三方差异

### 3.1 native capture

锁定当前 `native/main.cpp`：

```text
5008-5040  加载 GamepadMotion.dll 和校准函数地址；加载阶段明确不自动开始 HC 校准会话
6228-6250  capture init 清空 jsonl、ResetSamples、启动 WinRT sensors；不清零 g_gmLastTick
6310-6327  pairProven 固定 false；没有 provider-capability pair 时 GamepadMotion/virtual gyro safe-zero
6327-6333  未来 pairProven=true 才会用 GetTickCount64 delta 调 ProcessMotion
6339-6365  未来路径读取 offset/confidence/steady；只有 confidence==1、steady、finite offset、weight>0 才 SetOffset + Manual
```

`g_gmStartCal`、`g_gmPauseCal`、`g_gmResetCal` 在加载时取得函数地址，但当前 native path 没有调用 HC `ResetContinuousCalibration()`，也没有调用 `SetCalibrationMode(Stillness | SensorFusion)` 作为显式校准会话起点。native 的 `g_gmCalStart` 是未来 capture sample 路径的自动检查起点，不是 HC 的用户校准事务。

### 3.2 bridge 合同层

`src/bridge/gyroCalibrationSession.ts` 已表达 `ResetContinuousCalibration + Stillness|SensorFusion` 的合同文字和安全门；这属于 UI/合同层静态描述，不证明 native 已执行 HC 会话，也不证明 `deviceInstanceId` 已绑定并写入 HC `calibration.json`。

### 3.3 三方裁决

| 项目 | HC source | YMCC current source | 裁决 |
| --- | --- | --- | --- |
| 校准开始 | 用户显式 Calibrate 会话 | DLL load 不开始；future sample 自动尝试 | `HC-DEVIATION / ORIGINAL SESSION LOGIC` |
| 连续校准复位 | `ResetContinuousCalibration()` | 函数已加载但当前未调用 | `CALIBRATION GAP` |
| 校准模式 | `Stillness \| SensorFusion` | 当前未从 native 进入该模式 | `CALIBRATION GAP` |
| 成功判据 | confidence==1；5 秒超时后仍继续 offset/manual | confidence==1 + steady + finite offset + weight>0 | `SAFETY DELTA / NOT HC-EQUIVALENT` |
| 持久化 | `deviceInstanceId` → `calibration.json` | native 没有绑定/写入 HC calibration store 的证据 | `PROVENANCE GAP` |
| provider/epoch | SensorFamily + controller/instance 上下文 | WinRT gyro/accel 分别 callback，pairProven=false | `UNENCLOSED` |
| 当前输出 | HC source 可进入 ProcessMotion | YMCC 当前被 pairProven=false 阻断 | `RUNTIME-BLOCKED` |

这里的 `confidence==1 + steady + finite + weight` 更严格可能是安全设计，但不能被称为“完全按 HC 已实现”。它是 YMCC 的原创安全收紧逻辑；未授权本轮直接改 T10/native。

## 4. MSI Claw：扩大协议词汇后的 HC 对账

### 4.1 HC model、VID/PID 与 mode

锁定 `Devices/MSI/ClawA1M.cs`：

```text
44-53    GamepadMode = Offline/XInput/DirectInput/MSI/Desktop/BIOS/TESTING
61-88    SwitchMode、ReadGamepadMode、GamepadModeAck、SetMotionStatus、MotionDataAck、CalibrationControl/Ack
127-135  PID_XINPUT=0x1901、PID_DINPUT=0x1902、PID_TESTING=0x1903
149-166  MS-1T41、MS-1T42/MS-1T52、MS-1T8K、MS-1T91 固件/设备版本表
172-203  VID=0x0DB0、PID 集合、HID usage filter、GyroMatrix/AcceleroMatrix
454       Close 时 SwitchMode(GamepadMode.XInput)
571-613  SetMotionStatus、SwitchMode、恢复 XInput/切换 DirectInput/Desktop
```

HC 不把 MSI Claw 当作“一个普通 Xbox 360 设备”：其 mode、HID usage、WMI/ACPI 事件、motion status 和退出恢复均有专用路径。`DClawController` 与 `XClawController` 还是两个不同 controller consumer；DInput 侧存在特殊按钮/轴/震动语义。

### 4.2 当前 YMCC

`src/bridge/specialControllerProtocols.ts:1-55` 当前只有：

```text
VID/PID → 'msi-claw' / 'lenovo-legion' / 'generic-xinput'
MSI modeValues 数字枚举
requiresSpecialSignalPath 分类标记
```

只读 `rg` 未找到该分类被生产 input、HIDAPI、motion、InputHost 或 Coordinator 消费；唯一调用面是 `tools/special_controller_protocols_selftest.ts`。当前没有 MSI HID writer、mode switch、readback/ack、DInput decoder、motion report reader 或 rumble route。

### 4.3 MSI 裁决

```text
MSI VID/PID 分类                         = STATIC-ALIGNED / CLASSIFICATION-ONLY
MSI DMI/firmware model selection         = SOURCE-LOCATED / YMCC ABSENT
MSI XInput/DirectInput mode lifecycle    = UNENCLOSED
MSI SetMotionStatus/report decoder       = ABSENT
MSI DInput button/axis/rumble semantics  = ABSENT
MSI close -> XInput restore              = ABSENT / UNENCLOSED
新的 YMCC MSI 数学漂移                    = NOT FOUND
```

不得仅凭 `modeValues` 将 MSI 当作已经支持；也不得在没有 report descriptor、feature/control ACK、退出恢复回执的情况下接入 OEM HID/WMI 写入。

## 5. Lenovo / ROG 复核：无新增数学偏差

### 5.1 Lenovo

HC `LegionGoTablet.cs` 仍显示：

```text
17EF:6182/6183/6184/6185 与 17EF:61EB/61EC/61ED/61EE
XInput / DInput / dual-DInput / FPS 角色分离
Tablet GyroMatrix / AcceleroMatrix 与 GamepadMotion threshold
```

HC `LegionGoSZ1.cs` 仍显示：

```text
1A86:E310 = XInput
1A86:E311 = DInput
独立 SZ1 matrix
```

当前 YMCC 仅保留同 VID/PID 分类；没有 production HIDAPI reader、report decoder、DMI/model→matrix 绑定或 OEM attach/write。这个结论已经由 P55/P65 固化，本页未发现新的 axis/sign/unit/gain/deadzone 偏差。

### 5.2 ROG

HC `ROGAlly.cs` 仍显示 VID `0x0B05`、PID `0x1ABE/0x1B4C` 与 base Gyro/Accel matrix；`XboxROGAlly.cs`、`XboxROGAllyX.cs` 具有不同的 subclass matrix。ROG PID 只能缩小候选设备族，不能替代 DMI/SMBIOS 型号选择。当前 YMCC 仍保持 raw/fail-closed 与既有 P54/P65/P75 约束；本轮没有发现新的 ROG 轴线、符号、单位、增益或 deadzone 偏差。

## 6. 证据、原创、未知与缺口分离

### 证据 / FACT

- HC 显式校准包含倒计时、`ResetContinuousCalibration()`、`Stillness|SensorFusion`、confidence、offset/weight 持久化与 Manual 恢复。
- HC 校准 key 是 `deviceInstanceId`，并写入 `calibration.json`。
- HC MSI Claw 具有独立 mode、HID/WMI、motion command、DInput/XInput consumer 与关闭恢复 XInput。
- 当前 YMCC MSI/Lenovo 只存在静态 VID/PID 分类；没有生产 transport/decoder/readback。
- 当前 native 的 `pairProven=false` 仍阻断 GamepadMotion 和虚拟 gyro output。

### 明确偏差 / ORIGINAL LOGIC

- YMCC native future calibration 是 sample-path 自动尝试，不是 HC 显式校准会话。
- YMCC future admission 的 `confidence==1 && steady && finite offset && weight>0` 是比 HC 更严格的原创 safety delta，不能写成 HC parity。
- YMCC `GetTickCount64` 首帧/CLAMP 时序偏差沿用 P77，本页不重复计为新偏差。

### 未知 / UNKNOWN

- `pairProven` 打开后，WinRT gyro/accel 是否与当前手柄/同一 provider 对齐。
- HC resume 后旧 reading 是否在实际运行时被消费，以及是否造成用户报告的静态乱动。
- ROG/Lenovo/MSI 真实 report、consumer、Host owner/recovery 是否闭合。
- PS4 电量 5% 的真实来源；本页不猜测。

### 缺口 / GAP

```text
T11 calibration session -> native invocation       = UNENCLOSED
deviceInstanceId -> calibration store             = PROVENANCE GAP
same-provider pair / epoch / first-reading         = UNENCLOSED
MSI mode switch + feature/control ACK + restore    = UNENCLOSED
MSI DInput report/rumble/motion decoder            = ABSENT
Lenovo HIDAPI/report/model matrix                   = UNENCLOSED
ROG DMI -> native matrix receipt                    = UNENCLOSED
Host/HID/Steam/game consumer + recovery             = RUNTIME-BLOCKED
```

## 7. 结果与后续门

```text
HC calibration session parity                       = UNENCLOSED / T10-OWNER-BOUND
YMCC calibration safety gate                        = ORIGINAL SAFETY DELTA / NOT HC-EQUIVALENT
MSI Claw protocol parity                            = CLASSIFICATION-ONLY / TRANSPORT-UNENCLOSED
Lenovo HC vocabulary                                = STATIC-VERIFIED / existing gaps unchanged
ROG matrix math                                    = no new drift found / identity receipt unclosed
new axis/sign/unit/gain/deadzone drift               = NOT FOUND
new autonomous drift algorithm                      = NOT FOUND
runtimeUpgrade                                     = false
```

本页不创建 DGF，不打开 `pairProven`，不改 T10/native，不接入 MSI OEM writer，不把 HC latent stale-cache 风险写成已复现产品错误。下一步仍是：

```text
T22-RF00 → T20 → T21 → T14 → static regression
```

真正关闭校准与特殊手柄 transport 仍需要用户介入同一 Host 生命周期的 provider/epoch/pair/calibration、MSI/Lenovo/ROG descriptor/report/readback、Host/owner/release 与独立 consumer 证据；在此之前保持 `T11–T13/T15–T18 = UNENCLOSED / RUNTIME-BLOCKED`。
