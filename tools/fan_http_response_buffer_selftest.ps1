[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$fanCandidate=(Resolve-Path -LiteralPath $HostDll).Path
$fanOutput=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $fanOutput){throw 'Use a new evidence directory; previous evidence is retained.'}
[IO.Directory]::CreateDirectory($fanOutput)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_http_response_buffer_selftest.cs') -Destination (Join-Path $fanOutput 'Program.cs')
$fanProject='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>'
[IO.File]::WriteAllText((Join-Path $fanOutput 'Checks.csproj'),$fanProject,[Text.UTF8Encoding]::new($false))
& dotnet build (Join-Path $fanOutput 'Checks.csproj') -c Release -o (Join-Path $fanOutput 'bin')
if($LASTEXITCODE-ne0){exit $LASTEXITCODE}
& dotnet (Join-Path $fanOutput 'bin/Checks.dll') $fanCandidate (Join-Path $fanOutput 'logs') (Join-Path $fanOutput 'result.json')
exit $LASTEXITCODE
