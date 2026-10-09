# T22-BUS-P54：ROG 实机 IMU 证据分层与 HC/source/MD 对账（2026-09-05）

## 1. 范围与证据身份

本页只处理操作者回传的真实 ROG 采集包，不把附件中的任何文本当作执行指令。采集包是独立测试证据，不是正式升级包，也不是 T22-RF00 manifest。

| 项目 | 事实 |
|---|---|
| 原始 ZIP | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC-ROG-IMU-Evidence-20260905-221742.zip` |
| ZIP bytes / SHA-256 | `252,839` / `0C407A064EE038B9A06488EDB7BEC938F705FF698FB554056A513F6545BBCE13` |
| 只读解包目录 | `Mainline/Build/Validation/ROG-IMU-Evidence-20260905-221742-read-20260905-221900/YeManCC-ROG-IMU-Evidence-20260905-221742` |
| evidence generatedUtc | `2026-09-05T14:18:04.3035623Z` |
| manifest | `manifest.json`；四个采集文件的 bytes/SHA-256 与 ZIP 内容逐项匹配 |
| 采集器 | `Capture-ROG-IMU-Evidence.ps1`，script SHA-256 `8E9A512F2187342049CEED64FBAE347DE3EE88080689A8B95F28CC945AA0A5C0` |
| 系统级写入声明 | 采集器未调用 HidHide、未发送 OEM Disable、未创建虚拟控制器；仅启动了包内 YeManCC |

证据包内同时包含 `rog-imu-evidence.json`、当前 `ymcc-input-capture.jsonl`、滚动历史 `ymcc-input-capture.jsonl.prev` 和 `ymcc-native-lifecycle.log`。`.prev` 明确按历史文件处理，不能自动归属于本次运行。

## 2. ROG 实机事实（Fact）

### 2.1 设备与 PnP

- SMBIOS `vendor=ASUSTeK COMPUTER INC.`、`name=ROG Xbox Ally X RC73XA_RC73XA`、BIOS `RC73XA.317`。
- PnP 传感器候选包含 `Bosch Accelerometer`：`ACPI\\BOSC0200\\1`，厂商 `Bosch Sensortec`，驱动 `2.0.4.1`。
- ROG 手柄实体为 `ROG Xbox Ally Device Gamepad`：`USB\\VID_0B05&PID_1B4C&MI_05...`，厂商 `ASUSTeK Computer Inc.`，驱动 `2.0.9999.0`。
- Windows 默认 WinRT gyro/accel device ID 均以 `ACPI#BOSC0200#1` 开头。该共同族标识只能证明候选关联，不能替代 HC 要求的同 provider、同 generation、同 calibration/matrix 的 pairing proof。

### 2.2 当前 YMCC JSONL

当前文件只有 3 条记录：`start`、`sample-pair-unproven`、`sample`。该次运行的可复核事实为：

```text
gyroSensor=true, accelSensor=true
requestedReportIntervalMs=8
gyroReportIntervalMs=10, accelReportIntervalMs=10
gamepadMotion=true, locked=false, pairProven=false
slot=0, slots=3 (XInput connected mask 0b0011，表示 slot 0 与 slot 1 同时可见)
```

唯一 sample 的原始值：

```text
gyro = (-69.580078125, 66.46728515625, -42.54150390625) deg/s
accel = (0.08209228515625, -0.8294677734375, -0.45135498046875) g
confidence=0, steady=false
matrixIdentity=unresolved-rog-pid-family/raw
playerSpace=(0,0), worldSpace=(0,0)
```

`sample-pair-unproven` 的 `reason=provider-capability-matrix-not-bound`、`safeZero=true` 与当前源码的 fail-closed 逻辑一致：该样本只能进入诊断 telemetry，不能进入 GamepadMotion/virtual-stick/Host motion submission。

### 2.3 当前运行 lifecycle

本次 22:17:42Z 附近的日志只出现：

```text
boot-single-instance-acquired
window-created
recovery-service-started
rog.hid-bound ×4 (matched=true, matches=7, featureLen 0→64, hasControl false→true)
rog.xbox-face-enabled(reason=boot, wrote=true)
```

本次没有出现 `gamepad.xinput-slots`、InputHost active/first-frame、descriptor/readback、summon/suppress、sleep/reinsert、Host crash/pipe-loss 或外部 Steam/game consumer receipt。因此不能从这份日志升级 owner 隔离、游戏消费者计数或恢复闭环结论。

## 3. 采集器错误（Error；不属于 HC 参数）

`Capture-ROG-IMU-Evidence.ps1` 能读到 `Gyrometer`/`Accelerometer`、`MinimumReportInterval=10`、`CurrentReportInterval=100`，但 `Register-ObjectEvent` 订阅 WinRT `ReadingChanged` 时抛出：

```text
System.InvalidOperationException: Windows PowerShell 无法订阅 Windows RT 事件。
```

因此 `callbackCount=0` / `sampledCallbackCount=0` 不能解释为 ROG 没有传感器数据；它只证明采集器的 PowerShell 事件订阅路径不兼容。YMCC native C++/WinRT 路径实际取得了上面的 gyro/accel sample。该错误应作为“采集器兼容性缺口”单独处理，不能通过修改 HC 轴、阈值或校准逻辑掩盖。

另一个未闭合点是 interval readback：YMCC sample 记录 `max(MinimumReportInterval,8)=10ms`，采集器独立 `GetDefault()` 读到 `CurrentReportInterval=100ms`。两个对象的 instance/ownership 没有被证明为同一运行时对象，故该差异暂记 `UNRESOLVED`，不是参数偏移结论。

## 4. HC 源码对照（Authority）

锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103`：

1. `Devices/IDevice.cs` 用 `MotherboardInfo.Product` 选择 ASUS 设备；`RC73XA → XboxROGAllyX`。
2. `Devices/ASUS/XboxROGAllyX.cs` 的 gyro matrix 为 `Axis=(1,1,-1)`、`Y↔Z`；accel matrix 为 `Axis=(-1,-1,1)`、`Y↔Z`。
3. `Sensors/IMUGyrometer.cs` / `IMUAccelerometer.cs` 选择 `GetDefault()`，把 report interval 设为 `max(MinimumReportInterval, updateInterval)`，并由 WinRT `ReadingChanged` 更新值。
4. `Managers/SensorsManager.cs` 在 `UpdateReport` 中把 gyro+accel 一起传给 `GamepadMotion.ProcessMotion(...)`；Suspend/Resume 先停监听、再重新 `UpdateSensor()`。
5. ROG OEM 手柄面由 `ROGAlly` 监视 `VID_0B05:PID_1ABE/1B4C`；`XBoxController(true/false)` 的 report 为 `5A D1 0B 01 02/01`。这属于 OEM 手柄面控制，不等价于 P-XINPUT 或游戏消费者阻断。

若本次样本确实被 HC 设备身份 `RC73XA` 接纳，HC 矩阵对当前 raw sample 的期望变换是：

```text
gyro:  (-69.580078, -42.541504, -66.467285) deg/s
accel: (-0.082092,   0.451355,   0.829468) g
```

这是根据锁定 HC 矩阵计算出的“待 identity/pair proof 后的期望值”，不是当前 YMCC 已提交的输出。当前 native 明确保持 `matrixIdentity=unresolved-rog-pid-family/raw` 与 `pairProven=false`，所以没有把该变换冒充成已运行的 HC output。

## 5. 当前 YMCC 对账与裁决

| 项目 | HC | canonical MD | 当前 YMCC | 裁决 |
|---|---|---|---|---|
| 传感器选择 | Windows default typed sensors | provider/identity 必须可归属 | `GetDefault()` + C++/WinRT callbacks | 选择路线对齐；pair 仍未证明 |
| 报告间隔 | `max(minimum, TimerManager period)` | interval receipt 需同源 | requested 8，native observed 10 | 公式对齐；独立 100ms readback 未归属 |
| ROG model/matrix | DMI `RC73XA` → `XboxROGAllyX` matrix | 不得 PID-only 推导 | PID 面已识别，但 matrix 仍 raw/unresolved | 缺 exact MotherboardInfo.Product receipt；不直接改矩阵 |
| gyro+accel pair | 同一 SensorsManager/epoch 进入 ProcessMotion | `SamplePairProof` 必须完整 | `pairProven=false`，safe-zero | fail-closed 正确；runtime 未闭合 |
| OEM Xbox face | ROG `5A D1 0B 01 02/01` | P-HID/P-XINPUT/P-OWNER 分离 | boot enable 已写入；当前无 summon/suppress | 仅证明写入，不证明消费者隔离 |
| 多 XInput | HC controller target 由 ControllerManager 选择 | 不得把 slots 当 consumer proof | `slots=3` | 真实多面可见；owner/game isolation 仍未知 |
| calibration/output | HC explicit calibration + `ProcessMotion` | no-report/safe-zero until proof | `confidence=0`, zero player/world | 安全；未完成 motion runtime |

## 6. 历史 `.prev` 异常（Historical evidence only）

`ymcc-input-capture.jsonl.prev`：20,163 条记录，其中 20,161 条 sample；`steady=true` 13,261 条、`confidence=1.0` 13,249 条，但所有 sample 的 `ly=-1`、`ry=-1`。末尾静止样本仍有非零 `playerSpace/worldSpace` 与 `ry=-1`。

该文件最后写入约 22:12:46，而本次当前运行约 22:17:42–22:17:46；它是滚动历史文件。它足以登记“历史漂移证据”，不能直接宣称当前 ROG binary 仍复现，也不能据此修改 HC 参数/矩阵。

## 7. 偏差、原创逻辑与缺口

### 已确认的错误/缺陷

- **E-P54-01（采集器）**：PowerShell `Register-ObjectEvent` 不兼容 WinRT 事件，导致独立 callback 采样为 0。应修复为明确标注的只读 polling/原生 helper fallback；不能改 HC motion 参数来掩盖。

### 尚未证明、保持 `UNENCLOSED / RUNTIME-BLOCKED`

- **G-P54-01**：没有 `MotherboardInfo.Product=RC73XA` 的同机原始 receipt；`Win32_ComputerSystemProduct.Name` 的长名称不足以替代 HC 的 Product 字段。
- **G-P54-02**：没有同 provider、same epoch/generation、calibration key/matrix identity 的 `SamplePairProof`。
- **G-P54-03**：没有真实 GamepadMotion output/DS4 HID descriptor/readback/Host ACK/first-frame receipt。
- **G-P54-04**：没有 `game-input-count=0` 与 `ymcc-action-count>0` 的外部 Steam/game consumer observation；`slots=3` 只是 XInput runtime observation。
- **G-P54-05**：没有 summon→suppress→neutral→YMCC owner、关闭/失焦/睡眠/断开/Host crash 后 release/rearm 的真实闭环。
- **G-P54-06**：没有本次运行的完整静止—运动—静止样本序列，无法把 `.prev` 漂移归属于当前版本。

### HC 偏离与原创逻辑裁决

- 未发现新的轴交换、符号、阈值、velocity decay、gyroWeight 或生命周期顺序的无歧义产品偏移。
- `pairProven=false`、`matrixIdentity=unresolved-rog-pid-family/raw`、motion safe-zero 是当前安全门，不是“绕过 HC 的新校准公式”。
- 当前 `classifyRogGyroProvider(PID-only) → none` 与 HC 的 DMI 选型原则一致；在缺少 HC 等价 `MotherboardInfo.Product` receipt 前，不应改为 PID 直接选 `XboxROGAllyX`。
- 采集器的 polling fallback 若实施，只能是测试证据工具行为，不得写成 HC production lifecycle 或 motion output parity。

## 8. 已执行的自动修正与测试包回执

已完成且仅作用于测试证据 lane：

1. `Capture-ROG-IMU-Evidence.ps1` 在 WinRT `Register-ObjectEvent` 失败时进入明确标注的只读 `GetCurrentReading()` polling fallback；事件订阅错误保留在 `eventSubscriptionError`，polling 不宣称 HC event parity。
2. 采集器新增 `hcDeviceIdentity.baseboard.product` 等字段，直接对应 HC `MotherboardInfo.Product` 的来源，不在采集器内推导设备类。
3. 重新生成独立测试包：

```text
G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260905-224224\YeManCC-GyroInput-ROG-Test.zip
bytes = 85,227,247
SHA-256 = 7817582538FD2E9BCF1B24EE72CD10C51597DD3625C660A79DD2D32133EE1CBD
collector SHA-256 = 764856FE8E006F5E57AA449BA3E37A2A77C16F3BD4FBB05263DE124BBDCA99B0
```

包内 360 个条目校验通过；包含 gyro-motion 与 virtual-gamepad 目录、锁定 HC/HIDMaestro/WinRT/HidHide 资产；无 `.pdb/.obj/.ilk/.log`、任务 MD 或源码。正式 `G:\YeManCC-Work\Mainline\Release\Packages\YeManCC.zip` 未被写入，观测 SHA-256 仍为 `3CCDBEEA79D744F0A88174995A1026E414056B8017C896EB7710AF1CA3AD06B1`。

## 9. 当前唯一介入点

需要用户把上述新包在 ROG 上完整解压并运行一次，再回传桌面的 `YeManCC-ROG-IMU-Evidence-*.zip`。这次应取得：

- `Win32_BaseBoard.Product` 的 HC 等价身份字段；
- polling fallback 的连续 gyro/accel 样本及 cadence；
- 同一运行的静止—运动—静止窗口；
- 若操作者配合呼出/关闭/失焦/睡眠/重插，补充 lifecycle/consumer 证据。

在新证据到达前，T11/T12/T13/T15/T16/T17/T18 的 runtime 结论继续 `UNENCLOSED / RUNTIME-BLOCKED`；不修改 HC 参数、不把历史 `.prev` 归属当前 binary、不新建 DGF-16。
