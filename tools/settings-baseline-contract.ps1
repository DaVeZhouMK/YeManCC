# Immutable configuration contract; this file never reads installed user settings.
function Read-YmccSettingsBaselineContract([string]$ProjectRoot) {
  $manifestPath = Join-Path $ProjectRoot 'PowerControl\settings-baseline.json'
  $contract = Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($contract.formatVersion -ne 1 -or $contract.id -cne 'ymcc-settings' -or
      ($contract.revision -isnot [int] -and $contract.revision -isnot [long]) -or $contract.revision -lt 1 -or
      $contract.documentSchemaVersion -ne 1 -or $contract.stampKey -cne 'configurationBaseline' -or
      $contract.document.path -cne 'yeman-settings.json' -or $contract.document.backup -cne 'yeman-settings.json.bak' -or
      $contract.document.maxBytes -ne 4194304 -or
      $contract.defaultsFile -cne ('settings-defaults-v' + $contract.revision + '.json')) {
    throw 'SETTINGS_BASELINE_INVALID_CONTRACT'
  }
  $paths = @([string]$contract.document.path, [string]$contract.document.backup) +
    @($contract.legacyImports | ForEach-Object { [string]$_.path }) +
    @($contract.independentUserFiles | ForEach-Object { [string]$_.path; if ($_.PSObject.Properties['backup']) { [string]$_.backup } })
  foreach ($relative in $paths) {
    if (!$relative -or $relative -match '(^/|\\|:|\*|\?|[\x00-\x1f]|(^|/)\.\.?(/|$)|//|[. ]$)') { throw 'SETTINGS_BASELINE_UNSAFE_PATH' }
  }
  $defaults = Get-Content -LiteralPath (Join-Path $ProjectRoot ('PowerControl\' + $contract.defaultsFile)) -Raw -Encoding UTF8 | ConvertFrom-Json
  if ($defaults.schemaVersion -ne 1 -or $defaults.configurationBaseline.id -cne $contract.id -or
      $defaults.configurationBaseline.revision -ne $contract.revision -or !$defaults.configurationBaseline.legacyImported) {
    throw 'SETTINGS_BASELINE_DEFAULTS_MISMATCH'
  }
  foreach ($section in $contract.sections) {
    if (!$defaults.PSObject.Properties[[string]$section.name] -or $section.shape -cne 'object' -or
        $defaults.([string]$section.name) -isnot [pscustomobject]) { throw ('SETTINGS_BASELINE_SECTION_INVALID: ' + $section.name) }
  }
  return $contract
}
function Test-YmccMutableSettingsPath([string]$RelativePath, [object]$Contract) {
  $relative = $RelativePath.Replace('\','/').TrimStart('/')
  $paths = @([string]$Contract.document.path, [string]$Contract.document.backup) +
    @($Contract.legacyImports | ForEach-Object { [string]$_.path }) +
    @($Contract.independentUserFiles | ForEach-Object { [string]$_.path; if ($_.PSObject.Properties['backup']) { [string]$_.backup } })
  foreach ($owned in $paths) {
    if ($relative -ieq $owned -or $relative.StartsWith($owned + '/', [StringComparison]::OrdinalIgnoreCase) -or $relative.StartsWith($owned + '.bak', [StringComparison]::OrdinalIgnoreCase) -or
        $relative.StartsWith($owned + '.corrupt-', [StringComparison]::OrdinalIgnoreCase) -or
        $relative.StartsWith($owned + '.before-user-reset-', [StringComparison]::OrdinalIgnoreCase)) { return $true }
  }
  return $false
}
function Assert-YmccSettingsBaseline([string]$ProjectRoot, [string]$StagingPowerControl = '') {
  $contract = Read-YmccSettingsBaselineContract $ProjectRoot
  $source = Join-Path $ProjectRoot 'PowerControl'
  $manifestHash = (Get-FileHash -LiteralPath (Join-Path $source 'settings-baseline.json') -Algorithm SHA256).Hash
  $defaultsHash = (Get-FileHash -LiteralPath (Join-Path $source $contract.defaultsFile) -Algorithm SHA256).Hash
  if ($StagingPowerControl) {
    foreach ($name in @('settings-baseline.json', [string]$contract.defaultsFile)) {
      if (!(Test-Path -LiteralPath (Join-Path $StagingPowerControl $name) -PathType Leaf) -or
          (Get-FileHash -LiteralPath (Join-Path $StagingPowerControl $name) -Algorithm SHA256).Hash -cne
          (Get-FileHash -LiteralPath (Join-Path $source $name) -Algorithm SHA256).Hash) { throw ('SETTINGS_BASELINE_STAGING_DRIFT: ' + $name) }
    }
    $staging = [IO.Path]::GetFullPath($StagingPowerControl).TrimEnd('\')
    foreach ($item in @(Get-ChildItem -LiteralPath $staging -Recurse -Force)) {
      if ($item.Attributes -band [IO.FileAttributes]::ReparsePoint) { throw 'SETTINGS_BASELINE_STAGING_REPARSE' }
      if (Test-YmccMutableSettingsPath $item.FullName.Substring($staging.Length + 1) $contract) {
        throw ('SETTINGS_BASELINE_USER_DATA_IN_PACKAGE: ' + $item.FullName.Substring($staging.Length + 1))
      }
    }
  }
  return [ordered]@{ id=$contract.id; revision=$contract.revision; documentSchemaVersion=$contract.documentSchemaVersion;
    manifestPath='settings-baseline.json'; manifestSha256=$manifestHash; defaultsPath=$contract.defaultsFile;
    defaultsSha256=$defaultsHash; userData='preserve'; legacyImport='once-before-durable-baseline' }
}
