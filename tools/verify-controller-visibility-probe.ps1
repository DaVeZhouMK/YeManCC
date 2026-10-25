[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot,[switch]$Smoke)
$ErrorActionPreference='Stop';$root=Split-Path -Parent $PSScriptRoot
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot);New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set";if($LASTEXITCODE -ne 0){throw 'vcvars failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$winrt=Join-Path $env:WindowsSdkDir "Include\$($env:WindowsSDKVersion.TrimEnd('\'))\cppwinrt"
$winapp=Join-Path $env:WindowsSdkDir "Lib\$($env:WindowsSDKVersion.TrimEnd('\'))\um\x64\WindowsApp.lib"
$source="$root\tools\controller_visibility_probe.cpp";$before=(Get-FileHash -LiteralPath $source).Hash
$inputs=@($source,"$root\native\msi_controller_visibility_diagnostics.h","$root\native\msi_external_raw_receipts.h","$root\native\msi_external_raw_receipts_windows.h","$root\native\msi_external_gamepad_decode_windows.h","$root\native\msi_usb_name_origin_context.h","$root\native\msi_usb_name_origin_decode.h","$root\native\msi_usb_name_origin_io.h","$root\native\msi_usb_name_origin_windows.h","$root\native\msi_usb_name_origin_join.h")
$inputHashes=@($inputs|ForEach-Object {Get-FileHash -LiteralPath $_|Select-Object Path,Hash})
& cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 /DNDEBUG /DUNICODE /D_UNICODE "/I$root\deps\json" "/I$winrt" $source "/Fo$OutputRoot\probe.obj" "/Fe$OutputRoot\YeManControllerVisibilityProbe.exe" /link /SUBSYSTEM:CONSOLE $winapp user32.lib ole32.lib dinput8.lib dxguid.lib hid.lib setupapi.lib cfgmgr32.lib
if($LASTEXITCODE -ne 0){throw 'Independent DirectInput/WGI observer compile failed'}
& "$OutputRoot\YeManControllerVisibilityProbe.exe" --selftest
if($LASTEXITCODE -ne 0){throw 'Synthetic observer state/gating selftest failed'}
if($Smoke){& "$OutputRoot\YeManControllerVisibilityProbe.exe" "$OutputRoot\probe-smoke.jsonl" smoke 3;if($LASTEXITCODE -ne 0){throw 'Observer smoke failed'};$rows=@(Get-Content -LiteralPath "$OutputRoot\probe-smoke.jsonl"|ForEach-Object{$_|ConvertFrom-Json});if($rows[0].event -ne 'start' -or $rows[-1].event -ne 'stop' -or @($rows|Where-Object{$_.event -eq 'wgi-inventory'}).Count -lt 2){throw 'Incomplete observer output'}}
if($before -ne (Get-FileHash -LiteralPath $source).Hash){throw 'Observer source changed during verification'}
foreach($input in $inputHashes){if($input.Hash -ne (Get-FileHash -LiteralPath $input.Path).Hash){throw ('Observer input changed during verification: '+$input.Path)}}
[pscustomobject]@{ok=$true;sourceSha256=$before;sourceHashes=$inputHashes;rawReceiptsSupported=$true;msiGamepadSemanticWitnessVersion=1;msiUsbNameOriginSupported=$true;msiUsbNameOriginVersion=1;selftestPerformsHardwareIO=$false;binarySha256=(Get-FileHash -LiteralPath "$OutputRoot\YeManControllerVisibilityProbe.exe").Hash;smoke=[bool]$Smoke;targetMsiVerified=$false;writesHardware=$false;externalVisibilityProven=$false;scope='Independent observer compile and synthetic selftest; optional RawInput counts require explicit observation; no Steam/filter/dimming acceptance'}|ConvertTo-Json|Set-Content -LiteralPath "$OutputRoot\verification-summary.json" -Encoding UTF8
Write-Output "CONTROLLER_VISIBILITY_PROBE_OK $OutputRoot"
