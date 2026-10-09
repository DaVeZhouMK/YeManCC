# T22 BUS-P95：ROG 采集包 13:01 运行证据（2026-09-06）

状态：`COLLECTOR-CLOSED / ROG-SENSOR-OBSERVED / CLOSE-LIFECYCLE-NOT-EXERCISED / RUNTIME-BLOCKED`

## 1. 输入工件

用户回传：

```text
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-130139.zip
bytes = 73,360
sha256 = 4D503B8CE8F9483B8E79BFEE82F9802C22EEDEB267B69F47AA97B1F4FF03079A
```

证据目录：

```text
Mainline\Build\Validation\UserEvidence-20260906-130139\YeManCC-ROG-Input-Gap-Evidence-20260906-130139
```

包内包含 `rog-input-gap-evidence.json`、`manifest.json`、`imu-rog-imu-evidence.json`、两份 YMCC input-capture JSONL 和 `imu-ymcc-native-lifecycle.log`。

## 2. 已证实事实

### 2.1 采集器退出

```text
capture window       = 2026-09-06T05:01:39Z → 05:02:06Z / requested 20 s
launch               = started=true / YeManCC PID 11636
IMU child            = waitStatus=exited / exitCode=0 / startError=null / killError=null
```

因此，P94 修正后的 collector/CMD 退出链在这次 ROG 包上复现为正常结束；这不能推导 YMCC 主进程已退出，也不能推导 Host/DS4 release。

### 2.2 ROG 机型与传感器

```text
computer              = ASUSTeK COMPUTER INC. ROG Xbox Ally X RC73XA_RC73XA
baseboard             = RC73XA
BIOS                  = RC73XA.317
accelerometer PnP     = Bosch Accelerometer / ACPI\\BOSC0200\\1
WinRT gyrometer       = present
WinRT accelerometer   = present
minimum interval      = 10 ms
observed interval     = 100 ms
```

PowerShell 事件订阅失败后，collector 使用了只读 polling fallback：

```text
eventSubscriptionError = Windows PowerShell 无法订阅 Windows RT 事件
pollingReadCount       = 719
gyro samples           = 360
accel samples          = 359
gyro cadence           = min 15.099 ms / max 5050.617 ms / average 55.518 ms
accel cadence          = min 14.399 ms / max 1691.25 ms / average 38.225 ms
```

这些是 WinRT/provider-level observation，不是 HC calibration、matrix 或 game-consumer proof。

### 2.3 YMCC 输入链路标记

两份 input-capture JSONL 都包含：

```text
kind=start
kind=sample-pair-unproven
reason=provider-capability-matrix-not-bound
safeZero=true
matrixIdentity=unresolved-rog-pid-family/raw
confidence=0.0
```

后续样本确实带有 gyro/accel 数值，但 `pairProven`、HC model/matrix/calibration provenance 仍未闭合；不能把这些样本升级为 HC parity 或已消费的虚拟右摇杆输出。

### 2.4 PnP before/after

before/after 均为 16 个候选设备，字符串化 identity 集合无差异（`Compare-Object` 无输出）。同一采集窗口内已存在：

```text
physical ROG gamepad = VID_0B05&PID_1B4C
physical X360        = VID_045E&PID_028E
virtual bus          = ROOT\\SYSTEM\\0001 / Nefarius Virtual Gamepad Emulation Bus
gvinput              = ROOT\\HIDCLASS\\0000
Wireless Controller  = ROOT\\HIDCLASS\\0001
```

这说明本次采集开始前虚拟 `Wireless Controller` 已可被 PnP snapshot 看到；不能证明它是本次 collector 启动时创建，也不能证明关闭 YMCC 后才出现。

### 2.5 当前 PID 的生命周期

本次 launch PID `11636` 的 copied lifecycle log 只有启动、ROG bind 和 `rog.xbox-face-enabled(reason=window-hidden)` 事件；没有：

```text
window-close-request
window-destroy
input-host-stop-request
input-host-release-receipt
input-host-stop-result
```

after snapshot 仍看到 `YeManCC PID 11636`。日志中存在旧 PID 的 `window-destroy`，但它们不能归属本次 13:01 run，必须按 PID/run 分离。

## 3. 未闭合项

```text
关闭动作类型        = 未发生或未被当前 PID 记录
hide-only vs exit    = 未判定
Host PID/hostInstanceId/epoch = 未在本包形成同代 receipt
RELEASE_TARGET/SHUTDOWN       = 未证明
virtual DS4 device-gone       = 未观察
Steam consumer readback       = 未执行
DS4 descriptor/raw report     = DISABLED_SAFE_DEFAULT
PS4 5% producer               = UNKNOWN / RUNTIME-BLOCKED
```

`rog-input-gap-evidence.json` 明确标记 `descriptorBoundary=UNAVAILABLE_READONLY_HIDD`、`steamConsumerReadback=ABSENT_NOT_PERFORMED`，因此不能从本包猜 battery byte、profile 或 Steam producer。

## 4. 下一次介入动作

要闭合“关闭 YMCC 后 PS4 出现”，必须在同一次 20 秒采集中实际执行关闭动作，并回传未手动重打包的 ZIP：

1. 运行新 standalone 包的 `Start-YeManCC-GyroInput-Test.cmd`。
2. 在采集窗口内点击窗口 `X`；如果窗口只是隐藏，再从托盘执行真正退出。
3. 记录关闭前后 Steam/系统是否出现 `Wireless Controller`/`PS4 Controller`。
4. 回传完整 ZIP，同时保留 `capture-last-run.txt`（若 CMD 仍不结束）。

只有当前 PID 同时出现 `window-close-request`、`window-destroy`、`input-host-release-receipt`，并结合 PnP/Steam readback，才能继续判断是 hide-only、Host release 未完成，还是外部 consumer/producers 差异。

