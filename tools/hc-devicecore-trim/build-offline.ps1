param(
    [string]$ScratchRoot = 'D:\Codex\2026-10-18\kai-s\work\hc-large-strip\binary-trim',
    [string]$CecilPath = 'C:\Users\DaVe\.nuget\packages\microsoft.net.illink.tasks\10.0.11\tools\net\Mono.Cecil.dll'
)
$ErrorActionPreference = 'Stop'
if (!(Test-Path -LiteralPath $CecilPath)) { throw 'Specify the reviewed offline Mono.Cecil.dll with -CecilPath' }
$trusted = '6346621d998fe428b4b97bb365e5090537be5074861d726cead5767d3b2a7cf2'
if ((Get-FileHash -LiteralPath $CecilPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $trusted) { throw 'Cecil differs from the reviewed Microsoft.NET.ILLink.Tasks 10.0.11 dependency' }
New-Item -ItemType Directory -Force -Path $ScratchRoot, (Join-Path $ScratchRoot 'packages') | Out-Null
$env:DOTNET_CLI_HOME = Join-Path $ScratchRoot 'dotnet-home'
$env:NUGET_PACKAGES = Join-Path $ScratchRoot 'nuget-cache'
$env:DOTNET_CLI_TELEMETRY_OPTOUT = '1'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE = '1'
& dotnet build (Join-Path $PSScriptRoot 'HcDeviceCoreTrim.csproj') "-p:BaseIntermediateOutputPath=$ScratchRoot\obj\" "-p:OutputPath=$ScratchRoot\bin\" "-p:CecilPath=$CecilPath" --source (Join-Path $ScratchRoot 'packages') --nologo -v:q
if ($LASTEXITCODE -ne 0) { throw 'Offline tool build failed' }
Write-Output (Join-Path $ScratchRoot 'bin\HcDeviceCoreTrim.dll')
