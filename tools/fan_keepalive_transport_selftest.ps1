[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$NativeSource,[switch]$SharedPowerOnly)
$ErrorActionPreference='Stop'
$fanProject=Split-Path -Parent $PSScriptRoot
$fanSourcePath=if($NativeSource){[IO.Path]::GetFullPath($NativeSource)}else{Join-Path $fanProject 'native\main.cpp'}
$fanSourceHash=(Get-FileHash -LiteralPath $fanSourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
$fanHeaderPath=Join-Path $fanProject 'native\http_request_deadline.h'
$fanHeaderHash=(Get-FileHash -LiteralPath $fanHeaderPath -Algorithm SHA256).Hash.ToLowerInvariant()
$fanAdditionalHeaders=@{}
foreach($name in @('fan_keepalive_transport.h','fan_response_contract.h','sleep_power_state.h','sleep_power_observation.h','sleep_power_revocation.h')){$fanAdditionalHeaders[$name]=(Get-FileHash -LiteralPath (Join-Path $fanProject ('native/'+$name))).Hash.ToLowerInvariant()}
$fanRaw=[IO.File]::ReadAllText($fanSourcePath)
$fanStart=$fanRaw.IndexOf('static bool fanLeaseKeepalivePostHeartbeat(',[StringComparison]::Ordinal)
$fanEnd=$fanRaw.IndexOf("`nstatic void fanLeaseKeepaliveTick()",$fanStart,[StringComparison]::Ordinal)
if($fanStart -lt 0 -or $fanEnd -le $fanStart){throw 'Fan heartbeat source boundary missing'}
$fanExtract=$fanRaw.Substring($fanStart,$fanEnd-$fanStart)
$fanPortCall='connectionOwner.connect(8765, fanKeepaliveProcessSelection().revision())'
if($fanExtract.Split($fanPortCall).Count -ne 2){throw 'Fan heartbeat fixture expects one production loopback port'}
$fanFixture=$fanExtract.Replace($fanPortCall,'connectionOwner.connect(fixturePort, fanKeepaliveProcessSelection().revision())')
if($fanRaw.Contains('struct FanHttpPowerAdmission {')){
 $a=$fanRaw.IndexOf('struct FanHttpPowerAdmission {',[StringComparison]::Ordinal);$b=$fanRaw.IndexOf('// This is the original Fan operation/lease lifetime',$a,[StringComparison]::Ordinal)
 if($a -lt 0 -or $b -le $a){throw 'Shared Fan admission source missing'}
 $fanAdmission=$fanRaw.Substring($a,$b-$a)
}
$fanEvidence=[IO.Path]::GetFullPath($EvidenceRoot)
$fanControl=Join-Path $fanEvidence 'CONTROL'
New-Item -ItemType Directory -Path $fanControl -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fanControl 'KeepaliveAdmissionExtract.inc'),$fanAdmission,[Text.UTF8Encoding]::new($false))
[IO.File]::WriteAllText((Join-Path $fanControl 'KeepaliveExtract.inc'),$fanFixture,[Text.UTF8Encoding]::new($false))
$fanExtractHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanExtract))).ToLowerInvariant()
@{
    sourceSha256=$fanSourceHash; functionSha256=$fanExtractHash; transportHeaderSha256=$fanHeaderHash; additionalHeaders=$fanAdditionalHeaders
    replacement='loopback port only; local FixtureWriteGrant substitutes the remote envelope and peer, retaining actual public observation and revocation'
    scope='real WinHTTP/ephemeral loopback fixture; credential, peer, display and mode hooks inert; no FanHost, HC, device writes, installation or CPU benchmark'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fanEvidence 'source-binding.json') -Encoding utf8
$fanBuildFile=Join-Path $fanEvidence 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
if not defined WindowsSdkDir set "WindowsSdkDir=C:\Program Files (x86)\Windows Kits\10\"
if "%WindowsSDKVersion%"=="\" set "WindowsSDKVersion=10.0.26100.0\"
if not defined WindowsSDKVersion set "WindowsSDKVersion=10.0.26100.0\"
set "INCLUDE=%WindowsSdkDir%Include\%WindowsSDKVersion%um;%WindowsSdkDir%Include\%WindowsSDKVersion%shared;%WindowsSdkDir%Include\%WindowsSDKVersion%ucrt;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%WindowsSDKVersion%um\x64;%WindowsSdkDir%Lib\%WindowsSDKVersion%ucrt\x64;%LIB%"
cl /nologo /W4 /WX /std:c++20 /EHsc /utf-8 /MT /I"$fanProject\native" /I"$fanControl" "$PSScriptRoot\fan_keepalive_transport_selftest.cpp" /Fo"$fanControl\fixture.obj" /Fe"$fanControl\fixture.exe"
"@ | Set-Content -LiteralPath $fanBuildFile -Encoding ascii
& $fanBuildFile *> (Join-Path $fanEvidence 'compile.out')
if($LASTEXITCODE -ne 0){throw ('Fan keepalive fixture compile failed; see '+(Join-Path $fanEvidence 'compile.out'))}
& node (Join-Path $PSScriptRoot 'fan_keepalive_transport_selftest.mjs') $fanEvidence 'CONTROL' @($(if($SharedPowerOnly){'--shared-power-only'})) *> (Join-Path $fanEvidence 'CONTROL.out')
if($LASTEXITCODE -ne 0){throw ('Fan keepalive transport gate failed; see '+(Join-Path $fanEvidence 'CONTROL.out'))}
$fanResult=Get-Content -LiteralPath (Join-Path $fanEvidence 'CONTROL.json') -Raw | ConvertFrom-Json
$expectedCases=if($SharedPowerOnly){6}else{25}
if(-not $fanResult.ok -or $fanResult.cases -ne $expectedCases){throw 'Fan keepalive transport gate is incomplete'}
if((Get-FileHash -LiteralPath $fanSourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fanSourceHash -or (Get-FileHash -LiteralPath $fanHeaderPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fanHeaderHash){throw 'Source/transport changed while validating; result needs rebinding'}
foreach($name in $fanAdditionalHeaders.Keys){if((Get-FileHash -LiteralPath (Join-Path $fanProject ('native/'+$name))).Hash.ToLowerInvariant() -ne $fanAdditionalHeaders[$name]){throw ('Fan additional transport input changed: '+$name)}}
Write-Output ('Fan keepalive transport: PASS ('+$expectedCases+' cases; real WinHTTP + fixture port/identity/display; no FanHost or hardware)')
