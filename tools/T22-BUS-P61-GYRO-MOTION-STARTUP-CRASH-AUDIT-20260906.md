# T22-BUS-P61：`gyro-motion` 启动相关性与 worker 异常边界（2026-09-06）

## 证据输入

| 项目 | 值 |
|---|---|
| ROG IMU ZIP | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC-ROG-IMU-Evidence-20260906-004029.zip` |
| ZIP bytes / SHA-256 | `6,843` / `A961755151C9B3F9E48BD959B8CE3837828AB96610A1F3B2F7CC9D6DFC677090` |
| 日志 RAR | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC.rar` |
| RAR bytes / SHA-256 | `105,153` / `74EDE9B3B0A141D6323AEDCF3E9B7AE98BA483B4A58ABE51D0A85C50CD84BB05` |

两个包均只读解包到 BUS 临时审计目录，没有覆盖用户文件。

## Fact：附件没有记录闪退

ZIP 中的 `rog-imu-evidence.json` 显示：

- `durationSeconds=30`，`packageRoot=C:\SOFT\YeMan`；
- `startsYeManCC=true`、`launch.error=null`；
- runtime identity 明确包含 `PowerControl/feature-assets/gyro-motion/enabled.flag`、`input-capture.flag`，以及锁定 HC `GamepadMotion.dll`/`HandheldCompanion.dll`；
- 设备身份为 `ROG Xbox Ally X RC73XA_RC73XA`，HC 对应 baseboard product 为 `RC73XA`；
- capture 记录 `gyroSensor=true`、`accelSensor=true`、`pairProven=false`、`matrixIdentity=unresolved-rog-pid-family/raw`，符合当前 fail-closed 合同。

ZIP 与 RAR 的 `native-lifecycle.log`/`input-capture.jsonl` 内容 hash 一致。日志只出现 boot、window-created、recovery-service-started、ROG HID bind、WebView2 navigation/render-ready 和输入采样；没有 `browser-process-failed`、`gpu-process-exited`、`recovery-exhausted`、`window-destroy`、`exit` 或 crash dump/WER。两包不能证明“`gyro-motion` 存在时必闪退”，反而证明至少有一次 `gyro-motion` 路径存在且成功启动。

## Fact：目录存在会开启额外 native worker

当前 native 源码中：

```text
inputCaptureStart()
  → inputCaptureAssetsPresent()
  → 同时存在 feature-assets/gyro-motion 与 feature-assets/virtual-gamepad
  → CreateThread(inputCaptureThreadProc)
```

目录缺失/改名时，`inputCaptureStart()` 直接记录 skipped，不创建 worker；目录存在时，worker 延迟 2.5 秒后执行 `CoInitializeEx → inputCaptureInit → inputHostStart → capture loop`。因此“改名后不闪退”只能先确定为“绕过了这条额外路径”，不能单独证明是哪一个子调用出错。

## 已修复的确定性稳定性缺口

原 `inputCaptureThreadProc` 没有总异常边界。`inputCaptureInit`、`inputHostStart`、`inputHostConsumePowerRequests` 或 `inputCaptureOnGamepad` 若抛出 C++ 异常，会越过线程入口；在 Windows C++ 线程中这可能转为未处理异常并结束进程。这个缺口与 HC 参数/矩阵无关，且正好解释为什么改名目录会让问题消失（worker 不再启动）。

已在 `native/main.cpp` 做最小修正：

- worker 入口标为 `noexcept` 并捕获标准/非标准异常；
- 记录 `input-capture.worker-exception`、阶段、错误文本和 `safeStop=true`；
- 异常路径执行不抛出的 sensor/GamepadMotion/Host 清理；
- 不改变 HC 轴、倍率、阈值、配对、matrix、owner、HidHide、ROG OEM 或 Steam/game 语义。

该修正只能防止“未处理 C++ 异常导致进程结束”，不能捕获所有 native DLL/SEH access violation；如果仍有闪退，需要同刻的进程退出码和 dump/WER 才能继续定位。

## Fact：构建与新测试包

native 构建命令 `cmd.exe /d /c native\\build_native.bat` 返回 `BUILD_OK`。随后生成完整 ROG 测试包：

```text
ZIP      = Mainline/Build/TestPackages/GyroInput-ROG-20260906-005527/YeManCC-GyroInput-ROG-Test.zip
bytes    = 85,229,012
SHA-256  = 6278D607536A9626AE488B16FE3B7DE4F672520BB8A4C84260EE1646DA153ECE
YeManCC = 2,153,984 bytes / 6D3530C4C6AFC7060ECB522048E40CE759931BD73A621D0C1D5DADC04DD77AB1
HC       = 0.32.4.0 / 06c0b954 / 91 files / mismatch 0
defaultDurationSeconds = 30
formalUpdaterPackageTouched = false
systemMutationDuringBuild   = false
```

该包同时保留 `gyro-motion` 与 `virtual-gamepad`，用于直接验证“正常目录存在”的路径；运行包内 `Start-YeManCC-GyroInput-Test.cmd` 后，若 worker 发生可捕获异常，日志应出现 `input-capture.worker-exception`，而不是直接使 YeManCC 无记录退出。

## Error / Unknown

- 用户描述的“正常 `gyro-motion` 必闪退”尚未被附件日志复现；当前证据只支持路径相关性。
- 当前修复尚未在 ROG 实机上验证；不能把 build success 当作运行时闭口。
- 若闪退是 WebView2 或第三方 DLL 的 SEH `0xC0000005`，C++ `catch (...)` 可能不会捕获；仍需 crash-time `native-lifecycle.log`、`webview-failures.log`、`recovery-service.log`、Crashpad/WER。
- `pairProven=false`、safe-zero 和 `matrixIdentity=unresolved-rog-pid-family/raw` 仍是 HC 对齐的安全门，不得为了绕过闪退而放开。

## 当前裁决

```text
HC 参数/矩阵/单位偏移       = 未发现新的无歧义偏移
gyro-motion 目录相关性      = 已确认会开启 capture worker
worker 未处理 C++ 异常       = 已修复（source/build verified）
附件是否证明闪退            = 否
ROG runtime closure         = 仍待实机复测
T11–T13/T15–T18             = UNENCLOSED / RUNTIME-BLOCKED
runtimeUpgrade              = false
```

