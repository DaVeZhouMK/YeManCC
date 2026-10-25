# Verify the exact approved device-core file closure without requiring upstream binaries.
function Assert-YmccDeviceCoreRuntime {
  [CmdletBinding()]
  param([Parameter(Mandatory=$true)][string]$ProjectRoot, [string]$RuntimeRoot = '')
  $profilePath = Join-Path $ProjectRoot 'deps\ymcc-device-core-runtime\profile.json'
  $indexPath = Join-Path $ProjectRoot 'deps\ymcc-device-core-runtime\upstream.runtime.json'
  $profile = Get-Content -LiteralPath $profilePath -Raw -Encoding UTF8 | ConvertFrom-Json
  $hash = { param([string]$File) (Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant() }
  if ([int]$profile.schemaVersion -ne 1 -or (& $hash $indexPath) -cne [string]$profile.sourceManifestSha256) { throw 'Device-core upstream index is unapproved.' }
  $original = Get-Content -LiteralPath $indexPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$original.runtimeId -cne [string]$profile.sourceRuntimeId) { throw 'Device-core upstream identity mismatch.' }
  if (-not $RuntimeRoot) { $RuntimeRoot = Join-Path $ProjectRoot ('PowerControl\handheldcompanion-runtime\' + $profile.runtimeId) }
  $RuntimeRoot = [IO.Path]::GetFullPath($RuntimeRoot).TrimEnd('\')
  if (-not (Test-Path -LiteralPath $RuntimeRoot -PathType Container)) { throw "Device-core runtime missing: $RuntimeRoot" }
  $safeName = {
    param([string]$Name)
    if ([string]::IsNullOrWhiteSpace($Name) -or $Name.Contains('\') -or [IO.Path]::IsPathRooted($Name) -or $Name.Contains(':') -or $Name -match '(^|/)\.\.?(/|$)' -or $Name.Contains('//')) { throw "Unsafe device-core entry: $Name" }
    return $Name
  }
  $expected = @{}
  foreach ($item in $original.files) {
    $relative = & $safeName ([string]$item.path).Replace('\','/')
    if ($expected.ContainsKey($relative) -or [string]$item.sha256 -notmatch '^[0-9a-fA-F]{64}$') { throw "Invalid original device-core entry: $relative" }
    $expected[$relative] = ([string]$item.sha256).ToLowerInvariant()
  }
  # Every declared replacement is accepted only at its profile-pinned size and hash.
  # Files without a replacement still retain their frozen upstream identity.
  $modified = @{}
  foreach ($item in $profile.replacements) {
    $relative = & $safeName ([string]$item.path)
    if (-not $expected.ContainsKey($relative) -or $modified.ContainsKey($relative) -or [string]$item.sha256 -notmatch '^[0-9a-f]{64}$') { throw "Invalid device-core replacement: $relative" }
    $expected[$relative] = [string]$item.sha256
    $modified[$relative] = [int64]$item.bytes
  }
  foreach ($item in $profile.exclusions) {
    $relative = & $safeName ([string]$item.path)
    if (-not $expected.ContainsKey($relative) -or $modified.ContainsKey($relative)) { throw "Invalid device-core exclusion: $relative" }
    $expected.Remove($relative)
  }
  $manifestPath = Join-Path $RuntimeRoot 'HandheldCompanion.runtime.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([int]$manifest.schemaVersion -ne 1 -or [string]$manifest.runtimeId -cne [string]$profile.runtimeId -or [string]$manifest.upstreamRuntimeId -cne [string]$profile.sourceRuntimeId -or [string]$manifest.upstreamManifestSha256 -cne [string]$profile.sourceManifestSha256 -or [string]$manifest.deviceCoreProfileSha256 -cne (& $hash $profilePath) -or $manifest.modifiedByYmcc -ne $true) { throw 'Device-core derived manifest identity mismatch.' }
  if ($profile.PSObject.Properties['sharedDependencies']) {
    $shared = $profile.sharedDependencies
    if ([string]$shared.directory -cne 'PowerControl/ymcc-shared-runtime' -or
        [string]$shared.manifestSha256 -notmatch '^[0-9a-f]{64}$' -or @($shared.files).Count -ne 7 -or
        ($manifest.sharedDependencies | ConvertTo-Json -Depth 30 -Compress) -cne ($shared | ConvertTo-Json -Depth 30 -Compress)) {
      throw 'Device-core shared dependency contract mismatch.'
    }
    $sharedRoot = Join-Path $ProjectRoot 'deps\ymcc-shared-runtime'
    $sharedManifest = Join-Path $sharedRoot 'manifest.json'
    if (-not (Test-Path -LiteralPath $sharedManifest -PathType Leaf) -or (& $hash $sharedManifest) -cne [string]$shared.manifestSha256) {
      throw 'Device-core shared source manifest missing or changed.'
    }
    $sharedSource = Get-Content -LiteralPath $sharedManifest -Raw -Encoding UTF8 | ConvertFrom-Json
    $sharedSeen = @{}
    foreach ($item in @($shared.files)) {
      $relative = [string]$item.path
      $source = @($sharedSource.files | Where-Object { $_.packagedPath -ceq $relative })
      $upstream = @($original.files | Where-Object { $_.path -ceq $relative })
      if ($relative -notmatch '^[^/\\:]+\.dll$' -or $sharedSeen.ContainsKey($relative) -or $expected.ContainsKey($relative) -or
          $source.Count -ne 1 -or $upstream.Count -ne 1 -or [string]$source[0].path -cne ('bin/' + $relative) -or
          [string]$item.sha256 -cne [string]$source[0].sha256 -or [string]$item.sha256 -cne [string]$upstream[0].sha256 -or
          [int64]$item.bytes -ne [int64]$source[0].bytes) { throw "Invalid shared device dependency: $relative" }
      $sharedSeen[$relative] = $true
      $binary = Join-Path $sharedRoot ('bin\' + $relative)
      if (-not (Test-Path -LiteralPath $binary -PathType Leaf) -or (Get-Item -LiteralPath $binary).Length -ne [int64]$item.bytes -or
          (& $hash $binary) -cne [string]$item.sha256) { throw "Shared device dependency missing or changed: $relative" }
    }
    if ($sharedSeen.Count -ne @($sharedSource.files | Where-Object { !$_.PSObject.Properties['packagedDirectory'] -or [string]$_.packagedDirectory -ceq [string]$sharedSource.packagedDirectory }).Count) { throw 'Shared device dependency set incomplete.' }
  }
  $declared = @{}
  foreach ($item in $manifest.files) {
    $relative = & $safeName ([string]$item.path)
    if ($declared.ContainsKey($relative) -or -not $expected.ContainsKey($relative) -or [string]$item.sha256 -cne $expected[$relative]) { throw "Device-core manifest closure mismatch: $relative" }
    $declared[$relative] = $true
  }
  if ($declared.Count -ne $expected.Count) { throw 'Device-core manifest omits approved files.' }
  $actual = @{}
  [int64]$total = 0
  foreach ($entry in Get-ChildItem -LiteralPath $RuntimeRoot -Recurse -Force) {
    if ($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw "Device-core reparse entry rejected: $($entry.FullName)" }
    if ($entry.PSIsContainer) { continue }
    $relative = $entry.FullName.Substring($RuntimeRoot.Length + 1).Replace('\','/')
    if ($relative -ceq 'HandheldCompanion.runtime.json') { continue }
    if (-not $expected.ContainsKey($relative) -or (& $hash $entry.FullName) -cne $expected[$relative]) { throw "Device-core unexpected, missing or changed file: $relative" }
    if ($modified.ContainsKey($relative) -and $entry.Length -ne $modified[$relative]) { throw "Device-core replacement size mismatch: $relative" }
    $actual[$relative] = $true
    $total += $entry.Length
  }
  if ($actual.Count -ne $expected.Count) { throw 'Device-core payload file count mismatch.' }
  $derivativeDlls = @($modified.Keys | Where-Object { $_ -match '\.dll$' } | Sort-Object)
  $originalDlls = @($actual.Keys | Where-Object { $_ -match '\.dll$' -and -not $modified.ContainsKey($_) })
  return [pscustomobject]@{ status='PASS'; runtimeId=[string]$profile.runtimeId; files=$actual.Count + 1; fileBytes=$total; manifestSha256=(& $hash $manifestPath); physicalDeviceVerified=$false; ordinaryDllsComparedToFrozenUpstream=$originalDlls.Count; specialDerivativeDlls=$derivativeDlls; declaredDerivativeDlls=$derivativeDlls; profilePinnedDlls=$originalDlls.Count + $derivativeDlls.Count }
}
