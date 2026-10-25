param([string]$CandidateDll,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $CandidateDll){$CandidateDll=Join-Path $projectRoot 'PowerControl\fan-host\YeManFanHost.dll'}
if(-not $EvidenceRoot){throw 'Explicit isolated EvidenceRoot required'}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$candidate=[IO.Path]::GetFullPath($CandidateDll)
$source=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_http_request_isolation_selftest.cs'))
$project=Join-Path $EvidenceRoot 'HttpIsolation.csproj'
[IO.File]::WriteAllText($project,('<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$source+'"/></ItemGroup></Project>'),[Text.UTF8Encoding]::new($false))
$before=(Get-FileHash -LiteralPath (Join-Path $projectRoot 'FanLab/real-host/Program.cs')).Hash
& dotnet run --project $project -c Release -- $candidate (Join-Path $EvidenceRoot 'isolated-logs') *> (Join-Path $EvidenceRoot 'checks.txt')
$code=$LASTEXITCODE
$line=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt')|Where-Object{$_.StartsWith('{"ok"')}|Select-Object -Last 1
if(-not $line){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt') -Tail 10;throw 'HTTP isolation test emitted no result'}
$result=$line|ConvertFrom-Json
[IO.File]::WriteAllText((Join-Path $EvidenceRoot 'checks.exit'),[string]$code)
[IO.File]::WriteAllText((Join-Path $EvidenceRoot 'result.json'),$line,[Text.UTF8Encoding]::new($false))
if($before-ne(Get-FileHash -LiteralPath (Join-Path $projectRoot 'FanLab/real-host/Program.cs')).Hash){throw 'Fan source changed during HTTP isolation check'}
if($code-ne0-or-not$result.ok-or$result.hardwareCalls-ne0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt') -Tail 15;throw 'HTTP request isolation behavior failed'}
[pscustomobject]@{ok=$true;checks=$result.checks;unobservedTasks=$result.unobservedTasks.Count;hostDllSha256=$result.hostDllSha256;hardwareCalls=0;scope=$result.scope}|ConvertTo-Json -Compress
