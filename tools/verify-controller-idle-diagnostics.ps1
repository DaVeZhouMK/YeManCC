[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$OutputRoot,[switch]$Smoke)
$ErrorActionPreference='Stop';$root=Split-Path -Parent $PSScriptRoot
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot);New-Item -ItemType Directory -Path $OutputRoot -Force|Out-Null
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$lines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set";if($LASTEXITCODE -ne 0){throw 'vcvars failed'}
foreach($line in $lines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$files=@('native\input_idle_diagnostics.h','native\input_idle_windows.h','native\input_idle_windows_json.h','tools\controller_idle_core_selftest.cpp','tools\controller_idle_probe.cpp')
$before=@($files|ForEach-Object{Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256|Select-Object Path,Hash})
$common=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/O2','/DNDEBUG',"/I$root\deps\json")
& cl.exe @common "$root\tools\controller_idle_core_selftest.cpp" ("/Fo$OutputRoot\core.obj") ("/Fe$OutputRoot\controller-idle-core.exe") /link /SUBSYSTEM:CONSOLE user32.lib PowrProf.lib
if($LASTEXITCODE -ne 0){throw 'Idle core compile failed'}
& "$OutputRoot\controller-idle-core.exe"|Set-Content -LiteralPath "$OutputRoot\core-result.txt" -Encoding UTF8
if($LASTEXITCODE -ne 0){throw 'Idle core test failed'}
& cl.exe @common "$root\tools\controller_idle_probe.cpp" ("/Fo$OutputRoot\probe.obj") ("/Fe$OutputRoot\YeManControllerIdleProbe.exe") /link /SUBSYSTEM:CONSOLE user32.lib PowrProf.lib
if($LASTEXITCODE -ne 0){throw 'Read-only external observer compile failed'}
if($Smoke){
    & "$OutputRoot\YeManControllerIdleProbe.exe" "$OutputRoot\probe-smoke.jsonl" smoke 4
    if($LASTEXITCODE -ne 0){throw 'Read-only external observer smoke failed'}
    $rows=@(Get-Content -LiteralPath "$OutputRoot\probe-smoke.jsonl"|ForEach-Object{$_|ConvertFrom-Json})
    if($rows[0].event -ne 'start' -or $rows[-1].event -ne 'stop' -or @($rows|Where-Object {$_.event -eq 'sample'}).Count -lt 2){throw 'Incomplete probe lifecycle output'}
}
$after=@($files|ForEach-Object{Get-FileHash -LiteralPath (Join-Path $root $_) -Algorithm SHA256|Select-Object Path,Hash})
for($i=0;$i -lt $before.Count;$i++){if($before[$i].Hash -ne $after[$i].Hash){throw 'Source changed during verification'}}
[pscustomobject]@{ok=$true;sourceHashes=$after;coreResult=(Get-Content "$OutputRoot\core-result.txt" -Tail 1);readOnlyProbeCompiled=$true;probeSmoke=[bool]$Smoke;physicalMsiVerified=$false;writesHardware=$false;scope='Core behavior plus standalone read-only observer; local development machine only'}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath "$OutputRoot\verification-summary.json" -Encoding UTF8
Write-Output "CONTROLLER_IDLE_DIAGNOSTICS_OK $OutputRoot"
