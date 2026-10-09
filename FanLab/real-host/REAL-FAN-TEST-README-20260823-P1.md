# YeManFanHost ROG 真机测试包（P1 修复版）

本包用于 ROG Xbox Ally / Ally X 风扇路线的最终真机验证。它不修改 Handheld Companion，也不修改 YeManCC 主程序。`handshake` 模式只读取身份和 HC 路线；只有 `write` 模式同时满足管理员权限、授权文件和确认口令时，才会进入真实写入。

## 测试前

1. 仅在已授权的 ROG 掌机上测试；台式机只运行握手模式。
2. 关闭 Armoury Crate、G-Helper、FanControl、HWiNFO 控制、NBFC、HandheldCompanion 等风扇控制程序。
3. 确认已安装 **.NET Windows Desktop Runtime 10 x64**。缺少时脚本会显示官方下载地址并停止。
4. 使用管理员 PowerShell 解压并进入目录：

```powershell
New-Item -ItemType Directory -Force -Path C:\FanLab | Out-Null
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\YeManFanHost-real-framework-dependent-20260823-p1-rog.zip" -DestinationPath C:\FanLab\YeManFanHost-real-p1-rog -Force
Set-Location C:\FanLab\YeManFanHost-real-p1-rog
```

## 第一步：只读握手

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode handshake
```

必须看到：`ok=true`、`hardwareWritesEnabled=false`、`hardwareWritesObserved=false`，且 `deviceClass` 为 `HandheldCompanion.Devices.XboxROGAlly` 或 `HandheldCompanion.Devices.XboxROGAllyX`。不满足时停止，不要运行写入模式。

## 第二步：授权真实曲线写入

只有第一步通过，并确认已关闭冲突程序后，在同一个管理员 PowerShell 执行：

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode write -Authorization .\AUTHORIZATION-BATCH13-ROG-XBOX-HARDWARE-20260819.md -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST
```

脚本提交一条单调四节点曲线，等待 5 秒，读取状态，然后走 OEM restore、释放 lease、关闭。恢复未确认时不会强制结束 Host，会保留恢复通道。

## 结果和保底

结果在 `output\real-fan-test\session-summary.json`，同时回传完整 PowerShell 输出。若提示恢复未确认，不要使用 `Stop-Process -Force`，先把该 JSON 和 Host 日志发回。会话令牌在 `output\real-fan-test\session-token.txt`，仅按开发者指示用于 `/api/state`、`/api/restore`、`/api/close`。

本包为框架依赖版，不包含 HC 非必要语言目录；HC 程序集使用已固定哈希版本。握手和本机自检均不会执行硬件写入。
