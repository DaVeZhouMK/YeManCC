param([Parameter(Mandatory=$true)][string]$Out,[string]$ComponentRoot='',[ValidateSet('All','RowsSnapshot','RowsPaths','ManifestSnapshot','HashLifetime','ExportHashLifetime','BackupSnapshot')][string]$Case='All')
$ErrorActionPreference='Stop'
New-Item -ItemType Directory -Path $Out -Force|Out-Null
if(!$ComponentRoot){$ComponentRoot=Join-Path (Split-Path -Parent $PSScriptRoot) 'PowerControl'}
$source=Join-Path $ComponentRoot 'steam-appearance.ps1'
$parseErrors=$null;$tokens=$null
$ast=[Management.Automation.Language.Parser]::ParseFile($source,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw $parseErrors[0]}
# Execute only these production function definitions; all external IO is mocked.
$functions=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in @('Get-AppearanceStatus','Invoke-LoaderRpc')},$true)
foreach($fn in $functions){Invoke-Expression $fn.Extent.Text}
$script:LinkFile=Join-Path $Out 'missing-link.txt';$script:RpcDeadline=$null;$script:ThemeNames=@('Round')
$checks=New-Object Collections.Generic.List[string]
function Assert($condition,$label){if(!$condition){throw $label}}
function Check($name,[scriptblock]$work){& $work;$checks.Add($name);Write-Output ('PASS '+$name)}
function Reset-Fixture {
 $script:ThemeRoot='X:\before\themes';$script:actualRoot='X:\backend\themes';$script:running=$true
 $script:rowReads=0;$script:ownerChecks=0;$script:rpcCalls=New-Object Collections.Generic.List[string]
 $script:rowPaths=New-Object Collections.Generic.List[string];$script:rejectOwnerAt=0;$script:failRpc=''
}
function Get-BackendProcesses {if($script:running){[pscustomobject]@{Id=42}}}
function Get-AppearanceSteamSession {if($script:running){[pscustomobject]@{pid=43;birth='100';root='X:\fixture-steam'}}}
function Recover-PresetTransaction {}
function Save-PresetTargetHint {}
function Get-ThemeRows {$script:rowReads++;$script:rowPaths.Add($script:ThemeRoot);[pscustomobject]@{name='Round';installed=$true;configured=$true;backendEnabled=$null;parameterCount=0;error=''}}
function Get-ThemeTargetId {$script:ThemeRoot}
function Get-ThemeRevision($rows){'fixture-revision'}
function Get-Process {param($Name) @()}
function Test-BackendOwner {$script:ownerChecks++;$script:rejectOwnerAt -eq 0 -or $script:ownerChecks -lt $script:rejectOwnerAt}
function Invoke-RestMethod {
 param($Uri,$Method,$Body,$ContentType,$TimeoutSec)
 $request=[Text.Encoding]::UTF8.GetString($Body)|ConvertFrom-Json;$script:rpcCalls.Add($request.method)
 if($script:failRpc -eq $request.method){throw 'fixture unavailable'}
 if($request.method -eq 'fetch_theme_path'){return [pscustomobject]@{success=$true;res=$script:actualRoot}}
 if($request.method -eq 'get_themes'){return [pscustomobject]@{success=$true;res=@([pscustomobject]@{name='Round';enabled=$true})}}
 throw 'unexpected RPC'
}
if($Case -eq 'All'){
Check 'Status resolves the actual root before exactly one theme read' {
 Reset-Fixture;$value=Get-AppearanceStatus
 Assert ($script:rowReads -eq 1 -and $script:rowPaths[0] -eq $script:actualRoot) 'duplicated or premature theme read'
 Assert ($value.backendReady -and $value.configuredMatchesBackend -and $value.targetId -eq $script:actualRoot) 'backend target not reflected'
 Assert ($script:ownerChecks -eq 2 -and $script:rpcCalls.Count -eq 2) 'each RPC must check its current owner once'
}
Check 'Offline Status reads configurations once and sends no RPC' {
 Reset-Fixture;$script:running=$false;$value=Get-AppearanceStatus
 Assert ($script:rowReads -eq 1 -and $script:ownerChecks -eq 0 -and $script:rpcCalls.Count -eq 0) 'offline work amplified'
 Assert (!$value.backendReady) 'offline must not be ready'
}
Check 'Ownership change between RPCs prevents the second request' {
 Reset-Fixture;$script:rejectOwnerAt=2;$value=Get-AppearanceStatus
 Assert (!$value.backendReady -and $script:ownerChecks -eq 2 -and $script:rpcCalls.Count -eq 1) 'stale ownership was reused'
 Assert ($script:rowReads -eq 1 -and $value.note -match 'owner changed') 'failed status must remain bounded and informative'
}
Check 'Unavailable API keeps one local theme read and does not retry' {
 Reset-Fixture;$script:failRpc='fetch_theme_path';$value=Get-AppearanceStatus
 Assert (!$value.backendReady -and $script:rowReads -eq 1 -and $script:rpcCalls.Count -eq 1) 'failure caused repeated audits'
 Assert ($script:rowPaths[0] -eq 'X:\before\themes') 'unconfirmed root was adopted'
}
Check 'Native leased path skips initial listener discovery' {
 $BackendPath='X:\fixture\CssLoader-Standalone-Headless.exe';$script:listenerScans=0
 function Get-NetTCPConnection {param($State,$LocalPort) $script:listenerScans++;throw 'discovery should not run'}
 $text=[IO.File]::ReadAllText($source);$first=$text.IndexOf('$portOwners=@()');$last=$text.IndexOf('if(!$BackendPath -and !$portOwners.Count',$first)
 Assert ($first -ge 0 -and $last -gt $first) 'discovery boundaries changed'
 Invoke-Expression $text.Substring($first,$last-$first)
 Assert ($script:listenerScans -eq 0) 'leased owner was redundantly rediscovered'
}

}
if($Case -eq 'RowsSnapshot') {& {
 $script:ThemeRoot=Join-Path $Out 'profile\homebrew\themes';$script:ThemeNames=@('Round')
 $folder=Join-Path $script:ThemeRoot 'Round';[IO.Directory]::CreateDirectory($folder)|Out-Null
 $script:ConfigPath=Join-Path $folder 'config_USER.json';$manifest=Join-Path $folder 'theme.json'
 [IO.File]::WriteAllText($manifest,'{"name":"Round","patches":{}}',(New-Object Text.UTF8Encoding($false)))
 foreach($name in @('Get-ThemeRows','Get-ConfigHash','Read-AppearanceThemeConfig')){
  $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
  if($fn){Invoke-Expression $fn.Extent.Text}
 }
 function Get-AppearanceContentIndex {@{}}
 function Get-AppearanceThemeRenderPlan($Name,$Manifest,$Config,$ContentIndex){[pscustomobject]@{active=$Config.active}}
 $script:GrowBeforeRead=$false
 function Assert-ThemeTarget([string]$Path,[ref]$ResourceItem){
  if($script:GrowBeforeRead -and $Path -eq $script:ConfigPath){$script:GrowBeforeRead=$false;[IO.File]::WriteAllText($Path,(' '*262145)+'{"active":true}',(New-Object Text.UTF8Encoding($false)))}
  if($ResourceItem){$item=[IO.FileInfo]::new($Path);$ResourceItem.Value=if($item.Exists){$item}}
  $Path
 }
 function ByteHash([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','')}finally{$sha.Dispose()}}
 $text='{"active":true,"Size":"0.7","note":"中文预设"}'
 foreach($pair in @(@('utf8',(New-Object Text.UTF8Encoding($false))),@('utf8-bom',(New-Object Text.UTF8Encoding($true))),@('utf16-le',[Text.Encoding]::Unicode),@('utf16-be',[Text.Encoding]::BigEndianUnicode),@('utf32-le',[Text.Encoding]::UTF32))){
  $encoding=$pair[1];[byte[]]$bytes=$encoding.GetPreamble()+$encoding.GetBytes($text);[IO.File]::WriteAllBytes($script:ConfigPath,$bytes)
  $row=@(Get-ThemeRows)[0];$expectedHash=ByteHash $bytes
  Check ('Exact config text and byte hash preserved for '+$pair[0]) {
   Assert ($row.installed -and !$row.error -and $row.configured -and $row.raw -ceq $text -and $row.hash -ceq $expectedHash) 'config text or BOM-sensitive revision changed'
   Assert ($row.config.Size -ceq '0.7' -and $row.config.note -ceq '中文预设') 'personal parameters changed'
  }
 }
 $script:MutateAfterConfigParse=$false;$script:ReplacementBytes=[Text.Encoding]::UTF8.GetBytes('{"active":false,"Size":"0.5"}')
 function ConvertFrom-Json {
  [CmdletBinding()]param([Parameter(ValueFromPipeline=$true)]$InputObject)
  process {
   $parsed=Microsoft.PowerShell.Utility\ConvertFrom-Json -InputObject $InputObject
   if($script:MutateAfterConfigParse){$script:MutateAfterConfigParse=$false;[IO.File]::WriteAllBytes($script:ConfigPath,$script:ReplacementBytes)}
   $parsed
  }
 }
 Check 'Concurrent save cannot mix parsed flags with a newer config revision' {
  [byte[]]$original=[Text.Encoding]::UTF8.GetBytes($text);[IO.File]::WriteAllBytes($script:ConfigPath,$original);$script:MutateAfterConfigParse=$true
  $row=@(Get-ThemeRows)[0]
  Assert ($row.installed -and !$row.error -and $row.configured -and $row.raw -ceq $text -and $row.hash -ceq (ByteHash $original)) 'Config text and revision came from different file snapshots'
  Assert (([IO.File]::ReadAllText($script:ConfigPath)|Microsoft.PowerShell.Utility\ConvertFrom-Json).active -eq $false) 'owned save boundary did not actually change the file'
 }
 Check 'The next observation reads the newly saved user OFF without caching' {
  $row=@(Get-ThemeRows)[0];Assert ($row.installed -and !$row.error -and !$row.configured -and $row.hash -ceq (ByteHash $script:ReplacementBytes)) 'later config snapshot was cached or user OFF was lost'
 }
 Check 'Size limit is checked on the opened file after target validation' {
  [IO.File]::WriteAllText($script:ConfigPath,'{"active":true}',(New-Object Text.UTF8Encoding($false)));$script:GrowBeforeRead=$true
  $row=@(Get-ThemeRows)[0];Assert (!$row.installed -and $row.error -eq 'Theme config too large') 'post-validation config growth bypassed the byte bound'
 }
 Check 'Config exactly at the original byte limit remains supported' {
  $value='{"active":true}';[byte[]]$bytes=[Text.Encoding]::UTF8.GetBytes($value+(' '*(262144-$value.Length)));[IO.File]::WriteAllBytes($script:ConfigPath,$bytes)
  $row=@(Get-ThemeRows)[0];Assert ($row.installed -and !$row.error -and $row.hash -ceq (ByteHash $bytes)) 'exact byte limit rejected or mis-hashed'
 }
 Check 'Malformed config remains an explicit uninstalled error' {
  [IO.File]::WriteAllText($script:ConfigPath,'{"active":"true"}',(New-Object Text.UTF8Encoding($false)))
  $row=@(Get-ThemeRows)[0];Assert (!$row.installed -and $row.error -eq 'Invalid active field') 'malformed active silently accepted'
 }
 Check 'Read and hash handles are released after failure and success' {
  $held=[IO.File]::Open($script:ConfigPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose()
  [IO.File]::WriteAllText($script:ConfigPath,'{"active":true}',(New-Object Text.UTF8Encoding($false)));$row=@(Get-ThemeRows)[0]
  $held=[IO.File]::Open($script:ConfigPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose();Assert ($row.installed -and !$row.error) 'config read handle was retained'
 }
 Check 'Missing config keeps the original all-on uninstalled default' {
  [IO.File]::Delete($script:ConfigPath);$row=@(Get-ThemeRows)[0]
  Assert (!$row.installed -and $row.configured -and $row.hash -eq 'missing' -and !$row.error) 'missing-file default changed'
 }
}}

if($Case -eq 'RowsPaths'){& {
 . (Join-Path $ComponentRoot 'steam-appearance\preset.ps1')
 foreach($name in 'Get-ThemeRows','Read-AppearanceThemeConfig'){
  $fn=$ast.Find({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
  if(!$fn){throw ('Missing row snapshot function: '+$name)}
  Invoke-Expression $fn.Extent.Text
 }
 function Get-AppearanceContentIndex {@{}}
 function Get-AppearanceThemeRenderPlan($Name,$Manifest,$Config,$ContentIndex){[pscustomobject]@{active=$Config.active}}
 $script:ThemeRoot=[IO.Path]::GetFullPath((Join-Path $Out 'profile\homebrew\themes'));$script:ThemeNames=@('Round')
 $folder=Join-Path $script:ThemeRoot 'Round';$external=Join-Path ([IO.Path]::GetFullPath($Out)) 'other'
 [IO.Directory]::CreateDirectory($folder)|Out-Null;[IO.Directory]::CreateDirectory($external)|Out-Null
 $manifest=Join-Path $folder 'theme.json';$config=Join-Path $folder 'config_USER.json'
 $manifestText='{"name":"Round","patches":{}}';$configText='{"active":false}'
 [IO.File]::WriteAllText($manifest,$manifestText);[IO.File]::WriteAllText($config,$configText)
 function Test-Path {param([string]$LiteralPath,$PathType='Any',$ErrorAction)
  if($LiteralPath -eq $script:ThemeRoot -or $LiteralPath.StartsWith($script:ThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:RowPathTests++}
  Microsoft.PowerShell.Management\Test-Path -LiteralPath $LiteralPath -PathType $PathType -ErrorAction Stop
 }
 function Get-Item {param([string]$LiteralPath,[switch]$Force,$ErrorAction)
  if($LiteralPath -eq $script:ThemeRoot -or $LiteralPath.StartsWith($script:ThemeRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$script:RowItemReads++}
  Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath -Force:$Force -ErrorAction Stop
 }
 $script:RowPathTests=0;$script:RowItemReads=0
 $row=@(Get-ThemeRows)[0]
 $probes=[ordered]@{testPath=$script:RowPathTests;getItem=$script:RowItemReads;installed=$row.installed;configured=$row.configured}
 $probes|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'row-path-probes.json') -Encoding UTF8
 Check 'Healthy rows reuse the guarded leaf type without redundant provider queries' {
  Assert ($row.installed -and !$row.configured -and !$row.error -and $probes.testPath -eq 0 -and $probes.getItem -eq 2) 'Healthy path exceeded the guarded row probe budget or changed the saved selection'
 }
 Check 'A locked config is not confirmed' {
  $held=[IO.File]::Open($config,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None)
  try{$value=@(Get-ThemeRows)[0];Assert (!$value.installed -and $value.error) 'Locked config was incorrectly confirmed'}finally{$held.Dispose()}
 }
 Check 'Missing config and a directory leaf keep an uninstalled default row' {
  [IO.File]::Delete($config)
  $value=@(Get-ThemeRows)[0];Assert (!$value.installed -and $value.configured -and !$value.error) 'Missing config changed default row behavior'
  [IO.Directory]::CreateDirectory($config)|Out-Null
  $value=@(Get-ThemeRows)[0];Assert (!$value.installed -and $value.configured -and !$value.error) 'Directory leaf changed default row behavior'
  [IO.Directory]::Delete($config,$false)
 }
 Check 'Theme-directory junction is rejected before following its JSON' {
  [IO.File]::Delete($manifest);[IO.Directory]::Delete($folder,$false)
  [IO.File]::WriteAllText((Join-Path $external 'theme.json'),$manifestText)
  [IO.File]::WriteAllText((Join-Path $external 'config_USER.json'),$configText)
  New-Item -ItemType Junction -Path $folder -Target $external -ErrorAction Stop|Out-Null
  try{$value=@(Get-ThemeRows)[0];Assert (!$value.installed -and $value.error -match 'Theme directory links' -and $value.hash -eq 'missing') 'Theme JSON behind a junction was observed'}finally{[IO.Directory]::Delete($folder,$false)}
 }
}}

if($Case -eq 'ManifestSnapshot'){& {
 foreach($name in @('Get-ConfigHash','Read-AppearanceThemeConfig')){
  $fn=$ast.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true)
  if(!$fn){throw ('Manifest snapshot dependency missing: '+$name)}
  Invoke-Expression $fn.Extent.Text
 }
 . (Join-Path $ComponentRoot 'steam-appearance\preset.ps1')
 $script:PackRoot=Join-Path $Out 'bundle'
 Copy-Item -LiteralPath (Join-Path $ComponentRoot 'steam-appearance') -Destination $script:PackRoot -Recurse
 $script:ManifestPath=Join-Path $script:PackRoot 'manifest.json'
 $factory=Microsoft.PowerShell.Utility\ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($script:ManifestPath))
 $script:ThemeNames=@($factory.themes.name)
 $factory|Add-Member -NotePropertyName '_fixtureEpoch' -NotePropertyValue 'before'
 $script:OriginalManifest=$factory|ConvertTo-Json -Depth 32 -Compress
 $factory._fixtureEpoch='after!'
 $script:ReplacementManifest=$factory|ConvertTo-Json -Depth 32 -Compress
 $script:Mutation='';$script:Mutations=0;$script:ManifestLock=$null
 $script:BeforeStamp=$null
 function ConvertFrom-Json {
  [CmdletBinding()]param([Parameter(ValueFromPipeline=$true)]$InputObject)
  process{
   $parsed=Microsoft.PowerShell.Utility\ConvertFrom-Json -InputObject $InputObject
   if($script:Mutation -and [string]$InputObject -ceq $script:OriginalManifest){
    $action=$script:Mutation;$script:Mutation='';$script:Mutations++
    switch($action){
     replace {[IO.File]::WriteAllText($script:ManifestPath,$script:ReplacementManifest,(New-Object Text.UTF8Encoding($false)));[IO.File]::SetLastWriteTimeUtc($script:ManifestPath,$script:BeforeStamp)}
     delete {[IO.File]::Delete($script:ManifestPath)}
     lock {$script:ManifestLock=[IO.File]::Open($script:ManifestPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
     default {throw 'Unknown own manifest mutation'}
    }
   }
   $parsed
  }
 }
 function Set-OwnManifest([Text.Encoding]$Encoding){
  if($script:ManifestLock){$script:ManifestLock.Dispose();$script:ManifestLock=$null}
  [byte[]]$bytes=$Encoding.GetPreamble()+$Encoding.GetBytes($script:OriginalManifest)
  [IO.File]::WriteAllBytes($script:ManifestPath,$bytes)
  $script:BeforeStamp=[IO.File]::GetLastWriteTimeUtc($script:ManifestPath)
  $script:Mutation='';$script:Mutations=0
 }
 function Own-ByteHash([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','')}finally{$sha.Dispose()}}
 Check 'Manifest replacement during validation cannot confirm a mixed package' {
  Set-OwnManifest (New-Object Text.UTF8Encoding($false));$before=[IO.File]::ReadAllBytes($script:ManifestPath);$script:Mutation='replace';$failed=$false;$observed=$null;$failure=''
  try{$observed=Read-PresetManifest}catch{$failed=$_.Exception.Message -eq 'Bundled preset manifest changed during validation';$failure=$_.Exception.Message}
  $after=[IO.File]::ReadAllBytes($script:ManifestPath)
  [pscustomobject]@{mutationCount=$script:Mutations;failed=$failed;failure=$failure;returnedEpoch=$(if($observed){$observed.manifest._fixtureEpoch}else{$null});returnedDigest=$(if($observed){$observed.digest}else{$null});beforeDigest=(Own-ByteHash $before);afterDigest=(Own-ByteHash $after);sameLength=$before.Length -eq $after.Length;preservedMtime=[IO.File]::GetLastWriteTimeUtc($script:ManifestPath) -eq $script:BeforeStamp}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Out 'mutation-observation.json') -Encoding UTF8
  Assert ($failed -and $script:Mutations -eq 1 -and $before.Length -eq $after.Length -and (Own-ByteHash $before) -cne (Own-ByteHash $after)) 'Manifest replacement during validation cannot confirm a mixed package'
 }
 Check 'The next manifest read observes the completed replacement without caching' {
  $value=Read-PresetManifest
  Assert ($value.manifest._fixtureEpoch -ceq 'after!' -and $value.digest -ceq (Own-ByteHash ([IO.File]::ReadAllBytes($script:ManifestPath)))) 'Completed manifest replacement was cached or mis-hashed'
 }
 foreach($pair in @(@('utf8',(New-Object Text.UTF8Encoding($false))),@('utf8-bom',(New-Object Text.UTF8Encoding($true))),@('utf16-le',[Text.Encoding]::Unicode),@('utf16-be',[Text.Encoding]::BigEndianUnicode),@('utf32-le',[Text.Encoding]::UTF32))){
  Check ('Manifest snapshot preserves source encoding and byte digest: '+$pair[0]) {
   Set-OwnManifest $pair[1];$value=Read-PresetManifest
   Assert ($value.manifest._fixtureEpoch -ceq 'before' -and $value.digest -ceq (Own-ByteHash ([IO.File]::ReadAllBytes($script:ManifestPath))) -and $value.files.Count -eq $factory.files.Count) 'Manifest encoding or original byte digest changed'
  }
 }
 foreach($mutation in @('delete','lock')){
  Check ('Manifest '+$mutation+' during validation remains a failed read') {
   Set-OwnManifest (New-Object Text.UTF8Encoding($false));$script:Mutation=$mutation;$failed=$false
   try{Read-PresetManifest|Out-Null}catch{$failed=$true}finally{if($script:ManifestLock){$script:ManifestLock.Dispose();$script:ManifestLock=$null}}
   Assert ($failed -and $script:Mutations -eq 1) 'A missing or exclusively held source manifest was confirmed'
  }
 }
 Check 'Manifest and hash handles are released after all read outcomes' {
  Set-OwnManifest (New-Object Text.UTF8Encoding($false));Read-PresetManifest|Out-Null
  $held=[IO.File]::Open($script:ManifestPath,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose()
 }
 Check 'Source corruption continues to fail before a package is confirmed' {
  [IO.File]::WriteAllText($script:ManifestPath,'{"id":"wrong-package","version":1}',(New-Object Text.UTF8Encoding($false)))
  $failed=$false;try{Read-PresetManifest|Out-Null}catch{$failed=$_.Exception.Message -eq 'Invalid bundled preset manifest'}
  Assert $failed 'Malformed bundled manifest was accepted'
 }
}}

if($Case -in @('HashLifetime','ExportHashLifetime')){& {
 $exportSource=Join-Path (Split-Path -Parent $ComponentRoot) 'tools\steam-appearance-export.ps1'
 $exportTokens=$null;$exportErrors=$null
 $exportAst=[Management.Automation.Language.Parser]::ParseFile($exportSource,[ref]$exportTokens,[ref]$exportErrors)
 if($exportErrors.Count){throw 'Paired export source has parse errors'}
 $script:HashLeases=[Collections.Generic.List[object]]::new()
 $script:HashFactoryFault='';$script:HashComputes=0;$script:HashDisposals=0;$script:HashFactoryAttempts=0
 function New-TrackedAppearanceHash {
  $script:HashFactoryAttempts++
  if($script:HashFactoryFault -eq 'factory'){throw 'owned-hash-factory-failure'}
  $lease=[pscustomobject]@{inner=[Security.Cryptography.SHA256]::Create();closed=$false}
  $lease|Add-Member -MemberType ScriptMethod -Name ComputeHash -Value {
   param($Stream)
   $script:HashComputes++
   if($script:HashFactoryFault -eq 'compute'){throw 'owned-hash-compute-failure'}
   return ,($this.inner.ComputeHash($Stream))
  }
  $lease|Add-Member -MemberType ScriptMethod -Name Dispose -Value {
   if($this.closed){throw 'owned-hash-double-dispose'}
   $this.closed=$true;$script:HashDisposals++;$this.inner.Dispose()
  }
  $script:HashLeases.Add($lease)
  $lease
 }
 $adaptations=@()
 $methods=if($Case -eq 'ExportHashLifetime'){@('Get-SteamAppearanceFileHash')}else{@('Get-ConfigHash','Get-SteamAppearanceFileHash')}
 foreach($hashMethod in $methods){
  $tree=if($hashMethod -eq 'Get-ConfigHash'){$ast}else{$exportAst}
  $fn=$tree.Find({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $hashMethod},$true)
  if(!$fn){throw ('Original hash function missing: '+$hashMethod)}
  $original=$fn.Extent.Text;$needle='[Security.Cryptography.SHA256]::Create()';$port='(New-TrackedAppearanceHash)'
  if([regex]::Matches($original,[regex]::Escape($needle)).Count -ne 1){throw 'Exactly one hash-factory leaf required'}
  $adapted=$original.Replace($needle,$port)
  if($adapted.Replace($port,$needle) -cne $original){throw 'Hash factory adaptation changed other function bytes'}
  Invoke-Expression $adapted
  $adaptations+=@{name=$hashMethod;source=$fn.Extent.File;sourceLine=$fn.Extent.StartLineNumber;factoryOnly=$true;fullFunctionReverseExact=$true;fileOpenAndRead='original actual .NET FileStream';hashImplementation='real SHA256 inside a tracked disposable lease';runtimeFactoryFailure='explicit memory port; not an actual crypto-provider creation failure'}
 }
 $adaptations|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'hash-adaptations.json') -Encoding UTF8
 $script:HashObservations=[Collections.Generic.List[object]]::new()
 function Reset-OwnHash {
  foreach($lease in $script:HashLeases){if(!$lease.closed){$lease.Dispose()}}
  $script:HashLeases.Clear();$script:HashFactoryFault='';$script:HashComputes=0;$script:HashDisposals=0;$script:HashFactoryAttempts=0
 }
 function Exact-OwnHash([byte[]]$Bytes){$sha=[Security.Cryptography.SHA256]::Create();try{([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-','')}finally{$sha.Dispose()}}
 try{
  foreach($hashMethod in $methods){
   foreach($scenario in @('missing','locked','compute','factory','healthy','empty')){
    Check ($hashMethod+' releases its hash on '+$scenario) {
     Reset-OwnHash
     $file=Join-Path $Out ('hash-input-'+$scenario+'.bin');$lock=$null;$failure=$null;$value=$null
     [byte[]]$bytes=[Text.Encoding]::UTF8.GetBytes('Owned theme hash: '+[char]0x4e2d+[char]0x6587)
     if($scenario -eq 'empty'){$bytes=[byte[]]@()}
     if($scenario -ne 'missing'){[IO.File]::WriteAllBytes($file,$bytes)}
     if($scenario -eq 'locked'){$lock=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
     if($scenario -in @('compute','factory')){$script:HashFactoryFault=$scenario}
     try{try{$value=& $hashMethod $file}catch{$failure=$_.Exception}}finally{if($lock){$lock.Dispose()}}
     $observed=[pscustomobject]@{method=$hashMethod;scenario=$scenario;factoryAttempts=$script:HashFactoryAttempts;created=$script:HashLeases.Count;disposedBeforeTestCleanup=$script:HashDisposals;computes=$script:HashComputes;failed=$null -ne $failure;errorType=$(if($failure){$failure.GetType().FullName}else{''});value=$value;testCleanupRequired=@($script:HashLeases|Where-Object {!$_.closed}).Count}
     $script:HashObservations.Add($observed)
     $script:HashObservations.ToArray()|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'hash-observations.json') -Encoding UTF8
     Assert ($script:HashFactoryAttempts -eq 1) 'Original hash function did not reach its exact factory port'
     if($scenario -in @('healthy','empty')){
      $expected=Exact-OwnHash $bytes;if($hashMethod -eq 'Get-SteamAppearanceFileHash'){$expected=$expected.ToLowerInvariant()}
      Assert ($null -eq $failure -and $value -is [string] -and $value -ceq $expected -and $script:HashComputes -eq 1) 'Original exact byte digest or hexadecimal casing changed'
     }else{Assert ($null -ne $failure) 'Expected owned hash failure was swallowed'}
     Assert ($script:HashDisposals -eq $script:HashLeases.Count -and $observed.testCleanupRequired -eq 0) ($hashMethod+' must dispose a created hash before returning or throwing: '+$scenario)
     if($scenario -ne 'missing'){$held=[IO.File]::Open($file,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose()}
    }
   }
   Check ($hashMethod+' does not accumulate hashes across bounded failed reads') {
    Reset-OwnHash;$file=Join-Path $Out 'always-missing.bin'
    for($i=0;$i -lt 16;$i++){try{& $hashMethod $file|Out-Null;throw 'Owned missing-file failure expected'}catch{if($_.Exception.Message -eq 'Owned missing-file failure expected'){throw}}}
    Assert ($script:HashFactoryAttempts -eq 16 -and $script:HashLeases.Count -eq 16 -and $script:HashDisposals -eq 16 -and $script:HashComputes -eq 0) 'Repeated failed reads retained undisposed hash leases'
   }
  }
 }finally{
  $undisposed=@($script:HashLeases|Where-Object {!$_.closed}).Count
  foreach($lease in $script:HashLeases){if(!$lease.closed){$lease.Dispose()}}
  [pscustomobject]@{explicitFixtureCleanupCount=$undisposed;remaining=@($script:HashLeases|Where-Object {!$_.closed}).Count;doesNotChangeObservedProductDisposal=$true}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $Out 'hash-test-cleanup.json') -Encoding UTF8
 }
}}

if($Case -in @('All','BackupSnapshot')){
 $fixtureRoot=Join-Path $Out 'backup-owned';New-Item -ItemType Directory -Path $fixtureRoot -Force|Out-Null
 $script:ThemeRoot=Join-Path $fixtureRoot 'themes';$script:PresetStateBase=Join-Path $fixtureRoot 'state';New-Item -ItemType Directory -Path $script:ThemeRoot -Force|Out-Null
 $presetFile=Join-Path $ComponentRoot 'steam-appearance/preset.ps1';$tokens=$null;$errors=$null
 $tree=[Management.Automation.Language.Parser]::ParseFile($presetFile,[ref]$tokens,[ref]$errors);if($errors.Count){throw $errors[0]}
 foreach($name in @('Get-PresetStateDir','Assert-PresetStateTarget','Assert-ThemeTarget','Get-PresetRelative','Read-PresetBackup')){$fn=$tree.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq $name},$true);if(!$fn){throw $name};Invoke-Expression $fn.Extent.Text}
 $readGuard=$tree.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Assert-PresetBackupReadPath'},$true)
 if($readGuard){Invoke-Expression $readGuard.Extent.Text}
 $tree=[Management.Automation.Language.Parser]::ParseFile((Join-Path $ComponentRoot 'steam-appearance.ps1'),[ref]$tokens,[ref]$errors)
 $fn=$tree.Find({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst] -and $node.Name -eq 'Get-ConfigHash'},$true);Invoke-Expression $fn.Extent.Text
 $script:BackupMetadataReads=0;$script:BackupRace=$false;$script:BackupBlob='';$script:BackupBefore=$null
 function Get-Item {param([string]$LiteralPath,[switch]$Force,$ErrorAction) if($script:BackupBlob -and $LiteralPath -eq $script:BackupBlob){$script:BackupMetadataReads++;if($script:BackupRace){return $script:BackupBefore}};Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath -Force:$Force -ErrorAction Stop}
 function Make-Backup([byte[]]$Bytes){
  $dir=Join-Path (Join-Path (Get-PresetStateDir) 'backups') ([guid]::NewGuid().ToString('N'));New-Item -ItemType Directory -Path (Join-Path $dir 'files') -Force|Out-Null
  $script:BackupBlob=Join-Path $dir 'files/0000.bin';[IO.File]::WriteAllBytes($script:BackupBlob,$Bytes)
  $entry=[pscustomobject]@{relative='Round/config_USER.json';existed=$true;blob='0000.bin';hash=(Get-ConfigHash $script:BackupBlob)}
  $journal=[pscustomobject]@{version=1;themeRoot=[IO.Path]::GetFullPath($script:ThemeRoot);entries=@($entry)}
  [IO.File]::WriteAllText((Join-Path $dir 'journal.json'),($journal|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))
  [pscustomobject]@{directory=$dir;journal=$journal}
 }
 function Write-OwnJournal($Backup){[IO.File]::WriteAllText((Join-Path $Backup.directory 'journal.json'),($Backup.journal|ConvertTo-Json -Depth 8),(New-Object Text.UTF8Encoding($false)))}
 $observations=New-Object Collections.Generic.List[object]
 Check 'backup length/hash use the opened snapshot after stale metadata' {
  $backup=Make-Backup ([byte[]]@(1,2,3));$script:BackupBefore=Microsoft.PowerShell.Management\Get-Item -LiteralPath $script:BackupBlob;$null=$script:BackupBefore.Length
  [IO.File]::WriteAllBytes($script:BackupBlob,(New-Object byte[] 16777217));$backup.journal.entries[0].hash=Get-ConfigHash $script:BackupBlob;Write-OwnJournal $backup
  $script:BackupRace=$true;$rejected=$false;try{Read-PresetBackup $backup.directory|Out-Null}catch{$rejected=$true}
  $observations.Add([pscustomobject]@{scenario='stale-metadata-growth';rejected=$rejected;actualLength=[IO.FileInfo]::new($script:BackupBlob).Length;metadataLength=$script:BackupBefore.Length;metadataReads=$script:BackupMetadataReads})
  if($Case -ceq 'BackupSnapshot'){
   Check 'backup journal limit uses actual bytes instead of stale file metadata' {
    $backup=Make-Backup ([byte[]]@(1,2,3));$journalPath=Join-Path $backup.directory 'journal.json'
    $before=Microsoft.PowerShell.Management\Get-Item -LiteralPath $journalPath
    $originalLength=$before.Length;$raw=[IO.File]::ReadAllText($journalPath);$utf8=[Text.UTF8Encoding]::new($false)
    $bound=1048576;$padding=$bound-$utf8.GetByteCount($raw)
    $reads=[int[]]::new(1)
    function Get-Item {param([string]$LiteralPath) if($LiteralPath -ceq $journalPath){$reads[0]++;return $before};Microsoft.PowerShell.Management\Get-Item -LiteralPath $LiteralPath -ErrorAction Stop}
    [IO.File]::WriteAllText($journalPath,($raw+(' '*$padding)+' '),$utf8)
    $failure='';try{Read-PresetBackup $backup.directory|Out-Null}catch{$failure=$_.Exception.Message}
    $observations.Add([pscustomobject]@{scenario='journal-stale-metadata-growth';failure=$failure;originalLength=$originalLength;actualLength=[IO.FileInfo]::new($journalPath).Length;metadataReads=$reads[0]})
    Assert ($failure -ceq 'Invalid preset backup journal file' -and $reads[0] -eq 0) 'backup journal length was not checked on its read handle'
    [IO.File]::WriteAllText($journalPath,($raw+(' '*$padding)),$utf8)
    Assert ((Read-PresetBackup $backup.directory).journal.entries.Count -eq 1) 'exact journal byte limit must still be accepted'
   }
  }
  $observations.ToArray()|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'backup-observations.json') -Encoding UTF8
  Assert $rejected 'backup larger than per-file bound was accepted through stale metadata'
  $script:BackupRace=$false;$held=[IO.File]::Open($script:BackupBlob,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose()
 }
 foreach($scenario in @('healthy','empty','wrong-hash','missing','locked','invalid-blob','duplicate','escaped','absent','oversized','exact-limit')){
  Check ('backup snapshot '+$scenario) {
   $script:BackupRace=$false;$backup=Make-Backup ([byte[]]@(13,29,78));$expectReject=$scenario -notin @('healthy','empty','absent','exact-limit');$lock=$null
   switch($scenario){
    empty {[IO.File]::WriteAllBytes($script:BackupBlob,[byte[]]@());$backup.journal.entries[0].hash=Get-ConfigHash $script:BackupBlob}
    wrong-hash {$backup.journal.entries[0].hash='0'*64}
    missing {[IO.File]::Delete($script:BackupBlob)}
    locked {$lock=[IO.File]::Open($script:BackupBlob,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None)}
    invalid-blob {$backup.journal.entries[0].blob='../other.bin'}
    duplicate {$backup.journal.entries+=@($backup.journal.entries[0])}
    escaped {$backup.journal.entries[0].relative='../other.json'}
    absent {$backup.journal.entries[0].existed=$false;$backup.journal.entries[0].hash='';[IO.File]::Delete($script:BackupBlob)}
    oversized {[IO.File]::WriteAllBytes($script:BackupBlob,(New-Object byte[] 16777217));$backup.journal.entries[0].hash=Get-ConfigHash $script:BackupBlob}
    exact-limit {[IO.File]::WriteAllBytes($script:BackupBlob,(New-Object byte[] 16777216));$backup.journal.entries[0].hash=Get-ConfigHash $script:BackupBlob}
   }
   Write-OwnJournal $backup;$rejected=$false;$value=$null;try{try{$value=Read-PresetBackup $backup.directory}catch{$rejected=$true}}finally{if($lock){$lock.Dispose()}}
   Assert ($rejected -eq $expectReject) ('backup snapshot unexpected result: '+$scenario)
   if(!$rejected){Assert ($value.directory -ceq $backup.directory -and $value.journal.entries.Count -eq 1) 'backup result shape changed'}
   if(Test-Path -LiteralPath $script:BackupBlob -PathType Leaf){$held=[IO.File]::Open($script:BackupBlob,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::None);$held.Dispose()}
   $observations.Add([pscustomobject]@{scenario=$scenario;rejected=$rejected;expectedRejected=$expectReject;fileExclusiveReopen=$true})
  }
 }
 foreach($scenario in @('theme-parent-junction','state-parent-junction','blob-is-directory','journal-is-directory')){
  Check ('backup path '+$scenario) {
   $backup=Make-Backup ([byte[]]@(13,29,78));$link=$null;$directory=$backup.directory
   try{
    switch($scenario){
     'theme-parent-junction' {
      $link=Join-Path $script:ThemeRoot 'read-linked-theme';New-Item -ItemType Junction -Path $link -Target $fixtureRoot -ErrorAction Stop|Out-Null
      $backup.journal.entries[0].relative='read-linked-theme/config_USER.json';Write-OwnJournal $backup
     }
     'state-parent-junction' {
      $link=Join-Path (Split-Path -Parent $backup.directory) 'read-linked-backup';New-Item -ItemType Junction -Path $link -Target $backup.directory -ErrorAction Stop|Out-Null
      $directory=$link
     }
     'blob-is-directory' {[IO.File]::Delete($script:BackupBlob);[void][IO.Directory]::CreateDirectory($script:BackupBlob)}
     'journal-is-directory' {$path=Join-Path $backup.directory 'journal.json';[IO.File]::Delete($path);[void][IO.Directory]::CreateDirectory($path)}
    }
    $failure='';try{Read-PresetBackup $directory|Out-Null}catch{$failure=$_.Exception.Message}
    $expected=switch($scenario){
     'theme-parent-junction' {'*link*'}
     'state-parent-junction' {'*link*'}
     'blob-is-directory' {'Preset backup integrity check failed'}
     'journal-is-directory' {'Invalid preset backup journal file'}
    }
    Assert ($failure -like $expected) ('backup path expected rejection: '+$scenario+'; got: '+$failure)
   }finally{if($link -and (Microsoft.PowerShell.Management\Test-Path -LiteralPath $link -PathType Container)){[IO.Directory]::Delete($link,$false)}}
  }
 }
 $observations.ToArray()|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $Out 'backup-observations.json') -Encoding UTF8
}

[pscustomobject]@{ok=$true;passed=$checks.Count;cases=@($checks);selectedCase=$Case;powershellVersion=[string]$PSVersionTable.PSVersion;realSteamOperations=0;realCssOperations=0}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $Out 'result.json') -Encoding utf8
