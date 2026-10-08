# T22 BUS-P128：HC 风格精确实体可见性事务部署

Date: 2026-09-06（任务序列；本机文件系统部署目录按本地时钟生成了 `20260907` 名称）

Status: `DEPLOYED / STARTUP-SMOKE-PASS / P-HID-ARMED-BUT-NOT-TRIGGERED / P-XINPUT-P-OWNER-UNENCLOSED`

## 1. 本轮范围

本轮只部署当前已经构建的主线 native/WebView 组合到正式本机安装目录：

```text
G:\YeManCC-Work\Mainline\Build\App\Native
→ C:\SOFT\YeMan\YeManCC
```

部署前先关闭了正在运行的 `YeManCC.exe`（旧 PID `34876`），随后使用受管脚本
`tools/deploy-installed.ps1` 完成备份、资源替换和安装后 hash 对账。没有修改正式
Release/updater 目录，没有安装或启动 `HidHide_1.5.230_x64.exe`，没有执行 OEM
Disable，也没有操作 Steam、游戏或 ROG 设备。

## 2. 安装工件证据

部署脚本返回：

```text
DEPLOY_OK
Installed: C:\SOFT\YeMan\YeManCC
Backup:    C:\SOFT\YeMan\YeManCC\.deployment-backup-20260907-010216
Entry:     assets/index-YZE4PzvL.js
```

| 工件 | bytes | SHA-256 |
|---|---:|---|
| `native/main.cpp`（当前源码） | 1,152,736 | `F4A329A19616A5648C7FA628FF478C266EAD2BF24877871BD158DDFCE08E9F9B` |
| `Build/App/Native/YeManCC.exe` | 2,330,624 | `C6756AF22EB19870BFDC03BEC615438EE4818191724B3E003F9EB95760F7022B` |
| `C:\SOFT\YeMan\YeManCC\YeManCC.exe` | 2,330,624 | `C6756AF22EB19870BFDC03BEC615438EE4818191724B3E003F9EB95760F7022B` |
| `Build/App/Native/YeManRecoveryService.exe` | 282,112 | `F1014758A047AAC5A1F16D416EE78DD199FC051434F744733BA71C90EB3B1D19` |
| `C:\SOFT\YeMan\YeManCC\YeManRecoveryService.exe` | 282,112 | `F1014758A047AAC5A1F16D416EE78DD199FC051434F744733BA71C90EB3B1D19` |

## 3. 启动回归

部署后启动安装版，取得：

```text
PID=18564
Alive=true
Responding=true
```

该实例随后按 `window-destroy(exitRequested=true)` 正常退出，不是崩溃。为交付测试，
已重新启动同一已部署二进制；当前交互实例为 `PID=13844 / Alive=true / Responding=true`。

`C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log` 持续记录
`gamepad-fallback-tick`，没有启动崩溃或新的 `input-capture.worker-exception`。

当前持久配置仍是默认关闭虚拟目标：

```text
persona=dualshock4
buttonMappingEnabled=false
gyroEnabled=false
gyroMotion.enabled=false
outputMode=disabled
```

因此本次启动只证明安装版可运行；没有进入 `inputHostStart()` 的虚拟目标准入，
没有执行本轮新增的精确物理 identity → HidHide base-container/instance 隐藏事务。
不能把本次启动写成实体手柄已隐藏或 Steam/game 已隔离。

## 4. 静态回归

以下命令均返回 `exitCode=0`：

```text
pnpm run type-check
pnpm run test:physical-visibility-transaction
pnpm run test:rog-hidhide-contract
pnpm run test:rog-visibility-topology
pnpm run test:button-mapping-runtime-readiness
```

对应工件：

- `Build/Validation/GyroVirtual/T13-E01-physical-visibility-transaction-mock-20260904.json`
- `Build/Validation/GyroVirtual/T13-E02-rog-hidhide-static-contract-20260906.json`
- `Build/Validation/GyroVirtual/T18-E01-rog-visibility-topology-static-audit-20260906.json`
- `Build/Validation/GyroVirtual/BUS-P48-button-mapping-runtime-readiness-static-20260905.json`

## 5. 明确缺口

`tools/verify-yemancc-export.ps1` 的只读结果为 `BLOCKED`，原因不是安装目录，
而是以下两个发布位置仍保留旧二进制（`2,240,000` bytes、hash
`218D293E...`）：

```text
G:\YeManCC-Work\Mainline\Build\Package\UpdateRoot\YeManCC\YeManCC.exe
G:\YeManCC-Work\Mainline\Release\YeManCC\YeManCC.exe
```

本轮用户请求是部署本机安装目录，不等同于重新生成发布包；因此没有擅自覆盖这两个
目录。若要把本轮 source fix 发布给另一台 ROG，下一步必须另行执行 package/release
并重新做 updater exclusion、完整包 hash 和安装回归。

仍未闭口：

```text
P-HID runtime before/after + exact restore receipt
P-XINPUT consumer observation
P-OWNER / Steam / game isolation
DS4 HID descriptor/report readback
关闭、失焦、睡眠、断开、Host crash 的恢复矩阵
ROG 实机证据
```

## 6. 下一介入点

用户在已启动的安装版中手动打开控制器页，启用 `DualShock 4` 虚拟目标（暂不必开
陀螺仪），记录同一实例的 `native-lifecycle.log`，并独立观察：

1. 实体 Xbox/ROG 是否从 Steam/游戏消费者消失；
2. 虚拟 `Wireless Controller` 是否仍可见并接收按钮/轴；
3. 关闭虚拟目标后实体手柄是否恢复；
4. HidHide before/after 与 restore readback 是否精确回到部署前快照。

这些用户动作完成前，P128 只能保持 `P-HID source-level deployed`，不得升级为
`P-XINPUT`、`P-OWNER` 或 Steam/game isolation closed。
