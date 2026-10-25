[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$source=Join-Path $root 'native\main.cpp'
$test=Join-Path $PSScriptRoot 'msi_quarantine_diagnostic_io_selftest.cpp'
$slotTest=Join-Path $PSScriptRoot 'msi_slot_api_evidence_selftest.cpp'
$inputs=@($source,$test,$slotTest,(Join-Path $root 'native\msi_slot_api_evidence.h'),(Join-Path $root 'native\msi_quarantine_diagnostics.h'),$PSCommandPath)
$before=@($inputs|ForEach-Object{Get-FileHash -LiteralPath $_ -Algorithm SHA256|Select-Object Path,Hash})
$text=[IO.File]::ReadAllText($source)
$signature='__declspec(noinline) static json msiQuarantineSlotApiEvidence(DWORD connectedMask) {'
$start=$text.IndexOf($signature,[StringComparison]::Ordinal)
if($start -lt 0 -or $text.IndexOf($signature,$start+1,[StringComparison]::Ordinal) -ge 0){throw 'Expected one actual quarantine helper definition'}
$end=$text.IndexOf("`nstatic void msiExplicitRearmSettingChanged",$start,[StringComparison]::Ordinal)
if($end -lt 0){throw 'Cannot safely extract actual helper'}
$helper=$text.Substring($start,$end-$start).TrimEnd()
$include=Join-Path $OutputRoot 'msi_quarantine_actual_helper.inc'
[IO.File]::WriteAllText($include,$helper,(New-Object System.Text.UTF8Encoding($false)))
if([IO.File]::ReadAllText($include) -cne $helper){throw 'Actual-helper extraction did not round trip'}
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE -ne 0){throw 'vcvars64 failed'}
foreach($line in $lines){if($line-match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
# Pure callback contract first, then both exact actual helper bodies under five profiles.
& cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 $slotTest "/I$root\deps\json" "/I$root\native" "/Fo$OutputRoot\slot-api-callback.obj" "/Fe$OutputRoot\slot-api-callback.exe" *> "$OutputRoot\slot-api-callback-build.log"
if($LASTEXITCODE -ne 0){throw 'Pure slot API callback compile failed'}
& "$OutputRoot\slot-api-callback.exe" "$OutputRoot\slot-api-callback.json" *> "$OutputRoot\slot-api-callback-run.log"
if($LASTEXITCODE -ne 0){throw 'Pure slot API callback contract failed'}
$slotReceipt=Get-Content -LiteralPath "$OutputRoot\slot-api-callback.json" -Raw|ConvertFrom-Json
if(-not $slotReceipt.passed -or -not $slotReceipt.allPlatformApisStubbed -or $slotReceipt.realHardwareAccess){throw 'Pure callback evidence invalid'}
$results=@()
foreach($profile in @('fault-on','fault-default','foreground-test-on','nonfault-on','nonfault-default')){
    $defs=@()
    if($profile.EndsWith('-on')){$defs+='/DYMCC_MSI_QUARANTINE_DIAGNOSTIC'}
    if($profile.StartsWith('fault-')){$defs+='/DYMCC_FAULT_INJECT'}
    if($profile-eq 'foreground-test-on'){$defs+='/DYMCC_MSI_FOREGROUND_ISOLATION_TEST'}
    $exe=Join-Path $OutputRoot ($profile+'.exe')
    $json=Join-Path $OutputRoot ($profile+'.json')
    & cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /O2 $test "/I$root\deps\json" "/I$root\native" "/I$OutputRoot" @defs "/Fo$OutputRoot\$profile.obj" "/Fe$exe" *> "$OutputRoot\$profile-build.log"
    if($LASTEXITCODE -ne 0){throw "Stub-only quarantine I/O compile failed: $profile"}
    & $exe $json *> "$OutputRoot\$profile-run.log"
    if($LASTEXITCODE -ne 0){throw "Quarantine I/O boundary is not sealed: $profile; inspect $json before any native-entry run"}
    $receipt=Get-Content -LiteralPath $json -Raw|ConvertFrom-Json
    if(-not $receipt.passed -or -not $receipt.copiedActualSlotApiHelperBody -or -not $receipt.allPlatformApisStubbed -or $receipt.realHardwareAccess){throw 'Selftest I/O claims invalid'}
    $results+=@{profile=$profile;exitCode=0;result=$json;cases=@($receipt.cases).Count}
}
$after=@($inputs|ForEach-Object{Get-FileHash -LiteralPath $_ -Algorithm SHA256|Select-Object Path,Hash})
for($i=0;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw "Source changed during I/O sealing test: $($before[$i].Path)"}}
@{sourceHashes=$after;actualHelperSha256=(Get-FileHash -LiteralPath $include -Algorithm SHA256).Hash;exactActualHelper=$true;exactActualSlotApiHelper=$true;callbackCaseCount=$slotReceipt.caseCount;profiles=$results;allPlatformApisStubbed=$true;hardwareVerified=$false;scope='Exact current receipt plus slot API helper tested with wholly fake resolver/module/mapping APIs; zero slots do not resolve; missing/throwing offline hook cannot fall through; returned/knownness and bounded no-retry contract verified, not real MSI validation'}|ConvertTo-Json -Depth 6|Set-Content -LiteralPath "$OutputRoot\verification-summary.json" -Encoding UTF8
Write-Output "MSI_QUARANTINE_IO_SEALED $OutputRoot"
