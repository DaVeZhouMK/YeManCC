# T22-BUS-P81：HC DS4 身份与 HIDMaestro profile 对账及电量 5% 归因边界

日期：2026-09-05  
状态：`IDENTITY-PARITY-GAP / BATTERY-ORIGIN-UNKNOWN / NO-UNAMBIGUOUS-SOURCE-FIX / RUNTIME-BLOCKED`

本页是 BUS 对锁定 HC、当前 YMCC `InputHost`、锁定 HIDMaestro profile/encoder 的增量只读核对。目标是确认“PS4 Controller 电量低 5%”是否存在可按 HC 明确修正的 persona/profile 或 battery 逻辑；没有执行 Host、驱动、虚拟设备、Steam、游戏或系统状态变更。

## 1. 锁定 HC 身份事实

锁定 HC `VirtualManager.cs` 在 `HIDmode.DualShock4Controller` 分支创建：

```text
target type = DualShock4Target
VID         = 0x054C
PID         = 0x05C4
model       = Dual Shock 4 [CUH-ZCT1x]
report      = 31-byte VIIPER input report
```

锁定 HC `DualShock4Target.BuildReport()` 写入摇杆、按钮、触摸和 raw gyro/accel；未发现 battery assignment、charge flag 或 battery percentage writer。

## 2. 当前 YMCC 与 HIDMaestro 身份事实

当前 `InputHost/Program.cs` 对 `persona=dualshock4` 只接受：

```text
profileIdentity = dualshock-4-v2
```

并固定调用 HIDMaestro profile `dualshock-4-v2`：

```text
VID         = 0x054C
PID         = 0x09CC
model       = DualShock 4 v2 (CUH-ZCT2)
connection  = usb
report      = report ID 0x01 / 64 bytes
```

同一锁定 HIDMaestro profile catalog 中确实存在 `dualshock-4-v1`：

```text
VID:PID     = 0x054C:0x05C4
model       = DualShock 4 v1 (CUH-ZCT1)
connection  = usb
report      = report ID 0x01 / 64 bytes
```

因此已确认存在跨 backend persona/profile 身份差异：

```text
HC target                 = 054C:05C4 / CUH-ZCT1x / VIIPER 31-byte
YMCC selected HID profile = 054C:09CC / CUH-ZCT2 / HIDMaestro 64-byte
cross-backend identity    = NOT PARITY-CLOSED
```

该差异不能简单解释为“同一 report”：HC 的 31-byte VIIPER wire contract 与 HIDMaestro 的 64-byte profile contract 不同。HC byte offset、VID/PID 或报告长度不能直接复制到 HIDMaestro。

## 3. Battery metadata 三方事实

### HC

```text
DualShock4Target.BuildReport() = no battery assignment found
DS4OutDevice.bBatteryLvl       = legacy/unreferenced field; no production writer found
DSU DsBattery                  = separate DSU metadata plane, not DS4 HID report
```

### HIDMaestro

对 `dualshock-4-v1`、`dualshock-4-v2` 与 `dualshock-4-v2-bt` 的锁定 profile metadata 做只读 reflection：

```text
dualshock-4-v1     battery field = byte 14 / uint8-battery
dualshock-4-v2     battery field = byte 14 / uint8-battery
dualshock-4-v2-bt  battery field = byte 16 / uint8-battery
```

`dualshock-4-v1` 与 `dualshock-4-v2` 都是 `AlwaysArmed=false`、`ArmOn=null`；当前 USB profile 不会仅凭 `SubmitState` 自动进入 extended vendor-blob route。锁定 `HMGamepadState` 默认值为：

```text
BatteryLevel    = 0
BatteryCharging = false
BatteryFull     = false
```

受控静态 encoder 结果仍为原值编码：

```text
0   → 0x00
5   → 0x05
10  → 0x0A
100 → 0x64
255 → 0xFF
```

没有发现 `0 → 5` 的隐式转换。

### 当前 YMCC

`TrySubmitFrame()` 与 `TrySubmitNeutral()` 只写 `Buttons`、`Hat`、标准 `Axes`；未写 `BatteryLevel`、`BatteryCharging` 或 `BatteryFull`。当前 standard `SubmitState` 到 DS4 battery byte 的运行时 readback 仍未闭合。

## 4. 裁决：事实、可能解释、未知与缺口

### FACT

- HC 明确选择 `054C:05C4`，当前 YMCC 明确选择 `054C:09CC`。
- HIDMaestro catalog 中存在与 HC PID 对应的 `dualshock-4-v1` profile。
- v1/v2 的 battery metadata 均未证明当前 USB standard route 会写入电量。
- HC、当前 YMCC 均没有已证明的当前 DS4 battery writer。
- HIDMaestro encoder 没有把默认 0 隐式改成 5。

### 可能解释（未闭口）

身份差异可能导致 Steam/Windows 采用不同的 DS4 代际、descriptor 或电量能力解释；这是真实的 persona/profile parity gap，但不能单凭这个事实证明 5% 来源就是 VID/PID 或 profile。

### UNKNOWN / GAP

```text
cross-backend identity parity                = UNENCLOSED
identity mismatch -> Steam 5% causality     = UNKNOWN
actual virtual VID/PID/container identity   = RUNTIME-BLOCKED
actual descriptor/input/feature raw report   = RUNTIME-BLOCKED
byte-14 independent decode and readback      = RUNTIME-BLOCKED
OS/Steam identity-correlated battery readback = RUNTIME-BLOCKED
```

## 5. 处理决定

本轮没有可以按锁定 HC 无歧义直接实施的 battery 修复：

- 不把当前 profile 从 v2 猜改为 v1；两者不是相同 wire contract，切换会改变 persona identity、descriptor 和 consumer 行为。
- 不写 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`。
- 不将 HC `054C:05C4` 的身份事实直接当成 HIDMaestro 64-byte profile 的可复制报告契约。

关闭这条 identity/battery 缺口必须在同一 Host epoch 获取：实际虚拟 VID/PID、instance/container identity、descriptor 原始字节、input/feature report 的 report ID/长度/raw bytes、byte 14 解码与 Steam 同一设备 identity 的电量回读。

结论保持：

```text
HC identity                    = 054C:05C4
YMCC HID profile               = 054C:09CC
cross-backend persona parity  = UNENCLOSED / KNOWN GAP
PS4 battery 5% origin          = UNKNOWN / RUNTIME-BLOCKED
unambiguous HC source fix      = NOT FOUND
runtimeUpgrade                 = false
```

## 6. 写回后的静态回归与 provenance

本页与 `TASK.md` 写回后执行最终静态回归：

```text
pnpm run type-check                         = PASS
pnpm run test:persona-descriptor-parity     = PASS
pnpm run test:input-contracts               = PASS
pnpm run test:gyro-virtual-mainline-status  = RUNTIME_BLOCKED
pnpm run test:a1-gyro-release-boundary      = FAIL (HidHide release-boundary policy)
```

RF00/T20/T21/T14 的动态 provenance 以当前 manifest/re-audit 工件为唯一事实来源；由于本页和 `TASK.md` 都属于 RF00 key surface，本页不复制 sourceDigest，避免再次制造 stale digest：

```text
manifestPath  = tools/T22-RF00-T10-I-CURRENT-SOURCE-20260905-manifest.json
reAuditPath   = tools/T21-T14-RF00-T10-I-CURRENT-SOURCE-reaudit-20260905.md
runtimeUpgrade = false
```

这些回归结果没有把 profile identity gap 或 PS4 5% 提升为 runtime closure；仍需真实设备同 Host epoch 的 descriptor/raw/readback 证据。
