# T22 BUS-P107：ROG selector telemetry standalone 测试包

日期：2026-09-06  
状态：`STANDALONE-PACKAGE-READY / FORMAL-RELEASE-UNTOUCHED / USER-RUNTIME-REQUIRED`

## 1. 包体

```text
outputRoot = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-telemetry-selector
zip        = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-telemetry-selector\YeManCC-GyroInput-ROG-Test.zip
zipBytes   = 89,770,126
zipSha256  = 46ED23AC073A43198A4EF59622D73F33B002B3133E30F076198CE28DBFF006DF
```

包内 `YeManCC/YeManCC.exe` 是 P106 构建：

```text
bytes  = 2,189,312
sha256 = 1C6D87DD30692DCC12221FF79293C54399F3FA89425679F8199BABDAE7EE4075
```

InputHost/HIDMaestro/WinRT 与锁定 HC runtime 已由 package manifest 逐项记录并在 ZIP 内复核；standalone 测试包包含锁定 `PowerControl/redist/HidHide_1.5.230_x64.exe`，正式 Release/updater 未被修改。

## 2. 本轮测试目的

这次不是重复验证“是否有 gyro 数据”，而是验证 P106 的 native 自己能否记录：

```text
hc-motion-selector-receipt
dmiManufacturer / dmiProduct / dmiSystemProduct
hcSelectorId = asus-rc73xa
hcDeviceClass = HandheldCompanion.Devices.XboxROGAllyX
hcGyroMatrixId / hcAccelMatrixId
providerBindingProven = false
hostOutputAdmitted = false
```

仍保留 raw/candidate 两套诊断值；不会把 candidate 矩阵送入 Host，不会解除 pair gate，不会改变 P-HID/P-XINPUT/P-OWNER。

## 3. 用户操作

在 ROG Xbox Ally X RC73XA 上：

1. 解压整个 ZIP。
2. 运行 `Start-YeManCC.cmd`，由用户自己打开 YMCC 陀螺仪页并启用右摇杆模拟。
3. 运行 `Start-ROG-Background-Sampler.cmd 15`（15 秒即可，避免再次产生过大包）。
4. 采样期间保持一次静止即可；无需重复大幅转动，也不要安装/启动 HidHide。
5. 把桌面生成的 `YeManCC-ROG-Input-Gap-Evidence-*.zip` 原样回传。

采样器仍为后台只读模式：不启动/关闭 YMCC、不改 ReportInterval、不调用 OEM Disable、不创建虚拟手柄、不操作 Steam 或游戏。

## 4. 当前仍保持的安全状态

```text
T11 = PAIR-UNPROVEN / SAFE-ZERO
T12 = DIRECT-IMU-NO-REPORT
T15/T16/T17/T18 = RUNTIME-BLOCKED
runtimeUpgrade = false
```

PS4 低电量提示继续按低优先级 presentation issue 记录，不是本次包的阻塞项。

