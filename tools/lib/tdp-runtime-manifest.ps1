# Versioned helper metadata is immutable payload, separate from the 21 binary assets.
function Get-YmccTdpManifestSourcePayloadRoot([string]$ProjectRoot, [string]$WorkspaceRoot) {
  $lock = Get-Content -LiteralPath (Join-Path $ProjectRoot 'tools\release-assets.lock.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $assetsRoot = Join-Path $WorkspaceRoot 'Assets'
  $complete = (Test-Path -LiteralPath $assetsRoot -PathType Container) -and
    (@($lock.files | Where-Object { -not (Test-Path -LiteralPath (Join-Path $assetsRoot ([string]$_.assetPath).Replace('/', '\')) -PathType Leaf) }).Count -eq 0)
  $entries = @($lock.files | Where-Object { $_.releasePath -ceq 'pawnio/YeManTdpCtl.exe' })
  if ($entries.Count -ne 1) { throw 'TDP_RUNTIME_MANIFEST_HELPER_LOCK_REQUIRED' }
  $selected = if ($complete) { Join-Path $assetsRoot ([string]$entries[0].assetPath).Replace('/', '\') } else { Join-Path $ProjectRoot ([string]$entries[0].fallbackPath).Replace('/', '\') }
  return (Split-Path -Parent $selected)
}
function Assert-YmccTdpRuntimeManifestPayload([string]$ManifestPath, [string]$PayloadRoot, [string]$SourceFile, [string]$RecipeFile) {
  if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { throw 'TDP_RUNTIME_MANIFEST_MISSING' }
  if ((Get-Item -LiteralPath $ManifestPath).Length -gt 1048576) { throw 'TDP_RUNTIME_MANIFEST_TOO_LARGE' }
  $manifest = Get-Content -LiteralPath $ManifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if (-not ($manifest.schemaVersion -is [int] -or $manifest.schemaVersion -is [long]) -or $manifest.schemaVersion -ne 1) { throw 'TDP_RUNTIME_MANIFEST_SCHEMA' }
  if ($manifest.helperSha256 -isnot [string] -or $manifest.helperSha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'TDP_RUNTIME_MANIFEST_HELPER_DIGEST' }
  if (-not (Test-Path -LiteralPath $SourceFile -PathType Leaf) -or -not (Test-Path -LiteralPath $RecipeFile -PathType Leaf)) { throw 'TDP_RUNTIME_MANIFEST_SOURCE_INPUT_MISSING' }
  if ($manifest.source.sha256 -ine (Get-FileHash -LiteralPath $SourceFile -Algorithm SHA256).Hash) { throw 'TDP_RUNTIME_MANIFEST_SOURCE_MISMATCH' }
  if ($manifest.recipe.sha256 -ine (Get-FileHash -LiteralPath $RecipeFile -Algorithm SHA256).Hash) { throw 'TDP_RUNTIME_MANIFEST_RECIPE_MISMATCH' }
  $names = @('YeManTdpCtl.exe', '_internal/base_library.zip', '_internal/python313.dll')
  $files = @($manifest.files)
  if ($files.Count -ne $names.Count) { throw 'TDP_RUNTIME_MANIFEST_FILE_SET' }
  $observed = @{}
  foreach ($file in $files) {
    if ($file.name -isnot [string] -or $names -cnotcontains $file.name -or $observed.ContainsKey([string]$file.name)) { throw 'TDP_RUNTIME_MANIFEST_FILE_NAME' }
    if ($file.sha256 -isnot [string] -or $file.sha256 -notmatch '^[a-fA-F0-9]{64}$') { throw 'TDP_RUNTIME_MANIFEST_FILE_DIGEST' }
    $path = Join-Path $PayloadRoot ([string]$file.name).Replace('/', '\')
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw ('TDP_RUNTIME_MANIFEST_PAYLOAD_MISSING:' + $file.name) }
    $actual = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ine [string]$file.sha256) { throw ('TDP_RUNTIME_MANIFEST_PAYLOAD_MISMATCH:' + $file.name) }
    $observed[[string]$file.name] = $actual
  }
  if ($manifest.helperSha256 -ine [string]$observed['YeManTdpCtl.exe']) { throw 'TDP_RUNTIME_MANIFEST_HELPER_MISMATCH' }
  return [pscustomobject]@{ ManifestPath = [IO.Path]::GetFullPath($ManifestPath); ManifestSha256 = (Get-FileHash -LiteralPath $ManifestPath -Algorithm SHA256).Hash.ToLowerInvariant(); HelperSha256 = $observed['YeManTdpCtl.exe']; PayloadRoot = [IO.Path]::GetFullPath($PayloadRoot) }
}
