# T22 BUS-P96：ROG 立即退出的 Host 生命周期证据（2026-09-06）

状态：`COLLECTOR-CLOSED / LOCAL-HOST-RELEASE-PROVEN / EXTERNAL-RELEASE-UNPROVEN`

## 1. 输入工件

```text
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-131243.zip
bytes = 11,919
sha256 = DDB23BF077908ACB0531BFA44CF272BD7CC6A6A0EA6D472CBEC1619116564762
```

运行窗口：`2026-09-06 13:12:43–13:13:10`（20 秒采集包）。本次用户说明为“启动后马上退出”。

## 2. 同一 PID 的退出顺序

本次 launch PID 为 `16108`，Host PID 为 `16324`，同一 `runId`/`epoch` 为：

```text
runId = CAC2406E-DAD1-4321-B8B6-A57F6A745C4D
epoch = 1
hostInstanceId = 7a0013aa55374713aabfc845072534ec
```

关键事件顺序：

```text
13:12:44  boot-single-instance-acquired / window-created / recovery-service-started
13:12:47  rog.xbox-face-enabled(reason=exit)
13:12:47  exit-cleanup-worker-detached
13:12:47  rog.xbox-face-enabled(reason=destroy)
13:12:47  input-host-stop-request(reason=coordinator-stop, hadPreparedTarget=true, transportUsable=true)
13:12:47  input-host-release-receipt(ok=true)
13:12:47  input-host-stop-result(localReleaseAck=true)
13:12:47  window-destroy(exitRequested=true)
```

释放回执：

```text
disposition = local-dispose-complete/external-release-unproven
hostPid = 16324
hostInstanceId = 7a0013aa55374713aabfc845072534ec
```

这证明了当前 YMCC/Coordinator 到 InputHost 的本地 `RELEASE_TARGET` 路径已经收到成功回执；不能把该回执升级成 OS PnP device-gone、Steam consumer release 或游戏消费者 release。

## 3. 进程与 PnP before/after

```text
before PnP = 14
after PnP  = 14
PnP identity diff = none
```

before/after 都没有 `Wireless Controller` 或 `ROOT\\HIDCLASS\\0001`，只看到：

```text
ROG Xbox Ally Device Gamepad = VID_0B05&PID_1B4C
physical X360                = VID_045E&PID_028E
gvinput Device               = ROOT\\HIDCLASS\\0000
Nefarius Virtual Gamepad Bus= ROOT\\SYSTEM\\0001（若在完整列表中）
```

因此本次 run 没有观察到 DS4 `Wireless Controller` 在 before/after 出现或消失；它不能证明 Steam 没有短暂枚举，也不能解释另一包中采集开始前已存在的 `ROOT\\HIDCLASS\\0001`。

## 4. IMU 与 YMCC 输入链路

立即退出使本次 IMU collector 只得到少量样本：

```text
WinRT status       = polling-fallback
gyro samples       = 5
accel samples      = 1
event subscription = Windows PowerShell 无法订阅 Windows RT 事件
```

YMCC JSONL 同时记录：

```text
kind=start
kind=sample-pair-unproven(reason=provider-capability-matrix-not-bound, safeZero=true)
kind=sample
kind=stop
```

这次能证明输入采集在退出时写出 `kind=stop`，但不能证明 HC pair/matrix/calibration 或右摇杆已被游戏消费。

## 5. 仍未闭合的外部边界

```text
Host-local release ACK       = PROVEN
InputHost process stop       = not independently enumerated after release
OS PnP device-gone           = not proven (before/after no DS4 node)
Steam consumer readback      = ABSENT_NOT_PERFORMED
DS4 descriptor/raw report    = DISABLED_SAFE_DEFAULT
PS4 battery producer         = UNKNOWN / RUNTIME-BLOCKED
window X vs tray exit route  = no window-close-request; exact user exit route unknown
```

主线处置：不再把“本地 Host release”列为本次缺口；继续保留 `G-TARGET-RELEASE` 的外部 device/consumer 证据缺口，不修改关闭行为，不猜 battery 或 Steam producer。

