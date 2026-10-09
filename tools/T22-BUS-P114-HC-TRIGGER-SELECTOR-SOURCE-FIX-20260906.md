# T22 BUS-P114：HC trigger resolution / ROG selector coverage source fix

日期：2026-09-06  
状态：`SOURCE-FIXED / BUILD-VERIFIED / STANDALONE-LANE-ONLY / RUNTIME-BLOCKED`

## 1. 权威与范围

本页只登记两处能直接回链锁定 HC 源码的局部修正：

1. 未知 `motionTrigger` 不再被静默解释为“未按下”；
2. 已有 HC 明确矩阵的 `RC73YA` 不再被错误限制在 `RC73XA` 测试门。

本页不解除正式产品 `pairProven=false`、direct-IMU `no-report`、T15/T16/T17/T18 runtime gate，也不把 Host-local receipt 推导为 HID/Steam/game consumer receipt。

HC authority：

- `deps/handheldcompanion-runtime/source/Managers/MotionManager.cs:203-259`
- `deps/handheldcompanion-runtime/source/Devices/ASUS/XboxROGAlly.cs:22-42`
- `deps/handheldcompanion-runtime/source/Devices/ASUS/XboxROGAllyX.cs:22-42`

## 2. 已确认的 source divergence 与修正

### P114-F01：未知 trigger 被当成未按下

旧实现的 `inputCaptureMotionTriggerPressed()` 对未知字符串直接返回 `false`。这会使：

- `MotionMode.Off` 永远不触发；
- `MotionMode.On` 反而可能永远打开；
- 配置拼写/枚举不一致时没有 safe-stop 证据。

当前修正：

- 空 trigger、`none`、`disabled` 仍代表 HC `ButtonState` 的无按钮状态；
- 已支持的 XInput button/trigger 名称继续按现有映射解析；
- 其他字符串将 `motionTriggerResolved=false`，并令 `motionEnabled=false`、motion contribution safe-zero；
- telemetry 记录 `motionTriggerResolved`，不把错误配置伪造成“未按下”。

这保持 HC 的关键原则：`MotionTrigger` 必须先是已解析的按钮状态，再参与 `Off/On/Toggle` 语义；native 不自行猜测未知按钮。

### P114-F02：RC73YA 有 HC 矩阵却被限制为 RC73XA

锁定 HC 的 `XboxROGAlly.cs` 与 `XboxROGAllyX.cs` 都明确提供：

```text
gyro  = (rawX, rawZ, -rawY)
accel = (-rawX, -rawZ, rawY)
```

当前 selector 已能分别解析 `RC73XA → asus-rc73xa` 与 `RC73YA → asus-rc73ya`，但旧 `selectorBound` 只接受 `asus-rc73xa`。修正后：

- `RC73XA` 与 `RC73YA` 都可进入 standalone marker + same-provider admission；
- matrix 仍只在 `pairProven && selectorBound` 成立时进入 `GamepadMotion.ProcessMotion()`；
- `matrixIdentity`、`motionPairProofId` 随实际 selector 分别记录；
- standalone 包生成器的 `package-summary.json` / collector metadata 使用 selector-neutral 的 `hc-test-same-provider-selector-v1`，不再把 RC73YA 证据错误标记为 RC73XA；
- 普通 ROG `RC71L/RC72LA` 仍保持 unresolved，不套用 Xbox ROG 矩阵。

## 3. 非 HC 行为没有被扩展

以下边界保持不变：

- HC `GyroActions.DefaultGyroWeight=1.2` 对应的 `gyroWeight - padNorm` blend 保留；它有 HC `TouchpadActions.ApplyGyroOutput()` 直接依据，不属于新增原创曲线。
- HC calibration lock 仍只使用 `confidence == 1.0`、finite offset、weight/API 条件；`steady` 只作 telemetry。
- 正式包无 `HC-REAL-STICK-TEST-v1` marker 时仍为 `pair-unproven-safe-zero`，不提交 direct IMU。
- 未扩大 ROG vendor PID、未启用 Raw HID parser 作为普通 XInput 替代路径、未修改 HidHide/Steam/游戏/真实设备状态。

## 4. 验证证据

### 静态/构建

```text
calibration safety delta selftest       PASS
gyro motion mapper mock                 PASS
gyro calibration session mock            PASS
E-37 HC parity selftest                  PASS
input contracts selftest                PASS
input host supervisor selftest          PASS
ROG gyro protocol selftest               PASS
special controller protocols selftest   PASS
T8 Host loop mock                        PASS
vue-tsc --noEmit                         PASS
native/workspace build                   BUILD_OK
```

当前产物：

```text
YeManCC.exe
sha256 = 29F96CA2EE8C730358A9E4C5D658334A90E10527DEAF794FC6FF70CADA7B2B61
bytes  = 2,255,360

YeManInputHost.dll
sha256 = 0C4DC714C8AF3468FBA10D509FB8EE4A4836F303AF407FED431EB6C15E3CDE51
bytes  = 50,176
```

### Standalone 测试包

```text
path   = Mainline/Build/TestPackages/GyroInput-ROG-20260906-185654/YeManCC-GyroInput-ROG-Test.zip
bytes  = 88,604,210
sha256 = B20FB5948D8B69264AF1B2448AA356D8AB58E7371EC09B443064BF8304A69941
HC     = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
formalReleasePackageUntouched = true
systemMutation = false
```

包只用于 standalone 真实设备验证；没有部署到 `C:\SOFT\YeMan`，没有运行 HidHide、Steam、游戏或实机。

## 5. 未知、缺口与需要用户介入

以下仍不是 source-level 可闭合项：

- `StoreCalibration(deviceInstanceId, calibration)` 等价持久化与 calibration identity；
- WinRT sensor DeviceId ↔ PnP/container ↔ HC controller identity 的完整绑定；
- HC PnP/XInput slot identity，不得以最低 slot 代替；
- DS4 descriptor/report/encode/decode/raw readback；
- Host/HID/OS/Steam/game external consumer receipt；
- sleep/PnP/Host-crash recovery 的同代 release/rearm receipt；
- 普通 ROG `RC71L/RC72LA` selector 与 HC 矩阵仍未接通；
- 当前工作树与多份历史 manifest 已变化，必须重新执行 `T22-RF00 → T20 → T21 → T14`。

这些项目继续保持 `UNENCLOSED / RUNTIME-BLOCKED`。下一步需要用户在可用 ROG 设备上运行本页包并回传同代 capture、calibration、first-frame、DS4/Steam/game 独立观察；在此之前不以猜测替代证据。
