[CmdletBinding()]
param(
  [string]$PayloadRoot = '',
  [string]$SourceRoot = ''
)

$ErrorActionPreference = 'Stop'
# 2026-09-21 re-base (920 v1.15 section 26.3.4, isolated release candidate): the payload now
# carries the candidate Host built from the locked source with the pinned HC identity moved to
# the approved candidate HandheldCompanion.dll (08a38660...). The verification below is
# unchanged in kind - wrong/mixed/missing/tampered payloads are still refused; only the
# expected identities were recomputed from the actual approved artifacts.
#   frozen mainline  : dll BB0FBBEF... / manifest F6D460AF... / HC 886AC06B...
#   section 26       : dll 2525C1FD... / manifest 4B894EC3... / exe A3CE266B... (apphost unchanged)
#   section 28       : dll D38B91F8... / manifest D94CE760... / exe A3CE266B... (three-line rebase; apphost unchanged)
#   section 29 (2026-09-23, operator authorized): dll 43750166... / manifest CBA0FF9E...
#     (fan test mode + gamepad button logs; Host source FanLab/real-host/Program.cs
#      rebuilt and re-frozen via tools/build-fan-host-payload.ps1)
#   section 30 (2026-09-23, operator ruling: fan runs by HC capability-library semantics):
#     dll A972A798... / manifest 3136713E... / exe 77CBA5DF... (apphost unchanged)
#     (per-channel transport receipts are evidence, fatal only when zero channels transported;
#      CPU/log load reduction: adaptive runtime sample + conditional thread attribution)
#   section 31 (2026-09-23, operator T0 ruling: bounded host exit):
#     dll 0CDD92CB... / manifest 224EBAC1... / exe 77CBA5DF... (apphost unchanged)
#     (parent-exit/requested-stop finalize budget: the host never stays resident after its
#      parent exits; budget exhaustion is recorded as unverified, never as success)
#   section 31 addendum (2026-09-23, FAN-204 s31 addendum ruling: resume accept / bounded observe):
#     dll E70F45A5... / manifest 52B0C8A5... / exe 77CBA5DF... (apphost unchanged)
#     (/api/resume accepts fast and returns a structured Resuming phase while the rebuild runs in
#      the background; callers observe /api/state under the same generation for at most 15 s and
#      only Ready settles recovery; snapshot truth = resumePhase/retryAfterMs/controlAccepting/
#      lastControlWriteEvidence; sandbox-only slow-rebuild injection; P-3 read-only loop-stop
#      evidence)
#   2026-09-26 re-freeze (payload rebuilt via tools/build-fan-host-payload.ps1):
#     dll EBD12A24... / manifest 430455DE... / exe 77CBA5DF... (apphost unchanged)
# Recompute-and-refill after any change, never hand-edit.
$expected = [ordered]@{
  'YeManFanHost.exe' = 'A3CE266B880F67045D3F895E2FC68B1FE81F641D5F1A08A01F0DE19D2458F733'
  'YeManFanHost.dll' = '5C235B39FFA6EB43346D7EB5D0E43AAD5ECCB96BD5C3F19530E9B39131E266DF'
}
# Keep this file pure ASCII: PS 5.1 misdecodes non-ASCII scripts without BOM
# and drops the next statement (verified 2026-09-11); additionally the
# build-workspace pre-gate auto-verifies this chain.
$expectedManifestSha256 = 'ee6c11e8bc3128a6351b86d9f52b0272cdeed24de717ab11d60e25ebec5b127b'
# Installed artifact98/d8: exact fixed fan-batch C# inputs and two projects; broader39-source archive is independently pinned.
$compiledSourcePins = [ordered]@{
  'capabilities/AsusFanCapabilitySession.cs' = '45a0e2ef7d907f82c2a8a818a05db4202750800cdde6e8d1600862788b173dee'
  'capabilities/FanCapabilityCatalog.cs' = 'c07f040ddff59e4894364667a19a547df8998d7e02f771e29c473ef98f547c78'
  'capabilities/HcDeviceCapabilityCatalog.cs' = 'eeae40ef0a3b2c8065f79d74479086a01ea0a54806eda6528426c627896e40b8'
  'capabilities/HcDeviceCapabilityCatalog.generated.cs' = '718d9b0340bec0ff81d4ebff8e1d96e3724d2a362b8169ca72788bf92002b674'
  'capabilities/HcLightingCapabilityCatalog.cs' = '46145ce59b799cb46bc2f4ef55c88c6c153d7a438dda8b4337c5fc0ad6147a0e'
  'capabilities/HcLightingCapabilityCatalog.generated.cs' = '73fda1026ceac203317f868e81d3fee7444aa306cc46a6654433906f5ab5aab4'
  'FanLab/DeviceCoreSharedDependencies.cs' = '6bc3ea2958aad1238caa399a82249404717987f50fd6bfcbf91a56954df62c66'
  'FanLab/fan-backend/Asus/AsusFanBackendController.cs' = '8cae7c7611fb46686d3b5a4d2c7be6ead9ee4c3b17955e9ec0c00afdd222d303'
  'FanLab/fan-backend/Asus/AsusFanBackendReceipts.cs' = '2f00a3f2178d5713b73696595533bbb5df89bce41e2f7693bb1b12454af5afe2'
  'FanLab/fan-backend/Asus/AsusFanBackendTransport.cs' = 'cbf4100fbbc5fe412874366540d81bd07106eea8b65342d2c04146d136d851de'
  'FanLab/fan-backend/Asus/AsusFanCurveWriter.cs' = '14b09f20ee7ea896ad825ab9fa3c06b135cd24673c07ae15608a50fb4bb03f56'
  'FanLab/fan-backend/YeManFanBackend.csproj' = 'ef89f0f1059d68f35e15f717bf24eb60a9942b22fc46ebb555a972fa417fae58'
  'FanLab/real-host/DeferredHostSuccessor.cs' = '70caf39e6c44d6c557c8d26d58638760b3a139fe3887855908d861a4dcd7a29f'
  'FanLab/real-host/FanCapabilityPolicy.cs' = '0330205e9d996c6668065fe28178a2773825caf8b0560700cc9ae885e3bdf2ad'
  'FanLab/real-host/G1FanContractAdapter.cs' = '399004d8cb76145cfe2579a9492faadfec43f6f506f66d52f8506a8505a2f387'
  'FanLab/real-host/HostProcessLifetime.cs' = 'bbbb1b224508b32b394c384a87c3c8472ea5a8d0d51386f8677fcab896831040'
  'FanLab/real-host/MsiFamilyFanCurve.cs' = 'b5021e27129957de14798cd934205c208a58d9c791ed996601cc845ad1c27d83'
  'FanLab/real-host/MsiFamilyFanCurveSelfTest.cs' = 'f697aa7385ae763b03938e69b03021c34d448627c337d5b6f334a79258fc7392'
  'FanLab/real-host/MsiFanDeviceLifecycle.cs' = 'c6a21a708269b31284b0cb9f867d53bc4dd16efa85cf448b41b7239f3bedca13'
  'FanLab/real-host/MsiWmiClassRegistration.cs' = 'a0eebef2e4df02a723e142bcb4a0707e6e013d4606ec20907c43b47d17e6c605'
  'FanLab/real-host/MsiWmiPayloadResource.cs' = '39f025987159eb5ac4a236d343d13f56e844e13fc0e320af5a0f9d87f035f883'
  'FanLab/real-host/OneXFanDeviceLifecycle.cs' = '16c9f1f5546340329a6270a05e4d1dfe4206b38a5157d5c2fb7c8a4c89cbf549'
  'FanLab/real-host/Program.cs' = 'fcd7da7b778f0ecad24666c540fd1addab4a8a67d0f8d7d2f93a40c93cf29a4d'
  'FanLab/real-host/Section18CandidateFakes.cs' = 'ef35d49c85f1ff17a3be2452e7227fa5c4a13096d0afc2ff7463d313647bd7c6'
  'FanLab/real-host/Section18CandidateLifecycle.cs' = '70201596d0c0f569aeae8979e8851c708da2c3eb53e5595fe36a038d83fc7033'
  'FanLab/real-host/Section19CandidateLifecycle.cs' = '4715f9d7722d8b73c9f95cd7a5ee46c136e5d3265e5e4ba5d05efab99e453272'
  'FanLab/real-host/Section20CandidateLifecycle.cs' = '0f58e93da6e3fa2ef14916219cf0fe71b7defa0e18bf5727fe52369f80a404fa'
  'FanLab/real-host/Section21CandidateLifecycle.cs' = '5ba1da1fddcbb07b70ae57229c27561c7af8cb2f26249a12d72f5b50bc28b278'
  'FanLab/real-host/Section22CandidateLifecycle.cs' = '3ee412a754824439899c95da855ff66980416534cb29c6023257097891032dca'
  'FanLab/real-host/Section23CandidateLifecycle.cs' = '5e0b4ffa33aeece951d79e07d1339786ca1233991caf99e5773f164a97ac3891'
  'FanLab/real-host/YeManFanHost.csproj' = '791307d8d7bb132f940923b045a7f07e409531801125ad18c006aba462d2fda0'
  'FanLab/RuntimeDependencyContract.cs' = '4f0fa4d26102fba1a2ed8cd013eb029457ab2049dad649809c1a17e2f1cf71c1'
  'native/ProcessEfficiency.cs' = '4d59a116497f00a161e61790eba9c48ceb6e196c512f5e97e8f7ec255187d60c'
}
$payloadInput = if ([string]::IsNullOrWhiteSpace($PayloadRoot)) {
  Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl\fan-host'
} else { $PayloadRoot }
$payloadFull = [IO.Path]::GetFullPath($payloadInput).TrimEnd('\')
if (-not (Test-Path -LiteralPath $payloadFull -PathType Container)) { throw "Fan Host mainline payload missing: $payloadFull" }

function Get-Sha256([string]$Path) {
  $sha = [Security.Cryptography.SHA256]::Create()
  try {
    $stream = [IO.File]::OpenRead($Path)
    try { return ([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-', '').ToUpperInvariant() }
    finally { $stream.Dispose() }
  } finally { $sha.Dispose() }
}

# Select exact approved component sources independently of unfinished live development.
$liveProjectRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot)).TrimEnd('\')
$componentSourceBundleRelative = 'deps/fan-host-source/candidate-canonical-lhm-20261024'
$componentSourceBundleManifestSha256 = '561129d604ccbfd20621737d290fad691b127d1ce99b0ffdf4110955972ac03c'
$componentBackendSha256 = 'fb2f9396d81594bf573d158c5c9e216a1abc5e3820ba088297262da84d845d60'
$selectedSourceRoot = if ([string]::IsNullOrWhiteSpace($SourceRoot)) { Join-Path $liveProjectRoot ($componentSourceBundleRelative + '/source') } else { $SourceRoot }
$sourceBundleVerifier = Join-Path $PSScriptRoot 'hc-device-runtime/verify-host-source-bundle.ps1'
if (-not (Test-Path -LiteralPath $sourceBundleVerifier -PathType Leaf)) { throw 'FAN_HOST_SOURCE_BUNDLE_VERIFIER_UNAVAILABLE' }
. $sourceBundleVerifier
$sourceSelection = Assert-YmccHostSourceBundle -SourceRoot $selectedSourceRoot -ExpectedManifestSha256 $componentSourceBundleManifestSha256 -ExpectedHostSha256 $expected['YeManFanHost.dll'] -ExpectedBackendSha256 $componentBackendSha256 -ExpectedSourcePins $compiledSourcePins -LiveSourceRoot $liveProjectRoot
$sourceProjectRoot = $sourceSelection.sourceRoot
Write-Output ('FAN_HOST_SOURCE_SELECTION_OK: ' + $sourceSelection.componentId)
Write-Output ('FAN_HOST_LIVE_KNOWN_INPUT_DRIFT: ' + @($sourceSelection.knownLiveInputDrift).Count)
# Reuse the exact project Include/default source closure and source pins on the selected archive.
$compiledObserved = @('FanLab/real-host/YeManFanHost.csproj','FanLab/fan-backend/YeManFanBackend.csproj','native/ProcessEfficiency.cs','FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs')
foreach ($relativeRoot in @('FanLab/real-host','FanLab/fan-backend')) {
  $sourceRoot = Join-Path $sourceProjectRoot $relativeRoot
  if (-not (Test-Path -LiteralPath $sourceRoot -PathType Container)) { throw ('FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + $relativeRoot) }
  foreach ($sourceFile in Get-ChildItem -LiteralPath $sourceRoot -Recurse -File -Filter '*.cs' | Where-Object { $_.FullName -notmatch '[\\/](bin|obj)[\\/]' }) {
    $compiledObserved += $sourceFile.FullName.Substring($sourceProjectRoot.Length + 1).Replace('\','/')
  }
}
foreach ($projectRelative in @('FanLab/real-host/YeManFanHost.csproj','FanLab/fan-backend/YeManFanBackend.csproj')) {
  $projectPath = Join-Path $sourceProjectRoot $projectRelative
  [xml]$projectXml = Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8
  foreach ($include in $projectXml.SelectNodes('//*[local-name()="Compile" and @Include]')) {
    $relativeInclude = [string]$include.Include
    if ($relativeInclude -match '[*?$]' -or [IO.Path]::IsPathRooted($relativeInclude)) { throw 'FAN_HOST_COMPILED_INCLUDE_UNSUPPORTED' }
    $includedPath = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $projectPath) $relativeInclude))
    if (-not $includedPath.StartsWith($sourceProjectRoot + '\', [StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($includedPath) -ine '.cs') { throw 'FAN_HOST_COMPILED_INCLUDE_UNSAFE' }
    $compiledObserved += $includedPath.Substring($sourceProjectRoot.Length + 1).Replace('\','/')
  }
}
if (Compare-Object @($compiledObserved | Sort-Object -Unique) @($compiledSourcePins.Keys | Sort-Object)) { throw 'FAN_HOST_COMPILED_SOURCE_CLOSURE_CHANGED' }
foreach ($relative in $compiledSourcePins.Keys) {
  $sourceFile = Join-Path $sourceProjectRoot $relative
  if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) { throw ('FAN_HOST_COMPILED_SOURCE_UNAVAILABLE: ' + $relative) }
  if ((Get-Sha256 $sourceFile) -cne ([string]$compiledSourcePins[$relative]).ToUpperInvariant()) { throw ('FAN_HOST_COMPILED_SOURCE_CHANGED: ' + $relative) }
}
Write-Output ('FAN_HOST_COMPILED_SOURCE_OK: ' + $compiledSourcePins.Count)

$manifestPath = Join-Path $payloadFull 'YeManFanHost.payload.json'
if ($expectedManifestSha256 -notmatch '^[0-9A-F]{64}$' -or (Get-Sha256 $manifestPath) -ne $expectedManifestSha256) { throw 'Fan Host HC Candidate payload manifest SHA-256 mismatch' }
$manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$manifest.schemaVersion -ne 2 -or @($manifest.files).Count -ne 9) { throw 'Fan Host mainline payload manifest schema/file count mismatch' }
$listed = @{}
foreach ($entry in $manifest.files) {
  $relative = [string]$entry.path
  if ([string]::IsNullOrWhiteSpace($relative) -or [IO.Path]::IsPathRooted($relative) -or $relative.IndexOfAny([char[]]'\/') -ge 0) { throw "unsafe manifest path: $relative" }
  if ($listed.ContainsKey($relative.ToLowerInvariant())) { throw "duplicate manifest path: $relative" }
  $path = Join-Path $payloadFull $relative
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "manifest file missing: $relative" }
  if ((Get-Sha256 $path) -ne ([string]$entry.sha256).ToUpperInvariant()) { throw "manifest file hash mismatch: $relative" }
  $listed[$relative.ToLowerInvariant()] = $true
}
foreach ($pair in $expected.GetEnumerator()) {
  $path = Join-Path $payloadFull $pair.Key
  if ((Get-Sha256 $path) -ne $pair.Value) { throw "R5-v9 pinned hash mismatch: $($pair.Key)" }
}
$runtimeRelative = [string]$manifest.runtimeManifest
if ([string]::IsNullOrWhiteSpace($runtimeRelative) -or $runtimeRelative -notmatch '(?i)^\.\.[\\/][^\\/]+(?:[\\/][^\\/]+)*[\\/]HandheldCompanion\.runtime\.json$' -or $runtimeRelative.Substring(3) -match '(^|[\\/])\.\.([\\/]|$)') { throw 'HC runtime manifest path is unsafe' }
$runtimeFull = [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $payloadFull) $runtimeRelative.Substring(3)))
$runtimeFull = Split-Path -Parent $runtimeFull
$runtimeManifestPath = Join-Path $runtimeFull 'HandheldCompanion.runtime.json'
if (-not (Test-Path -LiteralPath $runtimeManifestPath -PathType Leaf)) { throw 'HC shared runtime manifest missing' }
$runtimeManifest = Get-Content -LiteralPath $runtimeManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
if ([int]$runtimeManifest.schemaVersion -ne 1 -or $runtimeManifest.runtimeId -ne [string]$manifest.runtimeId) { throw 'HC shared runtime manifest identity mismatch' }
$runtimeListed=@{}
foreach ($entry in $runtimeManifest.files) { $rp=[string]$entry.path; $rpath=Join-Path $runtimeFull $rp; if([IO.Path]::IsPathRooted($rp) -or $rp -match '(^|[\\/])\.\.([\\/]|$)' -or !(Test-Path -LiteralPath $rpath -PathType Leaf) -or (Get-Sha256 $rpath) -ne ([string]$entry.sha256).ToUpperInvariant()){throw "HC shared runtime hash/missing mismatch: $rp"}; $runtimeListed[$rp.ToLowerInvariant()]=$true }
$sharedListed = @{}
if ($runtimeManifest.PSObject.Properties['sharedDependencies']) {
  if ([string]$runtimeManifest.sharedDependencies.directory -cne 'PowerControl/ymcc-shared-runtime' -or @($runtimeManifest.sharedDependencies.files).Count -ne 7) { throw 'HC shared dependency contract is invalid.' }
  $sharedRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $runtimeFull)) 'ymcc-shared-runtime'
  foreach ($entry in @($runtimeManifest.sharedDependencies.files)) {
    $name = [string]$entry.path
    if ($name -notmatch '^[^/\\:]+\.dll$' -or $sharedListed.ContainsKey($name.ToLowerInvariant()) -or $runtimeListed.ContainsKey($name.ToLowerInvariant())) { throw 'HC shared dependency is unsafe, duplicated or also local.' }
    $path = Join-Path $sharedRoot $name
    if (!(Test-Path -LiteralPath $path -PathType Leaf) -or (Get-Item -LiteralPath $path).Length -ne [int64]$entry.bytes -or
        (Get-Sha256 $path) -cne ([string]$entry.sha256).ToUpperInvariant()) { throw "HC shared dependency missing or changed: $name" }
    $sharedListed[$name.ToLowerInvariant()] = $true
  }
}
foreach ($required in @('HandheldCompanion.dll','HandheldCompanion.deps.json','LibreHardwareMonitorLib.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll','GamepadMotion.dll','hidapi.dll','hidapi.net.dll')) {
  if (!$runtimeListed.ContainsKey($required.ToLowerInvariant()) -and !$sharedListed.ContainsKey($required.ToLowerInvariant())) { throw "HC runtime required file missing: $required" }
}
$physicalNames = @(Get-ChildItem -LiteralPath $payloadFull -File -Force |
  Where-Object { $_.Name -ne 'YeManFanHost.payload.json' } |
  Select-Object -ExpandProperty Name | Sort-Object)
$manifestNames = @($manifest.files | ForEach-Object { [string]$_.path } | Sort-Object)
if (Compare-Object $physicalNames $manifestNames) {
  throw 'Fan Host mainline payload has files outside or missing from manifest'
}
$auth = Get-Content -LiteralPath (Join-Path $payloadFull 'YeManFanHost.authorization.md') -Raw -Encoding UTF8
foreach ($marker in @(
  'implementationState: hc-candidate-0.32.4.0-fan-host-cp07-acceptance-binding-ui-free-single-owner-recovery',
  ('approvedHostExeSha256: ' + $expected['YeManFanHost.exe']),
  ('approvedHostDllSha256: ' + $expected['YeManFanHost.dll']),
  'approvedHcSha256: 08A386607A0E5E72C5FCAA98D3A6C0B3B256EB0FCA7D6547F6F124C1F1F33583',
  'rebaselineBasis: 2026-09-23 FAN-204 s31 addendum ruling - /api/resume accept separated from the background rebuild (resumeAccepted/resumePhase/generation/retryAfterMs, bounded 15 s state observation, no permanent 409, no second lifecycle owner)',
  'fan926Rebaseline: 2026-09-26 operator FAN-926 ruling - CP-07 (parent-exit acceptance binds the accepting host instance and the real recovery cycle BEFORE the acknowledgement'
)) { if (-not $auth.Contains($marker)) { throw "R5-v9 authorization marker missing: $marker" } }
Write-Output 'FAN_HOST_V2_OK'
Write-Output "Payload: $payloadFull"
Write-Output "Manifest SHA256: $expectedManifestSha256"
Write-Output "Manifest files: $(@($manifest.files).Count)"
foreach ($pair in $expected.GetEnumerator()) { Write-Output "$($pair.Key): $($pair.Value)" }



#   gate B (202/R-201-REPAIR): dll 399E4796... / manifest C3F92E83... (isolated chain rebuild; apphost unchanged)
