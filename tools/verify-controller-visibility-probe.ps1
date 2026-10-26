[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot,[switch]$Smoke)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
# Freeze the repository include closure, verifier and reused SDK calibration.
$relativeInputs=@(
    'tools/controller_visibility_probe.cpp',
    'native/controller_device_stack_evidence.h',
    'native/process_efficiency.h',
    'native/msi_controller_visibility_diagnostics.h',
    'native/msi_external_raw_receipts.h',
    'native/msi_external_raw_receipts_windows.h',
    'native/msi_external_gamepad_decode_windows.h',
    'native/msi_usb_name_origin_context.h',
    'native/msi_usb_name_origin_decode.h',
    'native/msi_usb_name_origin_io.h',
    'native/msi_usb_name_origin_windows.h',
    'native/msi_usb_name_origin_join.h',
    'deps/json/json.hpp',
    'tools/verify-controller-visibility-probe.ps1',
    'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
)
$frozenRoot=Join-Path $OutputRoot 'source'
$inputHashes=@(foreach($relative in $relativeInputs){
    $live=Join-Path $root $relative
    $frozen=Join-Path $frozenRoot $relative
    $hash=(Get-FileHash -LiteralPath $live -Algorithm SHA256).Hash
    New-Item -ItemType Directory -Path (Split-Path -Parent $frozen) -Force|Out-Null
    Copy-Item -LiteralPath $live -Destination $frozen -Force
    if((Get-FileHash -LiteralPath $frozen -Algorithm SHA256).Hash -ne $hash){throw "Freeze changed: $relative"}
    [pscustomobject]@{Path=$live;FrozenPath=$frozen;RelativePath=$relative;Hash=$hash}
})
$inputHashes|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$OutputRoot/source-hashes.json" -Encoding UTF8
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
# Existing calibrationToolchainPreamble selects a complete installed SDK when
# vcvars points at an old/uninstalled version. Environment is process-local.
$generator='import {pathToFileURL} from ''node:url''; const {calibrationToolchainPreamble}=await import(pathToFileURL(process.argv[1]).href); process.stdout.write(calibrationToolchainPreamble(process.argv[2]));'
$node=(Get-Command node.exe).Source
$preamble=& $node --input-type=module -e $generator (Join-Path $frozenRoot 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs') $vc
if($LASTEXITCODE -ne 0){throw 'Installed SDK calibration failed'}
$envBatch=Join-Path $OutputRoot 'toolchain-env.cmd'
($preamble -join "`r`n")+"`r`nset`r`n"|Set-Content -LiteralPath $envBatch -Encoding ASCII
$lines=& $env:ComSpec /d /s /c "`"$envBatch`""
if($LASTEXITCODE -ne 0){throw 'vcvars/calibrated SDK failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$version=$env:WindowsSDKVersion.TrimEnd('\','/')
$winrt=Join-Path $env:WindowsSdkDir "Include\$version\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$version\um\x64\WindowsApp.lib"
$source=Join-Path $frozenRoot 'tools/controller_visibility_probe.cpp'
$before=($inputHashes|Where-Object RelativePath -eq 'tools/controller_visibility_probe.cpp').Hash
$compiler=(Get-Command cl.exe).Source
$linker=(Get-Command link.exe).Source
$tools=@($compiler,$linker,$node,$vc,$winapp)|ForEach-Object{Get-FileHash -LiteralPath $_ -Algorithm SHA256|Select-Object Path,Hash}
$compileStarted=Get-Date
& $compiler /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE "/I$frozenRoot\deps\json" "/I$winrt" /sourceDependencies "$OutputRoot/compile-dependencies.json" $source "/Fo$OutputRoot/probe.obj" "/Fe$OutputRoot/YeManControllerVisibilityProbe.exe" /link /SUBSYSTEM:CONSOLE $winapp user32.lib ole32.lib dinput8.lib dxguid.lib hid.lib setupapi.lib cfgmgr32.lib 2>&1|Tee-Object -FilePath "$OutputRoot/compile.log"
if($LASTEXITCODE -ne 0){throw 'Independent DirectInput/WGI observer compile failed'}
$compileMs=[math]::Round(((Get-Date)-$compileStarted).TotalMilliseconds)
# Compiler-discovered local dependencies must all be frozen and declared.
$dependencies=Get-Content -LiteralPath "$OutputRoot/compile-dependencies.json" -Raw|ConvertFrom-Json
$dependencyHashes=@(foreach($include in $dependencies.Data.Includes){
    $full=[IO.Path]::GetFullPath($include)
    if($full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw "Unfrozen repository include: $full"}
    if($full.StartsWith($frozenRoot+'\',[StringComparison]::OrdinalIgnoreCase) -and @($inputHashes|Where-Object{$_.FrozenPath -eq $full}).Count -ne 1){throw "Undeclared frozen include: $full"}
    Get-FileHash -LiteralPath $full -Algorithm SHA256|Select-Object Path,Hash
})
$dependencyHashes|ConvertTo-Json -Depth 4|Set-Content -LiteralPath "$OutputRoot/dependency-hashes.json" -Encoding UTF8
$selftestStarted=Get-Date
& "$OutputRoot/YeManControllerVisibilityProbe.exe" --selftest 2>&1|Tee-Object -FilePath "$OutputRoot/selftest.json"
if($LASTEXITCODE -ne 0){throw 'Synthetic observer state/gating/device stack selftest failed'}
$selftestMs=[math]::Round(((Get-Date)-$selftestStarted).TotalMilliseconds)
$selftest=Get-Content -LiteralPath "$OutputRoot/selftest.json" -Raw|ConvertFrom-Json
if(-not $selftest.existingSelftestPassed -or -not $selftest.deviceStackPassed){throw 'Incomplete synthetic selftest receipt'}
if($Smoke){
    & "$OutputRoot/YeManControllerVisibilityProbe.exe" "$OutputRoot/probe-smoke.jsonl" smoke 3
    if($LASTEXITCODE -ne 0){throw 'Observer smoke failed'}
    $rows=@(Get-Content -LiteralPath "$OutputRoot/probe-smoke.jsonl"|ForEach-Object{$_|ConvertFrom-Json})
    if($rows[0].event -ne 'start' -or $rows[-1].event -ne 'stop' -or @($rows|Where-Object{$_.event -eq 'wgi-inventory'}).Count -lt 2){throw 'Incomplete observer output'}
}
foreach($input in $inputHashes){
    foreach($path in @($input.Path,$input.FrozenPath)){
        if($input.Hash -ne (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash){throw "Observer input changed during verification: $path"}
    }
}
foreach($input in @($tools)+@($dependencyHashes)){
    if($input.Hash -ne (Get-FileHash -LiteralPath $input.Path -Algorithm SHA256).Hash){throw "Observer tool/dependency changed during verification: $($input.Path)"}
}
[pscustomobject]@{
    ok=$true;sourceSha256=$before;sourceHashes=$inputHashes;toolHashes=@($tools);sdkVersion=$version
    rawReceiptsSupported=$true;msiGamepadSemanticWitnessVersion=1;msiUsbNameOriginSupported=$true;msiUsbNameOriginVersion=1
    deviceStackEvidenceVersion=1;deviceStackCases=$selftest.deviceStackCases;deviceStackSelftestPassed=$selftest.deviceStackPassed
    compileMs=$compileMs;selftestMs=$selftestMs;selftestPerformsHardwareIO=$false
    binarySha256=(Get-FileHash -LiteralPath "$OutputRoot/YeManControllerVisibilityProbe.exe" -Algorithm SHA256).Hash
    smoke=[bool]$Smoke;targetMsiVerified=$false;writesHardware=$false;externalVisibilityProven=$false
    scope='Frozen independent observer compile and existing synthetic selftest; device stack decoder only; no real PnP capture, Steam/filter/hiding acceptance'
}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$OutputRoot/verification-summary.json" -Encoding UTF8
Write-Output "CONTROLLER_VISIBILITY_PROBE_OK $OutputRoot"
