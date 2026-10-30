# Fresh compile directories plus diagnostic validation prevent an MSVC internal
# compiler error from publishing a stale EXE when the driver reports exit zero.
function Assert-CustomSteamLibraryCompilation {
  param(
    [int]$ExitCode,
    [string[]]$OutputLines,
    [string]$CompilationDirectory,
    [string[]]$ExpectedExecutables
  )
  $errors = @($OutputLines | Where-Object { $_ -match '(?i)\b(?:fatal\s+)?error\s+(?:C\d+|LNK\d+|RC\d+|MSB\d+|D\d+)\b' })
  if ($ExitCode -ne 0 -or $errors.Count) {
    throw "Custom Steam Library compiler failed (exit=$ExitCode). Refusing stale artifacts. $($errors -join '; ')"
  }
  foreach ($exe in $ExpectedExecutables) {
    if (-not (Test-Path -LiteralPath (Join-Path $CompilationDirectory $exe) -PathType Leaf)) {
      throw "Custom Steam Library compiler did not produce a fresh executable: $exe"
    }
  }
}

# Keep the actual compiler inputs (including dependencies/resources) rather than
# hashing whatever happens to be on disk after compilation. A mid-build edit
# must fail closed instead of blessing a stale executable with a new fingerprint.
function Get-CustomSteamLibrarySourceIndex {
  param([string]$ProjectRoot, [ValidateSet('host','worker')][string]$Kind)
  $native = Join-Path $ProjectRoot 'native\custom-steam-library'
  $stem = if ($Kind -eq 'host') { 'workspace_host' } else { 'steam_artwork_lab' }
  $paths = @(
    "native/custom-steam-library/$stem.cpp",
    "native/custom-steam-library/$stem.rc",
    "native/custom-steam-library/build_$Kind.bat",
    "tools/build-custom-steam-library-$Kind.ps1",
    'tools/custom_steam_library_build_guard.ps1',
    'CustomSteamLibrary/assets/custom-steam-library.ico',
    'native/game_detection_blacklist_entries.h',
    'native/process_efficiency.h',
    'deps/json/json.hpp'
  )
  $paths += @(Get-ChildItem -LiteralPath $native -Filter '*.h' -File | ForEach-Object { 'native/custom-steam-library/' + $_.Name })
  if ($Kind -eq 'host') {
    $paths += @('native/opti_catalog_startup.h','native/webview_process_efficiency.h','native/shared_game_media.h','native/shared_game_media_capacity.h',
      'native/shared_game_media_download_queue.h','native/shared_game_media_download_worker.h','native/shared_game_media_download_resources.h',
      'native/shared_game_media_shortcut.h','native/shared_game_media_identity_worker.h',
      'deps/webview2/build/native/include/WebView2.h',
      'deps/webview2/build/native/include/WebView2EnvironmentOptions.h',
      'deps/webview2/build/native/x64/WebView2LoaderStatic.lib')
  }
  foreach ($relative in @($paths | Sort-Object -Unique)) {
    $path = Join-Path $ProjectRoot $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing $Kind build input: $relative" }
    [pscustomobject]@{path=$relative;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()}
  }
}

function Assert-CustomSteamLibrarySourceIndex {
  param([string]$ProjectRoot, [ValidateSet('host','worker')][string]$Kind, [object[]]$SourceIndex)
  $current = @(Get-CustomSteamLibrarySourceIndex -ProjectRoot $ProjectRoot -Kind $Kind)
  $declared = @{}
  foreach ($entry in $SourceIndex) {
    $key = [string]$entry.path
    if ($declared.ContainsKey($key) -or [string]$entry.sha256 -notmatch '^[a-fA-F0-9]{64}$') {
      throw "Invalid/duplicate $Kind source fingerprint: $key"
    }
    $declared[$key] = [string]$entry.sha256
  }
  if ($current.Count -ne $declared.Count) { throw "$Kind source input coverage drifted; rebuild the child from current mainline" }
  foreach ($entry in $current) {
    if (-not $declared.ContainsKey($entry.path) -or $declared[$entry.path] -ine $entry.sha256) {
      throw "$Kind source/build drift: $($entry.path); rebuild the child from current mainline"
    }
  }
}

function Assert-CustomSteamLibraryMainlineIdentity {
  param([string]$ProjectRoot)
  $root = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
  $gitRoot = [string](& git -C $root rev-parse --show-toplevel 2>$null)
  if ($LASTEXITCODE -ne 0 -or [IO.Path]::GetFullPath($gitRoot).TrimEnd('\') -ine $root) {
    throw 'Custom Steam Library export must come from its actual Git repository root'
  }
  $branch = [string](& git -C $root branch --show-current)
  # The ordinary exporter explicitly marks local test packages as unaccepted.
  # Preserve the actual Git-root and canonical Mainline-path checks below;
  # this exception never applies to full acceptance or GitHub release jobs.
  $localTestExport = $env:YEMAN_TEST_EXPORT -eq '1' -and $env:YEMAN_FULL_ACCEPTANCE -ne '1' -and $env:GITHUB_ACTIONS -ne 'true'
  if ($LASTEXITCODE -ne 0 -or ($branch -ne 'main' -and -not $localTestExport -and -not ($env:GITHUB_ACTIONS -eq 'true' -and -not $branch))) {
    throw "Custom Steam Library export requires main (or the release CI detached checkout), not $branch"
  }
  if ($env:GITHUB_ACTIONS -ne 'true' -and $root -notmatch '[\\/]Mainline[\\/]YeManCC-source[\\/]YeManCC$') {
    throw "Custom Steam Library export must use the canonical Mainline worktree: $root"
  }
}

function Assert-CustomSteamLibrarySourceFreshness {
  param([string]$ProjectRoot, [string]$PackageRoot)
  $manifestPath = Join-Path $PackageRoot 'package-manifest.json'
  $manifest = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([int]$manifest.backgroundMediaProtocol -ne 1 -or [int]$manifest.backgroundCatalogProtocol -ne 1 -or [int]$manifest.backgroundIdentityProtocol -ne 1 -or [int]$manifest.backgroundVideoQualityPolicy -lt 3 -or [int]$manifest.backgroundManualProtocol -ne 1 -or [int]$manifest.backgroundVideoSelectionProtocol -ne 1) { throw 'Child download/catalog/shortcut-identity capability is missing; rebuild the current library host before export' }
  $version = Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  if ([string]$manifest.packageVersion -ne [string]$version.version) { throw 'Child package version differs from current mainline' }
  foreach ($kind in @('host','worker')) {
    $name = if ($kind -eq 'host') { 'CustomSteamLibrary.exe' } else { 'SteamArtworkLab.exe' }
    $hash = (Get-FileHash -LiteralPath (Join-Path $PackageRoot $name) -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($hash -ine [string]$manifest.sourceBuild.$kind) { throw "Child $kind executable differs from its build fingerprint" }
    Assert-CustomSteamLibrarySourceIndex -ProjectRoot $ProjectRoot -Kind $kind -SourceIndex @($manifest.sourceBuild.($kind + 'Sources'))
  }
  return $manifest
}
