# T22 BUS-P93：DS4 虚拟电量满电状态补丁（历史/已撤回，2026-09-06）

状态：`HISTORICAL / NON-HC-PRESENTATION-PATCH-RETRACTED / CURRENT-SOURCE-REQUIRES-RF00`

## 1. 历史源码改动（不再属于 current source）

修改文件：

```text
InputHost/Program.cs
```

历史版本曾在 `TrySubmitFrame()` 与 `TrySubmitNeutral()` 的 `HMGamepadState` 初始化中加入：

```csharp
BatteryLevel = 10;
BatteryCharging = false;
BatteryFull = true;
```

当时依据是 HIDMaestro state contract 的字段形状，试图将合成 DS4 显示为满电。三方复核后该理由不足：锁定 HC `DualShock4Target.BuildReport()` 没有对应 battery assignment，`DS4OutDevice` 的电量字段也没有定位到当前生产 writer；因此这是 YMCC 的原创 presentation 偏移，不是 HC parity 修正。该补丁已从当前 `InputHost/Program.cs` 删除，不得再把它描述为已修复或 HC-aligned。

## 2. 历史构建与包证据（仅证明该旧补丁曾可构建）

```text
dotnet build InputHost/YeManInputHost.csproj -c Release --nologo
exitCode = 0
warnings = 0
errors = 0

dotnet run --project InputHost/YeManInputHost.csproj -c Release --no-build -- --selftest-protocol
exitCode = 0

pnpm run build
exitCode = 0
native YeManCC.exe SHA-256 = 68107DCAEC4A549DE213D646E741287DD4EE1D846586A530895307EF985BD4D2

pnpm run package:gyro-input-test
exitCode = 0
zip = Mainline/Build/TestPackages/GyroInput-ROG-20260906-112745/YeManCC-GyroInput-ROG-Test.zip
zipBytes = 88,554,700
zipSHA256 = 9FDB62900F28ACA34AC3C1258EEFC1E4AA1A0DD0726220D47D80718EEA78121D
formalReleasePackageUntouched = true
```

同一历史 binary 的真实 Host 级回执已保存到：

```text
Mainline/Build/Validation/DS4-Battery-P93-Host-Receipt-20260906.json
```

回执证明 `HELLO → PREPARE_TARGET → SUBMIT_NEUTRAL → SUBMIT_FRAME → SHUTDOWN` 全部成功，`assetVerified=true`，frame 期间出现 `Wireless Controller` PnP 节点，最终 `local-dispose-complete`。它仍不是 Steam/Windows battery readback。

上述包和回执只证明历史 binary 的 Host-local 生命周期；不证明 battery byte、Steam/Windows consumer，也不证明该 presentation patch 符合 HC。正式 Release/updater 未被该历史实验包修改。

## 3. 当前源码复核（2026-09-06）

当前 `InputHost/Program.cs` 的 `TrySubmitFrame()` 与 `TrySubmitNeutral()` 只写入 HC/HIDMaestro 已有的按钮、Hat 和标准轴字段；没有 `BatteryLevel`、`BatteryCharging` 或 `BatteryFull` 赋值。

```text
dotnet build G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\YeManInputHost.csproj -c Release --nologo
exitCode = 0 / warnings = 0 / errors = 0

dotnet run --project G:\YeManCC-Work\Mainline\YeManCC-source\YeManCC\InputHost\YeManInputHost.csproj -c Release --no-build -- --selftest-protocol
exitCode = 0

current YeManInputHost.dll
bytes = 50,176
sha256 = 1E553B52E62D956721FB66C1E86C451F33D571B99478DE83E1FF8F2EB5576A01
```

本次没有重新生成 ROG standalone 包，也没有运行 HID/PnP/Steam/game 或真实设备测试；正式 Release/updater 仍未修改。

## 4. 已证明与未证明

### 已证明

- 当前 `YeManInputHost`（撤回 battery assignment 后）编译通过，协议 selftest 通过。
- HC 对账没有发现可直接证明的 battery writer 偏移；因此没有保留未经证实的“满电”逻辑。
- 历史 P93 包和 Host receipt 仍可作为历史工件，但不能作为 current source 或 battery 修复证据。

### 仍需实机验证

- 锁定 USB `dualshock-4-v2` profile 的当前 extended route 是否在最终报告中产生/消费 battery 字段。
- Steam/Windows 是否停止显示“PS4 电量不足 5%”。
- ROG 的 gyro/accel 是否进入采集器、是否静态稳定、是否有漂移，以及 UI 轴线图/虚拟摇杆是否实时变化。
- 这些现象必须由更新包的同一 run/epoch 日志和用户回传的 Desktop ZIP 闭合；不能由编译或包体 hash 推导。

## 5. 后续最小介入

只有在需要再次验证电量/ROG 运行时才生成包含当前 binary 的新 standalone 包。届时最小采集项为：

1. 同一 Host `runId/epoch` 的 DS4 descriptor、input/feature raw report；
2. byte-14 独立 decoder 与 `BatteryLevel`/profile 对账；
3. Steam/Windows/GameInput 对同一虚拟设备的独立读取；
4. ROG gyro/accel paired sample、静态漂移和 UI consumer 证据。

用户回传前不应重复猜测 `5/0x05/0x0A/0x0F/100/Full`，也不应切换 `dualshock-4-v1/v2`。

历史 P93 的原始测试步骤仅供追溯，不代表当前包已准备好：

```text
PS4/DS4 电量低于 5% 弹窗是否消失
Gyro 页面轴线图、波形、数字和虚拟摇杆是否随转动变化，静止时是否停止漂移
```

## 6. 安全边界

```text
PS4 battery producer/runtime proof = UNKNOWN / RUNTIME-BLOCKED
ROG gyro provider/pair/matrix/consumer proof = UNVERIFIED
runtimeUpgrade = false
```

若电量弹窗仍存在，不继续猜 battery byte 或切换 profile；下一步只做同一 Host epoch 的 descriptor、raw report、独立 decoder 与 Steam/Windows readback 对齐。
