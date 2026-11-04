[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [string]$HostAssembly = '',
    [string]$NodeExecutable = '',
    [switch]$JournalsOnly
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceRoot)
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $PSScriptRoot 'FanRepeatedSleepIntentSelfTest\FanRepeatedSleepIntentSelfTest.csproj'
if ($JournalsOnly) {
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $build = Join-Path $root 'build'
    $feed = Join-Path $root 'empty-nuget-feed'
    New-Item -ItemType Directory -Path $feed -Force | Out-Null
    & dotnet build $project -c Release -r win-x64 --artifacts-path $build --disable-build-servers -p:NuGetAudit=false "-p:RestoreSources=$feed" -v quiet
    if ($LASTEXITCODE -ne 0) { throw 'Original fan journal consumer build failed' }
    $consumer = Join-Path $build 'bin\FanRepeatedSleepIntentSelfTest\release_win-x64\FanRepeatedSleepIntentSelfTest.dll'
    if ([string]::IsNullOrWhiteSpace($HostAssembly)) { $HostAssembly = Join-Path (Split-Path -Parent $consumer) 'YeManFanHost.dll' }
    $hostPath = (Resolve-Path -LiteralPath $HostAssembly).Path
    $hostSha = (Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $runRoot = Join-Path $root 'journals'
    & dotnet $consumer $runRoot $hostPath '-' '-' 'journals'
    $consumerExit = $LASTEXITCODE
    if (-not (Test-Path -LiteralPath (Join-Path $runRoot 'result.json'))) { throw 'Fan journal result missing' }
    $result = Get-Content -LiteralPath (Join-Path $runRoot 'result.json') -Raw | ConvertFrom-Json
    if ($result.hostAssemblySha256 -ne $hostSha) { throw 'Fan journal consumer used another payload' }
    if ($consumerExit -ne 0 -or -not $result.ok) { throw "Fan journals self-test failed: $($result.failures.Count)" }
    Write-Output "fan journal original-consumer self-test: PASS ($($result.count); no network/Host Main/device)"
    $engineRoot = Join-Path $root 'engine-only'
    & dotnet $consumer $engineRoot $hostPath '-' '-' 'engine-only'
    if ($LASTEXITCODE -ne 0) { throw 'Original fan engine-only regressions failed' }
    $engineResult = Get-Content -LiteralPath (Join-Path $engineRoot 'result.json') -Raw | ConvertFrom-Json
    if (-not $engineResult.ok -or $engineResult.count -lt 72 -or $engineResult.hostAssemblySha256 -ne $hostSha) { throw 'Original engine-only result or payload binding is incomplete' }
    if ((Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hostSha) { throw 'Host payload changed during journal consumer checks' }
    return
}
if ([string]::IsNullOrWhiteSpace($HostAssembly)) { $HostAssembly = Join-Path $projectRoot 'PowerControl\fan-host\YeManFanHost.dll' }
$hostPath = (Resolve-Path -LiteralPath $HostAssembly).Path
$hostSha = (Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ([string]::IsNullOrWhiteSpace($NodeExecutable)) {
    $nodeCommand = Get-Command node -ErrorAction SilentlyContinue
    if ($nodeCommand) { $NodeExecutable = $nodeCommand.Source }
    else {
        $workspace = [IO.Path]::GetFullPath((Join-Path $projectRoot '..\..\..'))
        $NodeExecutable = Join-Path $workspace 'Environment\Toolchains\nodejs\node-v22.14.0-win-x64\node.exe'
    }
}
$nodePath = (Resolve-Path -LiteralPath $NodeExecutable).Path
New-Item -ItemType Directory -Path $root -Force | Out-Null
$bundle = Join-Path $root 'bridge-host-chain.mjs'
& $nodePath (Join-Path $projectRoot 'node_modules\esbuild\bin\esbuild') (Join-Path $PSScriptRoot 'fan_explicit_off_host_chain_selftest.ts') --bundle --platform=node --format=esm "--outfile=$bundle"
if ($LASTEXITCODE -ne 0) { throw 'Bridge/Host joint test bundle failed' }
foreach ($case in @('allowed','empty-resume','sleep','stale-native')) {
    $runRoot = Join-Path $root $case
    & dotnet run --project $project -c Release --artifacts-path (Join-Path $root 'build') -- $runRoot $hostPath $nodePath $bundle $case
    if ($LASTEXITCODE -ne 0) { throw "Repeated sleep/OFF joint case failed: $case" }
    $result = Get-Content -LiteralPath (Join-Path $runRoot 'result.json') -Raw | ConvertFrom-Json
    if (-not $result.ok -or $result.count -lt 82) { throw "Joint result is incomplete: $case" }
    if ($result.hostAssemblySha256 -ne $hostSha) { throw 'Test did not bind requested Host payload bytes' }
}
if ((Get-FileHash -LiteralPath $hostPath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $hostSha) { throw 'Host payload changed during test' }
Write-Output 'fan repeated sleep / explicit OFF self-test: PASS (four joint cases; hardware/native boundaries are fixtures)'