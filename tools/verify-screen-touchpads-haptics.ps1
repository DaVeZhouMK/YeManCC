[CmdletBinding()]
param([string]$ValidationRoot,[string]$AppExe,[string]$SourceRoot)
$ErrorActionPreference='Stop'
# pnpm may inherit PowerShell 7's module path while this command uses Windows
# PowerShell 5.1. Prefer the calling shell's built-in modules (same build policy).
$env:PSModulePath=[IO.Path]::Combine($PSHOME,'Modules')+';'+$env:PSModulePath
Import-Module Microsoft.PowerShell.Utility -ErrorAction Stop
$liveProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$liveProject}
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenTouchpads-Haptics'))}
if(!$AppExe){$AppExe=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\App\Native\YeManCC.exe'))}
New-Item -ItemType Directory -Force -Path $ValidationRoot|Out-Null
$files=@('native\main.cpp','native\screen_touchpads.h','native\touch_haptic_dispatch.h','native\radial_haptics.h','src\bridge\screenTouchpads.ts','src\components\ScreenTouchpadsSettings.vue')
$testFiles=@('tools\screen_touchpads_haptics_selftest.cpp','tools\verify-screen-touchpads-haptics.ps1')
function Hashes{@($files|ForEach-Object{$p=Join-Path $project $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})+@($testFiles|ForEach-Object{$p=Join-Path $liveProject $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{ok=$false;hardwareFeelVerified=$false;productDeployed=$false;hardwareWrites=0;sourceRoot=$project;sources=(Hashes);runs=@();startedUtc=[DateTime]::UtcNow.ToString('o')}
try {
  foreach($kind in @('legacy','candidate')){
    $define=if($kind -eq 'candidate'){'/DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1'}else{''}
    $exe=Join-Path $ValidationRoot "haptics-$kind.exe";$obj=Join-Path $ValidationRoot "haptics-$kind.obj";$cmd=Join-Path $ValidationRoot "build-haptics-$kind.cmd"
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE $define /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$liveProject\tools\screen_touchpads_haptics_selftest.cpp" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib
"@ | Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd;if($LASTEXITCODE -ne 0){throw "Compile failed: $kind"}
    $report=Join-Path $ValidationRoot "haptics-$kind.json";& $exe $report|Out-Host
    if($LASTEXITCODE -ne 0){throw "Haptics state test failed: $kind"}
    $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
    if(!$r.ok -or $r.hardwareWrites -ne 0 -or $r.systemInputInjected){throw "Haptics state result invalid: $kind"}
    $receipt.runs+=@([ordered]@{kind=$kind;checks=@($r.cases).Count;report=$report;ok=$r.ok})
  }
  if(!(Test-Path -LiteralPath $AppExe)){throw 'Actual main EXE is required for the production physical-output test'}
  foreach($file in @('native\main.cpp','native\screen_touchpads.h','native\touch_haptic_dispatch.h')){if((Get-Item -LiteralPath (Join-Path $project $file)).LastWriteTimeUtc -gt (Get-Item -LiteralPath $AppExe).LastWriteTimeUtc){throw "Actual EXE predates source: $file"}}
  $report=Join-Path (Split-Path $AppExe) 'screen-touchpads-haptics-output-selftest.json';if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
  $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$AppExe;$si.Arguments='--screen-touchpads-haptics-output-selftest';$si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$si.EnvironmentVariables['__COMPAT_LAYER']='RunAsInvoker'
  $p=[Diagnostics.Process]::Start($si);if(!$p.WaitForExit(15000)){throw "Owned selftest still live (PID=$($p.Id)); do not restart it"}
  $r=Get-Content -LiteralPath $report -Raw|ConvertFrom-Json
  if($p.ExitCode -ne 0 -or !$r.ok -or $r.hardwareWrites -ne 0 -or $r.virtualTargetStarted -or $r.configurationWrites -ne 0){throw 'Production no-device/fixture output test failed'}
  $saved=Join-Path $ValidationRoot 'app-haptics-output.json';Copy-Item -LiteralPath $report -Destination $saved -Force
  $receipt.runs+=@([ordered]@{kind='actual-app-output';checks=@($r.cases).Count;report=$saved;ok=$r.ok});$receipt.appSha256=(Get-FileHash -LiteralPath $AppExe).Hash
  $after=Hashes;for($i=0;$i -lt $after.Count;$i++){if($receipt.sources[$i].sha256 -ne $after[$i].sha256){throw "Source changed during verification: $($after[$i].path)"}}
  $receipt.ok=$true;Write-Output "TOUCH_HAPTICS_VERIFIED checks=$(($receipt.runs|ForEach-Object {$_.checks}|Measure-Object -Sum).Sum); no real hardware vibration claimed"
} catch {$receipt.error=$_.Exception.Message;throw}
finally {$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $ValidationRoot 'haptics-verification.json') -Encoding UTF8}
