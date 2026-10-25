param(
    [string]$EvidenceRoot,
    [string]$CandidateDll
)
$ErrorActionPreference = 'Stop'
$fanRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if ([string]::IsNullOrWhiteSpace($EvidenceRoot)) {
    $fanWorkspace = [IO.Path]::GetFullPath((Join-Path $fanRoot '..\..\..'))
    $EvidenceRoot = Join-Path $fanWorkspace ('_scratch\fan-request-json-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
}
$fanEvidence = [IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force | Out-Null
$fanCompiler = (Get-Command dotnet -ErrorAction Stop).Source
$fanSource = Join-Path $fanRoot 'FanLab\real-host\Program.cs'
$fanBefore = (Get-FileHash -LiteralPath $fanSource -Algorithm SHA256).Hash
if ([string]::IsNullOrWhiteSpace($CandidateDll)) {
    & $fanCompiler build (Join-Path $fanRoot 'FanLab\real-host\YeManFanHost.csproj') -c Release --artifacts-path (Join-Path $fanEvidence 'host-artifacts') --disable-build-servers *> (Join-Path $fanEvidence 'host-build.out')
    if ($LASTEXITCODE -ne 0) { throw ('Host build failed; see ' + (Join-Path $fanEvidence 'host-build.out')) }
    $CandidateDll = Join-Path $fanEvidence 'host-artifacts\bin\YeManFanHost\release_win-x64\YeManFanHost.dll'
}
$fanPayload = [IO.Path]::GetFullPath($CandidateDll)
if (-not (Test-Path -LiteralPath $fanPayload -PathType Leaf)) { throw 'Candidate DLL does not exist' }
$probeProject = Join-Path $fanEvidence 'RequestJsonChecks.csproj'
$probeSource = [System.Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_request_json_selftest.cs'))
$probeXml = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="' + $probeSource + '"/></ItemGroup></Project>'
[IO.File]::WriteAllText($probeProject, $probeXml, [Text.UTF8Encoding]::new($false))
& $fanCompiler build $probeProject -c Release --artifacts-path (Join-Path $fanEvidence 'probe-artifacts') --disable-build-servers *> (Join-Path $fanEvidence 'probe-build.out')
if ($LASTEXITCODE -ne 0) { throw ('Probe build failed; see ' + (Join-Path $fanEvidence 'probe-build.out')) }
$probeAssembly = Join-Path $fanEvidence 'probe-artifacts\bin\RequestJsonChecks\release\RequestJsonChecks.dll'
& $fanCompiler $probeAssembly $fanPayload *> (Join-Path $fanEvidence 'request-json.out')
$probeExit = $LASTEXITCODE
$probeLines = Get-Content -LiteralPath (Join-Path $fanEvidence 'request-json.out') -Encoding UTF8
$probeRecord = $probeLines | Where-Object { $_.StartsWith('{"ok"') } | Select-Object -Last 1
if (-not $probeRecord) { throw 'Probe emitted no final result' }
$probeResult = $probeRecord | ConvertFrom-Json
if ($probeExit -ne 0 -or -not $probeResult.ok -or $probeResult.hardwareCalls -ne 0) { throw ('Request JSON gate failed; see ' + (Join-Path $fanEvidence 'request-json.out')) }
$fanAfter = (Get-FileHash -LiteralPath $fanSource -Algorithm SHA256).Hash
if ($fanBefore -ne $fanAfter) { throw 'Host source changed during verification; do not bind this result to the changed source' }
[pscustomobject]@{ok=$true;checks=$probeResult.checks;hostDllSha256=$probeResult.hostDllSha256;programSha256=$fanAfter;hardwareCalls=0;candidateBuiltByThisRun=[string]::IsNullOrWhiteSpace($PSBoundParameters['CandidateDll']);evidenceRoot=$fanEvidence} | ConvertTo-Json -Compress
