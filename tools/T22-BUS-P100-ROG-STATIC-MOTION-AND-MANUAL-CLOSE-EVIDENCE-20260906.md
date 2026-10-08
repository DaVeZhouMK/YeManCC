# T22 BUS-P100：ROG 静止—转动—静止与手动关闭证据（2026-09-06）

状态：`RUNTIME-EVIDENCE-RECORDED / MOTION-PRESENCE-CLOSED / PNP-GONE-PROVEN / PAIR-MATRIX-UNENCLOSED / STEAM-UNPROVEN`

## 1. 输入证据包

### 第一次：静止—转动—静止

```text
source = C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-142218.zip
bytes  = 95,210
sha256 = B4988428278D3C358DA3E5EC74C90E6AF41CC33DB6EAC495D1A1D57AE6B1A771
capture = 2026-09-06T06:22:18.789Z → 06:22:47.917Z
launch PID = 2360
```

ROG identity：`ASUSTeK COMPUTER INC. / ROG Xbox Ally X RC73XA_RC73XA`，BIOS `RC73XA.317`，HC baseboard product `RC73XA`。

WinRT provider：gyro 与 accel 均 `present`，minimum report interval `10 ms`，observed current interval `100 ms`；每个传感器 454 callbacks，collector child `exitCode=0`。YMCC raw input capture 有 267 个 sample 行（另含 start/pair-unproven/stop）。

按 raw sample 的时间四分段复核（gyro 为 `sqrt(gx²+gy²+gz²)`）：

```text
segment   samples   gyro average   gyro max   gyro <=5 deg/s   accel magnitude range
start        67        52.29        705.14          35             0.96–1.45 g
middle      133      1059.91       2569.13           3             0.81–1.18 g
end          66       400.95       3267.28          41             0.74–1.32 g
```

这足以证明该 ROG 设备在用户指定窗口内输出了真实非零运动数据，并且存在低角速度片段；但末段仍有较高峰值，不能宣称静止漂移已解决或已符合 HC calibration。

### 第二次：打开后手动关闭

```text
source = C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-142335.zip
bytes  = 39,608
sha256 = CF736A8C747224AB8F55369F380A1A297CC3F3164B183C565660CBE3B3F9B0CA
capture = 2026-09-06T06:23:35.204Z → 06:24:02.077Z
launch PID = 10144
runId = A1E337BD-B8B8-44C2-9468-A77CCB9DBF59
epoch = 1
```

当前 PID 的关键生命周期顺序：

```text
14:23:38.702  window-hidden
14:23:41.784  rog.xbox-face-enabled(reason=exit, wrote=false)
14:23:46.259  rog.xbox-face-enabled(reason=destroy, wrote=false)
14:23:46.271  input-host-stop-request(hadPreparedTarget=true, hadProcess=true, transportUsable=true)
14:23:46.488  input-host-release-receipt(ok=true, local-dispose-complete/external-release-unproven)
14:23:46.507  input-host-stop-result(localReleaseAck=true)
14:23:46.507  window-destroy(exitRequested=true)
```

PnP before/after：`16 → 14`。消失的节点为：

```text
HID\\HIDCLASS\\1&4784345&31&0000   (Wireless Controller)
ROOT\\HIDCLASS\\0001             (Wireless Controller)
```

after 进程列表不再包含 `YeManCC` 或 `YeManInputHost`，但 `YeManRecoveryService` 和 Steam/steamwebhelper 仍在。

## 2. 可闭合与不可闭合

### 已闭合

- ROG WinRT gyro/accel provider presence：`RUNTIME-RECEIPT / COLLECTOR-CLOSED`。
- 当前测试包 collector 正常结束：两次 IMU child 均 `exitCode=0`。
- 手动关闭的 YMCC Host-local stop/release 顺序：`RUNTIME-RECEIPT`。
- 同一运行的 OS/PnP virtual target disappearance：`PNP-GONE-PROVEN`。
- 正式 Release HidHide 发布边界：用户裁决后已修改生成器，正式包重建成功，A1=`PASS`。

### 仍未闭合

- gyro/accel same-provider/same-epoch pair、HC matrix、calibration confidence、first paired sample；日志仍为 `sample-pair-unproven`、`matrixIdentity=unresolved-rog-pid-family/raw`、`confidence=0`。
- UI 轴线图/虚拟摇杆的真正 consumer receipt；当前 raw capture 的 `playerSpace/worldSpace` 仍为零，不能把传感器到 UI 的链路视为已闭合。
- DS4 descriptor/raw report、battery byte/producer、Steam/Windows/GameInput readback；`descriptorBoundary=UNAVAILABLE_READONLY_HIDD`，Steam readback 未执行。
- Steam/game consumer 是否停止读取虚拟设备；Steam 进程仍在，当前包没有独立 consumer count。

## 3. 采集策略结论

通用“静止—转动—静止”采集已经足够证明 provider presence 和基本数据到达，**不再重复同类采集**。数据采集阶段可暂时终止，转入下一测试阶段：

1. T11/T16：定向 pair、matrix、calibration/steady/offset 与 UI output 证据；
2. T17：DS4 descriptor、raw input/feature report、独立 decoder；
3. T18：Steam/game consumer 与恢复读回。

只有在上述定向阶段缺少具体字段时，才重新生成窄范围采集包；不再用重复通用采集替代缺失的 consumer/descriptor 证据。

## 4. 固定边界

```text
runtimeUpgrade = false
T11/T16 pair-matrix-calibration = UNENCLOSED
T17 descriptor/battery/consumer = RUNTIME-BLOCKED
T18 Steam/game consumer = UNENCLOSED
formal Release HidHide          = FORBIDDEN / A1-PASS
```
