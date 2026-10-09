# T22 BUS-P126: virtual DS4 mapping lifecycle correction

Date: 2026-09-06 (task sequence; the local machine log records `2026-09-07` timestamps)

Status: `SOURCE-FIXED / BUILD-DEPLOY-VERIFIED / HOST-LOCAL-T0-PROVEN / BUTTON-PRESS-AND-CONSUMER-OUT-OF-SCOPE`

## 1. User-facing problem

The durable configuration was already:

```text
outputTarget.buttonMappingEnabled = true
outputTarget.persona = dualshock4
outputTarget.gyroEnabled = false
gyroMotion.enabled = false
```

The virtual controller still did not create or switch to a PS4 target because
Native used the disabled gyro lane as the virtual-target lifecycle gate.

## 2. HC comparison and confirmed divergences

The locked HC source separates virtual-target lifecycle from motion:

- `Reference/HandheldCompanion/Frozen-HC-BINARY-BATCH12-20260831/Managers/VirtualManager.cs:435-526` creates the selected `DualShock4Target`, subscribes events and calls `SetControllerStatusCore`.
- `VirtualManager.cs:529-557` connects/disconnects the target according to `HIDstatus`.
- `VirtualManager.cs:608-617` sends `ControllerState` plus `GamepadMotion` to an already-selected target; motion is not the target-creation gate.

The previous YMCC code used `g_realStickTest.enabled &&
g_realStickTest.motionEnabled` to start/stop the Host and to decide whether to
submit a physical mapping frame. That was a direct HC lifecycle divergence:
`motionEnabled=false` incorrectly meant “no virtual controller”.

A second confirmed blocker was a null-unsafe settings read. The current file
stores `gyroMotion.motionTrigger=null`; the old `json::value(...,
std::string)` call threw during `capture-init`, producing a real
`input-capture.worker-exception` and stopping the worker before Host startup.

## 3. Source fix

File: `native/main.cpp`.

The test-lane state now keeps independent fields:

```text
virtualTargetEnabled = persona == dualshock4 AND
                       (buttonMappingEnabled OR gyroEnabled)
motionEnabled        = gyroMotion.enabled AND valid trigger AND
                       outputMode == virtual-stick
```

The fix therefore:

- starts/continues `InputHost` while the admitted DS4 target is enabled, even
  when gyro is off;
- submits physical XInput buttons, triggers and axes with safe-zero motion
  contribution when no motion proof is admitted;
- retains the existing close order: `QUIESCE -> neutral -> release/dispose -> epoch`;
- treats JSON null/wrong types as documented fallbacks instead of throwing;
- does not add HidHide mutation, OEM disable, SendInput, a second physical
  reader or any gyro formula change.

Relevant current-source locations:

- `native/main.cpp:4902-4992` — target/motion split and null-safe settings read.
- `native/main.cpp:5466-5490` — Host start/stop uses `virtualTargetEnabled`.
- `native/main.cpp:7058-7064` — physical mapping publication uses the target
  gate; motion remains a separate safe-zero contribution gate.

## 4. Build and deployment evidence

Commands:

```text
native\\build_native.bat
dotnet build InputHost\\YeManInputHost.csproj -c Release --nologo
```

Both completed with zero errors. Hashes:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `native/main.cpp` | 1,131,270 | `5F278936F2966EC7D4177643B31C09F5113EFC7143066123110DC0C09B54706E` |
| `Mainline/Build/App/Native/YeManCC.exe` | 2,273,792 | `C874D903E573287C1E39025DEFEBF441B4ABF4018439D690D8F30CD40AA9793A` |
| `C:\SOFT\YeMan\YeManCC\YeManCC.exe` | 2,273,792 | `C874D903E573287C1E39025DEFEBF441B4ABF4018439D690D8F30CD40AA9793A` |

The replaced executable was backed up at:

```text
C:\SOFT\YeMan\YeManCC\.deployment-backup-20260907-000404
```

`YeManRecoveryService.exe` was not replaced because an independent recovery
process held the installed file; it was not required for this fix and remains
unchanged.

## 5. Runtime evidence

Configuration: `C:\SOFT\YeMan\PowerControl\yeman-settings.json`.

Lifecycle log: `C:\Users\DaVe\AppData\Local\YeManCC\native-lifecycle.log`.

The same epoch/run tuple recorded:

- `HELLO` accepted;
- `PREPARE_TARGET` accepted (`target-prepared`);
- `SUBMIT_NEUTRAL` accepted (`neutral-accepted`);
- `input-host-open-result`: `hello=true`, `prepared=true`,
  `neutralized=true`, `transportFault=false`;
- 1,348 subsequent `frame-accepted` receipts;
- no new `input-capture.worker-exception` after the null-safe read fix.

Capture telemetry reports:

```text
realStickTestLane.enabled = true
virtualTargetEnabled     = true
virtualTargetPersona     = dualshock4
motionEnabled            = false
motionAdmission          = pair-unproven-safe-zero
```

The OS enumerated two `Wireless Controller` HIDClass entries:

```text
HID\\HIDCLASS\\1&4784345&13&0000
ROOT\\HIDCLASS\\0001
```

This proves an OS-visible virtual-controller candidate exists; it does not
prove Steam/game ownership or physical-device hiding.

## 6. Static regression evidence

All passed:

```text
pnpm run test:button-mapping-mock
pnpm run test:button-mapping-runtime-readiness
pnpm run test:persona-descriptor-parity
pnpm run test:input-host-supervisor
pnpm run test:input-runtime-admission
```

Generated artifacts include:

- `Build/Validation/GyroVirtual/BUS-P48-button-mapping-runtime-readiness-static-20260905.json`
- `Build/Validation/GyroVirtual/T17-E01-persona-descriptor-parity-static-audit-20260904.json`
- `Build/Validation/HC-Parity/S-17-input-host-supervisor-mock-trace.json`

## 7. Evidence, inference and remaining gaps

### Proven

- The previous “gyro disabled means no virtual target” gate was a source-level
  lifecycle error relative to HC.
- The null `motionTrigger` crash was a real capture-thread startup blocker.
- The corrected binary creates and maintains a local DS4 Host target while
  gyro is disabled, submits neutral then physical mapping frames, and exposes
  an OS-visible `Wireless Controller` target.

### Not proven and intentionally open

- No physical A/B/LB/RB/stick press was synthesized; button/axis value changes
  still need a real user action or same-machine hardware observation.
- No DS4 descriptor/report byte, HID raw readback, Steam observation or game
  consumer observation was performed.
- No HidHide visibility mutation was performed; the physical Xbox 360 remains
  enumerated, so game double-consumption/isolation is not claimed closed.
- No ROG sensor, pair, calibration, matrix or direct-IMU admission was changed.
- The RF00/T20/T21/T14 current-source snapshot is stale after this native
  source change; a later read-only RF00 refresh is required.

## 8. Next intervention point

While YMCC is open and the virtual target remains enabled, press one physical
Xbox button and move each stick briefly, then verify the virtual `Wireless
Controller` changes without enabling gyro. Return the same-instance lifecycle/
capture log and the observed DS4/Steam result. Until then P126 is
`HOST-LOCAL-T0-PROVEN`, not full mapping/consumer closure.
