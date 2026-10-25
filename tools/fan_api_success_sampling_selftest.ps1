[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot,[ValidateSet('typed','legacy')][string]$Mode='typed')
$ErrorActionPreference='Stop'
$hostFile=(Resolve-Path -LiteralPath $HostDll).Path
$out=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $out){throw 'OutputRoot must be new; existing evidence will not be cleared.'}
[IO.Directory]::CreateDirectory($out)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_api_success_sampling_selftest.cs') -Destination (Join-Path $out 'Program.cs')
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>' | Set-Content -LiteralPath (Join-Path $out 'SamplingChecks.csproj') -Encoding utf8
& dotnet build (Join-Path $out 'SamplingChecks.csproj') -c Release -o (Join-Path $out 'bin')
if($LASTEXITCODE -ne 0){exit $LASTEXITCODE}
& dotnet (Join-Path $out 'bin\SamplingChecks.dll') $hostFile (Join-Path $out 'log-cases') $Mode
exit $LASTEXITCODE
