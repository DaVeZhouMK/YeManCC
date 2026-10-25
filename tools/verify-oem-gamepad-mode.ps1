[CmdletBinding()]
param([string]$OutputRoot)
$ErrorActionPreference='Stop'
$root=Split-Path -Parent $PSScriptRoot
if([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $workspace=Split-Path (Split-Path (Split-Path $root -Parent) -Parent) -Parent
    $OutputRoot=Join-Path $workspace 'Build\Validation\OEM-Gamepad-Mode-20261012'
}
$OutputRoot=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot|Out-Null
$files=@('native\oem_gamepad_mode_contract.h','native\oem_gamepad_mode_session.h',
    'native\oem_gamepad_mode_journal.h','native\msi_oem_gamepad_mode_adapter.h',
    'native\oem_input_contract.h','native\oem_input_contract_evidence.h','native\oem_input_session.h',
    'native\oem_frontend_policy_contract.h',
    'tools\oem_gamepad_mode_selftest.cpp','tools\verify-oem-gamepad-mode.ps1')
function SourceHashes {
    $hashes=[ordered]@{}
    foreach($f in $files){$path=Join-Path $root $f;if(!(Test-Path -LiteralPath $path)){throw "missing $f"};$hashes[$f]=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash}
    return $hashes
}
$before=SourceHashes
$vc='C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
$envLines=& $env:ComSpec /d /s /c "`"$vc`" >nul && set"
if($LASTEXITCODE){throw 'vcvars64 failed'}
foreach($line in $envLines){if($line -match '^([^=]+)=(.*)$'){[Environment]::SetEnvironmentVariable($matches[1],$matches[2],'Process')}}
$inc=Join-Path $root 'deps\json'
$cpp=Join-Path $root 'tools\oem_gamepad_mode_selftest.cpp'
$results=@()
foreach($kind in @('release','debug')){
    $obj=Join-Path $OutputRoot "$kind-selftest.obj"
    $exe=Join-Path $OutputRoot "$kind-selftest.exe"
    $opts=@('/nologo','/std:c++20','/utf-8','/EHsc','/MT','/W4','/WX','/DNOMINMAX',"/I$inc",$cpp,"/Fo$obj","/Fe$exe")
    if($kind -eq 'release'){$opts+='/O2'}else{$opts+=@('/Od','/RTC1')}
    $compile=@(& cl.exe @opts /link /SUBSYSTEM:CONSOLE 2>&1)
    $code=$LASTEXITCODE;$compile|Set-Content -LiteralPath (Join-Path $OutputRoot "$kind-compile.txt") -Encoding utf8
    if($code){$compile|Write-Output;throw "$kind compile failed: $code"}
    $out=@(& $exe);$code=$LASTEXITCODE
    $out|Tee-Object -FilePath (Join-Path $OutputRoot "$kind-selftest.txt")
    if($code){throw "$kind selftest failed: $code"}
    if($out.Count -ne 1 -or $out[0] -notmatch '^OEM_GAMEPAD_MODE_SELFTEST_OK cases=(\d+) registry=fake controller_io=none runtime_graft=none$'){throw 'selftest receipt missing'}
    $results+= [pscustomobject]@{kind=$kind;cases=[int]$matches[1];exeSha256=(Get-FileHash -LiteralPath $exe).Hash;result=$out[0]}
}
# Compile each public header as the sole include, to catch hidden order dependencies.
$headerCount=0
foreach($f in $files|Where-Object {$_ -like '*.h'}) {
    $header=Join-Path $root $f
    $stem=[IO.Path]::GetFileNameWithoutExtension($header)
    $smoke=Join-Path $OutputRoot "$stem-smoke.cpp"
    $line='#include "'+$header.Replace('\','/')+'"'
    Set-Content -LiteralPath $smoke -Value $line -Encoding utf8
    $compile=@(& cl.exe /nologo /std:c++20 /utf-8 /EHsc /MT /W4 /WX /DNOMINMAX "/I$inc" /c $smoke "/Fo$OutputRoot\$stem-smoke.obj" 2>&1)
    $code=$LASTEXITCODE;$compile|Set-Content -LiteralPath (Join-Path $OutputRoot "$stem-smoke.txt") -Encoding utf8
    if($code){$compile|Write-Output;throw "standalone header failed: $f"}
    ++$headerCount
}
$after=SourceHashes
foreach($key in $before.Keys){if($before[$key] -ne $after[$key]){throw "source changed during verification: $key"}}
[pscustomobject]@{schema=1;completedAt=(Get-Date -Format o);scope='standalone mechanism; fake registry/journal/authority/submit proofs only';
    hardwareAccess=$false;registryAccess=$false;runtimeGraft=$false;productBuildVerified=$false;frontendAdaptersEnabled=$false;
    tests=$results;standaloneHeadersCompiled=$headerCount;files=$after}|
    ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $OutputRoot 'verification.json') -Encoding utf8


