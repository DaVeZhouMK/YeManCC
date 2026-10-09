# T22 BUS-P125: HC XInput route fix and T0 closure

Date: 2026-09-06 (Asia/Hong_Kong)
Status: SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / PHYSICAL-T0-CLOSED / GAME-CONSUMER-ISOLATION-OUT-OF-SCOPE

## 1. Conclusion

The native input entry was not aligned with HC: YMCC used the compile-time XInput route while HC XInputController uses xinput1_4.dll ordinal #100 (XInputGetStateSecret14). Before the fix, a read-only xinput1_4 probe observed LB+RB=768 while the old native instance only reported connected/buttonMask=0.

The fix dynamically loads xinput1_4.dll and resolves ordinal #100. No virtual controller, HidHide, SendInput, or second physical reader was introduced. The installed instance produced real receipts: buttonMask=768 -> gamepad-summon-emitted, page-next/nav-up/confirm semantic actions.

## 2. HC evidence

Reference: Isolated/HC-Candidate-0.32.4.0-06c0b954-20260902/HandheldCompanion/Controllers/XInputController.cs.

- UpdateXInputState calls XInputGetStateSecret14 and then reads Controller.GetState().Gamepad (source around lines 86-100).
- P/Invoke is DllImport("xinput1_4.dll", EntryPoint="#100") (source around lines 393-394).
- Therefore this is an entry-route parity fix, not a new input protocol.

## 2.1 Parity caveat

HC `XInputController.cs` 实际保留双路径：ordinal `#100` (`XInputGetStateSecret14`) 用于额外 Xbox 特殊键状态，而完整 `ButtonState`、扳机和四轴来自 `Controller.GetState().Gamepad`。当前 native 将 ordinal `#100` 的返回布局作为完整 `XINPUT_STATE` 使用；本机真实 T0 已通过，但这不能表述为与 HC 完整读取实现逐字等价，也不同于 Git v0.0.28 的公开 `XInputGetState` 路径。该项作为静态 parity caveat 保留，本轮不猜测、不新增修复。

## 3. Source/build/deploy

File: native/main.cpp. The canonical reader is LoadLibraryW("xinput1_4.dll") plus GetProcAddress(MAKEINTRESOURCEA(100)); all four slots use this function. A gamepad-xinput-api receipt records dll/ordinal/resolved.

Source bytes: 1129270.
Source SHA-256: 751B438ED2B7D0A9FC18661817E496793CAA5FFAC374F1B2AB13C635B5E2A3AD.
Build: native/build_native.bat -> BUILD_OK.
Deploy: tools/deploy-installed.ps1 -> DEPLOY_OK.
Build/install YeManCC.exe bytes: 2269696.
Build/install SHA-256: FB238BC57E964CF2CB10D41A87DBA82616E95C173109A8CC6131657D39DED31B.
Backup: C:\SOFT\YeMan\YeManCC\.deployment-backup-20260906-234141.
Runtime: YeManCC PID 27696, RecoveryService PID 30200.

## 4. Runtime receipt

Log: C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log.

- PID 27696 startup: gamepad-xinput-api {dll:xinput1_4.dll, ordinal:100, resolved:true}; rawInputRegistered=true; fallbackTimerArmed=true; connectedMask=1/primarySlot=0.
- 23:42:07: buttonMask=256 -> buttonMask=768 -> gamepad-summon-emitted.
- After summon: page-next semantic receipts targeted yemancc.
- A stick action emitted nav-up targeted yemancc.
- 23:42:27: buttonMask=4096 (A) -> gamepad-ui-input-emitted(action=confirm,target=yemancc).

User confirmation: LB+RB summon works and controller-page operations work.

## 5. Evidence versus gaps

### Proven

- HC canonical route is xinput1_4.dll ordinal #100.
- YMCC now resolves and uses that route.
- Real LB+RB, page navigation, stick navigation and A confirmation reached YMCC semantic input.

### Not claimed

- This does not re-close Steam/game consumer isolation.
- This does not close ROG HID, HidHide, virtual PS4, or gyro-motion evidence.
- Non-neutral stick observations were not converted into a new deadzone or axis rule.

### Remaining gaps

- A separate game/Steam owner-isolation regression must still prove game-input-count=0 and ymcc-action-count>0 while YMCC owns the controller.
- ROG/gyro/virtual-controller gates remain under the GyroVirtual task book.
- Release/updater boundaries were unchanged.

## 6. Task state

P125 closes the native XInput route and local T0 controller operation only. Unmeasured game consumer isolation and ROG gyro evidence remain explicitly open.
