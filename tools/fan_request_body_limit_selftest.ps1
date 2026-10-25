[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceRoot,[string]$CandidateDll)
$ErrorActionPreference='Stop'
$fanRoot=Split-Path -Parent $PSScriptRoot
$fanEvidence=[IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force|Out-Null
if(-not $CandidateDll){$CandidateDll=Join-Path $fanRoot 'PowerControl\fan-host\YeManFanHost.dll'}
$fanSource=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_request_body_limit_selftest.cs'))
$fanProj=Join-Path $fanEvidence 'BodyLimitChecks.csproj'
[IO.File]::WriteAllText($fanProj,'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$fanSource+'"/></ItemGroup></Project>',[Text.UTF8Encoding]::new($false))
& dotnet build $fanProj -c Release --artifacts-path (Join-Path $fanEvidence 'artifacts') --disable-build-servers *> (Join-Path $fanEvidence 'probe-build.txt')
if($LASTEXITCODE -ne 0){throw 'Body limit probe build failed'}
& dotnet (Join-Path $fanEvidence 'artifacts\bin\BodyLimitChecks\release\BodyLimitChecks.dll') $CandidateDll *> (Join-Path $fanEvidence 'checks.txt')
$fanExit=$LASTEXITCODE
$fanLines=Get-Content -LiteralPath (Join-Path $fanEvidence 'checks.txt')
$fanRecord=($fanLines|Where-Object{$_.StartsWith('{"ok"')}|Select-Object -Last 1)|ConvertFrom-Json
if($fanExit -ne 0 -or -not $fanRecord.ok){throw 'Actual Host body byte limit checks failed'}
if($fanRecord.hostDllSha256-ne (Get-FileHash -LiteralPath $CandidateDll).Hash.ToLowerInvariant()){throw 'Candidate DLL changed while probing'}
$fanRecord|ConvertTo-Json -Depth 4
