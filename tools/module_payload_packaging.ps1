Add-Type -AssemblyName System.IO.Compression
function Select-YmccInputBootstrapFunctions([string]$ScriptText) {
  # The file-only bootstrap used to ship the entire updater libraries, including
  # unrelated process termination and Decky port listeners. Emit only the exact
  # transitive function dependencies of its entry body. Function bodies remain
  # canonical source bytes; no runtime rewriting, generated code or new authority.
  $tokens=$null;$errors=$null
  $ast=[Management.Automation.Language.Parser]::ParseInput($ScriptText,[ref]$tokens,[ref]$errors)
  if($errors.Count){throw 'INPUT_BOOTSTRAP_SOURCE_PARSE_FAILED'}
  $functions=@{};$ordered=New-Object 'System.Collections.Generic.List[object]'
  $body=New-Object 'System.Collections.Generic.List[object]'
  foreach($statement in $ast.EndBlock.Statements){
    if($statement -is [Management.Automation.Language.FunctionDefinitionAst]){
      if($functions.ContainsKey($statement.Name)){throw ('INPUT_BOOTSTRAP_DUPLICATE_FUNCTION: '+$statement.Name)}
      $functions[$statement.Name]=$statement;$ordered.Add($statement)
    }else{$body.Add($statement)}
  }
  $pending=New-Object 'System.Collections.Generic.Queue[object]'
  foreach($statement in $body){$pending.Enqueue($statement)}
  $selected=@{}
  while($pending.Count){
    $node=$pending.Dequeue()
    foreach($command in $node.FindAll({param($item)$item -is [Management.Automation.Language.CommandAst]},$true)){
      $name=$command.GetCommandName()
      if(!$name){throw 'INPUT_BOOTSTRAP_DYNAMIC_COMMAND_REFUSED'}
      if($functions.ContainsKey($name)){
        if(!$selected.ContainsKey($name)){$selected[$name]=$true;$pending.Enqueue($functions[$name])}
      }elseif($name -like '*-Ymcc*'){throw ('INPUT_BOOTSTRAP_FUNCTION_MISSING: '+$name)}
    }
  }
  foreach($forbidden in @('Stop-YmccUpdaterParent','Open-YmccDeckyBackendLease','Install-YmccDeckyPackage','Restore-YmccDeckyPackage')){
    if($selected.ContainsKey($forbidden)){throw ('INPUT_BOOTSTRAP_NON_FILE_CAPABILITY_REFUSED: '+$forbidden)}
  }
  $output=New-Object Text.StringBuilder
  if($ast.ParamBlock){[void]$output.AppendLine($ast.ParamBlock.Extent.Text)}
  foreach($function in $ordered){if($selected.ContainsKey($function.Name)){[void]$output.AppendLine($function.Extent.Text)}}
  foreach($statement in $body){
    if($statement.Extent.Text.StartsWith('$bootstrapLease=')){
      [void]$output.AppendLine('# This body is appended after the embedded maintenance functions by packaging.')
    }
    [void]$output.AppendLine($statement.Extent.Text)
  }
  return $output.ToString()
}
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
  $text=Select-YmccInputBootstrapFunctions $text
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
