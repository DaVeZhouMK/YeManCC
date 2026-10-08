# T22 BUS P20 — HC raw-motion transport scope audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / FACT-INFERENCE-GAP-SEPARATED / DIRECT-DS4-IMU-SAFE-STOP-RETAINED / RUNTIME-BLOCKED`

Date: 2026-09-05. This is a read-only source audit. It did not run a build, test, YeManCC, InputHost, HIDMaestro, HidHide, Steam, game, virtual device, or hardware operation. It is not an RF00 manifest and must not be presented as a whole-repository current-source identity.

## Scope identity and P19 boundary

The following files were read during this audit pass:

| Scope | Path | SHA-256 |
| --- | --- | --- |
| HC | `HandheldCompanion/Helpers/GamepadMotion.cs` | `7BA36F0DCB0E280D526E5B09E95888AF58B98927B3C83001858817EC12FB23ED` |
| HC | `HandheldCompanion/Managers/SensorsManager.cs` | `660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3` |
| HC | `HandheldCompanion/Sensors/IMUGyrometer.cs` | `BC44A51518C82BDDAF7BA2D79A37A7F57EF762E7F0D5F5B291DBB472077930E1` |
| HC | `HandheldCompanion/Sensors/IMUAccelerometer.cs` | `F63C11C0729B013E7B03D717C09F25FC099A8433752B52DE9E3826BCDFFF7B4C` |
| HC | `HandheldCompanion/Sensors/IMUCalibration.cs` | `320BC0E671B4B77D39F3471D98B9CC684F92F4B784B119FDD416C42F4302998B` |
| HC | `HandheldCompanion/Targets/DualShock4Target.cs` | `30A1E2704B00017FD18238546BCBE798D916FC17316FC19B775B4EF131927337` |
| YMCC | `native/main.cpp` | `8797428BE985E9311FE60F1CFEA2E98FAD8F8BF1EF2ADFA7BD1890F238D808FA` |
| YMCC | `InputHost/Program.cs` | `9C855378DF2AE455AEA524B046A20078A0CACFD004D960CF9A17E8F7C422A95B` |
| HIDMaestro static evidence | `tools/T22-BUS-P12-E03-HIDMAESTRO-DS4-ENCODER-STATIC-EVIDENCE-20260905.json` | `D89DBB18EB4390C7282CE9835707C4E6F3317A1CE73DCDDC2C3D4C63E588398E` |

Before this document was written, an exact P19 key-file comparison already found two mismatches: `TASK.md` and `InputHost/Program.cs`. In particular, P19 recorded `InputHost/Program.cs=72F7E4F8…A484F3`, whereas this audit read `9C855378…22A95B`. Therefore P19 remains a valid historical, byte-bound snapshot only; it cannot be re-labelled as current-source evidence. This document does not repair or replace RF00.

## Facts — locked HC source route

1. `GamepadMotion.ProcessMotion(gyroX, gyroY, gyroZ, accelX, accelY, accelZ, delta)` stores all six input values before calling the native motion implementation. `GetRawGyro` and `GetRawAcceleration` return those stored input values exactly (`GamepadMotion.cs:91-120,136-155`). In this HC target contract, **raw** means the values presented to `GamepadMotion`; it does not mean untouched hardware-register axes.

2. HC `SensorsManager.UpdateReport` obtains the current gyrometer and accelerometer readings and presents their six components in one `GamepadMotion.ProcessMotion` call (`SensorsManager.cs:318-328`). `SetSensorFamily` constructs both wrappers from the selected `SensorFamily`; gyrometer construction also receives the current device `GamepadMotion` calibration threshold (`SensorsManager.cs:331-338`).

3. Before this call, HC's Windows sensor wrappers apply the current device's matrix/remap. `IMUGyrometer` applies the device gyro remap/sign matrix and its calibration threshold before publishing its reading (`IMUGyrometer.cs:110-133`). `IMUAccelerometer` applies the device accel remap/sign matrix before publishing its reading (`IMUAccelerometer.cs:107-130`). Hence a parity transport must preserve the HC **motion-input semantic**: unit, post-wrapper matrix/remap identity, gyro threshold identity, and source generation — not merely six raw Windows numbers.

4. HC `DualShock4Target` reads `GamepadMotion.GetRawGyro` and `GetRawAcceleration`, then encodes gyro as `round(clamp(dps, ±2048) * 16)` and acceleration as `round(clamp(g * 9.81, ±64) * 512)` (`DualShock4Target.cs:88-103`). It does **not** source its DS4 report from `GetCalibratedGyro`.

5. The locked HIDMaestro evidence E03 independently establishes a different backend layout: its `dualshock-4-v2` extended encoder consumes the six `int16` fields `HMGamepadState.GyroPitch/Yaw/Roll` and `AccelX/Y/Z`; it does not consume the three `float` `GyroDpsX/Y/Z` fields for that DS4 encoder. Its byte offsets are HIDMaestro descriptor facts, not the HC VIIPER byte offsets.

## Facts — YMCC boundary at the audited scope identity

1. YMCC independently asks the Windows Sensor Manager for the first gyrometer and the first accelerometer of their respective types (`native/main.cpp:4842-4850,5411-5416`). Each COM callback records its own timestamp, receipt tick, and sequence. The source comment correctly says those two legacy callbacks do not prove a same-provider/same-generation gyro/accel pair (`main.cpp:4756-4764,4813-4830`).

2. Accordingly `pairProven` is hard-coded `false`; no `g_gmProcess` call can execute and gyro publication to the Host is zeroed (`main.cpp:5473-5486,5575-5586`). This is a correct fail-closed state, not a completed motion implementation.

3. If a future change merely flips that boolean, YMCC would call `g_gmProcess` with matrix candidate gyro plus accel but then read `g_gmGetCal` and transport only that calibrated gyro triplet as `gx/gy/gz` (`main.cpp:5486-5497,5319-5359`). The native state-file contains no acceleration field.

4. At the recorded `InputHost/Program.cs=9C855378…22A95B` scope, `Program.cs:127-137` assigns no direct IMU field at all: its `gx/gy/gz` state-file values are explicitly diagnostic-only and do not enter `SubmitState`. This is the presently correct direct-DS4 fail-closed disposition. E03 still proves the locked encoder field requirement, but its earlier product-side `GyroDpsX/Y/Z` assignment observation is historical source evidence, not a current-source claim.

## Inferences and their limit

1. The same-provider/same-epoch proof and the explicit cross-process envelope are **YMCC safety-shell requirements**, caused by separate Windows callbacks, a separate InputHost process, and a HID backend. HC does not expose an identically named proof object. Calling that project envelope an HC API or an HC wire protocol would be false parity.

2. Nevertheless, the envelope's payload semantics must reproduce HC's `GamepadMotion.ProcessMotion` input: selected sensor family/provider, the relevant HC-equivalent post-wrapper matrix/remap and gyro threshold, units, source generation, six values, and monotonic timing. Passing pre-matrix hardware axes, a PID-family guess, fixed threshold `2000`, or calibrated gyro in place of the HC raw-motion input would be a material HC deviation.

3. A local `GyroDps → GyroPitch/Yaw/Roll` conversion is not a safe local correction. It would still omit acceleration, use the wrong HC source semantic if fed `g_gmGetCal`, and lacks descriptor encode/decode and named-consumer evidence. No product code change is justified from this audit alone.

## Remaining gaps — no inference permitted

1. A capability matrix that proves or rejects a concrete gyro+accelerometer provider binding, generation/sequence correlation, and PnP invalidation for each supported device/SKU.
2. A canonical six-axis `GamepadMotion` input envelope carrying the HC-equivalent post-wrapper semantics above. Its acceptance must reject mixed identity, matrix, unit, ABI, power generation, calibration generation, stale ordering, and partial samples.
3. Descriptor-specific DS4 conversion into HIDMaestro's six `int16` fields, followed by encoded-report capture and an independent decoder/readback. HC's VIIPER offsets must not be copied.
4. The distinct T10 Host lifecycle contract: `hostInstanceId`, epoch, revision/hash admission, first-frame receipt, neutral, release, crash/suspend/PnP recovery, and external consumer observation.

## Required disposition

```text
dualshock4 direct IMU = SAFE_STOP
  reason = paired HC-motion-input transport / descriptor assignment / readback unresolved

xbox360 motion persona = persona-motion-unsupported

virtual-stick = safe-zero unless its separate T11 admission is satisfied
```

This audit creates no DGF number and does not close T10, T11, T12, T15, T16, T17, T18, or R1.
