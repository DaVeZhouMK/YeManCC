Add-Type -AssemblyName System.IO.Compression
function Write-YmccInputCompatibilityPayload([string]$CompleteRoot, [string]$Version) {
  $pc=Join-Path $CompleteRoot 'PowerControl'
  # Public 035 backs up this user file only when a template entry is shipped.
  $playerTemplate=Resolve-YmccDeckyPath $CompleteRoot 'PowerControl/Sleep/player-blacklist.txt'
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($playerTemplate)) | Out-Null
  [IO.File]::WriteAllText($playerTemplate, '# Player exclusions: existing user content is preserved during upgrade.', [Text.UTF8Encoding]::new($false))
  $manifest=New-YmccModuleManifest $pc $Version
  $redist=Join-Path $pc 'redist'
  [IO.Directory]::CreateDirectory($redist) | Out-Null
  $script=Join-Path $redist 'ymcc-input-payload-apply.ps1'
  $text="param([Parameter(Mandatory=`$true)][string]`$InstallRoot,[string]`$SourceArchive='',[string]`$UpdateCommitState='',[int]`$UpdateOwnerPid=0)`n`$ErrorActionPreference='Stop'`n"
  $text += '$env:PSModulePath=(Join-Path $PSHOME "Modules")+";"+$env:PSModulePath'+"`n"
  $text += '[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); $OutputEncoding=[Console]::OutputEncoding'+"`n"
  # Emit the existing canonical HC cleanup functions, preserving one implementation.
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $PSScriptRoot '../native/updater_hc_runtime.ps1'),[ref]$tokens,[ref]$errors)
  if($errors.Count){throw 'HC_CLEANUP_SOURCE_PARSE_FAILED'}
  foreach($functionName in @('Assert-YmccHcTree','Remove-YmccSupersededHcRuntimes')){
    $definitions=@($ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -ceq $functionName},$true))
    if($definitions.Count -ne 1){throw ('HC_CLEANUP_SOURCE_AMBIGUOUS: '+$functionName)}
    $text+=$definitions[0].Extent.Text+"`n"
  }
  foreach ($name in @('decky_package_maintenance.ps1','module_package_maintenance.ps1','module_bootstrap_recovery.ps1','module_payload_bootstrap.ps1')) {
    $text += [IO.File]::ReadAllText((Join-Path $PSScriptRoot ('..\native\'+$name))).TrimStart([char]0xfeff) + "`n"
  }
  [IO.File]::WriteAllText($script,$text,[Text.UTF8Encoding]::new($true))
  # Module bytes occur only once in the complete package. The existing
  # bootstrap reuses that full package after035 commits its update.
  $obsolete=Resolve-YmccDeckyPath $CompleteRoot 'PowerControl/redist/ymcc-input-payload.zip'
  if([IO.File]::Exists($obsolete)){throw 'NESTED_INPUT_ARCHIVE_MUST_NOT_BE_STAGED'}
  [string[]]$rows=@($manifest.files|ForEach-Object{$_.path+'|'+$_.bytes+'|'+$_.sha256})
  [Array]::Sort($rows,[StringComparer]::Ordinal)
  $sha=[Security.Cryptography.SHA256]::Create()
  try{$payloadIdentity=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  $manifest | Add-Member -NotePropertyName compatibility -NotePropertyValue ([pscustomobject]@{
    sourceKind='full-package-v1';payloadHashScope='manifest-files-v1';payloadSha256=$payloadIdentity;scriptSha256=(Get-YmccDeckyHash $script);applyAfterYmccCommit=$true
  })
  $manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $CompleteRoot 'YeManCC/module-package.json') -Encoding UTF8
  return $manifest
}
