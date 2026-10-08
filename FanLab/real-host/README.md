# YeManFanHost（常驻 Fan API Host）

这是主程序 Fan API 的独立常驻进程实现。它不是一次性硬件测试程序，主程序可以通过本机回环 HTTP 连接它；`--parent-process YeManCC` 或 `--parent-pid` 用于主程序退出监护。

## 构建模式与安全边界

同一个 Host 有两种明确隔离的启动模式：

- 默认 `safe-no-hardware`：不加载 HC/native 依赖；所有硬件标志为 false，适合桌面机和主程序回归。
- 显式 `--real-backend`：只从随包的 pinned HC 依赖加载真实 `IDevice.GetCurrent()`，先执行 Batch 03 的 70 个已映射风扇 factory 路由 Gate。未同时提供授权记录、确认口令和 `--allow-hardware-writes` 时仍是 `real-hc-read-only`，不会 `Open()` 或写风扇。

真实写入会按识别到的 HC 原路线分派：通用设备使用 pinned HC 的 `FanProfile` 与 `CPUTemperatureChanged` 事件，事件处理顺序与 HC `PowerProfileManager` 完全一致（`SetTemperature` → `GetFanSpeed` → `IDevice.SetFanDuty`）；ROG/表格设备直接进入 pinned HC 的 `PowerProfileManager_Applied` 覆盖实现。Host 不再自定义 1 秒插值写入定时器，也不复制任何厂商寄存器/ACPI 算法。HC 初始化按原版执行 `MotherboardInfo.Collect` → `IDevice.GetCurrent` → `PullSensors` → `Initialize`，然后才允许授权会话 `Open/OpenEvents`。管理员 PowerShell、无冲突风扇工具、全局写入授权记录和一次性确认口令仍是硬条件。写入结束固定执行 OEM restore → release lease → Close；失败会进入 `FaultLocked`，保留 Host 与 lease 等待重试，绝不假装已停止。

当前主程序已接入可逆的真实 Host Gate；源码载荷输入为 `PowerControl\fan-host`；正式发布与运行包中的 `PowerControl\fan-host-v2` 使用框架依赖单文件 Host，主程序仍会在握手成功后才显示导航，并在用户明确开启控制时执行写入。将任一主程序 Gate 设回 false 即可停止 Fan UI/Host 调用，不删除已发布文件。

## 编译与桌面机验证

```powershell
dotnet publish .\FanLab\real-host\YeManFanHost.csproj `
  -c Release -r win-x64 --self-contained true `
  -p:PublishSingleFile=true `
  -p:IncludeNativeLibrariesForSelfExtract=true `
  -o C:\SOFT\YeManCC-Work\Build\Validation\fan-host-real-test-20260821\YeManFanHost-safe

& C:\SOFT\YeManCC-Work\Build\Validation\fan-host-real-test-20260821\YeManFanHost-safe\YeManFanHost.exe --self-test
```

启动常驻 API（仍为安全模式）：

```powershell
& .\YeManFanHost.exe --parent-process YeManCC --protocol-version 2
```

只监听 `127.0.0.1`。可用 `GET /health`、`POST /api/handshake`、`GET /api/state` 检查；停止时按 OEM restore → release → close 顺序清理内存状态。Host 的 `SystemEvents.PowerModeChanged` 回调只排入后台串行队列并立即返回，绝不在系统电源事件上等待 HC/EC 或阻止睡眠；Suspend/Resume 实际处理在后台执行且可与 HTTP/parent-watchdog 恢复幂等竞合。

如果主程序关闭后仍显示硬件接管，不要强制杀 `YeManFanHost.exe`；保留当前会话和日志，先核对当前 Host 身份与恢复状态，不得改用旧 `PowerControl\fan-host` 实例。

**手工应急工具的已知独立缺口（本轮只读核对）：** 主程序正常会话把令牌放在 `app.fanStateDir()` 返回的状态目录（通常为 `%LOCALAPPDATA%\YeManCC\fan-host`），而冻结随包的 `run-emergency-fan-restore.ps1` 只在脚本同目录找 `YeManFanHost.session`。因此不能把“直接在正式 `fan-host-v2` 目录执行该脚本”列为已验收的应急入口；缺少令牌时它会拒绝且不发送请求。不要为了运行该历史工具把令牌复制到受保护的代码目录，或回退启动旧 Host。修正此入口需要独立重新冻结/绑定载荷并重测，本次保持用户已验证的风扇文件字节不动，也没有执行应急请求。

该历史脚本当前的 `Test-RestoreConfirmed` 实际要求 `oemRestoreConfirmed=true` 且 `unknownState` 不为真，并非所有厂商都可独立证明 `oemPhysicalOwnershipConfirmed=true`。它不能替代当前设备的 OEM 恢复验收；409、超时或状态未知均不能作为允许强杀的依据。

## 真机测试入口

把 `YeManFanHost-real-gated` 整个目录复制到目标设备，在管理员 PowerShell 中运行同目录的 `run-real-fan-test.ps1`。先运行 `-Mode handshake`；只要 HC 返回 Batch 03 已映射的 factory，`supported=true`，否则不启用。握手本身不会 Open 或写入。完整命令和当前批次授权文件见 `REAL-HOST-TEST-INSTRUCTIONS-20260821.md`。

本机桌面复核结果为 `HandheldCompanion.Devices.ASUS.ASUS`，因此只读握手成功但 `supported=false`；没有调用 `Open/OpenEvents`，硬件写入标志保持 false。

2026-08-22 的路线审计记录仍保留在 `FAN-ROUTE-RESTORE-MANAGER-ISOLATION-20260822.md`，其中关于“关闭阶段不调用 `IDevice.Close()`”的结论属于当时的旧实现，已由当前生命周期实现取代。当前实现严格按 HC 的设备会话顺序执行：授权后 `DeviceManager.Start → IsReady → IDevice.Open → IDevice.OpenEvents`；交回 OEM 先走 HC `PowerProfileManager_Applied(FanMode.Hardware)`，再调用对应设备的真实 `IDevice.Close()`，最后在进程关闭边界停止 `DeviceManager`。睡眠边界保留 `DeviceManager`，与 HC 的 `SuspendWithOS=false` 一致。AYANEO CEii、Ayn Loki、GPD Win4、SteamDeck 等路线均只通过 HC 自身回调/资源路径，不复制厂商寄存器算法；跨厂商没有 HC 统一的物理 OEM ownership readback，Host 只报告 HC 回调返回或路线专用证据，不能把它伪装成通用硬件确认。
