[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$EvidenceRoot,
    [string]$HostAssembly = '',
    [string]$NodeExecutable = ''
)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($EvidenceRoot)
$projectRoot = Split-Path -Parent $PSScriptRoot
$project = Join-Path $PSScriptRoot 'FanRepeatedSleepIntentSelfTest\FanRepeatedSleepIntentSelfTest.csproj'
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