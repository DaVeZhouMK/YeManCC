# T22-BUS-P84：current-source HC 偏差与直接修复分流

日期：2026-09-05
状态：`CURRENT-SOURCE-TRIAGE / NATIVE-CONCURRENT / BATTERY-NO-WRITER / NO-SAFE-PATCH / RUNTIME-BLOCKED`

本页是 BUS 对当前工作树的再审计，目的不是重复声称“没有问题”，而是把已被 HC 源码证明的偏差、可直接修正项、需要合同/运行时证据的项分开。未启动设备、InputHost、HIDMaestro、HidHide、Steam、游戏或真实硬件。

## 1. 当前工作树边界

只读 `git diff --stat -- native/main.cpp` 结果为：

```text
native/main.cpp = 2757 insertions / 36 deletions
```

这表明 native/T10 owner 路线仍处于用户并发修改状态。本页不覆盖、不重排、不回滚该文件。`InputHost/Program.cs` 当前仍固定选择 HIDMaestro `dualshock-4-v2`，未发现本轮有用户并发改动。

## 2. 三方直接修复分流

| 项目 | HC source 事实 | current YMCC 事实 | 裁决 |
| --- | --- | --- | --- |
| PS4 battery | `DualShock4Target.BuildReport()` 无 battery assignment | `TrySubmitFrame/TrySubmitNeutral()` 无 battery writer | 无可按 HC 直接修复项；保持 `UNKNOWN` |
| HID identity | HC `054C:05C4`、VIIPER 31-byte | YMCC `dualshock-4-v2`、`054C:09CC`、64-byte | 已知 parity gap；不能猜切 v1，需 descriptor/raw/consumer 证据 |
| delta timing | HC 使用 master-lane delta，小 delta 保留上值 | native 有首帧、`GetTickCount64()`、`0.001..0.1` clamp | 明确 HC 偏差，但位于并发 native/T10；暂不覆盖 |
| gyro cutoff | HC threshold 绑定 calibration，2000 是默认值 | native/bridge 有 fixed 2000 候选路径 | 明确 latent 偏差；`pairProven=false` 阻断，待 native 稳定后单独修 |
| calibration start | HC `ResetContinuousCalibration → Stillness\|SensorFusion → confidence → Store → Manual` | current native 解析 ABI，但没有完整 T11 identity/epoch/receipt 调用链 | 明确合同缺口，不做孤立 patch，不打开 `pairProven` |
| Host release | HC/当前合同要求 neutral/release 顺序 | native 当前为 `QUIESCE → SUBMIT_NEUTRAL → RELEASE_TARGET → SHUTDOWN`，失败保留 `release-unproven` | 本轮未发现新顺序偏差 |
| A3 battery `0x0A` | 不属于锁定 HC | 历史非 HC raw encoder，current route 未证明可达 | 禁止恢复或复制 |

## 3. PS4 5% 专项结论

当前仍没有任何静态事实能把右下角 `PS4 Controller / 电量低 - 5%` 归因给 HC、YMCC 或 HIDMaestro：

```text
HC battery writer                 = NOT FOUND
YMCC battery writer               = NOT FOUND
HIDMaestro 0→5 conversion        = NOT FOUND
historical A3 raw battery route   = NON-HC / CURRENT-REACHABILITY-UNPROVEN
actual DS4 descriptor/raw report = RUNTIME-BLOCKED
Steam/Windows battery readback   = RUNTIME-BLOCKED
PS4 5% origin                    = UNKNOWN
```

不写 `BatteryLevel=5`、`0x05`、`0x0A`、`100` 或 `Full`；不把 HC 31-byte report 复制到 HIDMaestro 64-byte profile。

## 4. 当前不直接修改的理由

本轮没有发现一个同时满足以下条件的安全 patch：

```text
1. HC source 对目标行为有唯一、完整、可执行的定义；
2. current YMCC 偏差不依赖 physical identity/provider/descriptor/consumer 证据；
3. 修改不覆盖用户并发的 native/T10 owner 变更；
4. 可以用现有静态测试证明不会绕过 pair/epoch/release 安全门。
```

因此本轮不改源码、不改 HIDMaestro、不改正式包/updater、不改系统状态。已知 native 偏差继续保留为 `KNOWN / T10-OWNER-BOUND`，PS4 电量继续保留为 `UNKNOWN / RUNTIME-BLOCKED`。

## 5. 最小后续介入门

native 稳定后可重新裁决 timing/cutoff/calibration 三项；PS4 5% 则无论 native 是否稳定，都必须先取得同一 Host epoch 的虚拟 DS4 VID/PID、container/instance、descriptor、input/feature raw report、byte-14 解码和 Steam/Windows 电量 readback。运行时证据回来前保持：

```text
T11/T12/T13/T15-T18 = UNENCLOSED / RUNTIME-BLOCKED
PS4 battery 5%       = UNKNOWN / RUNTIME-BLOCKED
runtimeUpgrade       = false
```

