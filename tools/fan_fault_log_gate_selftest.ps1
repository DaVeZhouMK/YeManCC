[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$fanDll=(Resolve-Path -LiteralPath $HostDll).Path
$fanOut=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $fanOut){throw 'OutputRoot must be new; existing evidence will not be cleared'}
[IO.Directory]::CreateDirectory($fanOut)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_fault_log_gate_selftest.cs') -Destination (Join-Path $fanOut 'Program.cs')
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>'|Set-Content -LiteralPath (Join-Path $fanOut 'FaultGateChecks.csproj') -Encoding utf8
& dotnet build (Join-Path $fanOut 'FaultGateChecks.csproj') -c Release -o (Join-Path $fanOut 'bin') *> (Join-Path $fanOut 'build.log')
if($LASTEXITCODE-ne0){Get-Content -LiteralPath (Join-Path $fanOut 'build.log') -Tail 15;exit $LASTEXITCODE}
& dotnet (Join-Path $fanOut 'bin\FaultGateChecks.dll') $fanDll (Join-Path $fanOut 'logs') (Join-Path $fanOut 'result.json') *> (Join-Path $fanOut 'run.log')
$fanExit=$LASTEXITCODE
[IO.File]::WriteAllText((Join-Path $fanOut 'checks.exit'),[string]$fanExit)
Get-Content -LiteralPath (Join-Path $fanOut 'run.log')|Where-Object{$_-notlike'{*'}
if(Test-Path -LiteralPath (Join-Path $fanOut 'result.json')){Get-Content -LiteralPath (Join-Path $fanOut 'result.json') -Raw}
exit $fanExit
