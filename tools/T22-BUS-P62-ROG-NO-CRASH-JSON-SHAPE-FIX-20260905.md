# T22-BUS-P62：ROG 无闪退证据与 capture receipt JSON 类型修正（2026-09-05 UTC / 2026-09-06 HKT）

## 证据输入

| 项目 | 值 |
|---|---|
| ROG evidence ZIP | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC-ROG-IMU-Evidence-20260906-010005.zip` |
| ZIP bytes / SHA-256 | `18,708` / `C5FC986036C0EDEDFAAADF0AF6CDB9332F22684BF9B7C48C34BCA71324CB64CA` |
| evidence generatedUtc | `2026-09-05T17:01:00.6025772Z` |
| collector duration | `30 seconds` |
| package root | `C:\SOFT\YeMan` |

The ZIP manifest was verified read-only. All four listed files matched their
manifest byte counts and SHA-256 values, including the duplicated capture JSONL
copy. No user files were overwritten.

## Fact：本次没有闪退

`rog-imu-evidence.json` records `launch.started=true`, `launch.error=null`, and
ROG identity `ROG Xbox Ally X RC73XA_RC73XA` / HC baseboard product `RC73XA`.
The native lifecycle log contains boot, single-instance acquisition,
recovery-service start, window creation, 23 `rog.hid-bound` events and four
`rog.xbox-face-enabled` events. It contains no `exit`, WebView2 failure,
recovery exhaustion, dump, WER, or browser/gpu-process failure event.

This closes only the evidence claim that this run did not crash. It does not
prove that every `gyro-motion` run is crash-free, nor does it close ROG runtime
or consumer isolation.

## Fact：worker 在第一帧后 safe-stop，错误已定位

The five-line capture log records:

1. WinRT gyro and accelerometer start (`gyroSensor=true`, `accelSensor=true`);
2. `sample-pair-unproven` with `safeZero=true`;
3. one ROG sample with `pairProven=false` and
   `matrixIdentity=unresolved-rog-pid-family/raw`;
4. `input-capture.worker-exception` with
   `detail=[json.exception.type_error.306] cannot use value() with array`,
   `phase=capture-loop`, `safeStop=true`;
5. a normal `stop` record.

Therefore the observed run is:

```text
process crash              = NO
worker exception            = YES / caught
worker safe-stop            = YES
capture loop after error    = NO
HC parameter or matrix bug  = NOT EVIDENCED
```

## Source finding and fix

`inputCaptureOnGamepad()` calls `hostFrame.value("rightStick", ...)`.
The final `inputHostSubmitPad()` receipt used a nested braced initializer:

```cpp
{"frameProof", {"sampleSequence", ...}, {"motionPairProofId", ...}}
```

With nlohmann JSON this makes the enclosing initializer an array, so
`hostFrame.value(...)` throws `type_error.306`. This is a YMCC capture/Host
receipt construction defect, not HC axis, multiplier, matrix, pairing, owner,
HidHide, ROG OEM, Steam, or game-consumer behavior.

The current source now constructs `frameProof`, `result`, `rightStick`, and
`gyroDps` explicitly with `json::object()`. The existing
`pair-unproven-safe-zero` gate remains unchanged.

## Verification

| Check | Result |
|---|---|
| Native build (`cmd.exe /d /c native\\build_native.bat`) | `BUILD_OK` |
| Focused source-shape selftest (`tools/input_host_result_shape_selftest.ps1`) | `PASS` |
| Existing robust native static selftest | `PASS` (requires current native build artifact) |
| Standalone ROG test package | `BUILD/PACKAGE OK` |
| HC parameter/matrix changes | none |
| System/device/HidHide/Steam/game mutation | none |

The rebuilt package is:

```text
ZIP      = Mainline/Build/TestPackages/GyroInput-ROG-20260905-011500/YeManCC-GyroInput-ROG-Test.zip
bytes    = 85,228,332
SHA-256  = 19BFE66FFEA1195FBDD4A4981C92F192B01166F440964FBF5136510BE1C1B37C
YeManCC  = 2,152,448 bytes / 4972FED15DAA7AD5B75EDB4D7DB5C16C96206C5754DB9A24988D61E2A11A81F0
HC       = HC-CANDIDATE-0.32.4.0-06c0b954-20260902
```

The package script reports `formalReleasePackageUntouched=true` and
`systemMutation=false`; both `gyro-motion` and `virtual-gamepad` directories
are retained for the ROG runtime check.

The focused selftest asserts that the current `inputHostSubmitPad()` receipt
uses explicit JSON objects and that the legacy array-producing initializer is
absent. It is a source-shape guard; runtime ROG confirmation of the fixed build
is still pending.

## Error / Unknown / Gap

```text
附件是否复现闪退                         = NO
本次启动/生命周期完整                     = FACT
worker C++ exception protection            = SOURCE-IMPLEMENTED / VERIFIED
array/object receipt defect                = SOURCE-IDENTIFIED / FIXED
fixed build on ROG                        = PENDING USER RUNTIME CHECK
native DLL/SEH access violation            = UNKNOWN
same-provider pair + HC matrix proof       = UNENCLOSED
Host ACK/first-frame external receipt      = UNENCLOSED
P-HID/P-XINPUT/P-OWNER + consumer receipt  = RUNTIME-BLOCKED
runtimeUpgrade                             = false
```

No new DGF is created. `T11` remains pair-unproven safe-zero, `T12` remains
direct IMU no-report, and `T13/T15–T18` remain un-enclosed/runtime-blocked.
The next required intervention is to run the rebuilt package on the ROG with
both `gyro-motion` and `virtual-gamepad` directories present and return the
same three lifecycle/capture logs if the fixed worker still stops or exits.
