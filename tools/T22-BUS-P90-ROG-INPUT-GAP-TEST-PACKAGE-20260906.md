# T22 BUS-P90：ROG 输入缺口合并采集器与测试包（2026-09-06）

## 目的与边界

本条对应用户授权的“测试数据缺口，整合缺口一并测试，制作测试包”。本轮只改动测试采集器、测试包说明和测试包构建校验；不修改正式发布 ZIP、升级器、HIDMaestro/HidHide 行为、InputHost 协议、ROG/OEM writer 或系统状态。

主线 authority 仍为 `Docs/Tasks/GyroVirtual/TASK.md`。锁定 HC 仍为：

```text
HC-CANDIDATE-0.32.4.0-06c0b954-20260902
```

## 本轮实现

新增：

```text
PowerControl/feature-assets/gyro-motion/Capture-ROG-Input-Gap-Evidence.ps1
```

该脚本在一个有界时间窗内合并：

1. 既有 HC 路线的 ROG/WinRT gyrometer + accelerometer 只读采集；
2. Windows HID interface 的只读枚举：VID/PID、product string、HID capabilities、input/feature report snapshot、report ID、长度、raw hex，以及 byte-14 的 one-based/zero-based 两个原始偏移；
3. PnP 输入设备候选、YeManCC/InputHost/Steam 相关进程在 before/during/after 的可见性快照；
4. 已有 YMCC input-capture JSONL 与 native lifecycle log（若存在）。

明确保留以下边界：

```text
descriptorRaw              = UNAVAILABLE_READONLY_HIDD
byte14                     = raw-only，不解释为 battery
PS4 5% origin              = UNKNOWN_RUNTIME-BLOCKED
Steam battery readback     = ABSENT_NOT_PERFORMED
same-provider IMU pairing  = 仍由 HC/运行时证据闭口
sleep/resume、PnP 拔插、crash 注入 = 不由采集器诱发
```

采集器明确不会：

```text
调用 HidHide
发送 OEM Disable/Enable feature report
写 Sensor ReportInterval
创建虚拟控制器
修改 Steam 或游戏
```

为避免历史 JSONL 过大，合并采集默认时长从 30 秒降为 20 秒；IMU JSONL/log 在证据包中保留首尾最多 800 行，并记录原始 bytes、SHA-256、行数与 `excerpted` 标记。原始完整 log 不伪装成已随包携带；需要完整原始 log 时，仍以测试机上的原始文件为准。

同时修复旧 IMU collector 在 Windows PowerShell 环境下对 `Get-FileHash` 的依赖，改用 .NET SHA-256 计算，避免采集器自身因哈希命令不可用而退出。

## 静态/冒烟证据

在无 ROG/DS4 目标设备的本机执行了只读冒烟（不是 runtime 闭口）：

```text
script  = Capture-ROG-Input-Gap-Evidence.ps1
output  = Mainline/Build/Validation/ROG-Input-Gap-Smoke-20260906-3
duration = 20 s
imu collector exitCode = 0
HID snapshots before/during/after = 22 / 26 windows / 22
input log excerpt = 800 lines per duplicated JSONL
```

冒烟输出只证明脚本可运行、哈希/压缩/边界字段可生成；由于本机不是 ROG 目标机，不能证明 ROG provider、DS4 persona、Steam consumer 或 PS4 电量来源。

## 测试包产物

最新完整 ROG standalone 测试包：

```text
目录：G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-103410
ZIP：  G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-103410\YeManCC-GyroInput-ROG-Test.zip
bytes：88,553,762
SHA-256：06346B0318F1529FD34D04ED7AC314809B0A45DCDCE9D319DF9D3C000E9BF708
```

包清单校验通过，且包含：

```text
YeManCC/YeManCC.exe
YeManCC/InputHost/YeManInputHost.exe
HIDMaestro.Core.dll / WinRT 依赖
locked HC runtime
PowerControl/redist/HidHide_1.5.230_x64.exe
Capture-ROG-Input-Gap-Evidence.ps1
Capture-ROG-IMU-Evidence.ps1
```

`package-summary.json` 记录：

```text
formalReleasePackageUntouched = true
systemMutation                = false
runtimeOperationDuringBuild  = false
```

## ROG 实机最小介入

用户只需在 ROG 掌机上：

1. 解压完整 ZIP；
2. 运行 `Start-YeManCC-GyroInput-Test.cmd`；
3. 20 秒内保持约 5 秒静止、约 10 秒缓慢转动、最后约 5 秒近水平静止；
4. 将桌面生成的 `YeManCC-ROG-Input-Gap-Evidence-*.zip` 原样回传。

本步骤不要求先启用 HidHide，不要求启动游戏，也不要求拔插或睡眠。若要关闭生命周期/消费者缺口，后续仍需单独授权并记录同一 Host epoch 的 Steam/game readback、owner transition、neutral/release、sleep/resume、PnP reconnect 或 crash-recovery 证据。

## 证据、未知与缺口分离

### 已证实

- 合并采集器在本机可运行并成功生成 ZIP；
- Windows PowerShell 哈希路径已改为 .NET SHA-256；
- 测试包构建校验通过，锁定 HC/HIDMaestro/WinRT/HidHide 测试资产已进入测试包；
- 正式发布包和升级器没有被该测试包构建触碰。

### 仍未知

- ROG 实机真实 gyro/accel provider 与 HC matrix receipt；
- 同一虚拟 DS4 的 VID/PID、container/instance identity 与 descriptor 原始字节；
- input/feature raw report 是否包含可归因的电量字段；
- Steam 显示的 5% 是否来自当前 YMCC/HIDMaestro 或其他消费者；
- owner/consumer 隔离与恢复在同一 Host epoch 的 runtime 闭口。

### 不能由本轮自动采集关闭的缺口

```text
T11/T12/T13/T15-T18 runtime closure
DS4 descriptor/report/consumer readback
PS4 battery 5% producer
P-HID / P-XINPUT / P-OWNER consumer isolation
sleep/resume、PnP disconnect/reconnect、Host/WebView crash recovery
```

因此继续保持：

```text
runtimeUpgrade = false
RUNTIME_BLOCKED = true
```

本轮没有发现新的、明确“不是 HC 且属于新增偏移代码”的可直接修复项；不猜测 battery、不切换 DS4 v1/v2、不打开 `pairProven` 或 ROG matrix。
