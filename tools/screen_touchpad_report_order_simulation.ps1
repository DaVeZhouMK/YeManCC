[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [string]$CursorSourceDirectory,
    [string[]]$ExpectedFailures=@()
)
$ErrorActionPreference='Stop'
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(!$CursorSourceDirectory){$CursorSourceDirectory=Join-Path $project 'native'}
$CursorSourceDirectory=[IO.Path]::GetFullPath($CursorSourceDirectory)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$motionGateDefine=if(Select-String -LiteralPath (Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h') -Pattern 'inline void cursorResetDeckMotion' -Quiet){'/DYMCC_CURSOR_MOTION_GATE_FIXTURE_ENABLED=1'}else{''}
$moveOrderDefine=if(Select-String -LiteralPath (Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h') -Pattern 'cursorDeckReportPulseTick' -Quiet){'/DYMCC_CURSOR_MOVE_SUBMIT_ORDER_FIXTURE_ENABLED=1'}else{''}
$receiptPath=Join-Path $OutputDirectory 'receipt.json'
if(Test-Path -LiteralPath $receiptPath){throw 'Use a new output directory; existing evidence will not be overwritten.'}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$legacyOrder=!(Select-String -LiteralPath (Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h') -Pattern 'inline void cursorRequestBeforeReport\(' -Quiet)
$legacyDefine=if($legacyOrder){'/DYMCC_CURSOR_REPORT_ORDER_LEGACY_FIXTURE=1'}else{''}
$files=@((Join-Path $CursorSourceDirectory 'screen_touchpad_cursor.h'),(Join-Path $CursorSourceDirectory 'screen_touchpad_cursor_visibility.h'),(Join-Path $project 'native\screen_touchpad_display_policy.h'),(Join-Path $PSScriptRoot 'screen_touchpad_report_order_simulation.cpp'),(Join-Path $PSScriptRoot 'screen_touchpad_cursor_r1_simulation.cpp'),$PSCommandPath)
function SourceHashes { @($files | ForEach-Object {[ordered]@{path=$_;sha256=(Get-FileHash -LiteralPath $_ -Algorithm SHA256).Hash}}) }
$receipt=[ordered]@{ok=$false;status='started';startedAtUtc=[DateTime]::UtcNow.ToString('o');sources=(SourceHashes);expectedFailures=$ExpectedFailures;legacyOrderFixture=$legacyOrder;realInputInjected=0;realWindowsCreated=0;fieldFlickerVerified=$false}
function SaveReceipt { $receipt | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $receiptPath -Encoding utf8 }
SaveReceipt
try {
    $vcvars='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
    if(!(Test-Path -LiteralPath $vcvars)){throw 'MSVC x64 BuildTools are required.'}
    $exe=Join-Path $OutputDirectory 'cursor-report-order-simulation.exe'
    $obj=Join-Path $OutputDirectory 'cursor-report-order-simulation.obj'
    $source=Join-Path $PSScriptRoot 'screen_touchpad_report_order_simulation.cpp'
    $cmd=Join-Path $OutputDirectory 'build-simulation.cmd'
    @"
@echo off
call "$vcvars" >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /W4 /DNDEBUG /DUNICODE /D_UNICODE $motionGateDefine $moveOrderDefine $legacyDefine /I"$CursorSourceDirectory" /I"$project\native" /I"$project\deps\json" "$source" /Fo"$obj" /Fe"$exe" /link /NODEFAULTLIB:user32.lib /NODEFAULTLIB:gdi32.lib
if errorlevel 1 exit /b 1
dumpbin /imports "$exe" > "$OutputDirectory\imports.txt"
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd 2>&1 | Tee-Object -FilePath (Join-Path $OutputDirectory 'build.log')
    if($LASTEXITCODE -ne 0){throw 'Isolated simulation compilation failed.'}
    $imports=Get-Content -LiteralPath (Join-Path $OutputDirectory 'imports.txt') -Raw
    if($imports -match '(?im)^\s*(USER32|GDI32|DWMAPI|DINPUT8|XINPUT[^\s]*)\.dll\s*$'){throw 'Unexpected live graphics/input DLL import.'}
    $receipt.noLiveGraphicsOrInputDllImports=$true
    $receipt.executableSha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash
    $reportPath=Join-Path $OutputDirectory 'simulation.json'
    # Capture native stderr without PowerShell treating intentional baseline FAIL rows as exceptions.
    $run=Start-Process -FilePath $exe -ArgumentList @(('"'+$reportPath+'"')) -WindowStyle Hidden -Wait -PassThru -RedirectStandardOutput (Join-Path $OutputDirectory 'run.stdout.txt') -RedirectStandardError (Join-Path $OutputDirectory 'run.stderr.txt')
    $receipt.exitCode=$run.ExitCode
    if(!(Test-Path -LiteralPath $reportPath)){throw 'Simulation did not produce a report.'}
    $result=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
    $receipt.caseCount=@($result.cases).Count
    $receipt.actualFailures=@($result.cases | Where-Object {!$_.ok} | ForEach-Object {$_.name})
    $receipt.simulationOk=[bool]$result.ok
    $after=SourceHashes
    for($i=0;$i -lt $after.Count;$i++){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during simulation: $($after[$i].path)"}}
    $receipt.sourcesUnchanged=$true
    $actual=@($receipt.actualFailures | Sort-Object)
    $expected=@($ExpectedFailures | Sort-Object)
    if(($actual -join "`n") -ne ($expected -join "`n")){throw 'Observed failures differ from the explicitly expected list.'}
    $expectedExit=if($expected.Count){1}else{0}
    if($run.ExitCode -ne $expectedExit){throw "Unexpected exit code: $($run.ExitCode)"}
    $receipt.ok=$true;$receipt.status=if($expected.Count){'baseline-defects-reproduced'}else{'offline-simulation-passed'}
} catch {$receipt.status='failed';$receipt.error=$_.Exception.Message;throw} finally {$receipt.finishedAtUtc=[DateTime]::UtcNow.ToString('o');SaveReceipt}
$receipt | ConvertTo-Json -Depth 10