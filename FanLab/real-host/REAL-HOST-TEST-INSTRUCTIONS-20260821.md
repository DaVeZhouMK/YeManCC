# YeManFanHost 真实设备测试包说明

## 当前可测试内容

当前 YeManFanHost-real-gated 包已经包含：

- 常驻 YeManFanHost.exe；
- HC 0.32.3.2 及完整运行时依赖；
- GPD Win5 与 ROG Xbox factory Gate；
- HC Open/OpenEvents；
- GPD SetFanDuty 温度曲线调度；
- ROG Xbox PowerProfileManager_Applied 曲线路由；
- Batch 03 全部 70 个已映射 HC 风扇 factory 的自动握手识别；
- 通用 `SetFanDuty`、`GenericRpmTarget`、ROG 曲线和 Claw/Legion 表格曲线的 route dispatch；
- lease、heartbeat、OEM restore、release、close；
- 真实握手模式和显式写入模式。

主程序仍保持 FAN_REAL_HOST_ENABLED=false，因此本测试包由脚本直接启动，不会修改主程序安装目录。

## 框架依赖版（当前推荐）

当前精简包不携带 .NET 运行时。目标机需要系统已安装 `Microsoft.WindowsDesktop.App 10.x x64`；测试脚本会自动检查，缺失时打开官方安装地址并停止：

    https://dotnet.microsoft.com/download/dotnet/10.0/runtime

HC 语言 satellite、logs、output、cache 不属于正式 Host 包。Host 仍保留 HC 的真实依赖闭包；不要按文件名继续删除 DLL。

## 先做只读握手

在目标 Win5 或 ROG Xbox 上，使用管理员 PowerShell，进入解压目录后执行：

    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode handshake

这一步会加载 HC 并执行 IDevice.GetCurrent()，但不会调用 Open()、OpenEvents() 或任何风扇写入。

握手成功只表示设备已被 HC 风扇矩阵识别，不会启用控制。只有当结果中同时满足以下条件，才可以进入写入测试：

    ok: true
    supported: true
    Batch 03 已映射 factory：fanRoute 不为空，supported=true
    hardwareWritesObserved: false

当前随包的真实写入授权记录仍只有已完成 Batch 12 Win5 与 Batch 13 ROG Xbox；其他已映射设备会自动握手并保持 `real-hc-read-only`，不会因为“已映射”而盲写。后续只需增加全局真实写入授权，不需要再改设备识别 Gate。

## 写入测试

先关闭 Armoury Crate、G-Helper、FanControl、HWiNFO 控制、NBFC、RWEverything 等 Fan/EC 工具。保持 AC 电源，不执行睡眠/唤醒测试。

Win5 使用对应的 Batch 12 授权记录：

    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode write -Authorization .\AUTHORIZATION-BATCH11-TO-BATCH12-20260819.md -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST

ROG Xbox 使用对应的 Batch 13 授权记录：

    powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File .\run-real-fan-test.ps1 -Mode write -Authorization .\AUTHORIZATION-BATCH13-ROG-XBOX-HARDWARE-20260819.md -Confirm I-CONFIRM-YEMAN-REAL-FAN-TEST

脚本只应用一次受约束四节点曲线：

    0°C → 0%
    40°C → 20%
    60°C → 45%
    100°C → 90%

保持约 5 秒后，自动执行 OEM restore → release lease → Close。脚本不会执行睡眠/唤醒，也不会把 Host 安装到 C:\SOFT\YeMan\PowerControl\。

结果文件：

    .\output\real-fan-test\session-summary.json

如果握手 Gate 不通过、Open 失败、温度不可用、lease 失效或 OEM restore/Close 未确认，脚本退出失败并停止后续操作；不要重复写入测试，先提交 session-summary.json。

## 当前剩余门槛

- ROG Xbox Ally X 的只读握手已通过：`XboxROGAllyX`、`ProfileCurve`、`fanRouteCount=70`、无硬件写入；
- 台式机已通过安全自测和写入命令的阻断回归，因设备未映射而不会进入 Open/写入；
- v3 已修复首次 `Open()` 的状态机 409：授权 Gate 通过后才进入 HC `Open()`，并保留 OEM restore/Close 回滚路径；
- ROG Xbox Ally X v3 真机写入已通过：Open、OpenEvents、lease、曲线写入、5 秒读回、OEM restore、release、Close 均成功；最终 `unknownState=false`；
- 睡眠/唤醒、第三方冲突长时运行和主程序正式启用仍不在本次首次写入测试内。
