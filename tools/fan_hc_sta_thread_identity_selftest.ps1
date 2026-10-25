param([string]$CandidateDll,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $CandidateDll){$CandidateDll=Join-Path $projectRoot 'PowerControl\fan-host\YeManFanHost.dll'}
if(-not $EvidenceRoot){$workspace=[IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\..'));$EvidenceRoot=Join-Path $workspace ('_scratch\fan-hc-sta-thread-identity-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
if(Test-Path -LiteralPath $EvidenceRoot){throw 'Use a new evidence directory'}
New-Item -ItemType Directory -Path $EvidenceRoot|Out-Null
$candidate=[IO.Path]::GetFullPath($CandidateDll)
if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){throw 'Candidate Host DLL missing'}
$hostSource=Join-Path $projectRoot 'FanLab\real-host\Program.cs'
$before=(Get-FileHash -LiteralPath $hostSource).Hash
$checkSource=Join-Path $PSScriptRoot 'fan_hc_sta_thread_identity_selftest.cs'
$xmlPath=[Security.SecurityElement]::Escape($checkSource)
$project=Join-Path $EvidenceRoot 'StaThreadIdentity.csproj'
$xml='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup><ItemGroup><Compile Include="'+$xmlPath+'"/></ItemGroup></Project>'
[IO.File]::WriteAllText($project,$xml,[Text.UTF8Encoding]::new($false))
& dotnet run --project $project -c Release -- $candidate (Join-Path $EvidenceRoot 'logs') *> (Join-Path $EvidenceRoot 'checks.log')
$code=$LASTEXITCODE
$line=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.log')|Where-Object{$_.StartsWith('{"ok"')}|Select-Object -Last 1
if(-not $line){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.log');throw 'No STA identity result'}
$result=$line|ConvertFrom-Json
$after=(Get-FileHash -LiteralPath $hostSource).Hash
if($before-ne$after){throw 'Product source changed during thread identity checks'}
$record=[ordered]@{ok=($code-eq0-and$result.ok);checks=$result.checks;result=$result;hostDllSha256=(Get-FileHash -LiteralPath $candidate).Hash.ToLowerInvariant();programSha256=$after;testSourceSha256=(Get-FileHash -LiteralPath $checkSource).Hash;hardwareCalls=0;cpuBenchmark=$false}
$record|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $EvidenceRoot 'result.json') -Encoding UTF8
Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.log')
if(-not $record.ok){throw 'STA identity behavior failed'}