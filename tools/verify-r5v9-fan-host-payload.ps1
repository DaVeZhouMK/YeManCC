[CmdletBinding()]
param(
  [string]$PayloadRoot = ''
)

$ErrorActionPreference = 'Stop'
# 2026-09-21 re-base (920 v1.15 section 26.3.4, isolated release candidate): the payload now
# carries the candidate Host built from the locked source with the pinned HC identity moved to
# the approved candidate HandheldCompanion.dll (0c5132a9...). The verification below is
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
  'YeManFanHost.exe' = 'E33CB1604B5DC98AC265A51E1DC867DF4E742F6966ADE9F3E6DA48DFF4890BAA'
  'YeManFanHost.dll' = 'CC5E5B3FEB877E2B822B91B38BF7220918B8E4E633E558BD6E015A9DC310E1AB'
}
# Keep this file pure ASCII: PS 5.1 misdecodes non-ASCII scripts without BOM
# and drops the next statement (verified 2026-09-11); additionally the
# build-workspace pre-gate auto-verifies this chain.
$expectedManifestSha256 = '6da78d971f7112dc1b07fa041eb80ee240198919ad3ea5f10fe760bdb9fee1ac'
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
foreach ($required in @('HandheldCompanion.dll','HandheldCompanion.deps.json','LibreHardwareMonitorLib.dll','Microsoft.Windows.SDK.NET.dll','WinRT.Runtime.dll','GamepadMotion.dll','hidapi.dll','hidapi.net.dll')) { if(!$runtimeListed.ContainsKey($required.ToLowerInvariant())){throw "HC runtime required file missing: $required"} }
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
  'approvedHostExeSha256: E33CB1604B5DC98AC265A51E1DC867DF4E742F6966ADE9F3E6DA48DFF4890BAA',
  'approvedHostDllSha256: CC5E5B3FEB877E2B822B91B38BF7220918B8E4E633E558BD6E015A9DC310E1AB',
  'approvedHcSha256: 0C5132A9D13AEBFC5ADD2AA7C9AC54E8DAAA8EBC816A0C68D099BB9685DC2E49',
  'rebaselineBasis: 2026-09-23 FAN-204 s31 addendum ruling - /api/resume accept separated from the background rebuild (resumeAccepted/resumePhase/generation/retryAfterMs, bounded 15 s state observation, no permanent 409, no second lifecycle owner)',
  'fan926Rebaseline: 2026-09-26 operator FAN-926 ruling - CP-07 (parent-exit acceptance binds the accepting host instance and the real recovery cycle BEFORE the acknowledgement'
)) { if (-not $auth.Contains($marker)) { throw "R5-v9 authorization marker missing: $marker" } }
Write-Output 'FAN_HOST_V2_OK'
Write-Output "Payload: $payloadFull"
Write-Output "Manifest SHA256: $expectedManifestSha256"
Write-Output "Manifest files: $(@($manifest.files).Count)"
foreach ($pair in $expected.GetEnumerator()) { Write-Output "$($pair.Key): $($pair.Value)" }



#   gate B (202/R-201-REPAIR): dll 399E4796... / manifest C3F92E83... (isolated chain rebuild; apphost unchanged)
