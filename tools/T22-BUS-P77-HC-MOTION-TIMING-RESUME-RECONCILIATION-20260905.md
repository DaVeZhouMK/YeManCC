# T22-BUS-P77：HC GamepadMotion 时序、恢复与缓存边界对账

日期：2026-09-05

状态：`HC-SOURCE-BOUND / TIMING-DELTA-GAP-EXPLICIT / NO-RUNTIME-CLOSURE / T10-OWNER-BOUND`

本页是 BUS 对锁定 HC source、当前 YMCC native/source 与 GyroVirtual MD 的增量三方对账。检索词扩展为：

```text
ProcessMotion / deltaTime / TimerManager / MasterInterval / GetPeriod / Stopwatch /
PreviousTotalMilliseconds / Tick / UpdateReport / ReadingChanged / GetCurrentReading /
UpdateSensor / StartListening / StopListening / Suspend / Resume / ResetMotion /
ResetGamepadMotion / ResetContinuousCalibration / Dispose / timestamp / sequence /
stale / cache / rebind / provider / sensorFamily / generation / first reading
```

本页不启动 HC、InputHost、HIDMaestro、HidHide、Steam、游戏或真实设备；不修改 T10/Host 源码，不打开 `pairProven`，不把潜在时序问题写成已发生的漂移。

## 1. 锁定 HC 的可定位事实

### 1.1 主时钟和 delta 来源

锁定 HC `Managers/TimerManager.cs`：

```text
DefaultMasterInterval = 8 ms (125 Hz)                  : 10
MasterInterval 可由设置映射为 8/4/2/1 ms              : 77-94
VirtualManager override 也可映射为 8/4/2/1 ms         : 106-115
DoWork -> GetDelta -> Tick(Stopwatch ticks, delta)     : 166-177
GetDelta = (Stopwatch.Elapsed - PreviousTotalMilliseconds) / 1000
```

锁定 HC `Managers/ControllerManager.cs:301-347` 的调用顺序：

```text
TimerManager.Tick
  -> ControllerManager.Tick(ticks, delta)
  -> SensorsManager.UpdateReport(..., ref delta)
  -> GamepadMotion.ProcessMotion(..., delta)
  -> MotionManager.UpdateReport(..., delta)
```

因此 HC 的 GamepadMotion `deltaTime` 来源是同一 master timer lane 的 `delta`；传感器的 `ReportInterval` 只是采样请求，不等同于 motion consumer 的 delta。

### 1.2 GamepadMotion 的 delta 语义

锁定 HC `Helpers/GamepadMotion.cs:91-133`：

```csharp
this.gyro/accel = 本次输入;
if (deltaTimeSeconds >= 0.00001f)
    this.deltaTime = deltaTimeSeconds;
ProcessMotion(..., this.deltaTime);
Madgwick.UpdateReport(..., this.deltaTime);
```

可证事实：

- `deltaTimeSeconds` 为 `0` 或小于 `0.00001f` 时，HC 保留上一次 `deltaTime`；首次实例的字段默认值为 `0`。
- 当前 wrapper 没有 `0.001..0.1` 的通用 clamp，也没有把首次调用强制替换成 `0.016f` 的逻辑。
- `Reset()` 只调用 native `ResetGamepadMotion(handle)` 并重置 Madgwick；没有在 wrapper 中重写 `deltaTime`。
- `Dispose()` 删除 native handle；它是实例终止，不是每次 sensor rebind 的 delta 复位合同。

### 1.3 Resume / rebind 的 HC 缓存事实

锁定 HC `Managers/SensorsManager.cs:140-170,312-329`、`Sensors/IMUGyrometer.cs:78-98`、`Sensors/IMUAccelerometer.cs:78-98`：

```text
Suspend -> StopListening()
Resume  -> UpdateSensor() -> StartListening()
UpdateReport -> GetCurrentReading() -> GamepadMotion.ProcessMotion()
```

`StopListening()` 解绑 sensor 并将 `sensor=null`，但没有清除 `SensorReading.reading`、`SensorReading.timestamp` 或 `readingAxis`。因此在首个新的 `ReadingChanged` 前，HC source 存在“恢复后可能重新消费旧缓存”的潜在生命周期缺口；这与既有 P67/P64 条目一致，不能当作 HC 正确性合同或 YMCC 实际复现。

## 2. 当前 YMCC source 的对账事实

锁定当前 `native/main.cpp`：

```text
inputCaptureStartWinRtSensors(): 4882-4916
  typed WinRT GetDefault + ReportInterval(max(MinimumReportInterval, 8 ms))

inputCaptureSuspend/ResumeSensorsForPower(): 4919-4933
  stop/unsubscribe -> ResetSamples() -> re-subscribe

inputCaptureLoadGamepadMotion(): 5008-5040
  create/process/readback symbols; g_gmLastTick = static state
  no calibration start at DLL load

inputCaptureInit(): 6228-6265
  ResetSamples() + sensor start; does not reset g_gmLastTick

inputCaptureOnGamepad(): 6268-6333
  now = GetTickCount64()
  first dt = 0.016f
  later dt = (now - g_gmLastTick) / 1000
  clamp dt to 0.001..0.1
  update g_gmLastTick

inputCaptureShutdown(): 6458-6472
  delete GamepadMotion handle; does not reset g_gmLastTick
```

当前 `pairProven` 在 `native/main.cpp:6310-6314` 固定为 `false`；因此 `native/main.cpp:6327-6333` 的 GamepadMotion 调用当前不可达，下面的时序差异属于 future-admission 证据，不是本机已发生的产品漂移。

## 3. 三方差异分类

| 项目 | HC source | YMCC current source | 分类与裁决 |
| --- | --- | --- | --- |
| motion delta 时钟 | TimerManager master `Stopwatch` delta | capture lane `GetTickCount64()` delta | `HC-DEVIATION / T10-OWNER-BOUND`；尚未 runtime 可达 |
| 首次 delta | 字段默认 `0`，小 delta 保留上一次值 | 首次强制 `0.016f` | `ORIGINAL TIMING LOGIC / NOT HC`；不等同轴/单位/增益偏移 |
| delta clamp | 未发现通用 `0.001..0.1` clamp | 强制 clamp `0.001..0.1` | `ORIGINAL TIMING LOGIC / NOT HC`；可能是安全保护，但不是 HC parity |
| MasterInterval | 运行时可为 8/4/2/1 ms | capture 请求固定 8 ms | `CONFIG/CADENCE GAP`；已登记，不猜测替换设置链 |
| Suspend/Resume sensor cache | HC 未清 `SensorReading`，首个新事件前可能复用旧读数 | YMCC `ResetSamples()` 后再订阅 | YMCC 是 fail-closed 安全差异，不应回退为 HC stale-cache 行为 |
| GamepadMotion delta state reset | HC `Reset()` 不自动由 Resume 调用 | YMCC `g_gmLastTick` 在 sensor suspend/resume、init、shutdown 未显式清零 | `RESUME-REBIND TIMING GAP`；若未来打开 pair gate，需由 T10 明确定义 |
| calibration start | HC 仅显式 `SensorsManager.Calibrate()` 会话启动 | YMCC DLL load 不启动 calibration | 已按 HC 约束；不是缺陷 |

## 4. 不能升级为事实的推断

以下仅为基于 source 的条件推断：

1. 若未来 `pairProven` 被打开，YMCC 的 `GetTickCount64()` 与 HC master-lane delta 可能产生不同的 velocity/Madgwick 时间步长。
2. 若在同一进程中关闭并重新初始化 capture，`g_gmLastTick` 未清零可能使新会话首个可处理帧继承旧 session 的时间基准；当前 pair gate 固定为 `false`，没有 runtime output 证据。
3. 若 power resume 后先得到一对可准入样本，当前没有单独的 `first-new-reading`/`first-pair-after-rebind` gate；但 `ResetSamples()` 已避免旧 WinRT sink 快照直接复用。是否需要更强的 generation proof 仍属于 T11/T10 合同缺口。

不得把上述推断描述为“当前已经造成静态乱动”或“已解释用户设备漂移”。

## 5. 当前错误、未知、HC 偏离、原创逻辑、缺口

```text
HC master-lane delta parity                         = UNENCLOSED / T10-OWNER-BOUND
YMCC GetTickCount64 timing path                     = ORIGINAL LOGIC / NOT HC
YMCC first-delta 0.016 + 0.001..0.1 clamp           = ORIGINAL LOGIC / NOT HC
fixed 8 ms vs HC configurable 8/4/2/1 ms             = CADENCE GAP
HC resume cached reading before first event           = HC-SOURCE LATENT GAP
YMCC sink stale-sample reuse after power resume       = mitigated by ResetSamples()
YMCC GamepadMotion session delta reset               = UNKNOWN / UNENCLOSED
current GamepadMotion runtime output                 = RUNTIME-BLOCKED (pairProven=false)
new axis/sign/unit/gain/deadzone drift                = NOT FOUND in this audit
new autonomous drift algorithm                       = NOT FOUND in this audit
```

`GetTickCount64`、首帧 `0.016f`、delta clamp 和未清 `g_gmLastTick` 是当前 YMCC 添加的时序逻辑；它们不是 HC 的轴/符号/单位/增益规则，也不是已经证明的漂移算法。由于该路径仍由 `pairProven=false` 阻断，BUS 本轮不直接改 T10 源码，避免绕过正在进行的 T10 owner 修改。

## 6. 关闭条件和下一步

关闭该时序差异至少需要同一 Host epoch 的：

```text
HC master interval / TimerManager delta trace
YMCC capture-lane delta trace
GamepadMotion ProcessMotion input + output trace
power suspend/resume/rebind generation
first-new-reading / first-admitted-pair receipt
Host/HID/Steam/game consumer observation
```

文档写回后，当前 74-key RF00 快照不再覆盖本页和更新后的 `TASK.md`；必须重新执行：

```text
T22-RF00 -> T20 -> T21 -> T14
```

本页结论：

```text
BUS-P77 = HC timing semantics expanded and reconciled
direct source fix = none (T10 owner / runtime blocked)
runtimeUpgrade = false
device/system mutation = none
user intervention = not yet required for static audit; required for runtime closure
```
