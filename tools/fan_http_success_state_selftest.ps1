[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot,[ValidateSet('legacy','lazy')][string]$Mode='lazy')
$ErrorActionPreference='Stop'
$candidate=(Resolve-Path -LiteralPath $HostDll).Path
$out=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $out){throw 'Use a new evidence directory; previous evidence is never cleared.'}
[IO.Directory]::CreateDirectory($out)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_http_success_state_selftest.cs') -Destination (Join-Path $out 'Program.cs')
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>'|Set-Content -LiteralPath (Join-Path $out 'Checks.csproj') -Encoding utf8
& dotnet build (Join-Path $out 'Checks.csproj') -c Release -o (Join-Path $out 'bin')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& dotnet (Join-Path $out 'bin/Checks.dll') $candidate (Join-Path $out 'logs') $Mode (Join-Path $out 'result.json')
exit $LASTEXITCODE
