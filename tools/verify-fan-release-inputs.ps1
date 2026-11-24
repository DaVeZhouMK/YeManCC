#Requires -Version 7.0
[CmdletBinding()]
param(
  [string]$ProjectRoot = (Split-Path -Parent $PSScriptRoot),
  [Parameter(Mandatory)][ValidatePattern('^[a-fA-F0-9]{40}$')][string]$ExpectedCommit
)

$ErrorActionPreference = 'Stop'
$PSNativeCommandUseErrorActionPreference = $false
$root = (Resolve-Path -LiteralPath $ProjectRoot).ProviderPath
$commit = & git -C $root rev-parse --verify 'HEAD^{commit}'
if ($LASTEXITCODE -ne 0 -or $commit -ine $ExpectedCommit) {
  throw 'FAN_RELEASE_COMMIT_MISMATCH: check out the actual release commit.'
}

# Release approval is independent of the payload verifier selected by the checkout.
# Advance these pins together only when a replacement carries both GPD fixes.
$component = 'candidate-onex-f1-fan-aliases-20261124'
$archive = "deps/fan-host-source/$component"
$archiveSha = '88a501bbda40005f7584b35263b950559d3072f41c76bb665928e4389768798f'
$payload = 'PowerControl/fan-host'
$payloadSha = '01c0d834dd11d60559c5f7d8d4ff0627d9d617e0ec0071f19bc521740c166f10'
$selector = 'tools/verify-r5v9-fan-host-payload.ps1'

function Read-ApprovedManifest([string]$Relative, [string]$Hash) {
  $path = Join-Path $root $Relative
  if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-FileHash -LiteralPath $path).Hash -ine $Hash) {
    throw "FAN_RELEASE_APPROVED_MANIFEST_MISMATCH: $Relative"
  }
  Get-Content -LiteralPath $path -Raw -Encoding UTF8 | ConvertFrom-Json
}

$selected = [regex]::Matches((Get-Content -LiteralPath (Join-Path $root $selector) -Raw -Encoding UTF8), '(?m)^\s*\$componentSourceBundleRelative\s*=\s*''([^''\r\n]+)''[ \t\r]*$')
if ($selected.Count -ne 1 -or $selected[0].Groups[1].Value -cne $archive) {
  throw "FAN_RELEASE_APPROVED_COMPONENT_REQUIRED: expected $component in $selector. Bring forward the Win5 wake and Win4 Open fixes before tagging."
}
$sourceManifest = Read-ApprovedManifest "$archive/manifest.json" $archiveSha
$runtimeManifest = Read-ApprovedManifest "$payload/YeManFanHost.payload.json" $payloadSha
$archivePaths = @("$archive/manifest.json") + @($sourceManifest.files | ForEach-Object { "$archive/$($_.path)" })
$runtimePaths = @("$payload/YeManFanHost.payload.json") + @($runtimeManifest.files | ForEach-Object { "$payload/$($_.path)" })
# Reuse the payload verifier to validate shared-runtime paths and file hashes.
& (Join-Path $root $selector) -PayloadRoot (Join-Path $root $payload) | Out-Null
$hcManifestPath = 'PowerControl/' + ([string]$runtimeManifest.runtimeManifest).Replace('\','/').Substring(3)
$hcDirectory = $hcManifestPath.Substring(0, $hcManifestPath.LastIndexOf('/'))
$hcManifest = Get-Content -LiteralPath (Join-Path $root $hcManifestPath) -Raw -Encoding UTF8 | ConvertFrom-Json
. (Join-Path $root 'tools/hc-export-source-contract.ps1')
Assert-YmccMainlineHcExport -ProjectRoot $root -RuntimeRoot (Join-Path $root $hcDirectory) | Out-Null
# Bind the Win4-corrected runtime separately from the unchanged Fan Host.
$hcSha = 'f79edd9dc1c60d5b5e9f3c456174b3c049ea6087ea0c988facd95bc1c174b93c'
$hcRow = @($hcManifest.files | Where-Object { $_.path -ceq 'HandheldCompanion.dll' })
if ($hcRow.Count -ne 1 -or $hcRow[0].sha256 -cne $hcSha) {
  throw 'FAN_RELEASE_WIN4_HC_REQUIRED: the selected HC runtime lacks the Win4 port-pair correction.'
}
. (Join-Path $root 'tools/lighting-build-contract.ps1')
Assert-YmccLightingHcRuntimePin -ProjectRoot $root | Out-Null
$dependencies = @($hcManifestPath) + @($hcManifest.files | ForEach-Object { "$hcDirectory/$(([string]$_.path).Replace('\','/'))" })
if ($hcManifest.sharedDependencies) {
  $dependencies += @($hcManifest.sharedDependencies.files | ForEach-Object { "PowerControl/ymcc-shared-runtime/$($_.path)" })
}
$paths = $archivePaths + $runtimePaths + $dependencies + @(
  '.github/actions/release-build/action.yml',
  '.github/workflows/release-prepare.yml',
  '.github/workflows/release.yml',
  'tools/github-release-preflight.ps1',
  'tools/verify-fan-release-inputs.ps1',
  'tools/hc-export-source-contract.ps1',
  'tools/hc-export-baseline-guard.ps1',
  'tools/lighting-build-contract.ps1',
  'deps/ymcc-device-core-runtime/export-baseline.lock.json',
  'deps/ymcc-device-core-runtime/export-baseline/approved-20261121-win4-port.json',
  'deps/ymcc-device-core-runtime/profile.json',
  'deps/ymcc-device-core-runtime/bin/HandheldCompanion.dll',
  'FanLab/LightSetter/Program.cs',
  $selector,
  'tools/hc-device-runtime/verify-host-source-bundle.ps1',
  'tools/fan_boot_autostart_sim.ts',
  'tools/fan_manual_wake_ipc_selftest.mjs',
  'tools/fan_native_manual_wait_selftest.ts',
  'tools/fan_cold_open_admission_selftest.ts',
  'tools/fan_resume_continuous_recovery_selftest.ts',
  'tools/fan_duplicate_wake_replay_selftest.ts',
  'tools/fixtures/fan_no_io_host.ts',
  'native/main.cpp',
  'native/fan_resume_owner.h',
  'src/App.vue',
  'src/views/FanView.vue',
  'src/bridge/fanApi.ts',
  'src/bridge/fanHost.ts',
  'src/bridge/powerLifecycleState.ts'
)
$tree = @(& git -C $root ls-tree -r '--format=%(objectmode)%x09%(path)' $commit -- $paths)
if ($LASTEXITCODE -ne 0) { throw 'FAN_RELEASE_TREE_UNREADABLE' }
$inTree = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($line in $tree) {
  if ($line -cmatch '^100(?:644|755)\t(.+)$') { [void]$inTree.Add($Matches[1]) }
}
$missing = @($paths | Where-Object { !$inTree.Contains($_) })
if ($missing.Count) {
  throw "FAN_RELEASE_INPUT_NOT_COMMITTED: $($missing.Count) missing; first: $($missing[0])"
}
& git -C $root diff --quiet --no-ext-diff $commit -- $paths
if ($LASTEXITCODE -ne 0) { throw 'FAN_RELEASE_CHECKOUT_CHANGED: fan inputs differ from the immutable release commit.' }

foreach ($workflow in @('.github/workflows/release-prepare.yml', '.github/workflows/release.yml')) {
  $text = Get-Content -LiteralPath (Join-Path $root $workflow) -Raw -Encoding UTF8
  if ($text -cnotmatch '(?m)^\s+uses: \./\.github/actions/release-build[ \t\r]*$') {
    throw "FAN_RELEASE_WORKFLOW_NOT_WIRED: $workflow must use the shared release action."
  }
}
$action = Get-Content -LiteralPath (Join-Path $root '.github/actions/release-build/action.yml') -Raw -Encoding UTF8
if ($action -cnotmatch '(?m)^\s+node tools/fan_manual_wake_ipc_selftest\.mjs .* --native-owner-rearm-only[ \t\r]*$' -or
    $action -cnotmatch '(?m)^\s+node tools/fan_manual_wake_ipc_selftest\.mjs .* --background-ready-only[ \t\r]*$' -or
    !$action.Contains('committed wake retains a curve across delayed native permission:', [StringComparison]::Ordinal) -or
    !$action.Contains('committed wake survives an unavailable Native write fact:', [StringComparison]::Ordinal) -or
    !$action.Contains('first manual curve waits for a changing Native write fact', [StringComparison]::Ordinal)) {
  throw 'FAN_RELEASE_WAKE_REGRESSION_REQUIRED: restore the release checks for delayed permission, unavailable Native state, one-shot background wake and first manual control.'
}
foreach ($boundary in @('resume', 'read', 'off', 'new-sleep', 'pending', 'stale')) {
  if (!$action.Contains("'background-ready without the private notification: $boundary'", [StringComparison]::Ordinal)) {
    throw "FAN_RELEASE_WAKE_PROJECTION_REGRESSION_REQUIRED: missing projection boundary $boundary."
  }
}
foreach ($boundary in @('ipc-recovers', 'ipc-off', 'ipc-still-failing')) {
  if (!$action.Contains("'$boundary'", [StringComparison]::Ordinal)) {
    throw "FAN_RELEASE_WAKE_IPC_REGRESSION_REQUIRED: missing coordinator read boundary $boundary."
  }
}
$preflight = Get-Content -LiteralPath (Join-Path $root 'tools/github-release-preflight.ps1') -Raw -Encoding UTF8
if ($action -cnotmatch '(?m)^\s+& tools/github-release-preflight\.ps1 -ProjectRoot \$env:GITHUB_WORKSPACE -Mode \$env:YMCC_GIT_RELEASE_MODE -ExpectedCommit \$env:YMCC_RELEASE_COMMIT -EvidencePath \(Join-Path \$env:RUNNER_TEMP ''release-preflight-early\.json''\)[ \t\r]*$' -or
    $preflight -cnotmatch '(?m)^\s+\$fanInputs = & \(Join-Path \$PSScriptRoot ''verify-fan-release-inputs\.ps1''\) -ProjectRoot \$root -ExpectedCommit \$commit[ \t\r]*$') {
  throw 'FAN_RELEASE_WORKFLOW_NOT_WIRED: release preflight must check the approved fan inputs before build.'
}

# Older callers of the native recovery script use this embedded fallback.
$native = Get-Content -LiteralPath (Join-Path $root 'native/main.cpp') -Raw -Encoding UTF8
$fallback = [regex]::Matches($native, '\$expectedFanHostV2ManifestSha256 = ''([0-9a-fA-F]{64})''')
if ($fallback.Count -ne 1 -or $fallback[0].Groups[1].Value -ine $payloadSha) {
  throw 'FAN_RELEASE_NATIVE_PAYLOAD_BINDING_MISMATCH: update the native Fan Host V2 recovery fallback with the approved payload.'
}

Write-Output "FAN_RELEASE_INPUTS_OK commit=$commit component=$component archiveFiles=$($archivePaths.Count) payloadFiles=$($runtimePaths.Count) payloadManifestSha256=$payloadSha"
