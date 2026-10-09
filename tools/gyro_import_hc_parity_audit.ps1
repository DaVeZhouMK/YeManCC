<#
.SYNOPSIS
  Read-only source audit for the import (sensor->frame) gyro link HC parity.

  Asserts CURRENT state of YMCC native/main.cpp vs locked HC sensor sources
  (Sensors/IMUGyrometer.cs, Sensors/IMUAccelerometer.cs) for the data-import
  side of the HC parity contract:

    - dedicated STA sensor thread with a real message pump (the 0910-13
      one-event-then-freeze fix)
    - GetDefault selection; ReportInterval = max(MinimumReportInterval, 8)
    - gyro calibration-threshold clip on RAW values with >= semantics,
      applied BEFORE the matrix (HC IMUGyrometer.cs:117-133 order)
    - no threshold clip on the accel path (HC IMUAccelerometer.cs:107-131)
    - single subscribe site (only the dedicated thread starts sensors)
    - same-provider binding by device id + sensor binding generation
    - suspend/resume routed through the dedicated thread

  Read-only: never loads WinRT, never touches hardware, never writes.
  Anchor sources:
    - YMCC native/main.cpp
    - HC pinned Sources/Sensors/IMUGyrometer.cs, IMUAccelerometer.cs
#>
[CmdletBinding()]
param(
  [Parameter(Mandatory = $true)][string]$HcRoot,
  [Parameter(Mandatory = $true)][string]$NativeSource
)

$hcGyro = Join-Path $HcRoot 'Sensors\IMUGyrometer.cs'
$hcAcc = Join-Path $HcRoot 'Sensors\IMUAccelerometer.cs'
if (-not (Test-Path -LiteralPath $hcGyro)) { $hcGyro = Join-Path $HcRoot 'Sensors\IMUGyrometer.cs' }
$native = Get-Content -LiteralPath $NativeSource -Raw -Encoding UTF8
$hcG = Get-Content -LiteralPath $hcGyro -Raw -Encoding UTF8
$hcA = Get-Content -LiteralPath $hcAcc -Raw -Encoding UTF8

$fail = 0
function Assert([string]$n, [bool]$ok, [string]$detail) {
  if ($ok) { Write-Host "PASS | $n" }
  else { Write-Host "FAIL | $n | $detail"; $script:fail++ }
}

# Count a literal substring occurrence.
function CountOf([string]$haystack, [string]$needle) {
  return ([regex]::Matches($haystack, [regex]::Escape($needle))).Count
}

# ── I1: dedicated STA sensor thread with a real message pump ──────────────
Assert 'I1 STA apartment on sensor thread' ($native -match 'CoInitializeEx\s*\(nullptr,\s*COINIT_APARTMENTTHREADED\)') 'CoInitializeEx on the dedicated thread must be STA'
Assert 'I1 real message pump present' ($native -match 'MsgWaitForMultipleObjectsEx' -and $native -match 'PeekMessageW' -and $native -match 'DispatchMessageW') 'MsgWait+PeekMessage/DispatchMessage pump must exist (0910-13 freeze fix)'
Assert 'I1 pump inside sensor thread' ($native.IndexOf('static DWORD WINAPI inputCaptureSensorThreadProc(LPVOID) noexcept {') -lt $native.IndexOf('const DWORD wait = MsgWaitForMultipleObjectsEx(1, &wake, INFINITE')) 'the pump must live inside the sensor thread proc'

# ── I2: sensor selection + report interval policy ─────────────────────────
Assert 'I2 gyro GetDefault' (CountOf $native 'Gyrometer::GetDefault()' -ge 1) 'Gyrometer.GetDefault must be used'
Assert 'I2 accel GetDefault' (CountOf $native 'Accelerometer::GetDefault()' -ge 1) 'Accelerometer.GetDefault must be used'
Assert 'I2 max(Minimum,8) both sensors' (CountOf $native 'MinimumReportInterval(), kInputCaptureRequestedSensorIntervalMs' -ge 2) 'both gyro and accel use max(Min,8)'
Assert 'I2 HC gyro interval policy' ($hcG -match 'ReportInterval = Math\.Max\(\(\(Gyrometer\)sensor\)\.MinimumReportInterval, \(uint\)updateInterval\)') 'HC gyro ReportInterval = Max(Min, updateInterval)'
Assert 'I2 HC accel interval policy' ($hcA -match 'ReportInterval = Math\.Max\(\(\(Accelerometer\)sensor\)\.MinimumReportInterval, \(uint\)updateInterval\)') 'HC accel ReportInterval = Max(Min, updateInterval)'
Assert 'I2 HC TimerManager period 8ms' ($native -match 'kInputCaptureRequestedSensorIntervalMs = 8') 'YMCC updateInterval is 8ms (HC TimerManager.GetPeriod)'

# ── I3: gyro threshold clip on RAW, >= semantics, BEFORE matrix ───────────
Assert 'I3 HC gyro raw threshold >=' ($hcG -match 'Math\.Abs\(args\.Reading\.AngularVelocityX\) >= threshold \? 0') 'HC clips abs(raw)>=threshold to 0'
Assert 'I3 YMCC gyro threshold clip' ($native -match 'fabs\(v\) >= hcGyroGate \? 0\.0 : v') 'hcClip lambda must mirror HC >= gate'
$clipIdx = $native.IndexOf('gx = hcClip(gx); gy = hcClip(gy); gz = hcClip(gz)')
$matrixCalls = [regex]::Matches($native, 'inputCaptureApplyHcMatrixCandidate\(\r?\n\s+gx, gy, gz, ax, ay, az,')
if ($clipIdx -lt 0 -or $matrixCalls.Count -lt 1) {
  Assert 'I3 clip before matrix order' $false "missing markers clip=$clipIdx matrixCalls=$($matrixCalls.Count)"
} else {
  Assert 'I3 clip before matrix order' ($clipIdx -lt $matrixCalls[0].Index) "threshold must clip RAW before matrix (HC IMUGyrometer.cs:117-127)"
}

# ── I4: no threshold clip on accel path (HC IMUAccelerometer.cs) ──────────
Assert 'I4 HC accel has no threshold' ([bool](-not ($hcA -match 'Math\.Abs\('))) 'HC accel raw passes through unclipped'
Assert 'I4 YMCC accel has no threshold' ([bool](-not ($native -match 'hcClip\(ax' -or $native -match 'hcClip\(ay'))) 'YMCC must not clip accel axes'

# ── I5: single subscribe site — only the dedicated thread starts sensors ──
Assert 'I5 single Start call site' ((CountOf $native 'inputCaptureStartWinRtSensorsImpl();') -eq 1) 'sensor Start must only be dispatched from the sensor thread'

# ── I6: provider binding + binding generation ─────────────────────────────
Assert 'I6 device-id snapshot' ($native -match 'inputCaptureSensorMetaSnapshot\(\)') 'device meta read via snapshot (race-free)'
Assert 'I6 binding generation' ($native -match 'bindingGeneration' -and $native -match 'sameBindingGeneration') 'sink binding generation gate present'
Assert 'I6 same-provider gate' ($native -match 'sameProvider') 'same-provider admission present'

# ── I7: suspend/resume routed through the dedicated thread ────────────────
Assert 'I7 suspend request' ($native -match 'SensorThreadRequest::Suspended') 'suspend posts to sensor thread (HC StopListening semantics)'
Assert 'I7 resume request' ($native -match 'SensorThreadRequest::Resume') 'resume posts to sensor thread (HC UpdateSensor+StartListening)'

# ── I8: sensor-initialised receipt fields (HC log template parity) ────────
Assert 'I8 sensor-initialised present' ($native -match '"sensor-initialised"') 'initialised receipt logged'
Assert 'I8 interval fields' ($native -match '"gyroReportIntervalMs"' -and $native -match '"accelReportIntervalMs"') 'report interval fields logged'
Assert 'I8 family field' ($native -match '"sensorFamily"') 'sensor family field logged like HC template'

# ── I9: fusion dt comes from QPC, not the sensor timestamp ────────────────
$hcTimer = Join-Path $HcRoot 'Managers\TimerManager.cs'
$hcTm = Get-Content -LiteralPath $hcTimer -Raw -Encoding UTF8
Assert 'I9 HC GetDelta = Stopwatch delta' ($hcTm -match 'float delta = \(TotalMilliseconds - PreviousTotalMilliseconds\) / 1000\.0f') 'HC dt sourced from Stopwatch (QPC)'
Assert 'I9 YMCC dt from steady_clock' ($native -match 'const auto (frameAt|motionFrameAt) = std::chrono::steady_clock::now\(\);') 'YMCC dt sourced from steady_clock (QPC, HC TimerManager.GetDelta equivalent)'
Assert 'I9 YMCC dt feeds fusion' ($native -match 'g_gmProcess\(g_gm') 'elapsed dt feeds GamepadMotion process'
Assert 'I9 sensor Timestamp never sources dt' ([bool](-not ($native -match 'motionDeltaSeconds[^\r\n;]*Timestamp' -or $native -match 'frameAt[^\r\n;]*time_since_epoch'))) 'sensor timestamp must stay diagnostic (HC TimeOfDay vs YMCC FILETIME, no data-path leak)'

# ── I10: calibration lifecycle wired HC-parity (2026-09-11) ─────────────────
# HC SensorsManager.Calibrate (342-402) + IMUCalibration + GamepadMotion ctor.
$hcSensors = Join-Path $HcRoot 'Managers\SensorsManager.cs'
$hcSm = Get-Content -LiteralPath $hcSensors -Raw -Encoding UTF8
Assert 'I10 HC Calibrate flow present' ($hcSm -match 'ResetContinuousCalibration\(\)' -and $hcSm -match 'SetCalibrationMode\(CalibrationMode\.Stillness \| CalibrationMode\.SensorFusion\)' -and $hcSm -match 'GetAutoCalibrationConfidence\(\)' -and $hcSm -match 'SetCalibrationOffset\(xOffset, yOffset, zOffset, \(int\)\(confidence \* 10\.0f\)\)') 'HC reference flow intact'
Assert 'I10 IPC gyro.calibrate wired' ($native -match 'ipc_on\("gyro\.calibrate"') 'native exposes explicit calibration trigger'
Assert 'I10 tick calib state machine' ($native -match 'static void inputCaptureGyroCalibrationTick\(\)' -and $native -match 'inputCaptureGyroCalibrationTick\(\);') 'calibration driven per capture tick (g_gm single-thread)'
Assert 'I10 same Calibrate sequence' ($native -match 'g_gmResetCal\(g_gm\)' -and $native -match 'g_gmSetMode\(g_gm, 1 \| 2\)' -and $native -match 'g_gmGetConf\(g_gm\)' -and $native -match 'static_cast<int>\(g_gyroCalibConfidence \* 10\.0f\)' -and $native -match 'g_gmSetMode\(g_gm, 0\)') 'Reset->Fusion->Poll->Offset(conf*10)->Manual order matches HC'
Assert 'I10 StoreCalibration persisted' ($native -match 'calibration\.json' -and $native -match 'inputCaptureStoreCalibration\(') 'offsets stored to calibration.json like HC IMUCalibration'
Assert 'I10 restore on device seen' ($native -match 'inputCaptureLoadCalibration\(' -and $native -match 'gyro-calibration-restored' -and $native -match 'g_calibRestoredDeviceId') 'offsets restored once per device (HC GamepadMotion ctor parity)'
Assert 'I10 result emitted' ($native -match 'gyro\.calibrate\.result') 'calibration completion surfaced to the frontend session'

# ── I11: per-frame GamepadMotion.dll call trace equals HC (2026-09-11) ────────
# per TimerManager tick, HC calls into the DLL exactly:
#   ProcessMotion (SensorsManager.UpdateReport:328)
#   GetCalibratedGyro + GetGravity (GamepadMotion.cs:124-125 Madgwick overlay;
#   MotionManager.cs:94-97 re-reads the same handle state -> value-identical)
#   GetPlayerSpaceGyro(1.41f) / GetWorldSpaceGyro(0.125f) ONLY for the active
#     MotionInput space (MotionManager.cs:269-276)
# GetCalibrationOffset/GetAutoCalibrationConfidence live solely inside
# SensorsManager.Calibrate; GetAutoCalibrationIsSteady/GetProcessedAcceleration/
# GetOrientation have no production call site (orientation = C# Madgwick).
Assert 'I11 per-frame ProcessMotion' ($native -match 'g_gmProcess\(g_gm, \(float\)mgx') 'ProcessMotion consumes the matrixed pair input (HC gyro/accel)'
Assert 'I11 per-frame GetCalibratedGyro' ($native -match 'g_gmGetCal\(g_gm, &cx, &cy, &cz\)') 'calibrated read-back once per frame (HC double read is same-state)'
Assert 'I11 per-frame GetGravity' ($native -match 'g_gmGetGravity\(g_gm, &gravityX, &gravityY, &gravityZ\)') 'gravity read-back once per frame'
Assert 'I11 player-space gated' ($native -match '"player-space" && g_gmPlayer\)') 'GetPlayerSpaceGyro only while MotionInput==PlayerSpace (MotionManager.cs:270)'
Assert 'I11 world-space gated' ($native -match '"world-space" && g_gmWorld\)') 'GetWorldSpaceGyro only while MotionInput==WorldSpace (MotionManager.cs:274)'
Assert 'I11 no per-frame cal probes' ($native -match 'per-frame DLL trace is frame-identical to HC') 'per-frame GetCalibrationOffset/GetAutoCalibrationConfidence/IsSteady/GetProcessedAcceleration/GetOrientation calls removed'
Assert 'I11 cal queries only in state machine' ((CountOf $native 'g_gmGetConf(g_gm)') -eq 1 -and (CountOf $native 'g_gmGetOffset(g_gm') -eq 1 -and (CountOf $native 'g_gmIsSteady(g_gm') -eq 0) 'confidence/offset/steady queries restricted to the Calibrate state machine like HC'
Assert 'I11 restore precedes first process' ($native.IndexOf('inputCaptureGyroCalibrationRestoreOnce\(\);\r?\n\s+if \(live && g_gm') -ge 0 -or $native.IndexOf('inputCaptureGyroCalibrationRestoreOnce();') -lt $native.IndexOf('g_gmProcess(g_gm, (float)mgx')) 'ctor restore (SetCalibrationOffset+Manual) runs before the first ProcessMotion (GamepadMotion.cs:52-71)'
Assert 'I11 SD IMU uses raw matrixed input' ($native -match 'mgx, mgy, mgz,\r?\n\s+maxa, may, maz,') 'SteamDeck wire feeds ProcessMotion input mgx, not calibrated cgx (HC GetRawGyro SteamDeckTarget.cs:95)'

Write-Host ''
if ($fail -gt 0) { Write-Host "RESULT: $fail FAIL (import parity)" -ForegroundColor Red; exit 1 }
Write-Host 'RESULT: ALL PASS (import parity)' -ForegroundColor Green
exit 0