param([string]$CandidateDll,[string]$EvidenceRoot)
$ErrorActionPreference='Stop'
$projectRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $CandidateDll){$CandidateDll=Join-Path $projectRoot 'PowerControl\fan-host\YeManFanHost.dll'}
if(-not $EvidenceRoot){$workspace=[IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\..'));$EvidenceRoot=Join-Path $workspace ('_scratch\fan-runtime-maintenance-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}
New-Item -ItemType Directory -Force -Path $EvidenceRoot|Out-Null
$candidate=[IO.Path]::GetFullPath($CandidateDll)
if(-not(Test-Path -LiteralPath $candidate -PathType Leaf)){throw 'Candidate Host DLL missing'}
$hostSource=Join-Path $projectRoot 'FanLab\real-host\Program.cs'
$before=(Get-FileHash -LiteralPath $hostSource).Hash
$checkSource=Join-Path $PSScriptRoot 'fan_runtime_maintenance_isolation_selftest.cs'
$xmlPath=[Security.SecurityElement]::Escape($checkSource)
$project=Join-Path $EvidenceRoot 'RuntimeMaintenance.csproj'
$xml='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$xmlPath+'"/></ItemGroup></Project>'
[IO.File]::WriteAllText($project,$xml,[Text.UTF8Encoding]::new($false))
& dotnet run --project $project -c Release -- $candidate (Join-Path $EvidenceRoot "logs") *> (Join-Path $EvidenceRoot 'checks.txt')
$code=$LASTEXITCODE
$line=Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt')|Where-Object{$_.StartsWith('{"ok"')}|Select-Object -Last 1
if(-not $line){throw 'Runtime maintenance gate did not emit a result'}
$result=$line|ConvertFrom-Json
$after=(Get-FileHash -LiteralPath $hostSource).Hash
if($before-ne$after){throw 'Product source changed during Runtime maintenance checks'}
if($code-ne0-or-not$result.ok-or$result.hardwareCalls-ne0){Get-Content -LiteralPath (Join-Path $EvidenceRoot 'checks.txt');throw 'Runtime maintenance isolation behavior failed'}
[pscustomobject]@{ok=$true;checks=$result.checks;hostDllSha256=$result.hostDllSha256;programSha256=$after;testSourceSha256=(Get-FileHash -LiteralPath $checkSource).Hash;hardwareCalls=0;scope=$result.scope}|ConvertTo-Json -Compress