# This body is appended after the embedded maintenance functions by packaging.
# Only module files are copied. No driver installer, device operation or reboot.
$bootstrapLease=$null; $bootstrapStage=$null; $bootstrapMutationStarted=$false
try {
  $root=Resolve-YmccDeckyPath $InstallRoot
  $pc=Resolve-YmccDeckyPath $root 'PowerControl'
  $manifestPath=Resolve-YmccDeckyPath $root 'YeManCC/module-package.json'
  if (([IO.FileInfo]$manifestPath).Length -gt 4194304) { throw 'MODULE_MANIFEST_TOO_LARGE' }
  $manifest=Get-Content -LiteralPath $manifestPath -Raw -Encoding UTF8 | ConvertFrom-Json
  Assert-YmccModuleManifest $manifest
  $bootstrapLease=Enter-YmccModuleMaintenance -WaitMilliseconds 10000
  $postCommitStage=Register-YmccPostCommitHcCleanup $root $pc $manifest $manifestPath $UpdateCommitState $UpdateOwnerPid
  if (!(Test-YmccBootstrapPayloadCurrent $pc $manifest)) {
    if($manifest.compatibility.sourceKind -cne 'full-package-v1' -or $manifest.compatibility.payloadHashScope -cne 'manifest-files-v1'){throw 'MODULE_FULL_PACKAGE_SOURCE_CONTRACT_REQUIRED'}
    if([string]::IsNullOrWhiteSpace($SourceArchive)){$SourceArchive=Join-Path $root 'Packages/YeManCC.zip'}
    $sourceFull=[IO.Path]::GetFullPath($SourceArchive)
    $archive=Resolve-YmccDeckyPath ([IO.Path]::GetDirectoryName($sourceFull)) ([IO.Path]::GetFileName($sourceFull))
    if(![IO.File]::Exists($archive) -or ([IO.FileInfo]$archive).Length -gt 2147483648){throw 'MODULE_SOURCE_FULL_PACKAGE_REQUIRED: retain or re-download the complete YMCC package'}
    $bootstrapStage=Join-Path $root ('module-bootstrap-'+[guid]::NewGuid().ToString('N'))
    $safeStage=Resolve-YmccDeckyPath $root ([IO.Path]::GetFileName($bootstrapStage))
    if ($safeStage -ine $bootstrapStage) { throw 'MODULE_BOOTSTRAP_PATH_INVALID' }
    [IO.Directory]::CreateDirectory($bootstrapStage) | Out-Null
    Write-YmccBootstrapStage $bootstrapStage $pc ([string]$manifest.packageVersion) ([string]$manifest.compatibility.payloadSha256) 'preparing' @($manifest.files.path)
    Add-Type -AssemblyName System.IO.Compression, System.IO.Compression.FileSystem
    $zip=[IO.Compression.ZipFile]::OpenRead($archive)
    try {
      if($zip.Entries.Count -gt 50000){throw 'MODULE_SOURCE_ARCHIVE_ENTRY_LIMIT'}
      $expected=@{};foreach($file in $manifest.files){$expected['PowerControl/'+$file.path]=$file}
      $seen=@{};$all=@{};$sourceManifest=$null
      foreach($entry in $zip.Entries){
        $name=$entry.FullName.Replace('\','/').TrimEnd('/')
        if(!$name -or $name -match '(^/|:|//|(^|/)\.\.?(?:/|$)|[\x00-\x1f]|[. ]$)' -or $all.ContainsKey($name) -or ($entry.ExternalAttributes -band 0xF0000000) -eq 0xA0000000){throw 'MODULE_SOURCE_ENTRY_UNSAFE_OR_DUPLICATE'}
        $all[$name]=$true
        if($name -ceq 'YeManCC/module-package.json'){$sourceManifest=$entry}
      }
      if(!$sourceManifest -or $sourceManifest.Length -gt 4194304){throw 'MODULE_SOURCE_MANIFEST_MISSING'}
      $stream=$sourceManifest.Open();$sha=[Security.Cryptography.SHA256]::Create()
      try{$sourceManifestHash=([BitConverter]::ToString($sha.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose();$stream.Dispose()}
      if($sourceManifestHash -cne (Get-YmccDeckyHash $manifestPath)){throw 'MODULE_SOURCE_MANIFEST_MISMATCH'}
      foreach($entry in $zip.Entries){
        $name=$entry.FullName.Replace('\','/')
        if(!$entry.Name -or !$expected.ContainsKey($name)){continue}
        $file=$expected[$name]
        if($name -cne ('PowerControl/'+$file.path) -or $seen.ContainsKey($file.path) -or $entry.Length -ne $file.bytes){throw 'MODULE_SOURCE_ENTRY_INVALID'}
        $seen[$file.path]=$true
        $destination=Resolve-YmccDeckyPath $bootstrapStage $file.path
        [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($destination)) | Out-Null
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entry,$destination,$false)
      }
      if($seen.Count -ne $expected.Count){throw 'MODULE_SOURCE_ENTRY_MISSING'}
    }finally{$zip.Dispose()}
    $payloadRoot=Resolve-YmccDeckyPath $bootstrapStage 'payload'
    [IO.Directory]::CreateDirectory($payloadRoot) | Out-Null
    Move-Item -LiteralPath (Join-Path $bootstrapStage 'feature-assets') -Destination $payloadRoot
    $plan=Get-YmccModulePlan $payloadRoot $pc $manifest ([string]$manifest.packageVersion)
    Write-YmccBootstrapStage $bootstrapStage $pc ([string]$manifest.packageVersion) ([string]$manifest.compatibility.payloadSha256) 'applying' @($manifest.files.path)
    $bootstrapMutationStarted=$true
    $transaction=Install-YmccModulePackage $plan (Join-Path $bootstrapStage 'rollback')
    if (!(Test-YmccBootstrapPayloadCurrent $pc $manifest)) { throw 'MODULE_BOOTSTRAP_FINAL_HASH_MISMATCH' }
    Write-YmccBootstrapStage $bootstrapStage $pc ([string]$manifest.packageVersion) ([string]$manifest.compatibility.payloadSha256) 'committed' @($manifest.files.path)
  }
  # A successful retry retires known interrupted stages, even when all target
  # files were already committed before the previous helper was terminated.
  Sync-YmccBootstrapReceipt $pc $manifest
  Complete-YmccPostCommitHcCleanup $root $pc $manifest $postCommitStage $UpdateOwnerPid
  $cleanup=Remove-YmccBootstrapStages $root $pc
  Write-Output ('MODULE_PAYLOAD_READY cleanedStages='+$cleanup.removed)
 } finally {
  try {
    # A validation/extraction failure has not changed the installed module.
    # Retire only this newly owned stage; uncertain mutation backups survive.
    if ($bootstrapStage -and !$bootstrapMutationStarted -and (Test-YmccBootstrapStageOwned $bootstrapStage $pc)) {
      $safeFailedStage=Resolve-YmccDeckyPath $root ([IO.Path]::GetFileName($bootstrapStage))
      if ($safeFailedStage -ine $bootstrapStage -or !$safeFailedStage.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'MODULE_BOOTSTRAP_CLEANUP_PATH_INVALID' }
      try { $null=Remove-YmccBootstrapStage $safeFailedStage $pc } catch { }
    }
  } finally { Exit-YmccModuleMaintenance $bootstrapLease }
}
