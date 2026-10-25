[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$fanProject=Split-Path -Parent $PSScriptRoot
$fanSourcePath=Join-Path $fanProject 'native\main.cpp'
$fanSourceHash=(Get-FileHash -LiteralPath $fanSourcePath -Algorithm SHA256).Hash.ToLowerInvariant()
$fanHeaderPath=Join-Path $fanProject 'native\http_request_deadline.h'
$fanHeaderHash=(Get-FileHash -LiteralPath $fanHeaderPath -Algorithm SHA256).Hash.ToLowerInvariant()
$fanRaw=[IO.File]::ReadAllText($fanSourcePath)
$fanStart=$fanRaw.IndexOf('static bool fanLeaseKeepalivePostHeartbeat(',[StringComparison]::Ordinal)
$fanEnd=$fanRaw.IndexOf("`nstatic void fanLeaseKeepaliveTick()",$fanStart,[StringComparison]::Ordinal)
if($fanStart -lt 0 -or $fanEnd -le $fanStart){throw 'Fan heartbeat source boundary missing'}
$fanExtract=$fanRaw.Substring($fanStart,$fanEnd-$fanStart)
$fanPortCall='WinHttpConnect(session, L"127.0.0.1", 8765, 0)'
if($fanExtract.Split($fanPortCall).Count -ne 2){throw 'Fan heartbeat fixture expects one production loopback port'}
$fanFixture=$fanExtract.Replace($fanPortCall,'WinHttpConnect(session, L"127.0.0.1", fixturePort, 0)')
$fanEvidence=[IO.Path]::GetFullPath($EvidenceRoot)
$fanControl=Join-Path $fanEvidence 'CONTROL'
New-Item -ItemType Directory -Path $fanControl -Force | Out-Null
[IO.File]::WriteAllText((Join-Path $fanControl 'KeepaliveExtract.inc'),$fanFixture,[Text.UTF8Encoding]::new($false))
$fanExtractHash=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($fanExtract))).ToLowerInvariant()
@{
    sourceSha256=$fanSourceHash; functionSha256=$fanExtractHash; transportHeaderSha256=$fanHeaderHash
    replacement='loopback port only; process/credential/display/native mode hooks in C++ fixture'
    scope='real WinHTTP/ephemeral loopback fixture; no FanHost, HC, device writes, installation or CPU benchmark'
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $fanEvidence 'source-binding.json') -Encoding utf8
$fanBuildFile=Join-Path $fanEvidence 'compile.cmd'
@"
@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
cl /nologo /W4 /WX /std:c++20 /EHsc /utf-8 /MT /I"$fanProject\native" /I"$fanControl" "$PSScriptRoot\fan_keepalive_transport_selftest.cpp" /Fo"$fanControl\fixture.obj" /Fe"$fanControl\fixture.exe"
"@ | Set-Content -LiteralPath $fanBuildFile -Encoding ascii
& $fanBuildFile *> (Join-Path $fanEvidence 'compile.out')
if($LASTEXITCODE -ne 0){throw ('Fan keepalive fixture compile failed; see '+(Join-Path $fanEvidence 'compile.out'))}
& node (Join-Path $PSScriptRoot 'fan_keepalive_transport_selftest.mjs') $fanEvidence 'CONTROL' *> (Join-Path $fanEvidence 'CONTROL.out')
if($LASTEXITCODE -ne 0){throw ('Fan keepalive transport gate failed; see '+(Join-Path $fanEvidence 'CONTROL.out'))}
$fanResult=Get-Content -LiteralPath (Join-Path $fanEvidence 'CONTROL.json') -Raw | ConvertFrom-Json
if(-not $fanResult.ok -or $fanResult.cases -ne 13){throw 'Fan keepalive transport gate is incomplete'}
if((Get-FileHash -LiteralPath $fanSourcePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fanSourceHash -or (Get-FileHash -LiteralPath $fanHeaderPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $fanHeaderHash){throw 'Source/transport changed while validating; result needs rebinding'}
Write-Output 'Fan keepalive transport: PASS (13 cases; real WinHTTP + fixture port/identity/display; no FanHost or hardware)'
