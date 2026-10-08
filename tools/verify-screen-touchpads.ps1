[CmdletBinding()]
param([string]$WorkspaceRoot = $env:YEMAN_WORKSPACE_ROOT)
$ErrorActionPreference = 'Stop'
if ([string]::IsNullOrWhiteSpace($WorkspaceRoot)) {
  $WorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..\..'))
}
$ProjectRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$Native = Join-Path $WorkspaceRoot 'Build\App\Native'
$Validation = Join-Path $WorkspaceRoot 'Build\Validation\ScreenTouchpads'
New-Item -ItemType Directory -Path $Validation -Force | Out-Null
$exe = Join-Path $Native 'YeManCC.exe'
if (!(Test-Path -LiteralPath $exe)) { throw 'Build the existing main application first.' }
function Run-NativeTest([string]$Flag, [string]$Report) {
  $si = [Diagnostics.ProcessStartInfo]::new()
  $si.FileName = $exe; $si.Arguments = $Flag; $si.UseShellExecute = $false
  $si.CreateNoWindow = $true; $si.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
  # Only these early-return, driver-free tests run at caller integrity. The
  # product's requireAdministrator manifest is not changed or removed.
  $si.EnvironmentVariables['__COMPAT_LAYER'] = 'RunAsInvoker'
  $p = [Diagnostics.Process]::Start($si)
  if (!$p.WaitForExit(15000)) { throw "Self-test still running (PID=$($p.Id)); inspect that process, do not restart it." }
  $path = Join-Path $Native $Report
  if (!(Test-Path -LiteralPath $path)) { throw "No fresh test result: $Report" }
  $r = Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
  Copy-Item -LiteralPath $path -Destination (Join-Path $Validation $Report) -Force
  if ($p.ExitCode -ne 0 -or !$r.ok) {
    $failed = ($r.cases | Where-Object { !$_.ok } | ForEach-Object { $_.name }) -join ','
    throw "Self-test failed: $Flag; $failed"
  }
  Write-Host "PASS $Flag"
  return $r
}
$core = Run-NativeTest '--screen-touchpads-selftest' 'screen-touchpads-selftest.json'
$pointer = Run-NativeTest '--screen-touchpads-pointer-selftest' 'screen-touchpads-pointer-selftest.json'
$wire = Run-NativeTest '--screen-touchpads-wire-selftest' 'screen-touchpads-wire-selftest.json'
$hostExe = Join-Path $ProjectRoot 'InputHost\bin\Release\net10.0-windows\YeManInputHost.exe'
foreach ($test in @('--selftest-screen-buttons', '--selftest-dualsense-touchpad', '--selftest-ds4-touchpad', '--selftest-steamdeck-touchpads', '--selftest-steamdeck-state', '--selftest-protocol', '--selftest-ds-wire', '--selftest-xbox360-state')) {
  & $hostExe $test
  if ($LASTEXITCODE -ne 0) { throw "InputHost test failed: $test" }
  Write-Output "PASS InputHost $test"
}
& $hostExe '--selftest-native-touchpad-fixture' (Join-Path $Native 'screen-touchpads-wire-selftest.json')
if ($LASTEXITCODE -ne 0) { throw 'Native-to-Host fixture failed' }
Push-Location $ProjectRoot
try {
  & pnpm exec vue-tsc --noEmit --pretty false
  if ($LASTEXITCODE -ne 0) { throw 'Typecheck failed' }
  & pnpm exec esbuild tools/screen_touchpads_selftest.ts --bundle --platform=node --format=cjs "--outfile=$Validation\frontend-selftest.cjs"
  if ($LASTEXITCODE -ne 0) { throw 'Frontend test bundle failed' }
  & node (Join-Path $Validation 'frontend-selftest.cjs')
  if ($LASTEXITCODE -ne 0) { throw 'Frontend test failed' }
  & node tools/screen_touchpads_ui_selftest.mjs
  if ($LASTEXITCODE -ne 0) { throw 'Actual Vue UI render test failed' }
  & node tools/screen_touchpads_polish_browser_selftest.mjs
  if ($LASTEXITCODE -ne 0) { throw 'Production gamepad/profile browser test failed' }
} finally { Pop-Location }
$files = @('native\main.cpp','native\screen_touchpads.h','native\screen_button_overlay.h','native\screen_control_glyphs.h','InputHost\Program.cs','InputHost\ScreenTouchpadReport.cs','InputHost\ScreenButtonReport.cs','InputHost\ScreenButtonFixtureTest.cs',
  'src\bridge\screenTouchpads.ts','src\components\ScreenTouchpadsSettings.vue','src\views\ButtonMappingView.vue',
  'src\components\Slider.vue','src\gamepad\engine.ts','src\components\SteamDeckMouseSensitivity.vue',
  'tools\screen_touchpads_selftest.ts','tools\screen_touchpads_ui_selftest.mjs','tools\screen_touchpads_polish_browser_selftest.mjs','tools\verify-screen-touchpads.ps1')
$hashes = @($files | ForEach-Object { $p = Join-Path $ProjectRoot $_; [ordered]@{ path=$p; sha256=(Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash } })
# Verify the evidence belongs to a build newer than every changed compile input.
$nativeBuilt=(Get-Item -LiteralPath $exe).LastWriteTimeUtc
$hostBuilt=(Get-Item -LiteralPath (Join-Path (Split-Path $hostExe) 'YeManInputHost.dll')).LastWriteTimeUtc
foreach($relative in @('native\main.cpp','native\screen_touchpads.h','native\screen_button_overlay.h','native\screen_control_glyphs.h')) {
  if((Get-Item -LiteralPath (Join-Path $ProjectRoot $relative)).LastWriteTimeUtc -gt $nativeBuilt) { throw "Native source newer than tested binary: $relative" }
}
foreach($relative in @('InputHost\Program.cs','InputHost\ScreenTouchpadReport.cs','InputHost\ScreenButtonReport.cs','InputHost\ScreenButtonFixtureTest.cs')) {
  if((Get-Item -LiteralPath (Join-Path $ProjectRoot $relative)).LastWriteTimeUtc -gt $hostBuilt) { throw "Host source newer than tested binary: $relative" }
}
$webIndex = Join-Path $WorkspaceRoot 'Build\App\Web\index.html'
if (!(Test-Path -LiteralPath $webIndex)) { throw 'No mainline frontend build found' }
$webBuilt = (Get-Item -LiteralPath $webIndex).LastWriteTimeUtc
foreach($relative in @('src\bridge\screenTouchpads.ts','src\components\ScreenTouchpadsSettings.vue','src\views\ButtonMappingView.vue')) {
  if((Get-Item -LiteralPath (Join-Path $ProjectRoot $relative)).LastWriteTimeUtc -gt $webBuilt) { throw "Frontend source newer than built Web: $relative" }
}
$receipt = [ordered]@{ ok=$true; sources=$hashes; nativeSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash;
  webIndexSha256=(Get-FileHash -LiteralPath $webIndex -Algorithm SHA256).Hash; coreCases=$core.cases.Count; pointerCases=$pointer.cases.Count;
  steamDeckNativeFrames=$wire.messages.Count; dualSenseNativeFrames=$wire.dualSenseMessages.Count; psDualNativeFrames=$wire.psDualMessages.Count; buttonNativeFrames=$wire.screenButtonMessages.Count; pointerEvents=$pointer.pointerEvents;
  buttonPointerEvents=$pointer.buttonPointerEvents; buttonSummonCalls=$pointer.buttonSummonCalls;
  disabledMillionSnapshotsUs=$pointer.disabledMillionSnapshotsUs;
  nativeIdleHundredThousandSnapshotsUs=$pointer.nativeIdleHundredThousandSnapshotsUs;
  visibleIdleWallMs=$pointer.visibleIdleWallMs; visibleIdleCpuMs=$pointer.visibleIdleCpuMs;
  inputHostSha256=(Get-FileHash -LiteralPath (Join-Path (Split-Path $hostExe) 'YeManInputHost.dll') -Algorithm SHA256).Hash;
  systemKeyboardMouseInjected=$false;
  browserCases=(Get-Content -LiteralPath (Join-Path $WorkspaceRoot 'Build\Validation\ScreenTouchpads-Polish\browser-verification.json') -Raw | ConvertFrom-Json).cases.Count }
$receipt | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $Validation 'verification.json') -Encoding UTF8
Write-Output "SCREEN_TOUCHPADS_VERIFIED $Validation"
