param(
    [Parameter(Mandatory=$true)][string]$HostDll,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [ValidateSet('behavior','allocation-baseline','allocation-candidate')][string]$Mode='behavior'
)
$ErrorActionPreference='Stop'
$evidence=[IO.Path]::GetFullPath($OutputRoot)
if(Test-Path -LiteralPath $evidence){throw 'Use a new evidence directory'}
[IO.Directory]::CreateDirectory($evidence)|Out-Null
$inputDll=[IO.Path]::GetFullPath($HostDll)
$fixture=Join-Path $PSScriptRoot 'fan_maintenance_result_buffers_selftest.cs'
$inputHash=(Get-FileHash -LiteralPath $inputDll -Algorithm SHA256).Hash
$fixtureHash=(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash
$escaped=[Security.SecurityElement]::Escape($fixture)
$project=Join-Path $evidence 'MaintenanceBuffers.csproj'
$xml='<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0-windows10.0.19041.0</TargetFramework><UseWPF>true</UseWPF><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><TreatWarningsAsErrors>true</TreatWarningsAsErrors><EnableDefaultCompileItems>false</EnableDefaultCompileItems></PropertyGroup><ItemGroup><Compile Include="'+$escaped+'"/></ItemGroup></Project>'
[IO.File]::WriteAllText($project,$xml,[Text.UTF8Encoding]::new($false))
& dotnet build $project -c Release --nologo -o (Join-Path $evidence 'bin') *> (Join-Path $evidence 'build.log')
if($LASTEXITCODE-ne0){Get-Content -LiteralPath (Join-Path $evidence 'build.log');throw 'Maintenance result-buffer fixture build failed'}
$resultPath=Join-Path $evidence 'result.json'
& dotnet (Join-Path $evidence 'bin/MaintenanceBuffers.dll') $inputDll $Mode $resultPath *> (Join-Path $evidence 'checks.log')
$code=$LASTEXITCODE
if(-not(Test-Path -LiteralPath $resultPath -PathType Leaf)){throw 'No actual-consumer result emitted'}
$result=Get-Content -LiteralPath $resultPath -Raw|ConvertFrom-Json
if((Get-FileHash -LiteralPath $inputDll -Algorithm SHA256).Hash-ne$inputHash-or(Get-FileHash -LiteralPath $fixture -Algorithm SHA256).Hash-ne$fixtureHash){throw 'Validation input changed during execution'}
[pscustomobject]@{ok=$result.ok;checks=$result.checks;hostDllSha256=$inputHash.ToLowerInvariant();fixtureSha256=$fixtureHash.ToLowerInvariant();mode=$Mode;hardwareCalls=0;hostMainExecuted=$false;scope=$result.scope}|ConvertTo-Json -Compress
if($code-ne0-or-not$result.ok){exit 1}
