param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$fanOut=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $fanOut){throw 'Use a new evidence directory.'}
[IO.Directory]::CreateDirectory($fanOut)|Out-Null
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'fan_write_confirmation_selftest.cs') -Destination (Join-Path $fanOut 'Program.cs')
'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors></PropertyGroup></Project>'|Set-Content -LiteralPath (Join-Path $fanOut 'Checks.csproj') -Encoding utf8
& dotnet build (Join-Path $fanOut 'Checks.csproj') -c Release -o (Join-Path $fanOut 'bin') --disable-build-servers *> (Join-Path $fanOut 'build.out')
if($LASTEXITCODE-ne0){Get-Content (Join-Path $fanOut 'build.out') -Tail 14;exit $LASTEXITCODE}
& dotnet (Join-Path $fanOut 'bin/Checks.dll') ([IO.Path]::GetFullPath($HostDll)) (Join-Path $fanOut 'result.json')
exit $LASTEXITCODE
