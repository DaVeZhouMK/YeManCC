function Assert-YmccExportSourceVersion([string]$ProjectRoot, [string]$ExpectedVersion, [string]$ExpectedCommit) {
  $manifest = Get-Content -LiteralPath (Join-Path $ProjectRoot 'version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $package = Get-Content -LiteralPath (Join-Path $ProjectRoot 'package.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $version = [string]$manifest.version
  if ($version -cnotmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$' -or $version -cne [string]$package.version) {
    throw 'EXPORT_VERSION_MISMATCH: version.json and package.json must contain the same strict version.'
  }
  if ($ExpectedVersion -and $version -cne $ExpectedVersion) {
    throw "EXPORT_VERSION_MISMATCH: expected $ExpectedVersion, source contains $version. Refresh the source snapshot."
  }
  if ($ExpectedCommit) {
    $actual = git -C $ProjectRoot rev-parse --verify 'HEAD^{commit}'
    if ($LASTEXITCODE -ne 0 -or $ExpectedCommit -cnotmatch '^[a-f0-9]{40}$' -or $actual -cne $ExpectedCommit) {
      throw "EXPORT_COMMIT_MISMATCH: expected $ExpectedCommit, found $actual. Refresh the source snapshot."
    }
  }
  return $version
}

function Assert-YmccExportPackageVersion([string]$WorkspaceRoot, [string]$ExpectedVersion) {
  Add-Type -AssemblyName System.IO.Compression.FileSystem
  $zipPath = Join-Path $WorkspaceRoot 'Release/Packages/YeManCC.zip'
  $external = Get-Content -LiteralPath (Join-Path $WorkspaceRoot 'Release/version.json') -Raw -Encoding UTF8 | ConvertFrom-Json
  $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash
  if ([string]$external.version -cne $ExpectedVersion -or [string]$external.sha256 -ine $zipHash) {
    throw 'EXPORT_VERSION_READBACK_FAILED: external manifest and complete ZIP differ.'
  }
  $zip = [IO.Compression.ZipFile]::OpenRead($zipPath)
  $temp = Join-Path ([IO.Path]::GetTempPath()) ('ymcc-version-' + [guid]::NewGuid().ToString('N') + '.exe')
  try {
    $entries = @{}
    foreach ($entry in $zip.Entries) {
      $name = $entry.FullName.Replace('\','/')
      if ($entries.ContainsKey($name)) { throw "EXPORT_VERSION_READBACK_FAILED: duplicate archive entry: $name" }
      $entries[$name] = $entry
    }
    function Read-PackageText([string]$Path) {
      if (!$entries.ContainsKey($Path)) { throw "EXPORT_VERSION_READBACK_FAILED: missing $Path" }
      $reader = [IO.StreamReader]::new($entries[$Path].Open())
      try { $reader.ReadToEnd() } finally { $reader.Dispose() }
    }
    $manifestFields = [ordered]@{
      'YeManCC/version.json' = 'version'
      'YeManCC/update-manifest.json' = 'packageVersion'
      'YeManCC/module-package.json' = 'packageVersion'
      'YeManCC/export-identity.json' = 'version'
    }
    foreach ($path in $manifestFields.Keys) {
      $manifest = Read-PackageText $path | ConvertFrom-Json
      if ([string]$manifest.($manifestFields[$path]) -cne $ExpectedVersion) {
        throw "EXPORT_VERSION_READBACK_FAILED: $path does not declare $ExpectedVersion."
      }
    }
    $frontend = [regex]::Matches((Read-PackageText 'YeManCC/index.html'), '<script\b(?=[^>]*\btype="module")[^>]*\bsrc="\./(assets/main-[A-Za-z0-9_-]+\.js)"[^>]*>')
    if ($frontend.Count -ne 1) { throw 'EXPORT_VERSION_READBACK_FAILED: cannot identify the frontend entry.' }
    $frontendPath = 'YeManCC/' + $frontend[0].Groups[1].Value
    if (!(Read-PackageText $frontendPath).Contains('"' + $ExpectedVersion + '"')) {
      throw "EXPORT_VERSION_READBACK_FAILED: frontend entry does not contain $ExpectedVersion."
    }
    if (!$entries.ContainsKey('YeManCC/YeManCC.exe')) { throw 'EXPORT_VERSION_READBACK_FAILED: missing YeManCC.exe.' }
    [IO.Compression.ZipFileExtensions]::ExtractToFile($entries['YeManCC/YeManCC.exe'], $temp)
    $exe = [Diagnostics.FileVersionInfo]::GetVersionInfo($temp)
    $fileNumeric = "$($exe.FileMajorPart).$($exe.FileMinorPart).$($exe.FileBuildPart).$($exe.FilePrivatePart)"
    $productNumeric = "$($exe.ProductMajorPart).$($exe.ProductMinorPart).$($exe.ProductBuildPart).$($exe.ProductPrivatePart)"
    if ($exe.FileVersion -cne $ExpectedVersion -or $exe.ProductVersion -cne $ExpectedVersion -or
        $fileNumeric -cne "$ExpectedVersion.0" -or $productNumeric -cne "$ExpectedVersion.0") {
      throw "EXPORT_VERSION_READBACK_FAILED: executable file/product version is $($exe.FileVersion)/$($exe.ProductVersion), expected $ExpectedVersion."
    }
    $hid = @($entries.Keys | Where-Object { $_ -match '(^|/)HIDMaestro\.Core\.dll$' })
    if ($hid.Count -ne 1 -or $hid[0] -cne 'PowerControl/feature-assets/virtual-gamepad/HIDMaestro.Core.dll' -or
        @($entries.Keys | Where-Object { $_ -match '(^|/)ymcc-input-payload\.zip$' }).Count) {
      throw 'EXPORT_VERSION_READBACK_FAILED: duplicate or legacy input payload layout.'
    }
    Write-Host "EXPORT_VERSION_READBACK_OK: $ExpectedVersion ($fileNumeric)"
    [ordered]@{ version=$ExpectedVersion; zipSha256=$zipHash; exeFileVersion=$exe.FileVersion; exeProductVersion=$exe.ProductVersion;
      exeNumericVersion=$fileNumeric; exeProductNumericVersion=$productNumeric; frontendEntry=$frontendPath; inputDll=$hid[0] }
  } finally {
    $zip.Dispose()
    if (Test-Path -LiteralPath $temp -PathType Leaf) { Remove-Item -LiteralPath $temp -Force }
  }
}
