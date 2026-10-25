[CmdletBinding()]
param([string]$ValidationRoot,[switch]$DiagnoseOnly,[string]$AppExe,[string]$SourceRoot)
$ErrorActionPreference='Stop'
$liveProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=if($SourceRoot){[IO.Path]::GetFullPath($SourceRoot)}else{$liveProject}
if(!$ValidationRoot){$ValidationRoot=[IO.Path]::GetFullPath((Join-Path $project '..\..\..\Build\Validation\ScreenMousePass-Goal\mouse-clickthrough'))}
New-Item -ItemType Directory -Path $ValidationRoot -Force|Out-Null
$files=@('native\main.cpp','native\screen_touchpad_owner.h','native\screen_touchpads.h','native\screen_touchpad_input_surface.h','native\screen_button_overlay.h','native\build_native.bat','tools\screen_touchpads_native_topology_test_host.h','tools\screen_touchpads_mouse_clickthrough_selftest.cpp','tools\verify-screen-touchpads-mouse-clickthrough.ps1')
function Hashes{@($files|ForEach-Object{$root=if($_.StartsWith('native\')){$project}else{$liveProject};$p=Join-Path $root $_;[ordered]@{path=$p;sha256=(Get-FileHash -LiteralPath $p).Hash}})}
$receipt=[ordered]@{ok=$false;sourceRoot=$project;featureAcceptancePassed=$false;scope='ordinary mouse clicks inside the touchpad reach the lower window';hardwareTouchVerified=$false;simultaneousTouchTested=$false;productDeployed=$false;diagnoseOnly=[bool]$DiagnoseOnly;sources=(Hashes);startedUtc=[DateTime]::UtcNow.ToString('o')}
try {
    $exe=Join-Path $ValidationRoot 'mouse-clickthrough.exe';$obj=Join-Path $ValidationRoot 'mouse-clickthrough.obj';$cmd=Join-Path $ValidationRoot 'build-mouse-clickthrough.cmd'
    @"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "CPPWINRT_INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%cppwinrt"
cl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE /DYMCC_SCREEN_NATIVE_INPUT_CANDIDATE=1 /I"$project\native" /I"$project\deps\json" /I"%CPPWINRT_INCLUDE%" "$liveProject\tools\screen_touchpads_mouse_clickthrough_selftest.cpp" /Fo"$obj" /Fe"$exe" /link user32.lib gdi32.lib ole32.lib shell32.lib
"@|Set-Content -LiteralPath $cmd -Encoding ascii
    & $cmd;if($LASTEXITCODE -ne 0){throw 'Mouse clickthrough helper build failed'}
    $report=Join-Path $ValidationRoot 'mouse-clickthrough.json';if(Test-Path -LiteralPath $report){Remove-Item -LiteralPath $report}
    & $exe $report|Out-Host;$exit=$LASTEXITCODE
    if(!(Test-Path -LiteralPath $report)){throw 'No fresh clickthrough report'}
    $r=Get-Content -LiteralPath $report -Raw -Encoding UTF8|ConvertFrom-Json
    $receipt.diagnosticValid=[bool]$r.diagnosticValid;$receipt.featureAcceptancePassed=[bool]$r.featureAcceptancePassed
    $receipt.defaultMousePassed=[bool]$r.defaultMousePassed;$receipt.explicitNativeMousePassed=[bool]$r.explicitNativeMousePassed
    $receipt.legacyBlocksMouse=[bool]$r.legacyBlocksMouse;$receipt.cases=@($r.cases).Count;$receipt.helperSha256=(Get-FileHash -LiteralPath $exe).Hash
    $receipt.report=$report;$receipt.testMouseInjected=$true;$receipt.clickForwarded=[bool]$r.clickForwarded;$receipt.ownedTargetTerminal=[bool]$r.ownedTargetTerminal
    if(!$r.diagnosticValid -or !$r.ownedTargetTerminal -or !$r.legacyBlocksMouse -or !$r.explicitNativeMousePassed){throw 'Owned-window mouse control/explicit native comparison invalid'}
    $after=Hashes;for($i=0;$i -lt $after.Count;++$i){if($after[$i].sha256 -ne $receipt.sources[$i].sha256){throw "Source changed during clickthrough test: $($after[$i].path)"}}
    if(!$DiagnoseOnly -and ($exit -ne 0 -or !$r.featureAcceptancePassed)){throw 'DEFAULT_MOUSE_CLICKTHROUGH_FAILED'}
    if(!$DiagnoseOnly -or $AppExe) {
        if(!$AppExe){$AppExe=[IO.Path]::GetFullPath((Join-Path $project '..\..\Build\App\Native\YeManCC.exe'))}
        if(!(Test-Path -LiteralPath $AppExe)){throw "Missing actual main EXE: $AppExe"}
        foreach($file in @('native\main.cpp','native\build_native.bat','native\screen_touchpad_owner.h','native\screen_touchpad_input_surface.h','native\screen_touchpads.h','native\screen_button_overlay.h')) {
            if((Get-Item -LiteralPath (Join-Path $project $file)).LastWriteTimeUtc -gt (Get-Item -LiteralPath $AppExe).LastWriteTimeUtc){throw "Actual EXE predates source: $file"}
        }
        $receipt.appExe=$AppExe;$receipt.appSha256=(Get-FileHash -LiteralPath $AppExe).Hash;$receipt.appRuns=@()
        foreach($variant in @('default','opt-in','explicit-legacy','conflict')) {
            $extra=if($variant -eq 'opt-in'){' --screen-touchpads-native-input-candidate'}elseif($variant -eq 'explicit-legacy'){' --screen-touchpads-native-input-legacy'}elseif($variant -eq 'conflict'){' --screen-touchpads-native-input-candidate --screen-touchpads-native-input-legacy'}else{''}
            $appReport=Join-Path (Split-Path $AppExe) 'screen-touchpads-owner-selftest.json'
            if(Test-Path -LiteralPath $appReport){Remove-Item -LiteralPath $appReport}
            $si=[Diagnostics.ProcessStartInfo]::new();$si.FileName=$AppExe;$si.Arguments='--screen-touchpads-owner-selftest'+$extra
            $si.UseShellExecute=$false;$si.CreateNoWindow=$true;$si.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden;$si.EnvironmentVariables['__COMPAT_LAYER']='RunAsInvoker'
            $p=[Diagnostics.Process]::Start($si)
            if(!$p.WaitForExit(30000)){throw "Owner selftest still live PID=$($p.Id); inspect SAME process, do not restart"}
            $app=Get-Content -LiteralPath $appReport -Raw -Encoding UTF8|ConvertFrom-Json
            $expected=$variant -in @('default','opt-in')
            if($p.ExitCode -ne 0 -or !$app.ok -or !$app.startup.compiled -or [bool]$app.startup.active -ne $expected -or [bool]$app.restartStartup.active -ne $expected -or $app.systemInputInjected -or $app.deviceOperations -ne 0 -or $app.configurationWrites -ne 0){throw "Actual default-build owner policy failed: $variant"}
            $saved=Join-Path $ValidationRoot "app-owner-$variant.json";Copy-Item -LiteralPath $appReport -Destination $saved -Force
            $receipt.appRuns+=@([ordered]@{variant=$variant;ok=$true;active=$app.startup.active;cases=@($app.cases).Count;report=$saved})
            $p.Dispose()
        }
    }
    $receipt.ok=$r.featureAcceptancePassed
    Write-Output "MOUSE_CLICKTHROUGH_VERIFIED default=$($r.defaultMousePassed) cases=$($receipt.cases)"
}catch{$receipt.ok=$false;$receipt.featureAcceptancePassed=$false;$receipt.error=$_.Exception.Message;throw}
finally{$receipt.finishedUtc=[DateTime]::UtcNow.ToString('o');$receipt|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $ValidationRoot 'mouse-clickthrough-verification.json') -Encoding UTF8}
