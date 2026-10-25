param([string]$CandidateDll,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $CandidateDll){$CandidateDll=Join-Path $projectRoot 'PowerControl\fan-host\YeManFanHost.dll'}
if(-not $EvidenceRoot){throw 'Explicit isolated EvidenceRoot required'}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$candidate=[IO.Path]::GetFullPath($CandidateDll)
$source=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_wake_intent_gate_selftest.cs'))
$project=Join-Path $EvidenceRoot 'WakeIntentGate.csproj'
[IO.File]::WriteAllText($project,('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$source+'"/></ItemGroup></Project>'),[Text.UTF8Encoding]::new($false))
& dotnet run --project $project -c Release -- $candidate (Join-Path $EvidenceRoot 'isolated-logs') *> (Join-Path $EvidenceRoot 'checks.txt')
$code=$LASTEXITCODE
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt')
[IO.File]::WriteAllText((Join-Path $EvidenceRoot 'checks.exit'),[string]$code)
exit $code
