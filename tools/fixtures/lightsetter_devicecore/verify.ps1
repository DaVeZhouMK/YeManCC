param([Parameter(Mandatory = $true)][string]$Scratch)
$ErrorActionPreference = 'Stop'
$fixture = $PSScriptRoot
$repo = [IO.Path]::GetFullPath((Join-Path $fixture '../../..'))
$taskScratch = [IO.Path]::GetFullPath($Scratch)
New-Item -ItemType Directory -Path $taskScratch -Force | Out-Null
$build = Join-Path $taskScratch 'build'

function Checked([string]$Executable, [string[]]$Arguments) {
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$Executable failed with exit code $LASTEXITCODE" }
}
function Build-Project([string]$Project, [string]$Area, [string[]]$Properties = @()) {
    Checked 'dotnet' (@('build', (Join-Path $repo $Project), '-c', 'Release', '--artifacts-path', (Join-Path $build $Area), '--nologo') + $Properties)
}

# Never rewrites source locks or production pins. Drift needs explicit review.
Checked 'python' @((Join-Path $fixture 'extract_pinned.py'), '--check')
Build-Project 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj' 'core'
$core = Join-Path $build 'core/bin/YeManLightDeviceCore/release/YeManLightDeviceCore.dll'
$coreHash = (Get-FileHash -LiteralPath $core -Algorithm SHA256).Hash.ToLowerInvariant()
$pinSource = Get-Content -LiteralPath (Join-Path $repo 'FanLab/LightSetter/LightingAssemblyPins.cs') -Raw
if ($pinSource -notmatch ('StandaloneLightingSha256\s*=\s*"' + $coreHash + '"')) {
    throw "Production core pin mismatch ($coreHash). Review build inputs before updating LightingAssemblyPins.cs."
}
# An output-directory move must not silently invalidate the compiled pin.
Build-Project 'FanLab/LightSetter/DeviceCore/YeManLightDeviceCore.csproj' 'repro-core'
$reproHash = (Get-FileHash -LiteralPath (Join-Path $build 'repro-core/bin/YeManLightDeviceCore/release/YeManLightDeviceCore.dll') -Algorithm SHA256).Hash.ToLowerInvariant()
if ($reproHash -ne $coreHash) { throw 'Core output-directory reproducibility failure' }
Build-Project 'FanLab/LightSetter/YeManLightSetter.csproj' 'setter-core-only' @('-p:LightSetterLegacyDesktop=false')
Build-Project 'FanLab/LightSetter/YeManLightSetter.csproj' 'setter-legacy'
Build-Project 'tools/fixtures/lightsetter_devicecore/ReplayCore.csproj' 'replay-core'
$replay = Join-Path $build 'replay-core/bin/ReplayCore/release/YeManLightDeviceCore.dll'
$replayHash = (Get-FileHash -LiteralPath $replay -Algorithm SHA256).Hash.ToLowerInvariant()
# This compiled fixture trust root exists only in scratch; the shipping consumer
# retains its production pin and is proven to reject this replay assembly.
$replayPins = Join-Path $taskScratch 'ReplayPins.cs'
('internal static class LightingAssemblyPins { internal const string StandaloneLightingSha256 = "' + $replayHash + '"; }') | Set-Content -LiteralPath $replayPins -Encoding utf8
Build-Project 'tools/fixtures/lightsetter_devicecore/ReplayConsumer.csproj' 'replay-consumer' @("-p:ReplayPinSource=$replayPins")
Build-Project 'tools/fixtures/lightsetter_devicecore/LegacyActionReplay.csproj' 'legacy-replay'
$legacy = & dotnet (Join-Path $build 'legacy-replay/bin/LegacyActionReplay/release/LightSetterLegacyActionReplay.dll')
if ($LASTEXITCODE -ne 0) { throw 'Legacy action replay failed' }
$legacy | Set-Content -LiteralPath (Join-Path $taskScratch 'legacy-replay-report.json') -Encoding utf8
Write-Output $legacy
Checked 'python' @((Join-Path $fixture 'replay.py'), '--scratch', $taskScratch)
@{coreSha256=$coreHash;reproducibleAcrossOutputDirectories=$true;hardwareIo=$false} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $taskScratch 'build-verification.json') -Encoding utf8
