[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$CursorSourceDirectory,
    [string]$ReplayDirectory='G:/YeManCC-Work/Build/Analysis/Touchpad-Flicker-Field-20261015-231639',
    [switch]$ExpectBaseline
)
$ErrorActionPreference='Stop'
$motionProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$CursorSourceDirectory){$CursorSourceDirectory=Join-Path $motionProject 'native'}
$CursorSourceDirectory=[IO.Path]::GetFullPath($CursorSourceDirectory)
$ReplayDirectory=[IO.Path]::GetFullPath($ReplayDirectory)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$motionReplayPrefix=$ReplayDirectory.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
if(!$OutputDirectory.StartsWith($motionReplayPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be a child of ReplayDirectory.'}
$motionReceiptPath=Join-Path $OutputDirectory 'receipt.json'
if(Test-Path -LiteralPath $motionReceiptPath){throw 'Use a fresh output directory; prior evidence is preserved.'}
$motionTrace=Join-Path $ReplayDirectory 'export-trace.json'
$motionSegments=Join-Path $ReplayDirectory 'contact-segments.json'
$motionSource=Join-Path $PSScriptRoot 'screen_touchpad_motion_gate_selftest.cpp'
$motionHarness=Join-Path $PSScriptRoot 'screen_touchpad_cursor_r1_simulation.cpp'
$motionFrozenHarness=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor_r1_simulation.cpp'
if(Test-Path -LiteralPath $motionFrozenHarness){$motionHarness=$motionFrozenHarness}
$motionCursor=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h'
$motionVisibility=Join-Path $CursorSourceDirectory 'screen_touchpad_cursor_visibility.h'
$motionDisplay=Join-Path $CursorSourceDirectory 'screen_touchpad_display_policy.h'
if(!(Test-Path -LiteralPath $motionDisplay)){$motionDisplay=Join-Path $motionProject 'native/screen_touchpad_display_policy.h'}
$motionFiles=@($motionSource,$PSCommandPath,$motionHarness,$motionCursor,$motionVisibility,$motionDisplay,$motionTrace,$motionSegments)
function Get-MotionHashes {
    @($motionFiles | ForEach-Object { [ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash} })
}
$motionCursorText=Get-Content -LiteralPath $motionCursor -Raw
$motionFeature=$motionCursorText.Contains('cursorResetDeckMotion')
$motionContactShapeFeature=$motionCursorText.Contains('cursorDeckContactShapeBridgeShows')
if($ExpectBaseline -and $motionFeature){throw 'Baseline requires the frozen pre-motion-gate headers.'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$motionReceipt=[ordered]@{ok=$false;status='started';startedAtUtc=[DateTime]::UtcNow.ToString('o');baseline=[bool]$ExpectBaseline;motionGateFeatureCompiled=$motionFeature;sources=(Get-MotionHashes);realWindowsCreated=0;realHooksRegistered=0;realInputInjected=0;fieldFlickerVerified=$false;processCpuMeasured=$false}
function Save-MotionReceipt { $motionReceipt | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $motionReceiptPath -Encoding utf8 }
Save-MotionReceipt
$motionTempRoot=[IO.Path]::GetFullPath([IO.Path]::GetTempPath())
$motionBuild=[IO.Path]::GetFullPath((Join-Path $motionTempRoot ('ymcc-motion-gate-'+[guid]::NewGuid().ToString('N'))))
$motionArtifacts=@()
try {
    $motionVcvars='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat'
    if(!(Test-Path -LiteralPath $motionVcvars)){throw 'MSVC x64 BuildTools are required.'}
    New-Item -ItemType Directory -Path $motionBuild | Out-Null
    $motionExe=Join-Path $motionBuild 'motion-gate.exe'
    $motionObj=Join-Path $motionBuild 'motion-gate.obj'
    $motionCmd=Join-Path $motionBuild 'build-motion-gate.cmd'
    $motionArtifacts=@($motionExe,$motionObj,$motionCmd)
    $motionFrozen=@()
    foreach($motionCompileInput in @($motionSource,$motionHarness,$motionCursor,$motionVisibility,$motionDisplay)){
        $motionDestination=Join-Path $motionBuild ([IO.Path]::GetFileName($motionCompileInput))
        Copy-Item -LiteralPath $motionCompileInput -Destination $motionDestination
        $motionArtifacts+=@($motionDestination)
        $motionCopiedHash=(Get-FileHash -LiteralPath $motionDestination -Algorithm SHA256).Hash
        $motionExpectedHash=($motionReceipt.sources | Where-Object {$_.path -eq $motionCompileInput}).sha256
        if($motionCopiedHash -ne $motionExpectedHash){throw ('Source changed while freezing: '+$motionCompileInput)}
        $motionFrozen+=@([ordered]@{source=$motionCompileInput;frozenFile=[IO.Path]::GetFileName($motionDestination);sha256=$motionCopiedHash})
    }
    $motionFrozen | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'frozen-build-sources.json') -Encoding utf8
    $motionReceipt.compiledFromFrozenSources=$true
    $motionFrozenSource=Join-Path $motionBuild 'screen_touchpad_motion_gate_selftest.cpp'
    $motionDefine=if($motionFeature){1}else{0}
    $motionContactShapeDefine=if($motionContactShapeFeature){1}else{0}
    $motionReceipt.contactShapeBridgeFeatureCompiled=$motionContactShapeFeature
    $motionImports=Join-Path $OutputDirectory 'imports.txt'
    @"
@echo off
call "$motionVcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_TEST_MOTION_GATE=$motionDefine /DYMCC_TEST_CONTACT_SHAPE_BRIDGE=$motionContactShapeDefine /I"$motionBuild" /I"$motionProject/deps/json" "$motionFrozenSource" /Fo"$motionObj" /Fe"$motionExe" /link /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$motionExe" > "$motionImports"
if errorlevel 1 exit /b 1
"@ | Set-Content -LiteralPath $motionCmd -Encoding ascii
    & $motionCmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'build.log')
    if($LASTEXITCODE -ne 0){throw 'Motion fixture compilation/import inspection failed.'}
    $motionImportText=Get-Content -LiteralPath $motionImports -Raw
    if($motionImportText -match '(?im)^\s*(USER32|GDI32|DWMAPI|WIN32U|DINPUT8|XINPUT[^\s]*)\.dll\s*$'){throw 'Unexpected live graphics/input DLL import.'}
    $motionReceipt.noLiveGraphicsOrInputDllImports=$true
    $motionReceipt.executableSha256=(Get-FileHash -LiteralPath $motionExe -Algorithm SHA256).Hash
    $motionReportPath=Join-Path $OutputDirectory 'simulation.json'
    $motionMode=if($ExpectBaseline){'baseline'}else{'verify'}
    $motionArguments=@(('"'+$motionTrace+'"'),('"'+$motionSegments+'"'),('"'+$motionReportPath+'"'),$motionMode)
    $motionRun=Start-Process -FilePath $motionExe -ArgumentList $motionArguments -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'run.stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'run.stderr.txt')
    $motionReceipt.exitCode=$motionRun.ExitCode
    if(!(Test-Path -LiteralPath $motionReportPath)){throw 'Fixture did not produce its report.'}
    $motionResult=Get-Content -LiteralPath $motionReportPath -Raw | ConvertFrom-Json
    $motionReceipt.caseCount=@($motionResult.cases).Count
    $motionFailed=@($motionResult.cases | Where-Object {!$_.ok})
    $motionReceipt.actualFailures=@($motionFailed | ForEach-Object {$_.name})
    $motionReceipt.simulationOk=[bool]$motionResult.ok
    $motionReceipt.fieldReplay=$motionResult.fieldReplay | Select-Object compressedRecords,validRecords,invalidLifecycleRecords,knownEndpointsReplayed,contactSegments,suppressedRecords,recordedSuppressedMotionRecords,helperShows,helperHides,calls
    $motionAfter=Get-MotionHashes
    for($motionIndex=0;$motionIndex -lt $motionAfter.Count;$motionIndex++){
        if($motionAfter[$motionIndex].sha256 -ne $motionReceipt.sources[$motionIndex].sha256){throw ('Source/input changed during run: '+$motionAfter[$motionIndex].path)}
    }
    $motionReceipt.sourcesUnchanged=$true
    if($ExpectBaseline){
        if(@($motionFailed | Where-Object {$_.category -ne 'motion-policy14'}).Count){throw 'Baseline failed a preserved-behavior or input-integrity assertion.'}
        foreach($motionRequired in @('stationary-down-no-fallback','stationary-up-retained-no-fallback','field-no-fallback-from-stationary-suppression')){
            if($motionReceipt.actualFailures -notcontains $motionRequired){throw ('Baseline did not reproduce required pre-gate behavior: '+$motionRequired)}
        }
        if($motionRun.ExitCode -ne 1){throw 'Baseline must report real assertion failures, exit code 1.'}
        $motionReceipt.status='policy13-baseline-motion-rule-differences-reproduced'
    }else{
        if(!$motionResult.ok -or $motionRun.ExitCode -ne 0){throw 'Motion-gate candidate has failing assertions; see simulation.json.'}
        $motionReceipt.status='motion-gate-offline-tests-passed'
    }
    $motionReceipt.ok=$true
    $motionFailureText=if($motionFailed.Count){($motionFailed | ForEach-Object {'- '+$_.name}) -join "`n"}else{'None.'}
    @"
# Motion gate offline replay

Status: $($motionReceipt.status)

Actual assertion result: $(@($motionResult.cases | Where-Object {$_.ok}).Count)/$($motionReceipt.caseCount) passed. Baseline mode: $([bool]$ExpectBaseline). Baseline differences remain failed assertions in simulation.json; receipt acceptance does not turn them into policy14 passes.

Field input: $($motionResult.fieldReplay.compressedRecords) compressed rows, $($motionResult.fieldReplay.validRecords) valid rows, $($motionResult.fieldReplay.invalidLifecycleRecords) invalid lifecycle rows, $($motionResult.fieldReplay.knownEndpointsReplayed) known first/last endpoints, $($motionResult.fieldReplay.contactSegments) observed segments. Min/max is metadata only. Missing intermediate coordinates, input frames, original cache state and physical DOWN times were not reconstructed.

Replay helper shows/hides: $($motionResult.fieldReplay.helperShows)/$($motionResult.fieldReplay.helperHides). These are offline substitute calls, not a reproduced device flicker result. No User32/GDI/input DLL imports; no real windows, hooks, input or devices. Production/harness/input hashes unchanged across this run.

Failed assertions:

$motionFailureText

The dedicated fixture uses its own raw start/sample helpers. The original harness main and down setup are not invoked. Retained hidden UP, authorized retry, NULL shape, mode changes, Mouse/mixed and 24ms scheduling have independent synthetic coverage.
"@ | Set-Content -LiteralPath (Join-Path $OutputDirectory 'REPORT.md') -Encoding utf8
} catch {
    $motionReceipt.status='failed';$motionReceipt.error=$_.Exception.Message;throw
} finally {
    # Remove only explicitly named temporary build files, using one shell.
    $motionTempPrefix=$motionTempRoot.TrimEnd('\','/')+[IO.Path]::DirectorySeparatorChar
    if(!$motionBuild.StartsWith($motionTempPrefix,[StringComparison]::OrdinalIgnoreCase)){throw 'Temporary cleanup target escaped its verified root.'}
    foreach($motionArtifact in $motionArtifacts){
        $motionAbsolute=[IO.Path]::GetFullPath($motionArtifact)
        if(!$motionAbsolute.StartsWith($motionBuild+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Temporary file escaped the build directory.'}
        if(Test-Path -LiteralPath $motionAbsolute){Remove-Item -LiteralPath $motionAbsolute -Force}
    }
    if(Test-Path -LiteralPath $motionBuild){
        if(@(Get-ChildItem -LiteralPath $motionBuild -Force).Count -eq 0){Remove-Item -LiteralPath $motionBuild}
        else{$motionReceipt.temporaryBuildDirectoryRetained=$motionBuild}
    }
    $motionReceipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');Save-MotionReceipt
}
$motionReceipt | ConvertTo-Json -Depth 8