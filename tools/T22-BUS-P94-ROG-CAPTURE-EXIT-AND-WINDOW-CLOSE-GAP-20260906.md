# T22 BUS-P94：ROG 采集器退出链与窗口关闭生命周期缺口（2026-09-06）

状态：`SOURCE-FIXED-TEST-LANE / NATIVE-INSTRUMENTED / PACKAGE-PASS / LIFECYCLE-UNPROVEN`

## 1. 用户回传与范围

用户回传的手动证据包：

```text
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-113447.zip
sha256 = 66BAF7F9F887E997AF2855697906E14894065DC43BE9D811B9940EB057F0A699
```

用户观察：

```text
运行测试 CMD 不会正常结束；窗口只显示 standalone package header。
关闭 YeManCC.exe 主窗口后，Steam/系统又出现 PS4 Controller。
```

本条只处理测试采集器的确定性退出问题，以及关闭现象的源码/证据边界；不把用户现象直接归因到 HC、HIDMaestro、Steam 或某一个 battery byte。

## 2. 已证实的采集器缺陷与修正

### C-94-01：子 collector WaitForExit/ExitCode 竞态

旧路径：

```powershell
[void]$imuProcess.WaitForExit(30000)
$imuProcess.ExitCode
```

忽略 `WaitForExit()` 返回值，并在子进程尚未退出时读取 `ExitCode`；外层脚本可能异常退出，无法写出 `rog-input-gap-evidence.json`、manifest 和 ZIP。

修正：

```text
bounded WaitForExit(30000)
→ waitStatus=exited | timeout-terminated | start-failed
→ 仅在 HasExited 时读取 exitCode
→ 超时只终止本次 collector 子进程，不触碰 YeManCC.exe
→ 将 stdout/stderr/start/kill error 写入结果
```

### C-94-02：PowerShell 全量读取滚动 JSONL 导致高内存假死

旧 `Save-BoundedTextEvidence` 虽然最终只输出 800 行，但先用 `Get-Content` 读取整份 YMCC JSONL。实测 5 秒本机回归时，外层 Windows PowerShell 工作集升至约 600 MB–1.5 GB，且 `rog-input-gap-evidence.json` 尚未写出。

修正：

```text
≤1 MiB：有限文本读取
>1 MiB：只读取 64 KiB 头窗口 + 64 KiB 尾窗口
→ 输出受限 excerpt
→ 原始 bytes/SHA-256 保留
→ 大文件 originalLineCount 标为 unknown-large-file，不伪造行数
```

### C-94-03：CIM/PowerShell 对象直接序列化导致循环引用

锁定的 Windows PowerShell 5.1 路径会把某些 CIM/Process 派生属性保留为 `PSParameterizedProperty` 或 `RuntimeModule`。整棵根 dictionary 直接序列化会失败。

修正：

```text
PnP identity 只保留字符串化的 PNPDeviceID/VID_PID token
launch/process fields 显式 cast 为 string/bool/int/null
JavaScriptSerializer 按 top-level field 独立序列化后拼接 JSON
```

## 3. 本机回归证据

命令：

```text
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File
  PowerControl/feature-assets/gyro-motion/Capture-ROG-Input-Gap-Evidence.ps1
  -DurationSeconds 5 -SkipLaunchYeManCC
```

回归结果：

```text
exitCode                 = 0
elapsed                  ≈ 7.3 s
rog-input-gap-evidence   = written
manifest                 = written
ZIP                      = written
imuCollector.waitStatus  = exited
imuCollector.exitCode    = 0
```

验证目录：

```text
Mainline/Build/Validation/InputGapScriptRegression-20260906-final2
```

默认 20 秒路径也已回归：

```text
目录：Mainline/Build/Validation/InputGapScriptRegression-20260906-20s
exitCode = 0
elapsed ≈ 22.6 s
imuCollector.waitStatus = exited
imuCollector.exitCode = 0
rog-input-gap-evidence.json / manifest / ZIP = written
large JSONL = head/tail excerpt, original bytes/hash retained, line count unknown-large-file
```

该回归只证明脚本/文件/压缩链路，不证明 ROG provider、DS4 raw report、Steam consumer 或生命周期外部 release。

## 4. 新测试包

```text
目录：G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-1218
ZIP：  G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-1218\YeManCC-GyroInput-ROG-Test.zip
bytes：88,557,040
sha256：1CBAD0DA0CD14E1A256E30A5A7FE4391789359B2AEE7C1E9EE4959F4FA5FB2D7
```

包内含当前 YeManCC、InputHost、锁定 HC/HIDMaestro/WinRT/HidHide 测试资产与修正后的 collector。正式 Release/updater 包未改动。

<!-- The next legacy line was malformed by an earlier write; it is retained only as hidden audit residue. -->

## 5. 本轮 native 只读埋点与新包证据（2026-09-06）`r`n`r`n本轮对 `native/main.cpp` 只增加生命周期可观测性，不改变关闭、托盘、Host release 或设备策略：`r`n`r`n```text`r`ninputHostStopLocked(): input-host-stop-request`r`ninputHostStopLocked(): input-host-release-receipt`r`ninputHostStopLocked(): input-host-stop-result`r`nWM_CLOSE:                window-close-request`r`n``` `r`n`r`nnative build：`r`n`r`n```text`r`n命令：native\\build_native.bat`r`n结果：BUILD_OK`r`nEXE bytes：2,168,832`r`nEXE sha256：2F6518747E376DB437E3C8FA739E668BAECC562DF6BBA1235011DC4A8B476454`r`n``` `r`n`r`n新 standalone 测试包：`r`n`r`n```text`r`n目录：G:\\YeManCC-Work\\Mainline\\Build\\TestPackages\\GyroInput-ROG-20260906-1227`r`nZIP：G:\\YeManCC-Work\\Mainline\\Build\\TestPackages\\GyroInput-ROG-20260906-1227\\YeManCC-GyroInput-ROG-Test.zip`r`nbytes：89,757,265`r`nsha256：92C7A35E3A8A40795F9D76E83C220C67A51A9F5B7A333C033DE22EB712CDAE1D`r`nHC runtime：HC-CANDIDATE-0.32.4.0-06c0b954-20260902`r`nformal Release/updater touched：false`r`n``` `r`n`r`n包体包含上述新 EXE；二进制字符串中可检索到四个新增事件名。用包内 collector 以 `-DurationSeconds 5 -SkipLaunchYeManCC` 回归，结果为 `exitCode=0`、`imu-child-wait-result exited=True`，并写出 evidence JSON、manifest 与 ZIP（目录：`Mainline\\Build\\Validation\\PackagedInputGapRegression-20260906-1227-5s`）。该证据仍只闭合测试 lane，不证明真实 ROG/DS4/Steam 生命周期。`r`n`r`n## 6. 关闭后 PS4 出现：三方边界

-->

## 5. 本轮 native 只读埋点与新包证据（2026-09-06）

本轮对 `native/main.cpp` 只增加生命周期可观测性，不改变关闭、托盘、Host release 或设备策略：

```text
inputHostStopLocked(): input-host-stop-request
inputHostStopLocked(): input-host-release-receipt
inputHostStopLocked(): input-host-stop-result
WM_CLOSE:                window-close-request
```

native build：

```text
命令：native\build_native.bat
结果：BUILD_OK
EXE bytes：2,168,832
EXE sha256：2F6518747E376DB437E3C8FA739E668BAECC562DF6BBA1235011DC4A8B476454
```

新 standalone 测试包：

```text
目录：G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-1227
ZIP：G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-1227\YeManCC-GyroInput-ROG-Test.zip
bytes：89,757,265
sha256：92C7A35E3A8A40795F9D76E83C220C67A51A9F5B7A333C033DE22EB712CDAE1D
HC runtime：HC-CANDIDATE-0.32.4.0-06c0b954-20260902
formal Release/updater touched：false
```

包体包含上述新 EXE；二进制字符串中可检索到四个新增事件名。用包内 collector 以 `-DurationSeconds 5 -SkipLaunchYeManCC` 回归，结果为 `exitCode=0`、`imu-child-wait-result exited=True`，并写出 evidence JSON、manifest 与 ZIP（目录：`Mainline\Build\Validation\PackagedInputGapRegression-20260906-1227-5s`）。该证据仍只闭合测试 lane，不证明真实 ROG/DS4/Steam 生命周期。

## 6. 关闭后 PS4 出现：三方边界

### 已证实（FACT）

最新手动 ZIP 的 `imu-ymcc-native-lifecycle.log` 对用户本次 PID `14104` 只包含启动与 ROG 绑定事件，没有：

```text
window-destroy
input-host.stop
release-local-dispose-complete
target-exited
kind=stop
```

当前源码的真实关闭路径仍是：

```text
WM_CLOSE
→ 若 tray/taskbar resident：hide-only，不退出进程
→ 否则 beginAsyncExit
→ WM_APP_EXIT_READY
→ DestroyWindow / WM_DESTROY
→ inputCaptureStop
→ inputHostStop
```

`YeManRecoveryService` 只探测 YeManCC 进程、窗口和 WebView 响应；它不探测 HIDMaestro target、虚拟 DS4 PnP、Steam 或游戏 consumer。

### 高置信候选（不是闭口事实）

```text
用户关闭的是窗口而非进程（tray resident hide-only）
→ YeManCC 仍活着
→ InputHost parent/lease 仍活着
→ virtual DS4 仍可能被枚举
```

另一个候选是 Host release ACK/外部 device-gone 尚未证明；不能仅凭出现 PS4 推导 producer。

### 仍未知 / 缺口

```text
本次实际使用的是窗口关闭、托盘退出还是进程终止
PID 14104 是否完成 PREPARE_TARGET/CREATE
关闭后 InputHost PID、hostInstanceId、epoch 是否仍存活
RELEASE_TARGET/SHUTDOWN 是否收到 ACK
虚拟 DS4 的 PnP/container/descriptor/raw report
Steam 显示的 PS4 是否就是本次 Host session
PS4 5% 是否来自 report、cache、OS 或 Steam
```

因此：

```text
window-hide vs process-exit = HIGH-CONFIDENCE-CANDIDATE
Host local release         = UNPROVEN
external device-gone       = UNPROVEN
PS4 battery producer       = UNKNOWN / RUNTIME-BLOCKED
```

## 7. 下一次用户介入时只需回传

使用新包根目录的：

```text
Start-YeManCC-GyroInput-Test.cmd
```

不要手动重新打包输出目录。回传桌面生成的完整 ZIP，并保留：

```text
rog-input-gap-evidence.json
capture-last-run.txt
imu-ymcc-native-lifecycle.log
```

若要闭合“关闭后 PS4”缺口，必须在同一次 run/epoch 记录 window-hide/real-exit、InputHost PID/hostInstanceId、RELEASE_TARGET/SHUTDOWN receipt、DS4 PnP/raw report 和 Steam readback。

本条不关闭 T15–T18、`G-TARGET-RELEASE`、`G-CONSUMER`、`G-DS4-BATTERY-PRODUCER`，也不改变 `runtimeUpgrade=false`。native 埋点不是行为修复；在收到同一 run/epoch 的关闭动作、Host receipt、PnP/Steam readback 前，仍保持 `LIFECYCLE-UNPROVEN / RUNTIME-BLOCKED`。

