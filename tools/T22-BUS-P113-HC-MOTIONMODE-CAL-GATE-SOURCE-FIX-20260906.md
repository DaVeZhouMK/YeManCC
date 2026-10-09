# T22 BUS-P113：HC MotionMode / calibration gate source fix

日期：2026-09-06  
状态：`SOURCE-FIXED / BUILD-VERIFIED / STANDALONE-LANE-ONLY / RUNTIME-BLOCKED`

## 1. Authority

本页只记录可以直接对账锁定 HC 源码的局部修正；不改变 `TASK.md` 的唯一主线状态，不解除正式产品 `pairProven=false`、direct-IMU no-report、T15/T16/T17/T18 runtime gate，也不推导 Steam/game consumer 结果。

HC authority：

- `deps/handheldcompanion-runtime/source/Managers/MotionManager.cs:203-259`
- `deps/handheldcompanion-runtime/source/Managers/SensorsManager.cs:361-395`
- `deps/handheldcompanion-runtime/source/Sensors/IMUCalibration.cs:17`

## 2. 明确 source divergence and fix

### P113-F01：MotionMode/trigger was persisted but not consumed

旧 native standalone lane 只读取 `input.gyroMotion.enabled`、`outputMode` 和 `outputStick`；`motionMode`、`motionTrigger` 与 Toggle debounce 没有进入 Coordinator/native frame gate。这样 UI 的 HC 语义和 native 输出语义不一致。

已修正 `native/main.cpp`：

```text
MotionMode.Off    -> trigger pressed 才允许 gyro output
MotionMode.On     -> trigger not pressed 才允许 gyro output
MotionMode.Toggle -> trigger rising edge toggles status; held edge debounced
```

未触发时：

- gyro contribution 为零；
- physical stick 仍按原值透传；
- 配置 target/mode/trigger 变化时清除 toggle 状态，不能把旧 mapping 的 toggle 状态带入新 mapping。

这与 HC `MotionManager` 的 `MotionTriggered` 计算和 toggle debounce 一致；不会由 WebView 自行决定输出。

### P113-F02：YMCC added a non-HC `steady` lock predicate

旧 native calibration lock 要求：

```cpp
confidence == 1.0 && steady == true
```

锁定 HC 的 `SensorsManager.Calibrate()` 只以 confidence 达到 `1.0` 作为等待成功条件，再读取 offset、写入 `confidence * 10` weight、恢复 `Manual`。`GetAutoCalibrationIsSteady()` 不是 HC 的第二个 lock predicate。

已移除 `steady` 对 lock 的阻断，但仍保留：

- `confidence == 1.0`；
- offset finite；
- `weight > 0`；
- offset/mode API 存在；
- timeout 时 safe-zero。

`steady` 仍作为 telemetry 记录，不再伪造为 HC admission 条件。

## 3. Scope boundary

以上修正只在 `feature-assets/gyro-motion/real-stick-test.flag = HC-REAL-STICK-TEST-v1` 的 standalone lane 生效。正式包没有 marker 时：

```text
pairProven=false
virtual-stick=safe-zero
direct-IMU=no-report
```

本轮没有：

- 扩大 ROG PID 或 Raw HID parser 路径；
- 修改 HC matrix、provider identity、XInput slot 选择或 HIDMaestro descriptor；
- 修改 HidHide、Steam、游戏、真实设备或系统状态；
- 把 Host-local receipt 升级为 external consumer receipt。

## 4. Verification

```text
tools/build-workspace.ps1                         BUILD_OK
YeManCC.exe SHA-256                                07EFB528126AC50EB50A58A8CAF602B3AE3269BA8351CFF2943D64C86159C50B
InputHost protocol selftest                       exitCode=0
pnpm exec vue-tsc --noEmit                        PASS
```

当前 worktree 仍未经过新的 RF00 冻结；因此本页只属于 current-source static evidence，写回后必须执行：

```text
T22-RF00 -> T20 -> T21 -> T14 -> static regression
```

已生成新的 standalone 测试包（未写入正式 Release/updater）：

```text
Mainline/Build/TestPackages/GyroInput-ROG-RealStick-20260906-P113/YeManCC-GyroInput-ROG-Test.zip
bytes  = 88,604,045
sha256 = 874C5B0F14DDAD67C81357BA3C58339C31803D9D5136ABD0E9E31C219A642982
YeManCC.exe = 07EFB528126AC50EB50A58A8CAF602B3AE3269BA8351CFF2943D64C86159C50B
InputHost/YeManInputHost.dll = 0C4DC714C8AF3468FBA10D509FB8EE4A4836F303AF407FED431EB6C15E3CDE51
formalReleasePackageUntouched = true
systemMutation = false
```

这个包替代用户回传的旧 app-data ZIP；旧包内 9,728-byte Host DLL 不代表当前 P113 协议。

## 5. Remaining unknowns

P113 不证明：

- HC calibration persistence `StoreCalibration(deviceInstanceId, calibration)` 已与 YMCC identity 完全一致；
- ROG 实机 `confidence/cal-lock`；
- GamepadMotion matrix/provider pair 的硬件帧/容器证据；
- InputHost/HIDMaestro DS4 report、Steam/game consumer 或恢复事务；
- 当前 ROG 多 XInput slot 的 HC PnP identity binding。

因此主线继续保持 `RUNTIME-BLOCKED`，下一介入仍是用户运行最新 standalone 包并回传同代 runtime receipt。
