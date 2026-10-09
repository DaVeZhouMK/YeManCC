# T22 BUS-P105：ROG RC73XA → HC selector / matrix 静态证据

日期：2026-09-06  
状态：`STATIC-EVIDENCE-ADDED / RUNTIME-BINDING-OPEN / NO-OUTPUT-ADMISSION`

## 1. 目的与边界

本页记录用户回传的 ROG 运行包中已经存在、且可以回链到锁定 HC 源码的机型选择与轴矩阵事实。它不是对 `pairProven`、校准、右摇杆、HID、Steam 或游戏消费者的闭口；也不允许把外部采集器的 DMI 观察直接当成 YMCC native 已完成的 selector 绑定。

本轮只做读取、哈希和源码对账：没有启动 YMCC、InputHost、HIDMaestro、HidHide、Steam、游戏或真实设备；没有修改系统状态；没有解除 `pairProven=false`；没有把 raw gyro 写入 Host。

## 2. 用户证据包

来源：

```text
C:\Users\DaVe\Desktop\陀螺仪\YeManCC-ROG-Input-Gap-Evidence-20260906-151549.zip
bytes  = 121,715
sha256 = 9A9972519FCBB26DF63E6073FB5F1A385448392F607F6C10E963C733DBCE7F62
```

包内 `imu-rog-imu-evidence.json` 的可回链事实：

```text
collector.durationSeconds = 30
collector.startsYeManCC = false
collector.invokesHidHide = false
collector.invokesOemDisable = false
collector.createsVirtualController = false

smbios.computerSystemProduct.vendor = ASUSTeK COMPUTER INC.
smbios.computerSystemProduct.name = ROG Xbox Ally X RC73XA_RC73XA
smbios.bios.smbiosBiosVersion = RC73XA.317
hcDeviceIdentity.baseboard.product = RC73XA
hcDeviceIdentity.computerSystem.systemFamily = ROG Xbox Ally X
```

同一包明确保留了边界：

```text
identityAndPairingBoundary.pnpToWinRtIdentity = UNPROVEN
identityAndPairingBoundary.gyroAccelPair = UNPROVEN
```

因此本页只把 `RC73XA` 作为外部采集器在该 run 中观察到的 physical/DMI identity，不把它伪装成 native 已经完成的 provider binding。

## 3. HC selector 与矩阵事实

锁定 HC `0.32.4.0 / 06c0b9544db2b1f39abf9cd3796225ccfb096103` 中：

```text
HandheldCompanion/Devices/IDevice.cs
ASUSTEK COMPUTER INC. + ProductName RC73XA
→ new XboxROGAllyX()
```

`XboxROGAllyX.cs` 的 HC 运动矩阵为：

```text
GyroMatrix:
  Axis     = ( 1,  1, -1)
  AxisSwap = X→X, Y→Z, Z→Y

AcceleroMatrix:
  Axis     = (-1, -1,  1)
  AxisSwap = X→X, Y→Z, Z→Y
```

HC `IMUGyrometer.cs` / `IMUAccelerometer.cs` 先读取当前 device 的 `AxisSwap` 与 `Axis`，再执行 remap/sign；gyro 的阈值来自当前 GamepadMotion calibration，不由 `RC73XA` 字符串直接推导固定数值。

对应 current/frozen source hashes：

```text
HC Devices/IDevice.cs          FBAED1DFD329E3328376488B8EC70C3A7FDCF97D70DCB37F1525EE33AA169C20
HC Devices/ASUS/XboxROGAllyX.cs 404987B6116C5DC79FEC80AABDF97D6E28F146EC2FFC3AF69ED8FC71E40FF8EE
HC Sensors/IMUGyrometer.cs     BC44A51518C82BDDAF7BA2D79A37A7F57EF762E7F0D5F5B291DBB472077930E1
HC Sensors/IMUAccelerometer.cs F63C11C0729B013E7B03D717C09F25FC099A8433752B52DE9E3826BCDFFF7B4C
```

## 4. 与 YMCC current source 的三方结论

| 层 | 已证实 | 尚未闭合 |
|---|---|---|
| 用户包 / ROG runtime | 该 run 的 DMI/BaseBoard 为 `RC73XA`；gyro/accel 均 present；采集器没有执行 HidHide、OEM disable 或创建虚拟手柄 | 采集器自己声明 PnP→WinRT identity 与 gyro/accel pair `UNPROVEN` |
| HC source | `RC73XA → XboxROGAllyX`；该 class 的 gyro/accel remap/sign 已明确 | per-device threshold 仍来自 calibration，不可由本页猜出运行时数值 |
| YMCC native current source | 使用 typed `GetDefault()`，保留 raw 值，并把 `matrixIdentity` 写成 `unresolved-rog-pid-family/raw`；`pairProven=false` 时安全归零 | native 没有把 DMI selector receipt 绑定到 provider；没有应用上述 HC matrix；GamepadMotion/Host motion admission 仍未打开 |

结论：

```text
HC selector/matrix authority = STATIC-RESOLVED-FOR-RC73XA
YMCC runtime selector binding = UNENCLOSED
pair/calibration/output       = STILL-BLOCKED
```

这将原先的“ROG 机型矩阵未知”收窄为“RC73XA 的 HC 参考矩阵已知，但 YMCC native 尚未完成可回链 selector/provider 绑定”。它不等价于已经允许把矩阵写入 Host，也不改变 `T11 = PAIR-UNPROVEN / SAFE-ZERO`。

## 5. 电量提示处置

本包中的 DS4/PS4 低电量提示仍按主线既定裁决处理：`USER-OBSERVED / LOW-PRIORITY-PRESENTATION-ISSUE`。它不改变本页的 ROG selector/matrix 证据，也不阻塞 gyro/accel provider 证据继续推进。

## 6. 下一步缺口

只剩下需要 runtime/源码 receipt 的窄范围问题：

```text
native DMI selector receipt
→ same provider identity / container
→ HC matrix applied receipt
→ calibration threshold key + transaction
→ same-revision first paired GamepadMotion plane
→ Host first-frame / HID / Steam-game observation
```

本页没有解除任何安全门；在上述链路未闭合前，禁止把 RC73XA 矩阵直接硬编码到 raw path，禁止删除 `pairProven=false`，禁止把 `outX/outY=0` 改成 raw gyro 或固定轴猜测。

