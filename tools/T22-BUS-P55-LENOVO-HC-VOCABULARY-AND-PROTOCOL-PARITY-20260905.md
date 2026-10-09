# T22 BUS P55 — Lenovo/Legion 词表扩展与 HC 协议分层对账

Status: `SOURCE-SCOPED-STATIC-AUDIT / HC-LENOVO-ROLES-EXPANDED / CLASSIFICATION-ONLY / MOTION-RUNTIME-UNENCLOSED / RUNTIME-BLOCKED`

Date: 2026-09-05. 本轮只读取锁定 HC 源码、当前 YMCC source 与既有 MD；未启动 YeManCC/InputHost/HIDMaestro/HidHide，未创建虚拟设备，未操作 Steam/游戏/真实手柄/PnP/电源，也未改变系统状态。除本审计 MD 与主 TASK 回链/纠偏外，不修改运行时代码、JSON、包体或驱动。

## 1. Authority / provenance

| Layer | Authority | Scope |
| --- | --- | --- |
| HC | `Isolated/HC-Candidate-0.32.4.0-06c0b9544db2b1f39abf9cd3796225ccfb096103` | locked source, clean worktree at prior capture |
| HC device selection | `HandheldCompanion/Devices/IDevice.cs:856-875` | Lenovo `SystemModel` → device class |
| HC device protocol | `Devices/Lenovo/{LegionGo.cs,LegionGoTablet.cs,LegionGoTablet2.cs,LegionGoSZ1.cs,LegionGoSZ2.cs}` | OEM HID monitor, matrix, attach and model inheritance |
| HC controller protocol | `Controllers/Lenovo/{LegionControllerBase.cs,LegionController.cs,LegionControllerS.cs}` + `controller-hidapi.net/{GenericController.cs,LegionController.cs}` | XInput companion, HID report reader, motion decoder |
| HC routing | `Managers/{DeviceManager.cs,ControllerManager.cs}` | XUSB/HID arrival, VID/PID and controller class selection |
| YMCC | `src/bridge/specialControllerProtocols.ts` + `tools/special_controller_protocols_selftest.ts` | current static classifier only; no production consumer found |

Relevant read hashes were captured during this audit for traceability:

```text
HC IDevice.cs                         FBAED1DFD329E3328376488B8EC70C3A7FDCF97D70DCB37F1525EE33AA169C20  48018 bytes
HC Devices/Lenovo/LegionGo.cs         7F44A3B72C08823D21FE427AE8E389B67781B6B1F8ADF13875EF7F6DA12BDFCF  15499 bytes
HC Devices/Lenovo/LegionGoTablet.cs   A05910FDDD410A68E00F028B3760C784A6AA5A8BC0FCA27BEE96ED20D59F3414  14058 bytes
HC Devices/Lenovo/LegionGoTablet2.cs  823586B37D8580AE5545B2B1D5EA091F7CD066037B78C4E04184E77729F804B2  5646 bytes
HC Devices/Lenovo/LegionGoSZ1.cs      176C4AEE0153AAA9956837B95B044E493C90AE0C5CD86EAAA1AC2754CA54CF66  8890 bytes
HC Devices/Lenovo/LegionGoSZ2.cs      E03453471F06F9FECCFA4DB9AD165E0203DDF4CA69C141D011C4397C9AA5A241  154 bytes
HC Controllers/Lenovo/LegionController.cs   F22DA7BE57ADFD962176CB2E58A2C33259658D700C0CDA2975204CDD5A1E5F77  20075 bytes
HC Controllers/Lenovo/LegionControllerS.cs  46DC972BF2FA09B710D52FE472469A316DB85775C70CB52C81F8CBBCCFBB13B2  5059 bytes
HC controller-hidapi.net/LegionController.cs BB38C5DF07224E048069DEC7A9DAC46A5412E5F30235584EE34ECC7AD2662337 562 bytes
YMCC src/bridge/specialControllerProtocols.ts 145CC60A6BCA581300E2CCC9FA52FD88664A6EF5CD8AE90073FF88F8F709014D 2736 bytes
YMCC tools/special_controller_protocols_selftest.ts 015EF38071D0150C6F71395A017E2CB180BECEC96953D769F430C93704DA872E 1662 bytes
```

## 2. Expanded Lenovo vocabulary

这些词必须按层使用，不能把“Lenovo/Legion”当成单一普通 Xbox 类型：

```text
OEM / DMI:
  LENOVO, SystemModel, ProductName, MotherboardInfo.Product,
  Legion Go, Legion Go 2, Legion Go S Z1E, Legion Go S Z2 Go

device classes:
  LegionGo, LegionGoTablet, LegionGoTablet2,
  LegionGoSZ1, LegionGoSZ2

controller classes:
  LegionControllerBase, LegionController, LegionControllerS,
  controller_hidapi.net.LegionController, GenericController,
  XInputController, DInputController

physical faces / modes:
  XInput, xinput, DInput, DirectInput, dinput,
  dual_dinput, dual DInput, FPS, FPS interface,
  old FW, 2025 FW, companion HID, controller face

identity / PnP:
  VID, PID, VID:PID, UsagePage, Usage, HidFilter,
  deviceInstanceId, baseContainerDeviceInstanceId,
  devicePath, baseContainerDevicePath, SymLink, InterfaceGuid,
  EnumeratorName, MI_, isXInput, XInputUserIndex, XInputDeviceIdx,
  PnPDetails, physical, virtual, USB, HID, XUSB

report / reader:
  inputBufferLen, InputReportByteLength, OutputReportByteLength,
  OnControllerInputReceived, BeginRead, EndRead, Reading,
  IsDeviceValid, GetStatus, DeviceVersion, HID report misalignment,
  borked state, shifted by 2 bytes, attach, gone, plug, unplug

motion / sensor:
  MotionSensor, GamepadMotion, GyroMatrix, AcceleroMatrix,
  AxisSwap, AxisRemapIndices, GyroState, ProcessMotion,
  LeftJoyconIndex, RightJoyconIndex, controller gyro, controller accel,
  gyro index, LegionControllerGyroIndex, wired, wireless,
  timestamp, acceleration, angular velocity, calibration threshold

OEM HID / conditional DLL:
  SapientiaUsb.dll, USE_SAPIENTIAUSB, SetGamePadMode, GetGamePadMode,
  SetLeftGyroStatus, SetRightGyroStatus, SetGyroState,
  Get/SetGyroMode, Get/SetGyroModeStatus, Get/SetGyroSensitivity,
  Get/SetGyroXYSensitivity, Get/SetGyroAxisReverse,
  Get/SetGyroDeadzone, Get/SetGyroOutputMixer,
  SetGyroDataBackFunc, SetGyroSensorStatusBackFunc,
  StartGyroCalibration, StopGyroCalibration,
  SetFPSMode, SetGamepadOnorOff, setHandleMode, setMode

user-facing input semantics:
  LegionControllerPassthrough, LegionControllerSwap,
  RightPad, RightPadTouch, RightPadClick, RightPadClickDown,
  OEM1/LegionR, OEM2/LegionL, L4/L5/R4/R5, B5..B11,
  Page, Desktop, ScrollClick, M1/M2/M3, Y1/Y2/Y3
```

`SapientiaUsb` 的函数名是 HC 源码中的条件/Raw API surface；当前 `LegionGoTablet.cs` 顶部 `USE_SAPIENTIAUSB` 被注释，默认活动路径是 `HidLibrary.HidDevice.Write(...)`。因此“词汇存在”不能写成 YMCC 已接入或 HC 每个 API 都在当前产品路径被调用。

## 3. HC model / VID:PID / role matrix

| HC `SystemModel` | Device class | VID | PID / firmware | HC role in source | Motion source |
| --- | --- | ---:| --- | --- | --- |
| `83E1` | `LegionGoTablet` | `17EF` | `6182` XInput; `6183` DInput; `6184` dual DInput; `6185` FPS; `61EB/61EC/61ED/61EE` same four roles for 2025 FW | device-level monitor accepts all eight; XUSB controller path special-cases `6182/61EB`; HID views are not generic controller instances | Windows matrix + `LegionController` paired HID report |
| `83N0`, `83N1` | `LegionGoTablet2 : LegionGoTablet` | inherited `17EF` | inherited eight PID roles; no new PID list in this class | same inherited controller/HID surface; class explicitly overrides motion matrix and fan/EC behavior | Windows matrix override; controller path remains inherited |
| `83L3` | `LegionGoSZ2 : LegionGoSZ1` | inherited `1A86` | inherited `E310` XInput, `E311` DInput | same inherited SZ1 controller/HID surface | Windows matrix inherited from SZ1; `LegionControllerS` for XInput |
| `83N6`, `83Q2`, `83Q3` | `LegionGoSZ1` | `1A86` | `E310` XInput, `E311` DInput | XUSB path special-cases `E310`; DInput companion is not an ordinary generic XInput controller | Windows matrix + `LegionControllerS` report parser |

The exact device selection is by `LENOVO` + `SystemModel`; VID/PID alone cannot select the matrix. The two vendor families must remain paired:

```text
17EF ↔ 6182/6183/6184/6185/61EB/61EC/61ED/61EE
1A86 ↔ E310/E311
```

The HC device classes also use `UsagePage=0xFFA0`, `Usage=0x0001` as the OEM HID filter. `LegionGoSZ1` additionally requires `InputReportByteLength == 65` and `OutputReportByteLength == 65`; the base/tablet readiness path does not perform that length check. This distinction is source fact, not permission to guess a report descriptor.

## 4. HC routing distinction: XUSB vs HID

HC has two separate event routes:

1. `DeviceManager.RefreshXInput()`/`XUsbDeviceArrived()` marks XUSB details with `isXInput`, assigns `baseContainerDevicePath`, `XInputDeviceIdx`, and `XInputUserIndex`, then `ControllerManager` creates `LegionController` for `17EF:6182/61EB` and `LegionControllerS` for `E310`. Other XUSB PIDs have generic `XInputController` fallback.
2. `RefreshDInput()`/`HidDeviceArrived()` skips details already marked `isXInput`. In the locked `ControllerManager.cs:698-736`, `17EF:6183/6184/61EC/61ED` are explicitly listed and left without a generic controller instance. The `E311` case appears nested under the `0x17EF` branch rather than a separate `0x1A86` branch; this is an HC source oddity/possible unreachable case, not a safe YMCC rule. It must remain `HC-OBSERVED / UNRESOLVED`, not be “fixed” by adding a guessed controller route.

Consequently the previous broad wording “Lenovo remaining PIDs go to ordinary XInputController” was too wide. The correct statement is conditional: generic XInput fallback exists on the XUSB route for unlisted XUSB PIDs; the listed DInput/dual-DInput/FPS HID faces are not thereby ordinary XInput consumers.

## 5. HC controller-report and motion facts

### `LegionController` (17EF XInput companion)

- `LegionControllerBase` wraps `controller_hidapi.net.LegionController`, subscribes `OnControllerInputReceived`, opens/ends the reader, and releases it on `Gone()`/`Unplug()`.
- The adapter default is `inputBufferLen=64`; HC attaches `gamepadMotions[1]` to `${baseContainerDeviceInstanceId}\\4`, where `RightJoyconIndex=4`.
- Report status indexes are `LCONTROLLER_STATE_IDX=12` and `RCONTROLLER_STATE_IDX=13`; status values distinguish `Wired=2` and `Wireless=3`. `IsHidReportMisaligned()` checks `GetStatus(1)==0`; when true, selected indexes are shifted by `-2` and a warning is logged.
- The report contains separate left/right timestamps and motion blocks: left timestamp 32, accel 35, gyro 41; right timestamp 45, accel 48, gyro 54. The left and right Joy-Con paths use different axis order/signs.
- Left path: big-endian `Int16`, accel scale `4/Int16.MaxValue`, gyro scale `2000/Int16.MaxValue`, with HC's explicit signs and axis arrangement.
- Right path: big-endian `Int16`, accel and gyro axes are reordered differently from left; gyro Z is positive while left Z is negative in this source. The selected controller writes `GyroState`, `Accelerometer`, then calls `GamepadMotion.ProcessMotion(...)`.
- `LegionControllerGyroIndex` selects which controller's motion is exposed as the selected `GyroState`; both controller motion objects may still be processed.

### `LegionControllerS` (1A86 E310 XInput companion)

- HC uses `inputBufferLen=33`, not the 64-byte Tablet controller buffer.
- The parser uses `BitConverter.ToInt16` (little-endian on Windows) at byte offsets 14/16/18 for accel and 20/22/24 for gyro, with accel scale `4/Int16.MaxValue` and gyro scale `2000/Int16.MaxValue`.
- It calls `GyroState.SetGyroscope`, `SetAccelerometer`, and `GamepadMotion.ProcessMotion` after `UpdateXInputState()`.
- It exposes `RightPad`, `RightPadTouch`, `RightPadClick`, `L4/R4`, removes the generic `Special` button, and honors `LegionControllerPassthrough`.

These are two different report decoders. “Lenovo recognized” does not authorize using the Tablet decoder for `E310`, nor the `LegionControllerS` little-endian layout for `17EF` Joy-Con reports.

## 6. HC device-side motion / attach terms

The Windows-sensor matrices are class-specific:

| Device class | Gyro matrix | Accel matrix | Additional source fact |
| --- | --- | --- | --- |
| `LegionGoTablet` | axis `(-1,1,1)`, `Y↔Z` | axis `(1,-1,-1)`, `Y↔Z` | `GamepadMotion.SetCalibrationThreshold(124.0f, 2.0f)`; attach enables left/right gyro and high-quality mode |
| `LegionGoTablet2` | axis `(1,1,-1)` | axis `(-1,-1,1)` | explicit subclass override; also has separate EC fan implementation |
| `LegionGoSZ1` / `LegionGoSZ2` | axis `(-1,1,1)` | axis `(-1,1,1)` | SZ1 attach sends `040701`, `040501`, `041002` after reset; `SZ2` inherits the class |

The matrix above is consumed by HC Windows sensor callbacks. It is not automatically applied to the separately decoded controller report; `LegionController` and `LegionControllerS` already arrange their report axes before calling `ProcessMotion`. Serial/Sapientia callback names are a separate conditional surface.

## 7. Three-way comparison: HC / current YMCC / MD

| Topic | HC source fact | Current YMCC fact | MD disposition |
| --- | --- | --- | --- |
| Lenovo model selection | DMI/SystemModel selects four device classes | no DMI/model selector in current special registry | `GAP: model→matrix binding absent` |
| VID/PID identity | vendor and PID are paired; two families | exact cross-VID guard exists in classifier | `STATIC-ALIGNED / classification-only` |
| XInput/DInput/dual/FPS role | explicit face labels and separate XUSB/HID routes | all recognized PIDs collapse to `lenovo-legion` | `ROLE-PRECISION-LOSS / dormant risk; no production caller` |
| controller decoder | two dedicated report decoders, different buffer/endianness/offsets | no decoder, HIDAPI, report reader, or motion ingress | `UNENCLOSED` |
| Windows motion matrix | class-specific matrix + threshold | no Lenovo model-backed matrix adapter | `UNENCLOSED` |
| attach commands | HC has model-specific OEM HID writes | no YMCC production OEM writer | `SOURCE-LOCATED / do not copy without ABI/readback` |
| passthrough/swap/gyro-index | HC settings and controller-side behavior exist | YMCC only has PID classifier; no equivalent settings/consumer | `MD wording must not claim retained behavior` |
| PnP/identity lifecycle | base container/device/XInput indices participate | YMCC registry does not bind these identities | `UNENCLOSED` |

Current `rg` result is decisive: `classifySpecialController` and `requiresSpecialSignalPath` are referenced only by `specialControllerProtocols.ts` and its selftest. No production input, HIDAPI, motion, controller selection, InputHost, or Coordinator route consumes them. The static classifier prevents known Lenovo IDs from being labeled generic Xbox, but it does not make the device work.

## 8. Errors / unknowns / original YMCC logic

### Evidence / fact

- HC’s full Lenovo vocabulary and role split are now recorded above.
- Current YMCC preserves VID/PID pairing and recognizes all HC-listed Tablet/SZ PIDs as special.
- Current YMCC has no production signal path, no report decoder, no DMI matrix selection, no OEM writer, and no consumer isolation route for Lenovo.

### Error / correction

- `TASK.md` E-27’s wording that current classification “保留 Legion passthrough/swap/gyro 配置” overstated the source. It is corrected to “仅保留 HC-derived ID classification; passthrough/swap/gyro behavior remains unimplemented/unverified.”
- `TASK.md` AG1/AG3 wording that all other Lenovo PIDs “只走普通 XInputController” is corrected to distinguish the XUSB generic fallback from the listed DInput/dual-DInput/FPS HID faces.

### HC oddity / unresolved

- `ControllerManager.HidDeviceArrived()` nests `E311` under the `0x17EF` branch. It does not prove that `1A86:E311` should be instantiated as generic XInput, and no YMCC behavior is inferred from it.
- `LegionController.cs` contains a source comment `// todo: invert gZ on LGO2`; this is not a resolved HC matrix rule. No YMCC inversion is added from that TODO.
- The external `controller_hidapi.net` implementation exposes the reader surface but does not provide a descriptor/readback proof in this audit; report identity remains source-located, not runtime-verified.

### Original YMCC logic / risk

- The aggregate `LENOVO_LEGION` and return type `lenovo-legion` are YMCC convenience classification logic, not HC controller classes. The flattened lists must never be used as admission keys.
- If a future production caller uses only `requiresSpecialSignalPath=true`, it still cannot select the correct Tablet/SZ report decoder or model matrix. This is a dormant contract risk, not a live production defect because no caller exists.

## 9. Result and next gate

```text
Lenovo HC vocabulary / model-role matrix = STATIC-VERIFIED
YMCC VID:PID classification             = STATIC-ALIGNED / CLASSIFICATION-ONLY
Lenovo role precision                    = DORMANT-LOSS / no production reachability
Lenovo controller-report decoder        = ABSENT / UNENCLOSED
Lenovo model-backed Windows matrix      = ABSENT / UNENCLOSED
Lenovo OEM HID attach/write              = ABSENT / intentionally not guessed
P-HID/P-XINPUT/P-OWNER                  = UNENCLOSED
Steam/game consumer and recovery         = RUNTIME-BLOCKED
runtimeUpgrade                           = false
```

No DGF is created. Before any source-side runtime adapter is considered, the next evidence gate must provide: DMI/SystemModel receipt, VID/PID + PnP/container identity, exact report descriptor/length/endianness/offset proof for the selected face, same-provider gyro/accel and calibration/matrix identity, then Host/owner/release and independent consumer observations. This document does not authorize any of those runtime writes or tests.
