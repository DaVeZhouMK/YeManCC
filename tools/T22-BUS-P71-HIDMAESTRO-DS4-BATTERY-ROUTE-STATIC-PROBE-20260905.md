# T22-BUS-P71：HIDMaestro DS4 电量字段与当前 SubmitState 路由静态探针

日期：2026-09-05  
范围：只读反射/IL 审计锁定的 HIDMaestro Core、当前 `InputHost/Program.cs` 与任务书电量问题。  
运行边界：未创建 controller、未调用 `Start`/`SubmitState`、未安装驱动、未访问 HID/Steam/游戏或真实设备。

## 1. Fact：HIDMaestro assembly 与 state defaults

```text
assembly = G:\YeManCC-Work\Archives\Migration-Backup\20260831-152113\YMCC-Workspace\Build\External\HIDMaestro\v1.7.0\HIDMaestro.Core.dll
bytes    = 40,755,712
SHA-256  = BD42A99BCB260435CE25796C54A4B792F8A2CED6AB78659C0CF926011663938E
version  = 1.7.0.0
```

反射实例化默认 `HIDMaestro.HMGamepadState` 得到：

```text
BatteryLevel    = 0
BatteryCharging = false
BatteryFull     = false
Gyro/Accel      = 0
```

`HMGamepadState` 的 Battery 字段确实存在，但默认值不是 5。

## 2. Fact：dualshock-4-v2 profile and extended route

锁定 profile `dualshock-4-v2` 的静态值：

```text
InputReportSize       = 64
descriptor bytes      = 507
ExtendedReport        = present
ReportId              = 0x01
ExtendedReport.Size   = 64
AlwaysArmed           = false
ArmOn                 = null
field byte 14         = uint8-battery / semantic batteryLevel
```

HIDMaestro `HMController` 构造函数只有在 `AlwaysArmed` 或 `ArmOn` 存在非空触发器时才分配 `_extendedReportBuffer` 并将 `_extendedModeArmed` 置为 armed；该 profile 的两个条件均不成立。字段使用扫描只发现：构造函数写入 `_extendedModeArmed=false`，`SubmitState` 与 `OutputPollLoop` 读取它，没有其它生产写入点。

## 3. Fact：current YMCC SubmitState branch

当前 `InputHost/Program.cs` 的 `TrySubmitFrame()` 和 `TrySubmitNeutral()` 只构造并提交：

```text
HMGamepadState.Buttons
HMGamepadState.Hat
HMGamepadState.Axes
```

HIDMaestro `HMController.SubmitState` 的 extended branch 仅在 `_extendedModeArmed=true` 时调用 `VendorBlobCodec.EncodeInput`；否则调用 `HidReportBuilder.BuildReportInto`，参数只有 axes、hat、buttons、hat-degree/raw。profile `InputDefaults` 也为 null。

因此从当前 source 可证明：当前 YMCC 没有一条已闭合的 production path 把 `BatteryLevel` 写入 DS4 extended report。

## 4. Fact：encoder does not synthesize 5%

只读反射调用 `VendorBlobCodec.EncodeInput`（未连接 controller）并分别设置 `BatteryLevel=0/5/10/100/255`，得到 byte `14` 原值 `00/05/0A/64/FF`。这证明 encoder 没有把默认 `0` 隐式变换为 `5`。

该调用只是 encoder 单元级事实，不是最终 HID/OS/Steam 回读。

## 5. Fact / Inference / Gap

```text
Fact:
  current YMCC InputHost does not assign BatteryLevel/BatteryCharging/BatteryFull.
  current dualshock-4-v2 extended route is not armed by the captured profile.
  encoder byte14 is a direct BatteryLevel field; default state is zero.

Inference (not closure):
  Steam's displayed 5% is more consistent with an external producer,
  driver/consumer normalization, cached state, or a route not represented
  by the current source snapshot than with a hidden YMCC constant 5.

Gap:
  No same-lifecycle DS4 descriptor, raw input/feature report, final HID
  readback, device identity, or Steam consumer receipt is available.
```

结论仍固定为：

```text
PS4 battery 5% origin = UNKNOWN / RUNTIME-BLOCKED
```

不把 encoder 反射结果提升为 OS/Steam 证据；不猜写 `0x05`、`0x0A`、`100%` 或 `Full`。

## 6. HC parity decision

锁定 HC `DualShock4Target.BuildReport()` 没有发现 battery assignment；当前 YMCC 也没有已证明的 battery writer。给当前 source 强行设置满电或 5% 都会成为未经 HC 证明的原创行为，因此本轮无可直接修复的 HC 偏差。

关闭缺口仍需要同一 Host 生命周期的 DS4 descriptor、raw report/feature report、独立 decoder/readback 与 Steam 显示关联。此前已登记的 ROG provider/epoch/matrix/consumer 与 HC lifecycle gaps 不因本探针改变。

## 7. Provenance after P71 TASK write-back

P71 写回 `TASK.md` 后按固定顺序重新冻结并重审：

```text
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = 174ED4EAF5076BB8493B208F28B419E6DC7B0F461674F665CB157C5C03C501D4
keyFiles      = 74
porcelain raw = 458 entries
HC clean      = true
runtimeOperation = false
buildOrTest      = false
T20 = 4/13/4
T21 = 9 claims
DGF = DGF-01…DGF-15
runtimeUpgrade = false
```

以上只更新 provenance 身份，不改变 PS4 battery 的 `UNKNOWN / RUNTIME-BLOCKED` 结论。

## 8. Later final RF00 after P72 TASK write-back

P72 对 P64 的 docs-only stale narrative 修正写回 `TASK.md` 后，最新 RF00/T20/T21/T14 为：

```text
manifestId    = T22-RF00-20260905-T10-I-HC-ROUTE-STATIC-SNAPSHOT
sourceDigest  = BCAE917B828D51F3EC55025132567CF86D681F7DD01B33C8DB9A5557E4C767FF
keyFiles      = 74
porcelain raw = 458 entries
HC clean      = true
runtimeOperation = false
buildOrTest      = false
T20 = 4/13/4
T21 = 9 claims
DGF = DGF-01…DGF-15
runtimeUpgrade = false
```

该 provenance 更新不改变本页对 HIDMaestro battery route、PS4 5% UNKNOWN 或 runtime blocked 的结论。
