[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][string]$CandidateDll,
  [Parameter(Mandatory=$true)][string]$EvidenceRoot
)
$ErrorActionPreference = 'Stop'
$fanCandidate = (Resolve-Path -LiteralPath $CandidateDll).Path
$fanEvidence = [IO.Path]::GetFullPath($EvidenceRoot)
New-Item -ItemType Directory -Path $fanEvidence -Force | Out-Null
$fanCheckSource = [Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_hwinfo_mapping_lifetime_selftest.cs'))
$fanProject = Join-Path $fanEvidence 'MappingLifetime.csproj'
$fanXml = '<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="' + $fanCheckSource + '" /></ItemGroup></Project>'
[IO.File]::WriteAllText($fanProject, $fanXml, [Text.UTF8Encoding]::new($false))
& dotnet run --project $fanProject -c Release -- $fanCandidate (Join-Path $fanEvidence 'results.json') *> (Join-Path $fanEvidence 'run.log')
$fanCheckExit = $LASTEXITCODE
[IO.File]::WriteAllText((Join-Path $fanEvidence 'exit.txt'), [string]$fanCheckExit)
Get-Content -LiteralPath (Join-Path $fanEvidence 'run.log')
exit $fanCheckExit
