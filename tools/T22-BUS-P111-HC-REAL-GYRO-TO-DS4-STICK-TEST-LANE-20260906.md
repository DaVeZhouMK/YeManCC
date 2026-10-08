# T22 BUS-P111 — HC 对齐的真实陀螺仪 → DS4 左/右摇杆测试 lane 实现

日期：2026-09-06  
状态：`STANDALONE-REAL-STICK-LANE-READY / PRODUCT-RUNTIME-BLOCKED / ROG-CONSUMER-OBSERVATION-REQUIRED`

本页是对 P110 静态审计后的“明确局部实现”回写。P110 仍保留为历史审计快照；本页只记录经过用户授权后新增的 standalone 测试 lane，不把它升级为正式产品 runtime closure。

## 1. 实现边界

真实链路仅在完整 standalone 测试包中启用：

```text
PowerControl/feature-assets/gyro-motion/real-stick-test.flag
  = HC-REAL-STICK-TEST-v1
```

当前只有 marker 由 `tools/package-gyro-input-test.ps1` 写入 Stage；目标摇杆不再由打包参数决定，而是运行时读取统一设置 `input.gyroMotion.outputStick`。正式 Release/updater 不带 marker，因此正式包仍保持 `pair-unproven-safe-zero`，不会自动开始真实 gyro→stick 输出。

## 2. HC 对齐的测试 lane

### 2.1 ROG RC73XA 准入

P105/P103 用户证据已经确认 ROG 测试机为 `RC73XA / ROG Xbox Ally X`，gyro/accel DeviceId 共同属于 `ACPI#BOSC0200#1` provider 家族。测试 lane 只在以下条件同时成立时准入：

```text
marker present
∧ XInput source live
∧ gyro + accel current reading present
∧ normalized ACPI provider key identical
∧ HC selector == asus-rc73xa
∧ input.gyroMotion.enabled == true
∧ input.gyroMotion.outputMode == virtual-stick
```

HC `SensorsManager.UpdateReport()` 消费同一 tick 的 typed current readings；因此 timestamp skew 仅记录为诊断 provenance，不额外发明 HC 不要求的跨传感器硬阈值。正式 lane 没有 marker 时不进入该分支。

### 2.2 校准事务

测试 lane 执行锁定 HC `SensorsManager.Calibrate()` 的顺序：

```text
4 秒等待
→ ResetContinuousCalibration()
→ SetCalibrationMode(Stillness | SensorFusion)
→ ProcessMotion()
→ confidence == 1 且 offset finite
→ SetCalibrationOffset(offset, confidence*10)
→ SetCalibrationMode(Manual)
```

5 秒 calibration timeout 只记录并保持 safe-zero；不会猜测 offset、confidence 或 weight。`GamepadMotion.dll` 仍从锁定 HC runtime 目录加载。

### 2.3 默认 LocalSpace 与目标轴

实现使用锁定 HC Windows/Yaw 默认路线：

```text
SwapYawRoll(defaultGyro) = (calX, -calZ, -calY)
LocalSpace output         = (defaultGyro.Z, defaultGyro.X)
                          = (-calY, calX)
default sensitivity       = 1.0 * 1000
default gyroWeight        = 1.2
blend                     = currentStick + output * (1.2 - stickNorm)
```

`input.gyroMotion.outputStick=right` 合入实体 `sThumbRX/RY`；`outputStick=left` 合入 `sThumbLX/LY`。另一组轴保持实体值，最终统一走现有 InputHost `StandardAxes`→HIDMaestro DS4 writer。目标切换来自同一份 YMCC durable settings，不再通过两个测试包分叉。

### 2.4 Host active 门

当前 native 只有同代 `firstFrame=true` receipt 才设置 `host-active`；后续 accepted frame 不再把 active 错误覆盖为 neutralized。

## 3. 代码、构建与包证据

涉及源码：

```text
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\native\main.cpp
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\bridge\inputContracts.ts
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\src\views\GyroMotionView.vue
G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\tools\package-gyro-input-test.ps1
```

构建命令与结果：

```text
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-workspace.ps1
BUILD_OK
YeManCC.exe SHA-256 = 218D293E321AC17CF814C68DF60B10C18ACA914EC2CAA4B2237B89BF84C2E00F
InputHost Release = 0 warnings / 0 errors
pnpm exec vue-tsc --noEmit = PASS
pnpm exec vite build = PASS
RF00 sourceDigest = CCC151BF22A86E4E6AA28F84EFA1F780932BA2685EF52703187282BE85023346
RF00 porcelainCount = 502
T20 = 4/13/4; T21 = 9 claims; T14 = DGF-01…DGF-15; runtimeUpgrade = false
```

当前唯一 standalone 完整测试包（左/右由 YMCC 页面运行时选择）：

```text
G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-RealStick-20260906-Current\YeManCC-GyroInput-ROG-Test.zip
bytes = 88,598,706
SHA-256 = 81F7ED858AA83DC0C1AAF81FDF116950B720CCE5577FA6F31065150DA3BDCC16
HC runtime = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
HidHide = standalone lane only
formal Release/updater touched = false
```

运行 YMCC 陀螺仪页面的“左摇杆/右摇杆”选择并保存后，native standalone lane 刷新同一份 `input.gyroMotion` 设置；旧版 `real-stick-target.txt` 仅保留兼容回退，不再由当前打包器生成。

静态/自测结果：

```text
gyro-motion-mapper mock              PASS
assisted-stick-assembler             PASS
gyro-calibration-session             PASS
gyro-virtual-feature                 PASS
ROG gyro protocol                    PASS
gyro sidecar audit                   PASS
InputHost result-shape selftest      NOT-REPLAYABLE (current source has no matching script/package entry; historical claim retained only as unverified)
```

本机台式机安全回归没有 typed gyro/accel，因此只观察到 `gyro=false / accel=false / selectorResolved=false`，维持零输出；该回归不能替代 ROG 实机证据。本机直接启动还存在已安装 `C:\SOFT\YeMan\YeManCC.exe` 单实例/共享运行环境，不能把它解释成 ROG lane 失败。

本轮可复现补充：`Capture-ROG-Input-Gap-Evidence.ps1 -DurationSeconds 5 -SkipLaunchYeManCC -SkipImu` 在当前源码通过 `Add-Type`、结果构建和 ZIP 写出，退出码 `0`；因此历史 C# 集合初始化编译错误已不再复现。该 smoke 仍是采集器自检，不是 ROG motion/DS4 consumer 证据。

## 4. 证据、未知与介入点

已证实：

- source 中存在 marker gate、ROG provider key、RC73XA selector、HC calibration order、HC LocalSpace blend、left/right target route 和 Host first-frame gate；
- 完整包包含 YeManCC、InputHost/HIDMaestro/WinRT、锁定 HC runtime、gyro-motion/virtual-gamepad 目录和 standalone HidHide；
- 正式 Release/updater 不包含测试 marker，测试 marker 不会因正式导出而发布。

仍未知/未闭口：

```text
ROG 实机 confidence/cal-lock receipt
InputHost frame-accepted / firstFrame 同代 receipt
HIDMaestro DS4 descriptor/report/raw readback
Windows/Steam/game consumer 是否消费目标左/右轴
关闭、失焦、睡眠、断开、Host 崩溃后的 owner/rearm/consumer 恢复
```

用户下一步只需在 ROG RC73XA 上：

1. 解压完整 ZIP，运行 `Start-YeManCC.cmd`；
2. 保持掌机静止约 8–10 秒；
3. 转动掌机，观察 PS4 虚拟手柄测试器或游戏目标轴；
4. 将 Desktop 上的 `YeManCC-ROG-Input-Gap-Evidence-*.zip` 回传。

重点回传字段：`hc-real-stick-test-lane`、`hc-calibration-start`、`cal-lock`、`motionAdmission=hc-test-lane-admitted`、`targetStick`、`leftStick/rightStick`、`firstFrame` 和外部 consumer observation。

## 5. 裁决

```text
standalone 真实 ROG gyro → HC GamepadMotion → DS4 左/右轴测试入口 = READY
正式产品 runtimeUpgrade / Release closure                         = FALSE / BLOCKED
下一阻断                                                        = ROG 实机运行与 consumer 回读
```
