# T22 BUS-P99：撤回电量补丁后的当前 ROG standalone 测试包（2026-09-06）

状态：`PACKAGE-PASS / CURRENT-INPUTHOST / FORMAL-RELEASE-UNTOUCHED / RUNTIME-USER-INTERVENTION-REQUIRED`

## 1. 包体

```text
OutputRoot = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-134521
ZIP        = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-134521\YeManCC-GyroInput-ROG-Test.zip
ZIP bytes  = 88,564,455
ZIP SHA256 = A3DB5D6AF0732FF9B439F99F2B62AB9422EC6906F68F5B5EDC8EE84ADC26E340
HC runtime = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
```

打包命令：

```text
pnpm run package:gyro-input-test
exitCode = 0
result = GYRO_INPUT_ROG_TEST_PACKAGE_OK
formal Release package untouched = true
```

## 2. 包内语义

- 包内 YeManCC/YeManInputHost 来自撤回 `BatteryLevel/BatteryCharging/BatteryFull` assignment 后的当前构建。
- 包内保留锁定 HC runtime、HIDMaestro/WinRT 依赖、锁定 HidHide 安装器、`gyro-motion` 和 `virtual-gamepad` 测试资产。
- collector 默认不调用 HidHide/OEM Disable，不修改 Steam/game，不创建虚拟设备；它只收集 ROG WinRT IMU、DS4/HID 只读身份/能力、PnP 候选、Host/Steam 可见性和 YMCC 生命周期日志。
- 该包只写入 `Mainline\Build\TestPackages`，不替换 `Release\Packages\YeManCC.zip`，不安装或启动任何系统驱动。

## 3. 用户介入场景

在 ROG Xbox Ally X 上解压到临时目录，运行包根目录 `Start-YeManCC-GyroInput-Test.cmd`。回传桌面的 `YeManCC-ROG-Input-Gap-Evidence-*.zip`，并尽量在同一窗口记录：

```text
PS4 “电量低 - 5%” 是否仍出现
静止—转动—静止期间轴线图、波形、数字、虚拟右摇杆是否变化/停止漂移
如可行：窗口关闭、托盘退出、失焦、睡眠/唤醒的同一 run/epoch 生命周期结果
```

这一步是 T17/T18 runtime 证据采集，不是当前 BUS 可自行完成的静态工作；没有用户回传前不升级 runtime closure。

## 4. 仍保持的边界

```text
PS4 battery producer/report/consumer = UNKNOWN / RUNTIME-BLOCKED
ROG pair/matrix/calibration/consumer = UNENCLOSED
formal Release HidHide policy        = USER-RULING-REQUIRED
runtimeUpgrade                       = false
```
