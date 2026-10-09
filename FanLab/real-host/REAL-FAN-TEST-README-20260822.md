# YeManFanHost 真机检测包（P0 修复版）

本包是独立 Fan Host 的真机检测入口。它不会修改 Handheld Companion，也不会修改 YeManCC 主程序。默认 `handshake` 只读取设备身份和 HC 路由，不执行 `Open()`、EC/ACPI/HID 写入；只有明确使用 `-Mode write`、管理员 PowerShell、授权文件和确认口令，才会进入真实写入路径。

## 1. 准备

1. 只在已授权的 ROG Xbox Z2E/ROG Xbox Ally X 机器上操作；台式机只运行握手模式。
2. 关闭 Armoury Crate、G-Helper、FanControl、HWiNFO 控制、NBFC、HandheldCompanion 等可能接管风扇的程序。
3. 确认本机安装 **.NET Windows Desktop Runtime 10 x64**。若未安装，脚本会显示官方下载地址并停止。
4. 用管理员 PowerShell 解压本包并进入目录：

```powershell
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\YeManFanHost-real-framework-dependent-20260822-p0.zip" -DestinationPath C:\FanLab\YeManFanHost-real-p0 -Force
Set-Location C:\FanLab\YeManFanHost-real-p0
```

## 2. 只读握手（先做这一项）

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode handshake
```

合格条件：输出中的 `ok=true`、`hardwareWritesEnabled=false`、`hardwareWritesObserved=false`，并且 ROG 机器的 `deviceClass` 为 `HandheldCompanion.Devices.XboxROGAlly` 或 `HandheldCompanion.Devices.XboxROGAllyX`。握手失败、设备类型不匹配、依赖缺失都停止，不得继续写入。

## 3. 真实写入（仅在握手合格后）

仍在管理员 PowerShell 中执行：

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 `
  -Mode write `
  -Authorization .\AUTHORIZATION-BATCH13-ROG-XBOX-HARDWARE-20260819.md `
  -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST
```

脚本只提交一条单调四节点曲线，等待后通过 Host 的 OEM restore/readback 路径恢复，并记录 `output\real-fan-test\session-summary.json`。如果恢复未确认，脚本**不会强制结束 Host**，以保留恢复重试通道。

## 4. 恢复失败时的人工保底

脚本会把本次会话令牌保存到 `output\real-fan-test\session-token.txt`。只有脚本明确提示恢复未确认时才使用下面命令；不要自行结束 Host 或重启机器：

```powershell
$token = (Get-Content .\output\real-fan-test\session-token.txt -Raw).Trim()
$headers = @{ 'X-YeMan-Fan-Session' = $token }
Invoke-RestMethod http://127.0.0.1:8773/api/state -Headers $headers
Invoke-RestMethod http://127.0.0.1:8773/api/restore -Method Post -Headers $headers -Body '{}' -ContentType 'application/json'
Invoke-RestMethod http://127.0.0.1:8773/api/close -Method Post -Headers $headers -Body '{}' -ContentType 'application/json'
```

只有返回的 state 同时满足 `unknownState=false`、`oemRestoreConfirmed=true` 后，才可以请求退出。`hcRestoreCallbackReturned=true` 但 `oemRestoreConfirmed=false` 只表示 HC 的 void callback 返回，仍属于物理 OEM 未确认，禁止退出或继续写入：

```powershell
Invoke-RestMethod http://127.0.0.1:8773/api/shutdown -Method Post -Headers $headers -Body '{}' -ContentType 'application/json'
```

如果状态仍未知，保留 Host 运行并把 `session-summary.json`、Host 控制台日志和完整错误返回给开发者；不要使用 `Stop-Process -Force`。

## 5. 证据

本包不包含 HC 的非必要语言目录；只带 Host 运行时、WinRT 运行库、固定版本 HC 程序集及其必需 native 依赖。HC 程序集哈希仍须与授权记录一致。所有实际写入结果都以 `session-summary.json` 和 Host 返回的 `oemRestoreConfirmed`、`oemPhysicalOwnershipConfirmed`、`unknownState` 为准；callback-only 不能作为 OEM 物理恢复证据。
