# T22 BUS-P97：HC/当前源码三方电量对账与 P93 撤回（2026-09-06）

状态：`SOURCE-RECONCILED / HISTORICAL-RETRACTION / SUPERSEDED-BY-P103-USER-RULING / RUNTIME-BLOCKED`

## 1. 本轮结论

本轮只处理 PS4/DS4 “电量低 - 5%”相关的三方对账：锁定 HC source、当前 YMCC source、任务书/证据台账。结论如下：

```text
HC battery writer in DualShock4Target = 未定位到
YMCC 原 BatteryLevel=10/BatteryFull=true = 明确不是 HC 事实，已撤回
当前 YMCC frame/neutral battery assignment = 不存在
PS4 5% producer/report byte/consumer = UNKNOWN / RUNTIME-BLOCKED
```

这不是“5% 已修复”，也不是“5% 来自默认 0”的确认；在缺少同一 Host epoch 的 raw report 和独立 consumer readback 前，不能继续猜值或切换 profile。

## 2. HC source 事实

锁定源码位置：

```text
G:\YeManCC-Work\Isolated\Tasks\HC-Candidate-20260902\active\workspace\HandheldCompanion\Targets\DualShock4Target.cs
G:\YeManCC-Work\Isolated\Tasks\HC-Candidate-20260902\active\workspace\HandheldCompanion\Misc\DS4OutDevice.cs
```

已定位事实：

- `DualShock4Target.BuildReport()` 构造 31-byte VIIPER report，写入摇杆、按钮、触控和 raw gyro/accel；当前函数未定位 battery assignment。
- `DS4OutDevice.cs` 存在 `bBatteryLvl` / `bBatteryLvlSpecial` 结构字段，但本轮未定位到当前生产 writer，不能把字段存在当成写入路径。
- DS4 battery 5% 的 producer、report byte、profile 路由和 Steam/Windows consumer 尚未形成同代闭环。

## 3. YMCC/HIDMaestro 对账

历史 P93 曾在 `InputHost/Program.cs` 的 `TrySubmitFrame()` 与 `TrySubmitNeutral()` 加入：

```csharp
BatteryLevel = 10;
BatteryCharging = false;
BatteryFull = true;
```

该代码只反映 HIDMaestro state contract 的字段形状，不是锁定 HC 的 wire/report 事实；因此被分类为 `SOURCE-DIVERGENCE / NON-HC-PRESENTATION-PATCH`，已从当前源码删除。

当前 `InputHost/Program.cs` 的 frame/neutral 路径仅提交按钮、Hat 和标准轴；没有 `BatteryLevel`、`BatteryCharging` 或 `BatteryFull` 赋值。HIDMaestro `dualshock-4-v2` profile 的静态 encoder 仍需通过最终 descriptor/raw report/readback 证明其 battery route，不能由 `HMGamepadState` 字段默认值推导 Steam 语义。

## 4. 当前构建证据

执行目录：`G:\YeManCC-Work`

```text
dotnet build G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\YeManInputHost.csproj -c Release --nologo
exitCode = 0
warnings = 0
errors = 0

dotnet run --project G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\YeManInputHost.csproj -c Release --no-build -- --selftest-protocol
exitCode = 0

current artifact = G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\bin\Release\net10.0-windows\YeManInputHost.dll
bytes = 50,176
sha256 = 1E553B52E62D956721FB66C1E86C451F33D571B99478DE83E1FF8F2EB5576A01
```

本轮未运行 HidHide、HID/PnP、Steam、游戏或真实设备；未重新生成 standalone ROG 包；正式 Release/updater 未修改。

## 5. 证据/未知/处置分离

### 已有证据

- `EV-18`：公开 DS4 parser 的 `code 0 → 5%` 编码语义，只能说明一种候选显示机制。
- `EV-20`：P96 同一 run/epoch 的 Host-local release receipt；与 battery producer 无关。
- 当前源码编译和 protocol selftest 通过，且 frame/neutral 不再写入未经 HC 证明的 battery presentation state。

### 未知/缺口

- `G-DS4-BATTERY-PRODUCER`：真实 producer、report byte、descriptor/feature path、Steam/Windows/GameInput readback 未闭合。
- `G-ROG-BATTERY`：ROG 测试包尚未使用当前撤回后的 binary 复测；不能将历史 P93 现象转移到当前源码。
- `G-PROVENANCE`：本次 P93/P97 文档和源码变更使 T22-RF00 必须刷新，旧 manifest 只能作历史快照。

### 固定处置

```text
不猜 5 / 0x05 / 0x0A / 0x0F / 100 / Full
不切换 dualshock-4-v1/v2
不把 HMGamepadState 默认值当作 HC wire 事实
不把 Host-local ACK 当成 Steam/Windows consumer receipt
```

## 6. 下一步门槛

若用户再次要求验证电量，必须生成包含当前 binary 的 standalone 包，并在同一 Host `runId/epoch` 采集：

```text
descriptor + input/feature raw report
byte-14 independent decode
HIDMaestro profile/encoder identity
Steam/Windows/GameInput same-device readback
```

在这些数据到位前，`runtimeUpgrade=false`，HC battery wire/consumer producer 仍保持 `UNKNOWN / RUNTIME-BLOCKED`。

## 7. 后续用户裁决（由 P103 supersede）

P97 的“撤回”结论只表示该字段不是 HC DS4 source-level writer，不能表示 YMCC 产品永远不能采用该展示状态。2026-09-06 用户明确裁决：

```text
HIDMaestro 虚拟 DS4 的 BatteryLevel=10 / BatteryCharging=false / BatteryFull=true
作为 YMCC 用户认可的产品展示偏差保留；不计入 HC gyro/accel/matrix/lifecycle 偏移。
```

当前实现与完整证据见 [P103](T22-BUS-P103-ROG-MANUAL-GYRO-EVIDENCE-AND-DS4-BATTERY-RULING-20260906.md)。DS4 raw battery wire/consumer producer 仍不宣称已由 HC 证明；该低优先级展示问题不阻塞陀螺仪主线。
