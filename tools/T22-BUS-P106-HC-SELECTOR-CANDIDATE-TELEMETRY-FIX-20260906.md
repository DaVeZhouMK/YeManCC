# T22 BUS-P106：RC73XA 的 HC selector/matrix candidate telemetry 最小修正

日期：2026-09-06  
状态：`SOURCE-FIXED / DIAGNOSTIC-ONLY / PAIR-UNPROVEN / RUNTIME-BLOCKED`

## 1. 修正动机

P103 用户包给出了 `ASUSTeK COMPUTER INC. / RC73XA`；P105 已由锁定 HC 源码证明：

```text
RC73XA → HandheldCompanion.Devices.XboxROGAllyX
gyro  = Axis(1,1,-1) + X→X,Y→Z,Z→Y
accel = Axis(-1,-1,1) + X→X,Y→Z,Z→Y
```

此前 native 只有 ROG HID PID-family 识别，不能把 HID PID 当成 HC 机型 selector，因此日志一直写 `unresolved-rog-pid-family/raw`。本轮只修正“把已证实的 DMI selector 记录出来”这一缺口；不把它升级为 provider binding 或 motion admission。

## 2. current-source 修改

`native/main.cpp` 新增只读 `HcMotionSelectorReceipt`：

```text
HKLM\HARDWARE\DESCRIPTION\System\BIOS
  BaseBoardManufacturer
  BaseBoardProduct
  SystemProductName
```

仅精确匹配：

```text
ASUSTEK COMPUTER INC. + RC73XA → XboxROGAllyX
ASUSTEK COMPUTER INC. + RC73YA → XboxROGAlly
```

新增日志/采集字段：

```text
dmiManufacturer
dmiProduct
dmiSystemProduct
hcSelectorId
hcDeviceClass
hcGyroMatrixId
hcAccelMatrixId
matrixSourceSha256
hcSelectorResolved
providerBindingProven=false
hostOutputAdmitted=false
```

同时保留 `raw` gyro/accel，并增加单独的 `hcMatrixCandidate` 诊断值：

```text
gyro  candidate = (rawX, rawZ, -rawY)
accel candidate = (-rawX, -rawZ, rawY)
```

当前 `matrixIdentity` 在 selector 已解析但 provider 未绑定时写为：

```text
unresolved-provider-binding
```

避免把“DMI candidate 已知”误读为“WinRT provider 已绑定”。

## 3. 明确未改变的安全门

```text
pairProven                 = false
GamepadMotion ProcessMotion = not admitted
outX / outY                = 0.0 / 0.0
gyroSubmitAllowed          = false
P-HID / P-XINPUT / P-OWNER = unchanged
T10 parameter path         = unchanged
DS4 battery presentation   = unchanged
```

因此这次修正不会让 raw gyro 进入游戏、HIDMaestro 或 Host，也不会绕过校准/生命周期合同。

## 4. 构建证据

```text
command  = cmd /c build_native.bat
result   = BUILD_OK
output   = G:\YeManCC-Work\Mainline\Build\App\Native\YeManCC.exe
sha256   = 1C6D87DD30692DCC12221FF79293C54399F3FA89425679F8199BABDAE7EE4075
```

native protocol selftest：

```text
command  = YeManCC.exe --inputhost-protocol-selftest
exitCode = 0
```

该 selftest 只验证既有协议边界；本轮没有启动真实 YMCC、InputHost、HIDMaestro、HidHide、Steam、游戏或硬件。

## 5. 与用户日志的关系

这次修正直接消费 P103 日志中的 DMI 事实，但不改写 P103 原始结论：

```text
gyro/accel present       = true
P103 pair                = UNPROVEN
P103 confidence          = 0
P103 right-stick output  = safe-zero
```

下一次 ROG 运行包不需要重复普通静止—转动—静止；只需确认 native 自己的 selector receipt 与外部采集器都得到 `RC73XA`，并继续收集 provider DeviceId/PnP container/normalized timestamp/pair receipt。电量小于 5% 提示仍按低优先级 presentation issue 处理，不阻塞这条链路。

