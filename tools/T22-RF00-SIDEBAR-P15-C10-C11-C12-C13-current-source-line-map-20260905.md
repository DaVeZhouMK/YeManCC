# T22 RF00-SIDEBAR P15 current-source line map (2026-09-05)

状态：`STATIC-SOURCE-MAP / DOCS-ONLY / RUNTIME-BLOCKED`

- manifestId: `T22-RF00-20260905-SIDEBAR-P15-C10-C11-C12-C13-STATIC-CURRENT`
- sourceDigest: `C963D7F2047D2F89E2713FE9C6C3EECD884265FC692C544AC8C35BE1E6A98860`
- key files: `41`; YMCC HEAD: `aa8f38b83cc960267d2f68bf78d0e8adaca2234f`; HC: `06c0b9544db2b1f39abf9cd3796225ccfb096103` clean.
- capture mode: `static-read-hash-documentation-only`; no build, test, runtime, driver, virtual target, HidHide, ROG feature report, Steam, game, or hardware operation.

## P01 — parameter route remains unconsumed

- `native/main.cpp:5302-5332`: fixed `(Z,X)`, fallback threshold and scale only; source comments now explicitly mark this `PARTIAL_HC_CANDIDATE`.
- `InputHost/Program.cs:30-61,122-149`: state-file protocol has no `revision`, `configHash`, ACK, first-frame receipt, or target epoch.
- `src/bridge/settingsRepository.ts`, `src/bridge/gyroConfigActivation.ts`, `src/views/GyroMotionView.vue`, `src/views/ButtonMappingView.vue`: UI/CAS intent remains separate from native/Host consumption.
- HC authority: `HandheldCompanion/Managers/MotionManager.cs:245-351`, `Utils/InputUtils.cs`.

Result: `parameter-not-consumed / RUNTIME-BLOCKED`.

## P02 — Windows IMU / calibration admission

- `native/main.cpp:4749-4839`: one event-only COM snapshot source; per-sensor valid/value/FILETIME/receipt/sequence.
- `native/main.cpp:5392-5447`: legacy `GetData()` polling removed; event snapshot is diagnostic only, `pairProven=false` remains.
- `native/main.cpp:4926-4957`: GamepadMotion DLL load no longer enters calibration mode.
- `native/main.cpp:5460-5490`: if a future admitted pair exists, stricter pair/steady/finite gate remains a YMCC safety delta.
- HC authority: `Sensors/IMUGyrometer.cs:21-76,110-133`, `Sensors/IMUAccelerometer.cs:20-73,107-130`, `Managers/SensorsManager.cs:318-395`, `Managers/TimerManager.cs:73-94,166-179`.

Result: `event-source local correction / pairing-default-selection-rate-lifecycle UNENCLOSED`.

## P03 — descriptor/IMU transport

- `InputHost/Program.cs:122-149` writes `HMGamepadState.GyroDpsX/Y/Z`; no descriptor, report ID/length/offset/endian, independent decode or named consumer evidence exists.

Result: `descriptor-unverified / RUNTIME-BLOCKED`.

## P04 — ROG identity and stable HID lifecycle

- `native/main.cpp:14375-14377,14719-14723`: `0x1ABE/0x1B4C` remains a ROG HID-family discovery signal, not model identity.
- `native/main.cpp:5421-5428`: PID-family hits retain raw axes and emit `unresolved-rog-pid-family/raw`; fixed Xbox ROG matrix path removed.
- HC authority: `Devices/ASUS/ROGAlly.cs:79-109,275-313,353-363`; `Devices/IDevice.cs:821-840`; `ROGAllyX.cs`, `XboxROGAlly.cs`, `XboxROGAllyX.cs`.

Result: `model-matrix UNENCLOSED; stable HID lifecycle UNENCLOSED`.

## P05 — owner/frontend/sidebar scope

- `src/router.ts:23-36`, `src/components/NavRail.vue`, `src/bridge/gyroVirtualFeature.ts`, `src/bridge/inputDiagnosticsUi.ts`, `src/views/GyroMotionView.vue`, `src/views/ButtonMappingView.vue` are included in P15 sidebar coverage.
- `src/bridge/inputOwnerRuntime.ts`, `physicalInputOwnership.ts`, `inputCoordinatorMock.ts`, and `src/gamepad/engine.ts` remain limited to their named YMCC/mock route; no external Steam/game consumer producer was located by this freeze.

Result: `P-OWNER scoped; P-HID/P-XINPUT/external consumer UNENCLOSED`.

## P06 — release/recovery

- `native/main.cpp:5330-5360,5559-5579`: legacy capture/Host stop exists, but sensor-only rebind is not separated from Host/visibility release.
- HC authority: `Managers/SensorsManager.cs:114-170`; `Devices/ASUS/ROGAlly.cs:264-341`.

Result: `suspend/PnP/recovery UNENCLOSED / RUNTIME-BLOCKED`.

## P07 — provenance and DGF range

- P15 captures full YMCC porcelain raw and 41-key canonical digest; generated manifest/porcelain/line-map/T21-T14 artifacts are self-excluded.
- `TASK.md`, `20`, `25`, `30`, sidebar views/bridge and HC authority files are all key inputs.
- Existing DGF range remains strictly `DGF-01…DGF-15`; no DGF-16 is created.

Result: `static source provenance current only at P15 capture; runtimeUpgrade=false`.