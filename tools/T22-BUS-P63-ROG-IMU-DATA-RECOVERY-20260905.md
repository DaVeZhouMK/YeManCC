# T22-BUS-P63：ROG IMU 数据链恢复实证与 WebView 恢复事件（2026-09-05 UTC / 2026-09-06 HKT）

## 证据输入

| 项目 | 值 |
|---|---|
| ROG evidence ZIP | `C:/Users/DaVe/Desktop/陀螺仪/YeManCC-ROG-IMU-Evidence-20260906-011852.zip` |
| ZIP bytes / SHA-256 | `37,266` / `5E59FF7C9416F9926BFF67ED83B4BA86E4F96FDF40321EAF02835E942C5E8AAD` |
| evidence generatedUtc | `2026-09-05T17:19:46.7355343Z` |
| collector duration | `30 seconds` |
| package root | `C:\SOFT\YeMan` |
| YeManCC.exe | `2,152,448 bytes / 4972FED15DAA7AD5B75EDB4D7DB5C16C96206C5754DB9A24988D61E2A11A81F0` |

The ZIP manifest was verified read-only. All four listed files match the
manifest byte counts and SHA-256 values.

## Fact：真实 IMU sample 已恢复

`rog-imu-evidence.json` records `launch.started=true`, `launch.error=null`,
ROG `RC73XA`, and no HidHide/OEM disable/virtual-controller mutation. The
native capture file contains 552 parsed `kind=sample` records and no
`input-capture.worker-exception`. Gyro and accelerometer fields are present in
all 552 samples. Gyro sequence advances from 1 to 732 and accelerometer
sequence from 1 to 648; sample tick intervals are 78–110 ms, average 93.07 ms.

The duplicate Desktop copy contains 519 samples because it was copied while
the application file was still being updated; it is not treated as a second
runtime session. Both copies have the same first start record and the same
latest sample values.

## Fact：HC safety gates仍按合同生效

Every sample still carries:

```text
gyro=true
accel=true
gamepadMotion=true
confidence=0.0
steady=false
matrixIdentity=unresolved-rog-pid-family/raw
```

The first capture record is followed by
`sample-pair-unproven / provider-capability-matrix-not-bound / safeZero=true`.
All physical axes (`rx`, `ry`, `lx`, `ly`) remain zero. This means the raw
sensor transport is working, while virtual-stick admission remains correctly
closed because same-provider/same-generation/calibration+matrix proof is still
absent. This run does not prove HC motion output or a valid ROG matrix.

The external collector's WinRT event subscription reports a PowerShell
`InvalidOperationException` and uses its diagnostic fallback, but that is a
collector-side limitation; native YMCC still produced the 552 samples above.

## Fact：WebView2 曾失败一次但已自动恢复

`ymcc-native-lifecycle.log` records one:

```text
webview-browser-process-failed
exitCode = -1073741819 (0xC0000005)
attempt = 1
```

followed by `webview-recovery-complete`, generation 2, less than one second
later. The parent process remained alive and capture continued. Therefore this
run is **not a process-crash reproduction**, but it is a real recovered browser
process failure and remains a separate stability gap.

## Classification

```text
raw IMU data path                  = RUNTIME-PARTIAL-CLOSED / 552 samples
capture worker JSON regression     = NOT OBSERVED / fixed path held
process crash                      = NOT OBSERVED
WebView browser failure            = OBSERVED ONCE / RECOVERED
virtual-stick output               = SAFE-ZERO (expected fail-closed)
same-provider pair proof           = UNENCLOSED
ROG matrix/calibration proof       = UNENCLOSED
Host ACK/first-frame external      = UNENCLOSED
P-HID/P-XINPUT/P-OWNER consumer    = RUNTIME-BLOCKED
runtimeUpgrade                     = false
```

No source change is justified by this evidence. In particular, do not open the
motion gate, infer a matrix from the raw ROG PID family, or treat the recovered
WebView event as proof of a permanent crash. The remaining intervention is to
repeat the fixed package once if browser recovery stability must be closed; if
the browser failure recurs, return the same lifecycle/capture files plus
Crashpad/WER so the recovery path can be separated from native sensor capture.
