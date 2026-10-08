# T22-BUS-P76：HC DS4 电量与 Motion 参数扩展扫描

日期：2026-09-05
状态：`STATIC-EXPANDED / HC-PARITY-SCAN-COMPLETE / UNKNOWN-GAPS-EXPLICIT / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC source、当前 YMCC source/contract 和既有任务书的继续只读扫描。范围覆盖：PS4/DS4 电量生命周期、HC MotionManager action plane、SettingsMode0/1 参数边界、AxisActions 默认值、当前 UI/Host 入口、静态回归与发布边界。未启动 YeManCC、InputHost、HIDMaestro、HidHide、Steam、游戏、虚拟设备或真实硬件；未修改系统状态。

## 1. PS4 低电量 5%：扩大静态边界后的结论

### HC 三条独立路径

1. `Targets/DualShock4Target.cs` 的 VIIPER 31-byte `BuildReport()` 写摇杆、按钮、触控、raw gyro/accel；没有 battery assignment。
2. `Misc/DS4OutDevice.cs` 的 `DS4_REPORT_EX` 仅定义 `bBatteryLvl`/`bBatteryLvlSpecial` 字段；锁定 HC source 未找到生产 writer、target 引用或 report submit 链，属于 dead/unreferenced 结构。
3. `DSU/DSUServer.cs` 的 `DsBattery` 只属于 DSU 网络 metadata：从 Windows 系统电池状态映射 `None/Dying/Low/Medium/High/Full/Charging`，不等于 VIIPER/HIDMaestro DS4 HID report。

### 当前 YMCC/HIDMaestro

- `InputHost/Program.cs` 的 frame/neutral 提交只写 `Buttons`、`Hat`、标准 `Axes`，没有 `BatteryLevel`、`BatteryCharging`、`BatteryFull` 或 DS4 six-int16 IMU writer。
- `nativeBatteryStatus()` 只生成 YMCC 主机电源/监控 telemetry，不进入 `HMGamepadState`、HIDMaestro controller 或 DS4 report。
- `dualshock-4-v2` extended metadata 的 byte `14` 只证明 descriptor semantic；当前 profile 的 `AlwaysArmed=false`、`ArmOn=null`，反射/IL 结果未发现 `0→5` 隐式转换。

因此用户观察到的 `PS4 Controller / 电量低 - 5%` 继续保持：

```text
PS4 battery 5% origin = UNKNOWN / RUNTIME-BLOCKED
```

不得猜写 `0x05`、`0x0A`、`100%` 或 `Full`。关闭条件仍是同一 Host epoch 的虚拟 DS4 identity、descriptor、input/feature raw report、report ID/length、byte-14 decoder 和 OS/Steam 对应 readback。

## 2. HC Motion/action 参数扩展扫描

锁定 HC source 的可复核边界：

```text
SettingsMode0 SensitivityX/Y         = 0.1..3.0, default 1.0
ProfilesPage Gyro/Accel multiplier   = 0.1..3.0
Templates GyroWeight                 = 1.0..2.0, HC GyroActions default 1.2
AxisActions inner/outer/anti-deadzone= 0..25%
SettingsMode1 SteeringMaxAngle       = 10..80°, default 35° in HC page
SettingsMode1 SteeringPower           = 0.2..5.0, default 1.0
SettingsMode1 SteeringDeadzone       = 0..5°, default 0°
```

当前 YMCC durable/UI 边界与这些已证实范围一致；P74 已修正的五项范围继续有效。本轮未发现第二组可按 HC source 无歧义直接修改的范围、单位、轴、符号、增益或 velocity-decay 偏移。

HC action plane 仍必须分开记录：

- `MotionManager.SetupMotion()` 产生 calibrated GamepadMotion gyro/gravity 与 raw DSU gyro/accel；
- `SettingsMode0` 消费 Default gyro，`SettingsMode1` 消费 Inclination angles；
- `DualShock4Target` 的 DS4 IMU raw 编码是另一条 target/report 语义，不等于 GamepadMotion action plane。

当前 YMCC 页面展示诊断 telemetry、Host-local canonical receipt 和 fixture preview，但没有同 provider/identity/epoch 的 HC action receipt，也没有 HID/Steam/game consumer readback。不能把页面有数值、mock PASS 或 Host-local receipt 提升为 action/runtime closure。

## 3. 本轮静态回归事实

以下仅为 source/static/mock 证据，均不构成 runtime upgrade：

- `vue-tsc --noEmit`：PASS。
- settings CAS、gyro config activation、gyro motion mapper、motion sample、gyro E-37 HC parity：PASS。
- provider capability matrix、ROG gyro protocol/input adapter、special-controller protocols、persona descriptor/report：PASS。
- input contracts/lifecycle/runtime admission、Host supervisor、Coordinator/owner runtime/integration、physical ownership：PASS。
- physical visibility transaction、ROG/HidHide topology、evidence quality、HC parity ledger、parameter-consumption ledger、input diagnostics：PASS。
- `T22-RF00 → T20 → T21 → T14` 已重新执行：

```text
manifestId       = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest     = A9C6538A8515FA5BE674F8E7B7560A65484056F8E30BFF365360CCA765CC9F21
keyFiles         = 74
porcelain        = 461
HC clean         = true
runtimeOperation = false
buildOrTest      = false
T20              = 4/13/4
T21              = 9 claims
DGF              = DGF-01…DGF-15
runtimeUpgrade   = false
```

## 4. 发布边界冲突

`test:a1-gyro-release-boundary` 的审计结果仍为 `FAIL`，唯一已定位 forbidden entry 是：

```text
PowerControl/redist/HidHide_1.5.230_x64.exe
```

这与“测试包允许携带 HidHide 安装包、正式发布边界屏蔽输入测试资产”的策略存在冲突；它不是 HC 陀螺仪数学偏差。本轮没有删除、移动或改写正式包/updater，保留为发布策略缺口，等待明确的正式包策略决策。

## 5. 三方裁决：事实、未知、原创逻辑

```text
HC DS4 virtual HID battery writer       = NOT FOUND
current YMCC battery writer             = NOT FOUND
HIDMaestro 0→5 conversion               = NOT FOUND
HC SettingsMode/AxisActions range drift = NOT FOUND after P74 correction
HC action-plane runtime receipt          = UNENCLOSED
same-provider pair/epoch/calibration    = UNENCLOSED
DS4 descriptor/report/readback           = UNENCLOSED
P-HID/P-XINPUT/P-OWNER/consumer          = RUNTIME-BLOCKED
new original battery logic              = NOT FOUND
new original motion drift algorithm     = NOT FOUND
```

`pairProven=false`、safe-zero、direct DS4 IMU no-report、未绑定 ROG matrix 与 UI fixture-only 仍是 fail-closed 保护或测试边界，不归类为原创漂移逻辑。

## 6. 必须用户介入的唯一运行时门

静态可执行工作已到当前证据边界。若要继续关闭缺口，需要用户在 ROG/同一 Host 生命周期回传：

1. provider/sensor identity、epoch、gyro+accel pair、HC matrix/calibration receipt；
2. Host first-frame/config ACK、虚拟 DS4 descriptor 与 input/feature raw report；
3. byte-14 独立 decoder、OS/Steam device identity 与电量 readback；
4. P-HID/P-XINPUT/P-OWNER、Steam/game consumer、sleep/PnP/crash recovery 的外部观察。

在这些证据回来前，主线保持：

```text
T10 = SOURCE-IMPLEMENTED / HOST-LOCAL-RECEIPT-ONLY
T11-T13 = UNENCLOSED / RUNTIME-BLOCKED
T15-T18 = DESIGN-ONLY / RUNTIME-BLOCKED
PS4 battery 5% = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade = false
```
