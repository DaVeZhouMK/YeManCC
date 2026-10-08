# T22 BUS-P124：旧输入释放闸门的 Button/Axis 生命周期拆分

日期：2026-09-06 23:08（Asia/Hong_Kong）  
状态：`SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / PHYSICAL-SEMANTIC-RECEIPT-PENDING`

## 1. 明确根因

P123 已修正“连接时统一 neutral barrier”对 A/B/LB/RB 的阻断，但仍有一个同类的生命周期残留：

```cpp
g_inputReleaseRequired=true
    -> !gpAnyShortcutHeld()
    -> gpAnyShortcutHeld() 仍包含左右摇杆轴
```

该闸门由恢复、睡眠和 CustomSteamLibrary 返回路径设置。若实体手柄的摇杆持续漂移，`gpAnyShortcutHeld()` 永远为真，导致：

- `gamepadEval()` 在 LB+RB 呼出、A/B、肩键和页面 semantic 之前提前 `return`；
- CustomSteamLibrary `returning` 状态不能回到 `disabled`；
- YMCC 侧表现为“连接态存在但全部手柄无效”。

这不是 HidHide 或虚拟手柄缺失；是新增协调层 release barrier 把 ButtonState 与 AxisState 再次绑在一起。HC 的手柄生命周期将按钮/扳机状态与模拟轴状态分开处理。

## 2. 最小源码修正

文件：`native/main.cpp`

- 新增 `gpAnyButtonOrTriggerHeld()`：只检查数字按钮、D-pad、LT/RT 和待完成的 B 关闭事务，不检查左右摇杆轴。
- `g_inputReleaseRequired` 的清除条件改为 `!gpAnyButtonOrTriggerHeld()`；释放闸门因此只排空旧按钮/扳机边沿。
- CustomSteamLibrary `returning -> disabled` 的条件同步改为 `!gpAnyButtonOrTriggerHeld()`，避免漂移轴永久占住 owner 返回。
- 左右摇杆仍由既有 `g_gamepadAxisNeutralAdmissionRequired` 独立控制；未添加死区、未改写 HC 轴值、未创建虚拟手柄、未启用 HidHide。
- 增加一次性 `gamepad-release-barrier` 日志，记录等待/清除状态及当时按钮、扳机和轴快照。

## 3. 对账证据

- 对比基线：Git v0.0.28，commit `02744128576f213fd6f1a38e0d338b4265cd6bec`。
- HC 规则锚点：`Docs/Research/HC/Gamepad/HC-GAMEPAD-LIFECYCLE.md` 的 `ClearInputState`、`Suspend/Resume`、`VirtualManager` 单 writer/neutral 约束，以及 `Docs/Tasks/GyroVirtual/24-UNIFIED-LIFECYCLE-AND-COORDINATION-CONTRACT-20260903.md` 的 Coordinator release/rearm 规则。
- 当前源码 `native/main.cpp`：`1,127,295` bytes，SHA-256 `FAAE23C28F850E373FA99436F82DE9F4F437E450A97B3816ACDB2DA5768768B8`。
- `native/build_native.bat`：`BUILD_OK`。
- `tools/deploy-installed.ps1`：`DEPLOY_OK`。
- 安装包 `C:\SOFT\YeMan\YeManCC\YeManCC.exe` 与构建产物 SHA-256 一致：`0948D43C7129CCBC153D283533A13DC9E59E6B5791F20E01A13E1A5A964CC5AF`，bytes `2,267,136`。
- 部署备份：`C:\SOFT\YeMan\YeManCC\.deployment-backup-20260906-230747`。
- 当前实例：YeManCC PID `14880`，RecoveryService PID `14104`。
- 当前实例已记录 `rawInputRegistered=true`、XInput `connectedMask=1/primarySlot=0/source=xinput`、`gamepad-neutral-admission=status=admitted-neutral` 和 `gamepad-state-replay`。

## 4. 仍未闭口的事实

本机不能由 BUS 自动生成真实物理 A/B/LB/RB 边沿，故以下仍未宣称通过：

- `gamepad-ui-input-emitted`；
- `gamepad-summon-emitted`；
- YMCC 控制器页实际按钮动作；
- 外部 Steam/游戏 consumer 隔离。

需要一次真实回归：在当前安装实例上按住 `LB+RB` 约 0.5 秒呼出，释放后按一次 `A` 或 `B`。随后检查：

```text
C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log
```

预期至少出现 `gamepad-summon-emitted` 或 `gamepad-ui-input-emitted`；若仍失败，依据同一时间窗中的 `gamepad-release-barrier`、`gamepad-ui-input-dropped` 和 `gamepad-xinput-input` 继续定位 focus/owner，而不再猜测虚拟手柄或 HidHide。
