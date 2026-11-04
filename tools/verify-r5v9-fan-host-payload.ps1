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
  'YeManFanHost.exe' = 'F1163D7AAFACAA376E32D6AB14F10DD1B3C51380FF9BE7B392573F9AEBA77C0E'
  'YeManFanHost.dll' = 'FCF9152B7C6A47B14150A130C31A8E7F1B2420306E9FC079E7435B9DB52C8651'
}
# Keep this file pure ASCII: PS 5.1 misdecodes non-ASCII scripts without BOM
# and drops the next statement (verified 2026-09-11); additionally the
# build-workspace pre-gate auto-verifies this chain.
$expectedManifestSha256 = '00d9aa83d3d035bffe93293b43342ca81b06be325514b4b1e8f77b70b168d46f'
# Installed artifact98/d8: exact fixed fan-batch C# inputs and two projects; broader39-source archive is independently pinned.
$compiledSourcePins = [ordered]@{
  'capabilities/AsusFanCapabilitySession.cs' = '45a0e2ef7d907f82c2a8a818a05db4202750800cdde6e8d1600862788b173dee'
  'capabilities/FanCapabilityCatalog.cs' = '025b63ba0fe495ada8709ee8e1f0805d2ca83d53141ae4d3a0a9130f1911dcfd'
  'capabilities/HcDeviceCapabilityCatalog.cs' = 'eeae40ef0a3b2c8065f79d74479086a01ea0a54806eda6528426c627896e40b8'
  'capabilities/HcDeviceCapabilityCatalog.generated.cs' = '718d9b0340bec0ff81d4ebff8e1d96e3724d2a362b8169ca72788bf92002b674'
  'capabilities/HcLightingCapabilityCatalog.cs' = '0f5d5a6d2af9aa5a2d171c32d1f397233c44821a0dbe759fc9485f03858e6924'
  'capabilities/HcLightingCapabilityCatalog.generated.cs' = '8436d9b00d45a2a463bf994fcc1c276fb71a49446acf21a721ad54627cdb37f8'
  'capabilities/PinnedFanSourceCatalog.cs' = 'e390f8fef9d9a33232bae9ba0bce19cb6f93ef266057cf89e953805d68843ebc'
  'FanLab/DeviceCoreSharedDependencies.cs' = '6bc3ea2958aad1238caa399a82249404717987f50fd6bfcbf91a56954df62c66'
  'FanLab/fan-backend/Asus/AsusFanBackendController.cs' = '8cae7c7611fb46686d3b5a4d2c7be6ead9ee4c3b17955e9ec0c00afdd222d303'
  'FanLab/fan-backend/Asus/AsusFanBackendReceipts.cs' = '2f00a3f2178d5713b73696595533bbb5df89bce41e2f7693bb1b12454af5afe2'
  'FanLab/fan-backend/Asus/AsusFanBackendTransport.cs' = 'cbf4100fbbc5fe412874366540d81bd07106eea8b65342d2c04146d136d851de'
  'FanLab/fan-backend/Asus/AsusFanCurveWriter.cs' = '14b09f20ee7ea896ad825ab9fa3c06b135cd24673c07ae15608a50fb4bb03f56'
  'FanLab/fan-backend/PinnedSourceFanCurve.cs' = 'd8bfdbb2abf64fd7c3fe7ae227b5809dfdfeebfbad8726cb62353c9c0e1b9966'
  'FanLab/fan-backend/PinnedSourceFanSession.cs' = 'e6f6ed0b22336cce212b2f32a047d50623aa8751733ee0e9f6bf8cd8acb3c7d0'
  'FanLab/fan-backend/YeManFanBackend.csproj' = 'e473c22939eef91700ee556bacbae0c1b2fbfaf54006b48ef1da6403baa87f30'
  'FanLab/real-host/DeferredHostSuccessor.cs' = '70caf39e6c44d6c557c8d26d58638760b3a139fe3887855908d861a4dcd7a29f'
  'FanLab/real-host/FanCapabilityPolicy.cs' = '9a97242787f0e9234b0e86d37e282e464655288d3729b8f57680b51299a1416b'
  'FanLab/real-host/FanProfileReturnState.cs' = '69243953ae3ee1987831e7c631a09b22affd9a02807260937fa9f2b15135cdb4'
  'FanLab/real-host/G1FanContractAdapter.cs' = '8bfca4361e8f45fc2f3dac6f9265af93a91a7e8641115073d819582b0fe73b59'
  'FanLab/real-host/HostProcessLifetime.cs' = 'bbbb1b224508b32b394c384a87c3c8472ea5a8d0d51386f8677fcab896831040'
  'FanLab/real-host/MiniAmdFanContractAdapter.cs' = 'b36988fe0201f628be43d63585abeaef5dfe210ecda5e233ecf77d968d861eca'
  'FanLab/real-host/MsiFamilyFanCurve.cs' = 'b5021e27129957de14798cd934205c208a58d9c791ed996601cc845ad1c27d83'
  'FanLab/real-host/MsiFamilyFanCurveSelfTest.cs' = 'f697aa7385ae763b03938e69b03021c34d448627c337d5b6f334a79258fc7392'
  'FanLab/real-host/MsiFanDeviceLifecycle.cs' = 'c6a21a708269b31284b0cb9f867d53bc4dd16efa85cf448b41b7239f3bedca13'
  'FanLab/real-host/MsiWmiClassRegistration.cs' = 'a0eebef2e4df02a723e142bcb4a0707e6e013d4606ec20907c43b47d17e6c605'
  'FanLab/real-host/MsiWmiPayloadResource.cs' = '39f025987159eb5ac4a236d343d13f56e844e13fc0e320af5a0f9d87f035f883'
  'FanLab/real-host/OneXFanDeviceLifecycle.cs' = '16c9f1f5546340329a6270a05e4d1dfe4206b38a5157d5c2fb7c8a4c89cbf549'
  'FanLab/real-host/PinnedSourceFanConsumer.cs' = 'e684d9f1cf198a03fae716a3b1d4b339ad19d0fb0807cdec9b25a19b7dca28dc'
  'FanLab/real-host/PinnedSourceFanEngine.cs' = 'b73a50b1c12f427bd8db5a4a207d6cfc031b024a9040f1f6f2cb3da288931adf'
  'FanLab/real-host/PinnedSourceFanSelection.cs' = '628b0cf4d338cb2ec3b41d4b628c583a7d068aa5a23abc589e6cef7ab1680b8a'
  'FanLab/real-host/PinnedSourceFanTransports.cs' = '08f04dbce67593f440d03dfc25778a07ff5723bb3180550ef42eca9507610a5f'
  'FanLab/real-host/PinnedUmaEcFanTransport.cs' = '1f2f32091472402ccd97afffcb9541f3b05d9eefcd51e5c39e9494aff7bc81ba'
  'FanLab/real-host/Program.cs' = '38647018c5f4bc2b179b82b2972a28c6c4db31379b85dc5b0ce4d5d8cd1e8e0a'
  'FanLab/real-host/Section18CandidateFakes.cs' = 'ef35d49c85f1ff17a3be2452e7227fa5c4a13096d0afc2ff7463d313647bd7c6'
  'FanLab/real-host/Section18CandidateLifecycle.cs' = '10d8841234bebe348d5b3d4c7d98a50ea2c869b51828077d9cc287fad7a556ca'
  'FanLab/real-host/Section19CandidateLifecycle.cs' = '4715f9d7722d8b73c9f95cd7a5ee46c136e5d3265e5e4ba5d05efab99e453272'
  'FanLab/real-host/Section20CandidateLifecycle.cs' = '0f58e93da6e3fa2ef14916219cf0fe71b7defa0e18bf5727fe52369f80a404fa'
  'FanLab/real-host/Section21CandidateLifecycle.cs' = '5ba1da1fddcbb07b70ae57229c27561c7af8cb2f26249a12d72f5b50bc28b278'
  'FanLab/real-host/Section22CandidateLifecycle.cs' = '3ee412a754824439899c95da855ff66980416534cb29c6023257097891032dca'
  'FanLab/real-host/Section23CandidateLifecycle.cs' = '5e0b4ffa33aeece951d79e07d1339786ca1233991caf99e5773f164a97ac3891'
  'FanLab/real-host/YeManFanHost.csproj' = '763fa30796f9251e5f16836ae80938d56b2d10160c3d072248f221823a0e8e66'
  'FanLab/RuntimeDependencyContract.cs' = '2b4c7769abb6ba97c3ffe2baae474aea27aa92a3b019ee291a98ba14b97af16b'
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
$componentSourceBundleRelative = 'deps/fan-host-source/candidate-onex-superx-uma-20261104'
$componentSourceBundleManifestSha256 = '296687d4f179cd228d73edd68ae6fb6e33a0331880c64c39f9ab1706777eb867'
$componentBackendSha256 = 'f59113d094e1d5c8d15472ce7bef7b93c37a9bbd2b60db96b0520eaf94df3429'
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
