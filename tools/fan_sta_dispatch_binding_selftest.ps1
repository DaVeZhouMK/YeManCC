param([Parameter(Mandatory=$true)][string]$HostDll,[Parameter(Mandatory=$true)][string]$OutputRoot)
$ErrorActionPreference='Stop'
$out=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $out){throw 'Use a new evidence directory.'}
[IO.Directory]::CreateDirectory($out)|Out-Null
$source=[Security.SecurityElement]::Escape((Join-Path $PSScriptRoot 'fan_sta_dispatch_binding_selftest.cs'))
$project='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$source+'"/></ItemGroup></Project>'
[IO.File]::WriteAllText((Join-Path $out 'Checks.csproj'),$project,[Text.UTF8Encoding]::new($false))
& dotnet build (Join-Path $out 'Checks.csproj') -c Release -o (Join-Path $out 'bin') --disable-build-servers *> (Join-Path $out 'build.out')
if($LASTEXITCODE-ne0){Get-Content (Join-Path $out 'build.out') -Tail 12;exit $LASTEXITCODE}
& dotnet (Join-Path $out 'bin/Checks.dll') ([IO.Path]::GetFullPath($HostDll)) (Join-Path $out 'result.json')
exit $LASTEXITCODE
