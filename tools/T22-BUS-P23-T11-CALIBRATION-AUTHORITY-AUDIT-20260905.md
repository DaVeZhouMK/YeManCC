# T22 BUS P23 — T11 calibration authority audit

Status: `SOURCE-SCOPED-STATIC-AUDIT / HC-REFERENCE-FACT / TASKBOOK-FAIL-CLOSED-DELTA-ALREADY-ADJUDICATED / NO-SOURCE-CHANGE / RUNTIME-BLOCKED`

Date: 2026-09-05. Static read only. No calibration command, GamepadMotion DLL call, InputHost, HIDMaestro, device, build, or test was run.

## Source identity

| Scope | Path | SHA-256 |
| --- | --- | --- |
| HC | `HandheldCompanion/Managers/SensorsManager.cs` | `660CC003DF8C425CC4AA7D478C3D7327991C194D8FAE13F1EFD060B38BC2B4A3` |
| YMCC native | `native/main.cpp` | `8797428BE985E9311FE60F1CFEA2E98FAD8F8BF1EF2ADFA7BD1890F238D808FA` |
| YMCC mock | `src/bridge/gyroCalibrationSession.ts` | `0EBCA3D780B12BDED2206B1DC0AC66E80C84A9B5E855548473F0DC47A16CDE3C` |
| Authority | `Docs/Tasks/GyroVirtual/30-GYRO-PARAMETER-SHIELD-RESET-ACCEL-CONTRACT-20260904.md` | `071CFA522F62B2015C7E8A7562B0048B0C5352E38D2D43617A6619879070A998` |
| Authority | `Docs/Tasks/GyroVirtual/TASK.md` | `1ED5088AB1F4403867CEB46DE6E702D8C1DDBF095C300DAF2617486BF9074141` |

This is not an RF00 manifest; the authority files changed again after this record was written.

## Facts — HC and current YMCC source

1. Locked HC `SensorsManager.Calibrate` is an explicit UI session: it displays a four-second preparation countdown, calls `ResetContinuousCalibration`, sets `Stillness | SensorFusion`, observes confidence for at most five seconds, then always reads and stores the offset with `weight=(int)(confidence*10)` and sets `Manual`. `confidence==1` only ends the wait early; it is not a post-timeout admission requirement (`SensorsManager.cs:340-405`).
2. Current native loads `StartContinuousCalibration`, `PauseContinuousCalibration`, and `ResetContinuousCalibration` function pointers but invokes none of them. `g_gmStartCal` and `g_gmPauseCal` occur only in declaration/lookup; `g_gmResetCal` occurs only in declaration/lookup/required-symbol validation. No native `calibrate-start`, `calibrate-abort`, or user calibration command route was located.
3. The future-only native branch behind `pairProven` starts an elapsed timer on first paired sample. It locks Manual only for `confidence==1 && steady && finite offset && weight>0`; a five-second timeout logs safe-zero and does not lock (`main.cpp:5501-5529`). At present `pairProven=false`, so neither the process nor this lock branch executes.
4. `gyroCalibrationSession.ts` models `calibrate-start` and the strict lock criteria, but has no instantiation in `src`. It explicitly records timeout as `safe-zero-continues; no-manual-lock` (`gyroCalibrationSession.ts:120-225`). It is a pure/mock artifact, not a native calibration transaction.

## Authority resolution — not an open implementation choice

`TASK.md` Q4 and the T11/T30 contract already adjudicate this divergence: HC's timeout-to-Manual behavior is retained as a reference and a static-drift negative case, while YMCC requires `confidence==1`, valid offset/weight, and safe-zero after timeout. The taskbook explicitly says not to restore the HC timeout path merely for word-for-word parity.

Therefore this is an intentional, documented **project fail-closed delta** rather than an unapproved new rule. It must never be labelled `EXACT_HC` or runtime parity, but it also must not be locally reverted while the current authority remains unchanged.

## Remaining gaps

1. An actual Coordinator-owned T11 transaction that binds user action, provider identity, pair proof, matrix/unit, calibration epoch, and persisted calibration identity before calling the locked motion asset.
2. An authority-approved decision for any future change to the current fail-closed delta. That decision must update the taskbook before native/UI/mock code changes.
3. Same-generation Host admission, output no-report/safe-zero evidence, and device/runtime validation after T10/T12 prerequisites are met.

No DGF is created. No runtime closure, calibration success, or HC-exact status follows from this source audit.
