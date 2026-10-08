# T22 BUS-P102：真实 YMCC 手动控制 + CMD 后台采样测试包（2026-09-06）

状态：`PACKAGE-PASS / MANUAL-YMCC-CONTROL / BACKGROUND-SAMPLER / USER-RUNTIME-OBSERVATION-RECORDED`

## 1. 用户裁决与测试模式

后续 ROG 采集不再使用“采集器自动启动并控制 YMCC”的全自动模式。测试包固定为：

```text
用户启动真实 YeManCC
→ 用户自行打开陀螺仪页面并启用陀螺仪
→ CMD 仅启动最小化后台 sampler
→ sampler 强制 -SkipLaunchYeManCC
→ 用户自行操作 YMCC / 进行运动 / 关闭
→ sampler 到时写出桌面证据 ZIP
```

采样器仍保持只读边界：不调用 HidHide、不发 ROG OEM feature report、不调 ReportInterval、不创建虚拟设备、不修改 Steam 或游戏。

## 2. 当前测试包

```text
OutputRoot = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-145137
ZIP        = G:\YeManCC-Work\Mainline\Build\TestPackages\GyroInput-ROG-20260906-145137\YeManCC-GyroInput-ROG-Test.zip
ZIP bytes  = 88,565,895
ZIP SHA256 = 8C0ECF5B9A4E39CBD535FC7CEBF34F8C8DE8B85C12F7B6B7F38EEB0CE27ACB43
package result = GYRO_INPUT_ROG_TEST_PACKAGE_OK / exitCode 0
HC runtime = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
formal Release package untouched = true
```

manifest 的 collector 关键字段：

```text
automaticStart=false
userControlsYeManCC=true
backgroundCmdSampler=true
skipLaunchYeManCC=true
defaultDurationSeconds=30
systemMutation.startsPackagedYeManCC=false
```

入口：

- `Start-YeManCC.cmd`：仅启动真实 YMCC。
- `Start-YeManCC-GyroInput-Test.cmd`：启动最小化后台 sampler，不启动 YMCC。
- `Start-ROG-Background-Sampler.cmd`：同步调用后台 sampler 的直接入口。
- `PowerControl/feature-assets/gyro-motion/Start-ROG-Manual-Background-Sampler.cmd`：执行 `Capture-ROG-Input-Gap-Evidence.ps1 -SkipLaunchYeManCC`，日志写入 `capture-manual-last-run.txt`。

## 3. 打包路径修正

首次按默认环境变量打包时，`YEMAN_WORKSPACE_ROOT` 指向工作区根，旧候选路径未找到锁定 `HidHide_1.5.230_x64.exe`，命令退出码为 1。生成器已增加以下只读候选顺序：

```text
<WorkspaceRoot>\Release\PowerControl\redist\HidHide_1.5.230_x64.exe
<projectRoot>\..\..\Release\PowerControl\redist\HidHide_1.5.230_x64.exe
<projectRoot>\PowerControl\redist\HidHide_1.5.230_x64.exe
C:\SOFT\YeMan\PowerControl\redist\HidHide_1.5.230_x64.exe
```

随后 `pnpm run package:gyro-input-test` 成功。该修正只影响 standalone 测试 lane；正式 `package-release.ps1` 与 Release ZIP 的 HidHide 禁止边界不变。

## 4. 用户观察的新增证据

用户报告当前 YMCC 已能观察到“陀螺仪自动模拟右摇杆”。目前没有随该观察提供同一 runId/epoch、raw frame、pair/matrix/calibration 或独立 consumer receipt，因此分类为：

```text
USER-OBSERVED / FUNCTIONAL-SIGNAL-PRESENT / RUNTIME-CORRELATION-MISSING
```

该观察可以作为下一次手动包的定向场景提示，但不能单独关闭 T11/T16 的 pair/matrix/calibration，也不能关闭 T15 UI consumer 或 T18 Steam/game consumer。

## 5. 下一次采集操作

在 ROG 上：

1. 解压完整 ZIP。
2. 运行 `Start-YeManCC.cmd`。
3. 用户自行打开陀螺仪页面并开启自动右摇杆模拟。
4. 运行 `Start-YeManCC-GyroInput-Test.cmd 30`；该 CMD 会立即返回并在最小化窗口后台采样 30 秒。
5. 采样结束后回传桌面 `YeManCC-ROG-Input-Gap-Evidence-*.zip`，并说明右摇杆/轴线图/波形是否同步变化，以及关闭 YMCC 的时刻。

需要的仍是窄范围定向证据：T16 same-provider pair、HC matrix/calibration/first paired sample；T17 DS4 descriptor/raw report/battery；T18 P-HID/P-XINPUT/P-OWNER 与 Steam/game consumer readback。
