# YeManFanHost 外部真机测试包

本包是框架依赖版。Host 在 `fan-host` 子目录，测试脚本和授权文件在包根目录；
不要把根目录文件复制进 `fan-host`，否则 manifest 会按设计拒绝启动。

前置条件：目标机安装 `Microsoft Windows Desktop Runtime 10 x64`，并关闭
Armoury Crate、G-Helper、FanControl、HWiNFO 控制、NBFC、HandheldCompanion
等其他风扇/EC 工具。不要在测试过程中睡眠或唤醒机器。

## 只读握手

使用管理员 PowerShell：

```powershell
New-Item -ItemType Directory -Force -Path C:\FanLab | Out-Null
Expand-Archive -LiteralPath "$env:USERPROFILE\Downloads\YeManFanHost-real-framework-dependent-20260823-test.zip" -DestinationPath C:\FanLab\YeManFanHost-real-test -Force
Set-Location C:\FanLab\YeManFanHost-real-test
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode handshake
```

脚本会先对 `fan-host` 执行 manifest/hash/ACL 预检，再启动 Host。握手不会调用
`Open()`、`OpenEvents()` 或任何风扇写入。

必须先看到：`ok=true`、`supported=true`（或明确的只读不支持结果）、
`hardwareWritesEnabled=false`、`hardwareWritesObserved=false`。如果出现依赖、ACL
或身份错误，停止并把输出和 `output\real-fan-test\session-summary.json` 发回。

## ROG Xbox 写入测试

只有只读握手确认目标为 `XboxROGAlly` 或 `XboxROGAllyX` 后，才执行：

```powershell
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode write -Authorization .\AUTHORIZATION-BATCH13-ROG-XBOX-HARDWARE-20260819.md -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST
```

脚本只提交一条受约束曲线，随后执行 OEM restore、release、close。恢复未确认时
不会强制结束 Host；请保留进程并回传完整输出、`session-summary.json` 和 Host 日志。

## Win5 写入测试

将 `-Authorization` 换为：

```text
AUTHORIZATION-BATCH11-TO-BATCH12-20260819.md
```

结果目录：`output\real-fan-test\`。本包不包含 .NET Runtime，也不修改主程序安装目录。
