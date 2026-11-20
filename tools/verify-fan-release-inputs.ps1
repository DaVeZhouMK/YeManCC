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
$component = 'candidate-fan-win4-open-c01-20261119'
$archive = "deps/fan-host-source/$component"
$archiveSha = '1dfb0d6ba86fa436e90573069e42927755343606d178af2927d249d500bac020'
$payload = 'PowerControl/fan-host'
$payloadSha = '1776faaefe9df422dbd38452e46542a3efb183803f30426d6c4bb8677b06f79e'
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
$dependencies = @($hcManifestPath) + @($hcManifest.files | ForEach-Object { "$hcDirectory/$(([string]$_.path).Replace('\','/'))" })
if ($hcManifest.sharedDependencies) {
  $dependencies += @($hcManifest.sharedDependencies.files | ForEach-Object { "PowerControl/ymcc-shared-runtime/$($_.path)" })
}
$paths = $archivePaths + $runtimePaths + $dependencies + @(
  '.github/actions/release-build/action.yml',
  '.github/workflows/release-prepare.yml',
  '.github/workflows/release.yml',
  'tools/verify-fan-release-inputs.ps1',
  $selector,
  'tools/hc-device-runtime/verify-host-source-bundle.ps1',
  'tools/fan_boot_autostart_sim.ts',
  'tools/fan_native_manual_wait_selftest.ts',
  'tools/fan_resume_continuous_recovery_selftest.ts',
  'tools/fan_duplicate_wake_replay_selftest.ts',
  'tools/fixtures/fan_no_io_host.ts',
  'native/main.cpp',
  'native/fan_resume_owner.h',
  'src/App.vue',
  'src/bridge/fanHost.ts'
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
if ($action -cnotmatch '(?m)^\s+& tools/verify-fan-release-inputs\.ps1 -ProjectRoot \$env:GITHUB_WORKSPACE -ExpectedCommit \$env:YMCC_RELEASE_COMMIT[ \t\r]*$') {
  throw 'FAN_RELEASE_WORKFLOW_NOT_WIRED: the shared release action must check the approved fan inputs.'
}

# Older callers of the native recovery script use this embedded fallback.
$native = Get-Content -LiteralPath (Join-Path $root 'native/main.cpp') -Raw -Encoding UTF8
$fallback = [regex]::Matches($native, '\$expectedFanHostV2ManifestSha256 = ''([0-9a-fA-F]{64})''')
if ($fallback.Count -ne 1 -or $fallback[0].Groups[1].Value -ine $payloadSha) {
  throw 'FAN_RELEASE_NATIVE_PAYLOAD_BINDING_MISMATCH: update the native Fan Host V2 recovery fallback with the approved payload.'
}

Write-Output "FAN_RELEASE_INPUTS_OK commit=$commit component=$component archiveFiles=$($archivePaths.Count) payloadFiles=$($runtimePaths.Count) payloadManifestSha256=$payloadSha"
