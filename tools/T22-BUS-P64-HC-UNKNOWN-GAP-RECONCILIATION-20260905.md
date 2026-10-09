# T22-BUS-P64：HC / YMCC / HIDMaestro 未知、缺口与歧义扩展对账（2026-09-05）

Status: `STATIC-REAUDIT / EVIDENCE-UNKNOWN-SEPARATED / NO-UNAMBIGUOUS-HC-SOURCE-FIX / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC、当前 YMCC source、锁定 HIDMaestro 证据和既有任务书的补充静态对账。范围扩展到：DS4/PS4 电池字段、IMU sample provenance、HC sensor lifecycle、ROG matrix、UI 真实遥测与本地 fixture 预览、Host-local receipt 的边界。

本轮没有启动 YeManCC、InputHost、HIDMaestro、HidHide、Steam、游戏或虚拟设备；没有访问实体手柄、PnP、系统电源或驱动；没有修改源码、JSON、配置或正式包。因此本页不构成 runtime closure，也不解释操作者看到的 Steam UI 现象为某个源码字段。

## 1. Authority 与当前 source 身份

### 1.1 读取对象

| 层 | authority / path | 用途 |
|---|---|---|
| HC | `Reference/HandheldCompanion/Frozen-HC-BINARY-BATCH12-20260831` | DS4 VIIPER、DSU battery、GamepadMotion、Windows sensor、ROG matrix |
| HIDMaestro | `InputHost/bin/Release/net10.0-windows/HIDMaestro.Core.dll`，锁定 `1.7.0.0` | DS4 v2 profile/extended encoder 的静态字段链 |
| YMCC native | `native/main.cpp` | WinRT sensor callback、telemetry、GamepadMotion safe gate、Host receipt |
| YMCC Host | `InputHost/Program.cs` | `HMGamepadState` 构造与 SubmitState |
| YMCC UI | `src/App.vue`、`src/views/GyroMotionView.vue`、`src/bridge/inputContracts.ts`、`src/bridge/gyroMotionMapperMock.ts` | telemetry 接收、真实/fixture 预览和准入门 |

### 1.2 本次读 hash（用于复核，不是新的 RF00 manifest）

```text
HC Targets/DualShock4Target.cs  30A1E2704B00017FD18238546BCBE798D916FC17316FC19B775B4EF131927337  7508 bytes
HC Misc/DS4OutDevice.cs         2D2E400770923564589D9AFCD40A4C459F2803C41665055B5F2F875F29AE97D8  2037 bytes
HC DSU/DSUServer.cs              0826FB21A4E9FAC7524050ED66898CBE23E0CB4BB12F513ED8D7AE720446834C  28965 bytes
HC Helpers/GamepadMotion.cs      7BA36F0DCB0E280D526E5B09E95888AF58B98927B3C83001858817EC12FB23ED  12816 bytes
HC Managers/SensorsManager.cs   660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3  16413 bytes
HC Managers/MotionManager.cs    19A9C645351DC74B5A0E2A74D99C3AD95F105CF85EDF173C8AF8531713F6FC7E  15971 bytes
YMCC InputHost/Program.cs       F017F534A75424227297C8D0B618E7EC72E9A40985D79C42AFC2EE8CD1DAB59C  38365 bytes
YMCC native/main.cpp            0246970E4EFFFA2F1DD0C8583987E25A7E758A3538ABE96A9015DD6E672828D3  1081061 bytes
YMCC GyroMotionView.vue         B58127DC85EF749F417854C478ECED5D21FF3BD756D20B46B9AE3FC21E46D1FE  24906 bytes
YMCC inputContracts.ts          697600B4668CED2B67FCB9306189CB36E2FBDC6D11BFEF6D08770224CD085F02  14418 bytes
YMCC gyroMotionMapperMock.ts    E2F25BC842C967E2B6C875F65ECC47468B7215B39629B4403FD5D64226F3FC68  7913 bytes
YMCC virtualReportAssembler.ts  B6FF222B8267F2979B6DF1745EE6C880086DE71D13DD5377E3B40A0803744EA6  5446 bytes
```

`native/main.cpp`、`InputHost/Program.cs`、UI/contract 文件和本页均不属于此前 71-key RF00 快照的同一工作树状态；此前 manifest 只能继续作为历史 static snapshot。要恢复 current-source 身份，仍必须按 `T22-RF00 → T20 → T21 → T14` 重冻；本页不伪造新的 digest。

## 2. DS4 / PS4 电池字段：事实、未知与结论

### 2.1 HC 事实

1. `Targets/DualShock4Target.cs:14,24–128` 的 VIIPER DS4 路径固定构造 **31-byte** input report。`BuildReport()` 清空 buffer 后写入摇杆、按钮、触摸、gyro `data[19..24]` 和 accel `data[25..30]`；没有 `Battery`、`Charge`、`bBatteryLvl` 或百分比写入。
2. `Misc/DS4OutDevice.cs:18–43` 定义了另一个 **63-byte `DS4_REPORT_EX`**，其中 `bBatteryLvl` 位于 offset 11，`bBatteryLvlSpecial` 位于 offset 29；但静态调用图搜索只找到该文件自身的 union/`CopyBytes` 定义，没有 `new DS4_REPORT_EX`、字段赋值或 `CopyBytes` 的其它调用。该结构目前是 **dead/unreferenced report definition**，不能当作 VIIPER DS4 当前输出事实。
3. `DSU/DSUServer.cs:44–50,119,293–327,649–674,768` 的 `DsBattery` 属于 DSU pad metadata：启动默认 `Full`，随后约每秒读取 `SystemInformation.PowerStatus.BatteryChargeStatus`，映射为 `Charging/None/High/Low/Dying/Medium` 并写入 DSU PortInfo/PadData。它是 DSU 网络元数据，不是 VIIPER 31-byte DS4 report，也不是 HIDMaestro profile 的电量字段。

### 2.2 YMCC / HIDMaestro 事实

1. `InputHost/Program.cs:471–478,500–506` 的 `HMGamepadState` 只赋 `Buttons`、`Hat`、标准 `Axes`；没有 battery/charge 字段赋值。
2. YMCC `native/main.cpp:2023–2037,2748–2771` 的 `nativeBatteryStatus()` 只为 YMCC Top Monitor 生成主机 AC/系统电池字段；没有把 `batteryPercent` 送入 `HMGamepadState`、HIDMaestro controller 或 DS4 report。
3. 锁定静态证据 [E02](T22-BUS-P12-E02-HIDMAESTRO-EMBEDDED-PROFILE-STATIC-EVIDENCE-20260905.json) 与 [E03](T22-BUS-P12-E03-HIDMAESTRO-DS4-ENCODER-STATIC-EVIDENCE-20260905.json) 证明 `dualshock-4-v2` extended encoder 消费六个 `int16` motion 字段（`gyroPitch/Yaw/Roll`、`accelX/Y/Z`）；当前证据没有 DS4 battery/charge semantic、`HMGamepadState` battery field 或默认 5% 赋值。

### 2.3 对“Steam 显示 PS4 5%”的裁决

```text
HC VIIPER DS4 主动写 5%        = 未发现证据
HC DS4_REPORT_EX 电池结构       = 结构存在，但调用/赋值未闭合且不在当前 VIIPER 路径
HC DSU BatteryStatus            = 存在；只属于 DSU metadata
YMCC Host 主动写电池            = 未发现
YMCC Top Monitor system battery = 存在；与 PS4 HID 电量平面分离
HIDMaestro DS4 profile 默认 5%  = 当前静态证据未证明
Steam/OS/旧虚拟设备/缓存来源    = UNKNOWN
```

因此不能把 5% 改成 100%，也不能把它归因于 HC/YMCC。要关闭该未知，必须在同一 Host/设备生命周期内捕获虚拟 DS4 的原始 HID input/feature report，并独立按 `dualshock-4-v2` descriptor 解码；仅凭 Steam UI 数字不能证明字段来源。该运行时观察需要用户介入，当前不自行启动。

## 3. HC motion pipeline 与当前 YMCC 的三方对账

| 对账面 | HC source | 当前 YMCC source | 裁决 |
|---|---|---|---|
| Windows sensor 选择 | `IMUGyrometer/IMUAccelerometer.GetDefault()`，按 `ReportInterval = max(MinimumReportInterval, TimerManager.GetPeriod())`，订阅 `ReadingChanged` | WinRT `GetDefault()`、`max(MinimumReportInterval, 8ms)`、独立 gyro/accel callback sink | selection/lifecycle 方向对齐；8ms 是锁定默认假设，不是 runtime HC 配置证明 |
| raw sample 进入 GM | `SensorsManager.UpdateReport()` 分别取两路最新缓存，再同一 controller tick 调 `GamepadMotion.ProcessMotion(raw gyro, raw accel, delta)` | native 分别记录两路最新缓存，但 `pairProven=false` 固定，不能调用 `ProcessMotion` 或发布 motion output | YMCC 更严格 fail-closed；不是 HC 轴/倍率偏移 |
| gyro threshold | `IMUGyrometer` 在设备 matrix 前对 raw axis 按 `GetGyroThreshold()` 置零；阈值来自 calibration/device | native 使用固定 `abs(v) >= 2000 → 0`，并已记录没有 HC-equivalent per-device calibration key | `HC-THRESHOLD-IDENTITY-GAP / UNENCLOSED`；不能直接把固定值宣称完整 parity |
| ROG matrix | `Devices/ASUS/XboxROGAllyX.cs`：gyro `(1,1,-1), Y↔Z`；accel `(-1,-1,1), Y↔Z`；class 由 SMBIOS/DMI device identity 选择 | native 仅识别 ROG HID PID family，`matrixIdentity=unresolved-rog-pid-family/raw`，不猜测套矩阵 | identity proof 未闭合；当前 raw 保留正确 |
| HC planes | `SetupMotion` 同时写 GamepadMotion（calibrated/gravity）、DSU（raw）和 Default（profile multiplier 后）plane | native 只保留 raw/calibration diagnostic 字段；`gamepadMotionPlane` 未准入，UI contract 会丢弃未经证明的 plane | plane ownership/receipt 未闭合；不能让图动而绕过 gate |
| Local/Player/World/Joystick | HC `MotionManager` 使用 Default、`GetPlayerSpaceGyro(...,1.41)`、`GetWorldSpaceGyro(...,0.125)`、`Inclination` | UI fixture 只有收到已准入 `gamepadMotionPlane` 才调用 `mapGyroSample`; native 当前返回零且不提交 gyro | production route 未闭合；不是“少一个倍率”的局部错误 |
| DS4 wire | HC VIIPER 31-byte：raw gyro `clamp ±2048 * 16`，accel `clamp(g*9.81, ±64) * 512` | HIDMaestro DS4 v2 是不同 64-byte extended report；E03 仅证明六个 `int16` 字段链，当前 RF00 `InputHost` 不写 float `GyroDpsX/Y/Z`，也不写六个 int16；旧“只写 float GyroDps”句子属于 stale historical source scope | descriptor-specific transport gap；禁止复用 HC offsets |

## 4. 新发现的 provenance / UI 链路缺口

### 4.1 采样序号与时间戳的语义不一致（GAP / UNKNOWN）

- native `InputCaptureSensorSink` 分别维护 `gyroSequence`、`accelSequence`、两个 FILETIME timestamp 和两个 receipt tick（`native/main.cpp:4820–4851`）。这四组值没有同帧关系、最大允许 skew、source generation 或 provider identity。
- 写入 jsonl 的顶层 `sample.sequence` 是 capture loop 的 `GetTickCount64()`，不是 gyro/accel 任一硬件 sequence（`native/main.cpp:6442`）；UI 也把 `detail.sequence` 作为 mapper sample sequence。
- UI 顶层 `timestampUtc` 取 `max(gyroTimestamp100ns, accelTimestamp100ns)`（`native/main.cpp:6436–6439`），因此一条 telemetry 可能以较新的 gyro 时间戳标记一条较旧 accel 值；没有 `timestampSkew` 或 stale-side rejection。

这不证明当前产生了错误输出，因为 `pairProven=false` 仍阻止 GamepadMotion/virtual-stick；但如果未来只打开 pair gate 而不补齐 source/epoch/skew proof，就会把“循环时间序号”误当成硬件帧序号。该项列为 `T11/T12 provenance gap`，不能通过 UI fixture 修正。

### 4.2 sensor rebind 后旧值清除只在部分路径成立（LATENT GAP）

- HC `StopListening()` 取消两路事件并清空 sensor references；切换 sensor family 时再创建新对象。
- YMCC `inputCaptureSuspendSensorsForPower()` / resume 路径会 `ResetSamples()`，初始 `inputCaptureInit()` 也会 reset；但 `inputCaptureStartWinRtSensors()` 自身先 stop 再重新订阅，未在函数内部强制 reset sink 或提升 sensor epoch（`native/main.cpp:4863–4930`）。当前 source 没有其它生产 rebind caller，因此尚未证明这是现行运行时缺陷；它是未来设备重绑/恢复时的 stale-sample 风险。
- 关闭路径最终 `inputCaptureShutdown()` 会 stop、reset sink/GM，因此正常 shutdown 不保留旧值。

裁决：`KNOWN-CODE-SHAPE / UNTESTED-REBINDS / no direct HC fix yet`。若后续添加 sensor restart、PnP 或 provider switch，必须在重新订阅前清 sample、递增 epoch，并把 epoch 写入 telemetry；否则不得宣称恢复闭口。

### 4.3 UI 的 fixture 预览不是实时输出缺失

- `GyroMotionView.vue:109–155` 先接收 native `gyro.telemetry`，再仅当持久化 config 已启用、右摇杆、无未解析 trigger 且 `gamepadMotionPlane` 通过严格 contract 时，调用 `mapGyroSample()` 和 `assembleCanonicalFrame()`。
- `inputContracts.ts:108–133` 要求 `processProven=true`、`pairProven=true`、`calibrationLocked=true`、有效 Default gyro/accel、steering axis 和 timestamp；不满足即不构造 GamepadMotion plane。
- 因此当前 ROG 实机可以有 raw gyro/accel chips，但 virtual-stick preview/host dot 保持零/非 active；这正是 `pair-unproven-safe-zero` 合同，不是页面漏接 raw data。`gyroMotionMapperMock.ts` 的命名表示它是前端 fixture stage；它没有写 HID/Host，也不能成为 runtime evidence。

## 5. 错误、未知、HC 偏离与原创逻辑

### 已证实错误 / 风险

1. **Current-source provenance stale：**此前 RF00 71-key manifest 不能覆盖本次读取到的 native/UI/Host 工作树；本页新增后也必须重新 RF00。不能把旧 manifest 的 source hash 当作当前值。
2. **Input provenance 不闭合：**capture loop sequence、gyro/accel hardware sequence、FILETIME 和 provider/epoch 尚未形成同一 proof。
3. **DS4 battery origin 未闭合：**Steam 5% 没有源码归因证据；需要外部 HID report observation。

### 已知未知 / 缺口

```text
HC per-device gyro threshold/calibration identity       = UNENCLOSED
ROG SMBIOS/DMI identity -> matrix receipt               = UNENCLOSED
same-provider/same-generation gyro+accel proof          = UNENCLOSED
sample timestamp skew/age rejection                     = UNENCLOSED
sensor rebind epoch + stale sample invalidation         = LATENT-GAP / UNTESTED
GamepadMotion Default/Player/World production plane     = UNENCLOSED
HIDMaestro six-int16 DS4 transport source               = UNENCLOSED
DS4 encoded HID report/readback                         = UNENCLOSED
PS4 battery field/default/Steam interpretation           = UNKNOWN
Host ACK/first-frame external receipt                   = UNENCLOSED
P-HID/P-XINPUT/P-OWNER + game consumer isolation         = RUNTIME-BLOCKED
sleep/PnP/Host-crash restore                            = RUNTIME-BLOCKED
```

### 没有发现可直接修改的 HC 偏移

本轮没有发现新的、可按 HC source 无歧义直接修正的轴交换、符号、单位、倍率、deadzone、velocity decay、battery assignment 或生命周期顺序。`pairProven=false`、safe-zero、direct IMU no-report 和未套用未绑定 ROG matrix 均是 fail-closed/identity protection，不归类为原创漂移逻辑。

## 6. 下一步门与用户介入点

当前无需用户介入的工作已完成：静态 HC/source/HIDMaestro 对账、未知/缺口分类、UI fixture 与真实 telemetry 边界澄清。

只有以下动作需要用户介入或明确授权运行时验证：

1. 在 ROG 上运行保持 `gyro-motion` 与 `virtual-gamepad` 两个目录的测试包，回传同一生命周期的 jsonl/native lifecycle/WebView2/Crashpad/WER；用于重测 sensor rebind、WebView recovery 和 raw IMU path。
2. 若继续追查 Steam “PS4 5%”，在该 Host 实例存活期间采集虚拟 DS4 的原始 HID input/feature report，并保存 descriptor、report ID、report length、原始 bytes、设备 instance/container identity；不得只回传 Steam UI 截图。
3. 若需要关闭 T11/T12，需补同 provider/same generation/calibration/matrix proof 与独立 DS4 decoder/readback；在此之前保持 direct DS4 IMU safe-stop。

## 7. 最终状态

```text
HC/source/HIDMaestro static comparison       = EXPANDED / EVIDENCE-ALIGNED
DS4 battery origin                           = UNKNOWN / external-or-default-unproven
UI raw telemetry path                        = PARTIAL-CLOSED (diagnostic only)
UI virtual-stick preview                    = CONTRACT-SAFE-ZERO until plane proof
GamepadMotion pair/calibration/matrix        = UNENCLOSED
DS4 extended HID transport/readback          = UNENCLOSED
P-HID/P-XINPUT/P-OWNER + Steam/game          = RUNTIME-BLOCKED
runtimeUpgrade                               = false
source modification this round               = none
formal package touched                        = false
next governance gate                         = T22-RF00 → T20 → T21 → T14
```
