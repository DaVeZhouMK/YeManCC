[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[switch]$SkipMutationProof,
 [ValidateRange(1,30000)][int]$TotalBudgetMs=30000,
 [ValidateRange(1,25000)][int]$RunBudgetMs=25000,
 [ValidateRange(0,5000)][int]$CleanupReserveMs=5000)
$ErrorActionPreference='Stop'
# One stopwatch per consumer; includes all its preparation and all mutations.
$taskBudgetWatch=[Diagnostics.Stopwatch]::StartNew()
if($RunBudgetMs+$CleanupReserveMs -gt $TotalBudgetMs){throw 'DECKY_PACKAGE_INVALID_BUDGET_PARTITION'}
$taskBudgetExhausted=$false;$taskIncomplete=$false;$taskCleanupUsedMs=0;$taskCleanupCurrentWatch=$null
$ownModules=Join-Path $PSHOME 'Modules'
$env:PSModulePath=$ownModules+';'+$env:PSModulePath
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$EvidenceDir=[IO.Path]::GetFullPath($EvidenceDir)
[IO.Directory]::CreateDirectory($EvidenceDir) | Out-Null
# Execute exactly the script shipped inside the native executable.
$header=Get-Content -LiteralPath (Join-Path $root 'native/decky_package_maintenance_script.h') -Raw -Encoding UTF8
$scriptLiteralMatches=[regex]::Matches($header,'(?s)R"YMCCDECKYPKG\((.*?)\)YMCCDECKYPKG"')
if (!$scriptLiteralMatches.Count) { throw 'Missing native package-maintenance script' }
$body=(@($scriptLiteralMatches | ForEach-Object { $_.Groups[1].Value }) -join '')
foreach ($part in $scriptLiteralMatches) { if ([Text.Encoding]::UTF8.GetByteCount($part.Groups[1].Value) -gt 8192) { throw 'Native package-maintenance literal exceeds chunk budget' } }
$source=(Get-Content -LiteralPath (Join-Path $root 'native/decky_package_maintenance.ps1') -Raw -Encoding UTF8).TrimStart([char]0xfeff).Replace("`r`n","`n")
if ($source -cne $body) { throw 'Embedded package-maintenance script differs' }
# Keep the real named-Mutex consumer, replacing only its Windows namespace.
# Parallel fixtures must never contend with the user's product maintenance lock.
$executionBody=$body
$productMutexName="'Global\YeManCC.ModuleMaintenance'"
$fixtureMutexRemapped=$false
if ($body.Contains('function Enter-YmccModuleMaintenance')) {
  if ([regex]::Matches($body,[regex]::Escape($productMutexName)).Count -ne 1) { throw 'Review unique maintenance Mutex namespace boundary' }
  $fixtureMutexName="'Local\YMCC-Decky-Maintenance-Fixture-"+[guid]::NewGuid().ToString('N')+"'"
  $executionBody=$body.Replace($productMutexName,$fixtureMutexName)
  $fixtureMutexRemapped=$true
}
. ([scriptblock]::Create($executionBody))
$cases=New-Object 'System.Collections.Generic.List[object]'

$taskProgressFile=Join-Path $EvidenceDir 'case-progress.jsonl'
$taskMutationStates=[ordered]@{'no-rollback'='NOT_RUN';'allow-downgrade'='NOT_RUN';'stale-rollback-snapshot'='NOT_RUN'}
$taskMutationReceipts=New-Object 'System.Collections.Generic.List[object]'
function Write-TaskPackageProgress([string]$Phase,$Detail){
 $row=[ordered]@{at=[DateTimeOffset]::UtcNow.ToString('o');phase=$Phase;pid=$PID;elapsedMs=$taskBudgetWatch.Elapsed.TotalMilliseconds;totalBudgetMs=$TotalBudgetMs;runBudgetMs=$RunBudgetMs;cleanupReserveMs=$CleanupReserveMs;cleanupUsedMs=$taskCleanupUsedMs;partial=$true;formalPass=$false;detail=$Detail}
 [IO.File]::AppendAllText($taskProgressFile,(($row|ConvertTo-Json -Depth 12 -Compress)+[Environment]::NewLine),(New-Object Text.UTF8Encoding($false)))
}
function Get-TaskRunElapsed {
 $active=if($taskCleanupCurrentWatch){$taskCleanupCurrentWatch.Elapsed.TotalMilliseconds}else{0}
 return [Math]::Max(0,$taskBudgetWatch.Elapsed.TotalMilliseconds-$taskCleanupUsedMs-$active)
}
function Get-TaskPackageRemaining {
 $cleanupLeft=[Math]::Max(0,$CleanupReserveMs-[int][Math]::Ceiling($taskCleanupUsedMs))
 $remaining=[Math]::Min($RunBudgetMs-[int][Math]::Ceiling((Get-TaskRunElapsed)),$TotalBudgetMs-[int][Math]::Ceiling($taskBudgetWatch.Elapsed.TotalMilliseconds)-$cleanupLeft)
 if($remaining -le 0){$script:taskBudgetExhausted=$true;$script:taskIncomplete=$true;throw 'DECKY_PACKAGE_TEST_RUN_BUDGET_EXHAUSTED'}
 return $remaining
}
function Get-TaskCleanupRemaining {
 $active=if($taskCleanupCurrentWatch){$taskCleanupCurrentWatch.Elapsed.TotalMilliseconds}else{0}
 return [Math]::Max(0,[Math]::Min($CleanupReserveMs-[int][Math]::Ceiling($taskCleanupUsedMs+$active),$TotalBudgetMs-[int][Math]::Ceiling($taskBudgetWatch.Elapsed.TotalMilliseconds)))
}
function Wait-TaskOwnedChildExit($Process){
 $limit=Get-TaskCleanupRemaining
 if($Process.HasExited){return $true}
 if($limit -le 0){$script:taskIncomplete=$true;return $false}
 return $Process.WaitForExit($limit)
}
function Quote-TaskPackageLiteral([string]$Value){return "'"+$Value.Replace("'","''")+"'"}
Write-TaskPackageProgress 'consumer-start' @{skipMutationProof=[bool]$SkipMutationProof}

function Need([bool]$Condition,[string]$Message) { if (!$Condition) { throw $Message } }
function Reject([scriptblock]$Action,[string]$Reason) {
  $message=''
  try { & $Action | Out-Null } catch { $message=$_.Exception.Message }
  if (!$message -or ($Reason -and !$message.Contains($Reason))) { throw ('Expected rejection '+$Reason+', got '+$message) }
}
function Check([string]$Name,[scriptblock]$Action) {
 Write-TaskPackageProgress 'case-enter' @{name=$Name;ordinal=$cases.Count+1}
 if($taskBudgetExhausted){$cases.Add([pscustomobject]@{name=$Name;pass=$false;executed=$false;status='NOT_RUN';error='DECKY_PACKAGE_TEST_RUN_BUDGET_EXHAUSTED'});Write-TaskPackageProgress 'case-not-run' $cases[$cases.Count-1];return}
 try{[void](Get-TaskPackageRemaining); & $Action;$cases.Add([pscustomobject]@{name=$Name;pass=$true;executed=$true})}
 catch{$cases.Add([pscustomobject]@{name=$Name;pass=$false;error=$_.Exception.Message;stack=$_.ScriptStackTrace;position=$_.InvocationInfo.PositionMessage})}
 Write-TaskPackageProgress 'case-complete' $cases[$cases.Count-1]
}
function Put([string]$Base,[string]$Name,[string]$Text) {
  $file=Join-Path $Base $Name.Replace('/','\')
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file)) | Out-Null
  [IO.File]::WriteAllText($file,$Text,(New-Object Text.UTF8Encoding($false)))
  return $file
}
function New-Case([string]$Name) {
  $caseRoot=Join-Path $EvidenceDir ($Name+'-'+[guid]::NewGuid().ToString('N'))
  $incoming=Join-Path $caseRoot 'incoming';$installed=Join-Path $caseRoot 'installed'
  [IO.Directory]::CreateDirectory($incoming) | Out-Null
  [IO.Directory]::CreateDirectory($installed) | Out-Null
  foreach ($name in Get-YmccDeckyFiles) { Put $incoming $name ('inert-v330-'+$name) | Out-Null }
  Put $incoming 'plugins/ymcc-sidebar/plugin.json' '{"name":"ymcc-sidebar"}' | Out-Null
  $lock=[ordered]@{repository='SteamDeckHomebrew/decky-loader';origin='official-release';byteIdentical=$true;loaderPatches=0;version='v3.3.0';asset=@{sha256=(Get-YmccDeckyHash (Join-Path $incoming 'PluginLoader_noconsole.exe'));size=([IO.FileInfo](Join-Path $incoming 'PluginLoader_noconsole.exe')).Length}}
  $lockFile=Put $caseRoot 'inert-lock.json' ($lock | ConvertTo-Json -Depth 8 -Compress)
  $manifest=New-YmccDeckyManifest $incoming $lockFile
  return [pscustomobject]@{home=$caseRoot;source=$incoming;target=$installed;manifest=$manifest;lock=$lockFile}
}
function Seed($Case) {
  $plan=Get-YmccDeckyPlan $Case.source $Case.target $Case.manifest
  return Install-YmccDeckyPackage $plan (Join-Path $Case.home ('backup-'+[guid]::NewGuid().ToString('N'))) -BackendPort 0
}
function Snapshot([string]$caseRoot) {
  return (@(Get-ChildItem -LiteralPath $caseRoot -File -Recurse -Force | ForEach-Object { $_.FullName.Substring($caseRoot.Length)+':'+(Get-YmccDeckyHash $_.FullName) } | Sort-Object) -join "`n")
}
Check 'Windows PowerShell direct source parses the same seven-file contract as the embedded UTF8 path' {
  $parseErrors=$null;$parseTokens=$null
  [Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'native/decky_package_maintenance.ps1'),[ref]$parseTokens,[ref]$parseErrors)|Out-Null
  Need ($parseErrors.Count -eq 0) 'Windows PowerShell direct source encoding cannot parse the maintenance consumer'
  $direct=& { . (Join-Path $root 'native/decky_package_maintenance.ps1'); @(Get-YmccDeckyFiles) }
  Need (@($direct).Count -eq 7 -and (($direct -join '|') -ceq ((Get-YmccDeckyFiles) -join '|'))) 'Direct and embedded maintenance contracts differ'
}
Check 'incoming seven assets adopt legacy home and preserve all user/other-plugin content' {
  $c=New-Case 'legacy';$settings=Put $c.target 'settings/config.json' '{"keep":true}';$other=Put $c.target 'plugins/other/dist/index.js' 'other';$loose=Put $c.target 'legacy-user.exe' 'not-ours'
  $t=Seed $c
  Need $t.plan.legacyAdoption 'Legacy adoption not recorded'
  Need (([IO.File]::ReadAllText($settings) -eq '{"keep":true}') -and ([IO.File]::ReadAllText($other) -eq 'other') -and ([IO.File]::Exists($loose))) 'Unowned data modified'
  Need ($t.applied.Count -eq 8) 'Missing asset/receipt commit'
}
Check 'unchanged package is idempotent with zero target file mutations' {
  $c=New-Case 'idempotent';Seed $c | Out-Null
  $p=Get-YmccDeckyPlan $c.source $c.target $c.manifest
  Need (@($p.entries).Count -eq 0) 'Same package unnecessarily rewrites files'
}
Check 'official root JSON migration no longer consumes the YMCC receipt across two starts' {
  $c=New-Case 'safe-receipt';Seed $c|Out-Null
  Put $c.target 'settings/.ymcc-managed.json' 'older-private-settings-copy'|Out-Null
  foreach($restart in 1,2){foreach($entry in @(Get-ChildItem -LiteralPath $c.target -File -Force -Filter '*.json')){[IO.File]::Move($entry.FullName,(Join-Path $c.target ('settings/'+$entry.Name)))}}
  Need ([IO.File]::Exists((Join-Path $c.target '.ymcc-managed.receipt'))) 'Official settings migration consumed installed ownership metadata'
  Need ((Get-YmccDeckyPlan $c.source $c.target $c.manifest).entries.Count -eq 0) 'Repeated Loader startup invalidated the managed package identity'
}
Check 'duplicate legacy root receipt is retired transactionally while settings stay untouched' {
  $c=New-Case 'legacy-duplicate';Seed $c|Out-Null
  [IO.File]::Copy((Join-Path $c.target '.ymcc-managed.receipt'),(Join-Path $c.target '.ymcc-managed.json'))
  $settings=Put $c.target 'settings/.ymcc-managed.json' 'old-private-settings-copy';$before=Snapshot $c.target
  $transaction=Seed $c
  Need (![IO.File]::Exists((Join-Path $c.target '.ymcc-managed.json'))) 'Old root JSON still crashes official settings migration'
  Need ([IO.File]::ReadAllText($settings) -ceq 'old-private-settings-copy') 'Settings-side metadata changed'
  Undo-YmccDeckyTransaction $transaction
  Need ((Snapshot $c.target) -ceq $before) 'Migration rollback did not restore exact prior root and canonical receipt'
}
Check 'legacy-only receipt adopts the safe name and still enforces downgrade refusal' {
  $c=New-Case 'legacy-only';Seed $c|Out-Null
  [IO.File]::Move((Join-Path $c.target '.ymcc-managed.receipt'),(Join-Path $c.target '.ymcc-managed.json'))
  Seed $c|Out-Null
  Need (![IO.File]::Exists((Join-Path $c.target '.ymcc-managed.json')) -and [IO.File]::Exists((Join-Path $c.target '.ymcc-managed.receipt'))) 'Legacy identity not migrated'
  [IO.File]::Move((Join-Path $c.target '.ymcc-managed.receipt'),(Join-Path $c.target '.ymcc-managed.json'))
  $old=Get-Content -LiteralPath (Join-Path $c.target '.ymcc-managed.json') -Raw|ConvertFrom-Json;$old.loaderVersion='v3.4.0';Put $c.target '.ymcc-managed.json' ($old|ConvertTo-Json -Depth 8 -Compress)|Out-Null
  $before=Snapshot $c.target;Reject {Get-YmccDeckyPlan $c.source $c.target $c.manifest} 'DOWNGRADE_REFUSED'
  Need ((Snapshot $c.target) -ceq $before) 'Downgrade refusal mutated legacy identity'
}
Check 'settings-migrated old identity still prevents package downgrade when no root receipt exists' {
  $c=New-Case 'moved-downgrade';Seed $c|Out-Null
  $old=Get-Content -LiteralPath (Join-Path $c.target '.ymcc-managed.receipt') -Raw|ConvertFrom-Json;$old.loaderVersion='v3.4.0';Put $c.target 'settings/.ymcc-managed.json' ($old|ConvertTo-Json -Depth 8 -Compress)|Out-Null
  [IO.File]::Delete((Join-Path $c.target '.ymcc-managed.receipt'));$before=Snapshot $c.target
  Reject {Get-YmccDeckyPlan $c.source $c.target $c.manifest} 'DOWNGRADE_REFUSED'
  Need ((Snapshot $c.target) -ceq $before) 'Migrated settings identity was ignored or changed'
}
Check 'foreign corrupt legacy JSON is never removed to hide a launch collision' {
  $c=New-Case 'corrupt-legacy';Seed $c|Out-Null;Put $c.target '.ymcc-managed.json' '{bad'|Out-Null;$before=Snapshot $c.target
  $refused=$false;try{Get-YmccDeckyPlan $c.source $c.target $c.manifest|Out-Null}catch{$refused=$true}
  Need $refused 'Corrupt legacy root did not block the install'
  Need ((Snapshot $c.target) -ceq $before) 'Corrupt or foreign legacy data was erased'
}
Check 'missing and corrupt owned resources automatically repair as one paired package' {
  $c=New-Case 'repair';Seed $c | Out-Null
  [IO.File]::Delete((Join-Path $c.target 'plugins/ymcc-sidebar/dist/index.js'))
  Put $c.target 'PluginLoader_noconsole.exe' 'corrupt' | Out-Null
  $t=Seed $c
  Need ($t.applied.Count -eq 2) 'Did not repair both resources'
  foreach ($f in $c.manifest.files) { Need ((Get-YmccDeckyHash (Join-Path $c.target $f.path)) -eq $f.sha256) 'Repair not verified' }
}
Check 'higher incoming official version consumes incoming identity rather than old client digest' {
  $c=New-Case 'upgrade';Seed $c | Out-Null
  Put $c.source 'PluginLoader_noconsole.exe' 'inert-v340-official' | Out-Null
  $lock=Get-Content -LiteralPath $c.lock -Raw | ConvertFrom-Json;$lock.version='v3.4.0';$lock.asset.sha256=Get-YmccDeckyHash (Join-Path $c.source 'PluginLoader_noconsole.exe');$lock.asset.size=([IO.FileInfo](Join-Path $c.source 'PluginLoader_noconsole.exe')).Length
  $lock | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $c.lock -Encoding UTF8
  $c.manifest=New-YmccDeckyManifest $c.source $c.lock
  $t=Seed $c
  Need ($t.plan.manifest.loaderVersion -eq 'v3.4.0') 'Upgrade pinned to outgoing version'
}
Check 'old YMCC package cannot downgrade a recorded newer Loader' {
  $c=New-Case 'downgrade';Seed $c | Out-Null
  $receipt=Join-Path $c.target '.ymcc-managed.receipt';$old=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json;$old.loaderVersion='v3.4.0';$old | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding UTF8
  $before=Snapshot $c.target
  Reject { Get-YmccDeckyPlan $c.source $c.target $c.manifest } 'DOWNGRADE_REFUSED'
  Need ((Snapshot $c.target) -eq $before) 'Downgrade rejection changed files'
}
Check 'first old-updater package identity prevents later downgrade without a managed receipt' {
  $c=New-Case 'first-upgrade';Seed $c|Out-Null
  [IO.File]::Delete((Join-Path $c.target '.ymcc-managed.receipt'))
  $old=$c.manifest|ConvertTo-Json -Depth 8|ConvertFrom-Json;$old.loaderVersion='v3.4.0'
  $identity=Put $c.home 'program/decky-package.json' ($old|ConvertTo-Json -Depth 8 -Compress)
  $before=Snapshot $c.target
  Reject { Get-YmccDeckyPlan $c.source $c.target $c.manifest $identity } 'DOWNGRADE_REFUSED'
  Need ((Snapshot $c.target) -eq $before) 'First-upgrade downgrade changed files'
  $old.loaderVersion='v3.3.0';[IO.File]::WriteAllText($identity,($old|ConvertTo-Json -Depth 8 -Compress))
  $plan=Get-YmccDeckyPlan $c.source $c.target $c.manifest $identity
  Need (!$plan.legacyAdoption) 'Known first-upgrade identity was discarded'
  $t=Install-YmccDeckyPackage $plan (Join-Path $c.home 'first-managed-backup') -BackendPort 0
  Need ([IO.File]::Exists((Join-Path $c.target '.ymcc-managed.receipt'))) 'Next transaction did not record managed receipt'
  Put $c.target '.ymcc-managed.receipt' 'corrupt'|Out-Null
  Reject { Get-YmccDeckyPlan $c.source $c.target $c.manifest $identity } ''
}
Check 'package hash mismatch is rejected before any installed mutation' {
  $c=New-Case 'tamper';Seed $c | Out-Null;$before=Snapshot $c.target
  Put $c.source 'plugins/ymcc-sidebar/dist/index.js' 'tampered' | Out-Null
  Reject { Seed $c } 'PACKAGE_HASH_MISMATCH'
  Need ((Snapshot $c.target) -eq $before) 'Tampered package changed installed files'
}
Check 'missing incoming manifest never authorizes blind merge' {
  $c=New-Case 'missing-manifest'
  Reject { Get-YmccDeckyPlan $c.source $c.target $null } 'MANIFEST_INVALID'
  Need ([string](Snapshot $c.target) -eq '') 'Missing manifest wrote files'
}
Check 'missing incoming file cannot form a partial plugin install' {
  $c=New-Case 'missing-source';[IO.File]::Delete((Join-Path $c.source 'plugins/ymcc-sidebar/plugin.json'))
  Reject { Seed $c } 'PACKAGE_HASH_MISMATCH'
  Need ([string](Snapshot $c.target) -eq '') 'Partial incoming package installed'
}
Check 'foreign plugin identity cannot be consumed as YMCC' {
  $c=New-Case 'foreign-plugin';Put $c.source 'plugins/ymcc-sidebar/plugin.json' '{"name":"other"}' | Out-Null;$c.manifest=New-YmccDeckyManifest $c.source $c.lock
  Reject { Seed $c } 'PLUGIN_IDENTITY_INVALID'
}
Check 'old locked Loader fails before first mutation without killing its owner' {
  $c=New-Case 'locked';Seed $c | Out-Null;Put $c.target 'PluginLoader_noconsole.exe' 'old-busy-loader' | Out-Null;$before=Snapshot $c.target
  $lease=[IO.File]::Open((Join-Path $c.target 'PluginLoader_noconsole.exe'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  try { Reject { Seed $c } ''; Need ($lease.CanRead) 'Foreign resource owner terminated' } finally { $lease.Dispose() }
  Need ((Snapshot $c.target) -eq $before) 'Lock rejection partially committed'
}
Check 'receipt-owned obsolete code is removed only with exact original bytes' {
  $c=New-Case 'obsolete';Seed $c | Out-Null
  $stale=Put $c.target 'plugins/ymcc-sidebar/dist/old-chunk.js' 'old chunk'
  $receipt=Join-Path $c.target '.ymcc-managed.receipt';$prev=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
  $prev.files+= [pscustomobject]@{path='plugins/ymcc-sidebar/dist/old-chunk.js';bytes=([IO.FileInfo]$stale).Length;sha256=(Get-YmccDeckyHash $stale)}
  $prev | ConvertTo-Json -Depth 8 -Compress | Set-Content -LiteralPath $receipt -Encoding UTF8
  $t=Seed $c
  Need (![IO.File]::Exists($stale)) 'Recorded obsolete code survived'
  Restore-YmccDeckyPackage $t -BackendPort 0
  Need ([IO.File]::ReadAllText($stale) -eq 'old chunk') 'Outer updater rollback lost retired code'
}
Check 'modified obsolete code and unrecorded files survive cleanup' {
  $c=New-Case 'modified-obsolete';Seed $c | Out-Null
  $stale=Put $c.target 'plugins/ymcc-sidebar/dist/old-chunk.js' 'old chunk';$unrecorded=Put $c.target 'plugins/ymcc-sidebar/dist/user.js' 'user'
  $receipt=Join-Path $c.target '.ymcc-managed.receipt';$prev=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
  $prev.files+= [pscustomobject]@{path='plugins/ymcc-sidebar/dist/old-chunk.js';bytes=([IO.FileInfo]$stale).Length;sha256=(Get-YmccDeckyHash $stale)}
  $prev | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding UTF8
  [IO.File]::WriteAllText($stale,'user-modified')
  $t=Seed $c
  Need (@($t.plan.preservedObsolete).Count -eq 1) 'Modified obsolete record lost'
  Need (([IO.File]::ReadAllText($stale) -eq 'user-modified') -and [IO.File]::Exists($unrecorded)) 'User content erased'
}
Check 'receipt cannot claim settings or another plugin as obsolete code' {
  foreach ($name in @('settings/config.json','plugins/other/dist/index.js','../outside.js','plugins/ymcc-sidebar/dist/../../settings/config.js')) {
    $c=New-Case 'bad-receipt';Seed $c | Out-Null;$receipt=Join-Path $c.target '.ymcc-managed.receipt';$prev=Get-Content -LiteralPath $receipt -Raw | ConvertFrom-Json
    $prev.files+= [pscustomobject]@{path=$name;bytes=1;sha256=('0'*64)}
    $prev | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $receipt -Encoding UTF8
    Reject { Seed $c } 'MANAGED_FILE_INVALID'
  }
}
Check 'malformed and oversized installed receipts fail safely' {
  foreach ($text in @('not-json',('x'*131073))) {
    $c=New-Case 'bad-json';Put $c.target '.ymcc-managed.receipt' $text | Out-Null;$before=Snapshot $c.target
    Reject { Seed $c } ''
    Need ((Snapshot $c.target) -eq $before) 'Invalid receipt mutated target'
  }
}
Check 'mid-transaction copy failure restores all prior committed files' {
  $c=New-Case 'mid-copy';Seed $c | Out-Null
  Put $c.target 'PluginLoader_noconsole.exe' 'old loader' | Out-Null;Put $c.target 'LICENSE.decky-loader' 'old license' | Out-Null
  $p=Get-YmccDeckyPlan $c.source $c.target $c.manifest;$before=Snapshot $c.target
  Put $c.source 'LICENSE.decky-loader' 'changed-after-planning' | Out-Null
  Reject { Install-YmccDeckyPackage $p (Join-Path $c.home 'failed-backup') -BackendPort 0 } 'STAGED_HASH_MISMATCH'
  Need ((Snapshot $c.target) -eq $before) 'Failed install did not restore exact prior state'
}
Check 'later YMCC startup handshake failure rolls Decky back with whole updater' {
  $c=New-Case 'outer-rollback';Seed $c | Out-Null;Put $c.target 'PluginLoader_noconsole.exe' 'previous' | Out-Null
  $before=Snapshot $c.target;$t=Seed $c
  Restore-YmccDeckyPackage $t -BackendPort 0;Restore-YmccDeckyPackage $t -BackendPort 0
  Need ((Snapshot $c.target) -eq $before) 'Outer rollback or repeated rollback changed state'
}
Check 'target changes after planning cannot be overwritten as stale data' {
  $c=New-Case 'target-race';Seed $c | Out-Null;Put $c.target 'PluginLoader_noconsole.exe' 'previous' | Out-Null
  $p=Get-YmccDeckyPlan $c.source $c.target $c.manifest;Put $c.target 'PluginLoader_noconsole.exe' 'new concurrent owner' | Out-Null
  Reject { Install-YmccDeckyPackage $p (Join-Path $c.home 'backup-race') -BackendPort 0 } 'TARGET_CHANGED_BEFORE_INSTALL'
  Need ([IO.File]::ReadAllText((Join-Path $c.target 'PluginLoader_noconsole.exe')) -eq 'new concurrent owner') 'Concurrent write overwritten'
}
Check 'rollback refuses to overwrite another owner after successful commit' {
  $c=New-Case 'rollback-race';$t=Seed $c
  Put $c.target 'PluginLoader_noconsole.exe' 'new concurrent owner' | Out-Null
  Reject { Restore-YmccDeckyPackage $t -BackendPort 0 } 'ROLLBACK_CONCURRENT_CHANGE'
  Need ([IO.File]::ReadAllText((Join-Path $c.target 'PluginLoader_noconsole.exe')) -eq 'new concurrent owner') 'Rollback overwrote concurrent owner'
}
Check 'rollback revalidates every file after its initial snapshot before restoring it' {
  $c=New-Case 'rollback-late-race';Seed $c|Out-Null
  Put $c.target 'PluginLoader_noconsole.exe' 'previous-loader'|Out-Null
  Put $c.target 'LICENSE.decky-loader' 'previous-license'|Out-Null
  $t=Seed $c
  $script:DeckyLateRaceTarget=Join-Path $c.target 'PluginLoader_noconsole.exe'
  $script:DeckyLateRaceReads=0
  $script:DeckyLateRaceHash=(Get-Command Get-YmccDeckyHash).ScriptBlock
  function script:Get-YmccDeckyHash([string]$Path) {
    if ($Path -ceq $script:DeckyLateRaceTarget) {
      $script:DeckyLateRaceReads++
      # First read is the whole-transaction snapshot. The second happens only
      # after another entry has already been restored, immediately at this file.
      if ($script:DeckyLateRaceReads -eq 2) { [IO.File]::WriteAllText($Path,'late-concurrent-owner') }
    }
    return & $script:DeckyLateRaceHash $Path
  }
  try {
    Reject { Restore-YmccDeckyPackage $t -BackendPort 0 } 'ROLLBACK_CONCURRENT_CHANGE'
    Need ([IO.File]::ReadAllText($script:DeckyLateRaceTarget) -eq 'late-concurrent-owner') 'Late concurrent update was silently overwritten'
  } finally { Invoke-Expression ('function script:Get-YmccDeckyHash {'+$script:DeckyLateRaceHash.ToString()+'}') }
}
Check 'path traversal directory targets and overlapping source are rejected' {
  $c=New-Case 'paths'
  foreach ($bad in @('../outside','/outside','C:/outside','a\\b','a//b','a./b')) { Reject { Resolve-YmccDeckyPath $c.target $bad } '' }
  Reject { Get-YmccDeckyPlan $c.source $c.source $c.manifest } 'OVERLAP'
  [IO.Directory]::CreateDirectory((Join-Path $c.target 'PluginLoader_noconsole.exe')) | Out-Null
  Reject { Seed $c } 'TARGET_FILE_IS_DIRECTORY'
}
Check 'reparse ancestor cannot redirect cleanup or update outside managed home' {
  $c=New-Case 'junction';$external=Join-Path $c.home 'external';[IO.Directory]::CreateDirectory($external) | Out-Null
  $link=Join-Path $c.target 'plugins';New-Item -ItemType Junction -Path $link -Target $external | Out-Null
  Reject { Seed $c } 'REPARSE_POINT_REFUSED'
  Need ((Snapshot $external) -eq '') 'Junction escaped home'
}
Check 'duplicate or noncanonical incoming paths cannot bypass exact fileset' {
  foreach ($kind in @('duplicate','case','traversal','extra')) {
    $c=New-Case 'incoming-fileset';$m=($c.manifest | ConvertTo-Json -Depth 8 | ConvertFrom-Json)
    if ($kind -eq 'duplicate') { $m.files+= $m.files[0] }
    elseif ($kind -eq 'case') { $m.files[1].path='license.decky-loader' }
    elseif ($kind -eq 'traversal') { $m.files[1].path='../LICENSE.decky-loader' }
    else { $m.files+= [pscustomobject]@{path='plugins/ymcc-sidebar/dist/new.js';bytes=1;sha256=('0'*64)} }
    Reject { Get-YmccDeckyPlan $c.source $c.target $m } ''
  }
}
Check 'official provenance identity and size mismatches reject package construction' {
  foreach ($kind in @('patched','origin','hash','size')) {
    $c=New-Case 'lock';$l=Get-Content -LiteralPath $c.lock -Raw | ConvertFrom-Json
    if ($kind -eq 'patched') { $l.loaderPatches=1 } elseif ($kind -eq 'origin') { $l.origin='fork' } elseif ($kind -eq 'hash') { $l.asset.sha256='0'*64 } else { $l.asset.size=1 }
    $l | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $c.lock -Encoding UTF8
    Reject { New-YmccDeckyManifest $c.source $c.lock } ''
  }
}
Check 'backend port lease refuses an active watcher and is returned on failure' {
  $c=New-Case 'backend-watch';$p=Get-YmccDeckyPlan $c.source $c.target $c.manifest
  $foreign=Open-YmccDeckyBackendLease 0;$port=$foreign.LocalEndpoint.Port
  try {
    Reject { Install-YmccDeckyPackage $p (Join-Path $c.home 'occupied-backup') -BackendPort $port } 'BACKEND_IN_USE_OR_UNAVAILABLE'
    Need ([string](Snapshot $c.target) -eq '') 'Busy backend allowed partial install'
    Need ($foreign.LocalEndpoint.Port -eq $port) 'Foreign backend was stopped'
    Reject { $unexpected=Open-YmccDeckyBackendLease $port; $unexpected.Stop() } 'BACKEND_IN_USE_OR_UNAVAILABLE'
  } finally { $foreign.Stop() }
  $t=Install-YmccDeckyPackage $p (Join-Path $c.home 'unoccupied-backup') -BackendPort $port
  $probe=Open-YmccDeckyBackendLease $port;$probe.Stop()
  Restore-YmccDeckyPackage $t -BackendPort $port
  $probe=Open-YmccDeckyBackendLease $port;$probe.Stop()
  Need ([string](Snapshot $c.target) -eq '') 'Rollback after reservation lost data'
}
# Parse the actual emitted command without executing any updater operation.
function Get-TaskNativeUpdaterCommand([string]$Native,[string]$Name) {
  $pattern='(?m)^\s*f << "\s*(?<command>'+[regex]::Escape($Name)+' \$powerControlSource [^"\r\n]*)\\n";\s*$'
  $found=[regex]::Matches($Native,$pattern)
  Need ($found.Count -eq 1) ('Native updater command count changed: '+$Name)
  $tokens=$null;$errors=$null
  $parsed=[Management.Automation.Language.Parser]::ParseInput($found[0].Groups['command'].Value,[ref]$tokens,[ref]$errors)
  Need ($errors.Count -eq 0 -and $parsed.EndBlock.Statements.Count -eq 1) ('Native updater command is not a single parsed statement: '+$Name)
  $statement=$parsed.EndBlock.Statements[0]
  Need ($statement -is [Management.Automation.Language.PipelineAst] -and $statement.PipelineElements.Count -eq 1) ('Native updater command pipeline changed: '+$Name)
  $command=$statement.PipelineElements[0]
  Need ($command -is [Management.Automation.Language.CommandAst] -and $command.GetCommandName() -ceq $Name) ('Native updater command identity changed: '+$Name)
  return $command
}
Check 'native updater includes production transaction and excludes generic merge/rollback' {
  $native=Get-Content -LiteralPath (Join-Path $root 'native/main.cpp') -Raw -Encoding UTF8
  Need ($native.Contains('f << ymcc::deckypackage::maintenanceScript()')) 'Native updater does not embed actual consumer'
  Need ($native.Contains('$deckyPlan = Get-YmccDeckyPlan') -and $native.Contains('$deckyTransaction = Install-YmccDeckyPackage')) 'Native updater lacks plan/commit'
  Need ($native.Contains('Restore-YmccDeckyPackage $deckyTransaction')) 'Outer updater lacks Decky rollback'
  $rollback=Get-TaskNativeUpdaterCommand $native 'Register-TreeForRollback'
  Need ($rollback.CommandElements.Count -eq 5 -and $rollback.CommandElements[1].Extent.Text -ceq '$powerControlSource' -and $rollback.CommandElements[2].Extent.Text -ceq '$pcDir' -and $rollback.CommandElements[3].SafeGetValue() -ceq 'PowerControl') 'Generic rollback source/target changed'
  $excluded=@($rollback.CommandElements[4].SafeGetValue())
  Need (@('pawnio','fan-host-v2','decky'|Where-Object { $excluded -cnotcontains $_ }).Count -eq 0) 'Generic rollback still includes Decky'
  $copy=Get-TaskNativeUpdaterCommand $native 'Copy-TreeChecked'
  Need ($copy.CommandElements.Count -eq 4 -and $copy.CommandElements[1].Extent.Text -ceq '$powerControlSource' -and $copy.CommandElements[2].Extent.Text -ceq '$pcDir' -and $copy.CommandElements[3] -is [Management.Automation.Language.ArrayExpressionAst]) 'Generic copy source/target/exclusion array changed'
  $copyExclusions=$copy.CommandElements[3]
  $xd=@($copyExclusions.FindAll({param($node) $node -is [Management.Automation.Language.StringConstantExpressionAst] -and $node.Value -ceq '/XD'},$true))
  Need ($xd.Count -eq 1) 'Generic copy exclusion switch missing'
  foreach($ownedName in @('fan-host','decky')){
    $joins=@($copyExclusions.FindAll({param($node) $node -is [Management.Automation.Language.CommandAst] -and $node.GetCommandName() -ceq 'Join-Path'},$true)|Where-Object {$_.CommandElements.Count -eq 3 -and $_.CommandElements[1].Extent.Text -ceq '$powerControlSource' -and $_.CommandElements[2].SafeGetValue() -ceq $ownedName})
    Need ($joins.Count -eq 1) ('Generic copy still overwrites owned directory '+$ownedName)
  }
  Need ($native.IndexOf('$deckyPlan = Get-YmccDeckyPlan') -lt $native.IndexOf('  Register-TreeForRollback $programSource')) 'Decky validation occurs after mutations'
}
# Candidate fixture owner only. New Add-Type/compiler authority must be reviewed
# and pinned before execution; this proposal has only AST validation.
if (!$SkipMutationProof -and !$taskBudgetExhausted) {
$fixtureToolManifest=Join-Path $root 'tools/ymcc_sim/global/decky-update-recovery/package-fixture-tools.json'
Need ((Get-YmccDeckyHash $fixtureToolManifest) -ceq '2ba2e58c4c1367375cd265192972a371e993544880623f4b51ca412cfe85746a') 'Fixture compiler manifest identity changed'
$fixtureAuthority=Get-Content -LiteralPath $fixtureToolManifest -Raw -Encoding UTF8|ConvertFrom-Json
foreach($toolPin in $fixtureAuthority.files){Need ((Get-YmccDeckyHash $toolPin.path) -ceq $toolPin.sha256) ('Fixture compiler input changed: '+$toolPin.path)}
Add-Type -ReferencedAssemblies @($fixtureAuthority.referencedAssemblies) -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class YmccDeckyPackageFixtureJob : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong r,w,o,rb,wb,ob; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC {
  public long perProcess,perJob; public uint flags; public UIntPtr minWorking,maxWorking;
  public uint active; public UIntPtr affinity; public uint priority,scheduling;
 }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED {
  public BASIC basic; public IO_COUNTERS io; public UIntPtr processMemory,jobMemory,peakProcess,peakJob;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attrs,string name);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref EXTENDED info,uint length);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 IntPtr handle;
 public YmccDeckyPackageFixtureJob(){
  handle=CreateJobObject(IntPtr.Zero,null);if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());
  var info=new EXTENDED();info.basic.flags=0x00002000;
  if(!SetInformationJobObject(handle,9,ref info,(uint)Marshal.SizeOf(typeof(EXTENDED)))){var e=Marshal.GetLastWin32Error();Dispose();throw new Win32Exception(e);}
 }
 public void Assign(IntPtr process){if(!AssignProcessToJobObject(handle,process))throw new Win32Exception(Marshal.GetLastWin32Error());}
 public bool Close(){if(handle==IntPtr.Zero)return true;if(!CloseHandle(handle))return false;handle=IntPtr.Zero;return true;}
 public void Dispose(){Close();}
}
"@
Write-TaskPackageProgress 'fixture-job-type-ready' @{compiler=$fixtureAuthority.compiler;manifestSha256='2ba2e58c4c1367375cd265192972a371e993544880623f4b51ca412cfe85746a';moduleVersionId=[YmccDeckyPackageFixtureJob].Assembly.ManifestModule.ModuleVersionId.ToString();loadedModuleCompleteness=$false}
}
if (!$SkipMutationProof) {
  Check 'rollback and downgrade semantic mutations fail their original production assertions' {
    $mutations=@(
      @{name='no-rollback';from='Undo-YmccDeckyTransaction $transaction';to='Write-Verbose "injected missing rollback"';case='mid-transaction copy failure restores all prior committed files'},
      @{name='allow-downgrade';from="throw 'DECKY_PACKAGE_DOWNGRADE_REFUSED_UPDATE_YMCC'";to="Write-Verbose 'injected downgrade permission'";case='old YMCC package cannot downgrade a recorded newer Loader'},
      @{name='stale-rollback-snapshot';from='if ($now -cne $entry.after) { throw ''DECKY_ROLLBACK_CONCURRENT_CHANGE'' }';to='Write-Verbose "injected stale rollback snapshot"';case='rollback revalidates every file after its initial snapshot before restoring it'}
    )
    # Independent roots and Mutex namespaces share one wall-clock run deadline
    # and cleanup reserve. Every child executes the complete original suite.
    $ownedMutations=New-Object 'System.Collections.Generic.List[object]'
    $batchError=$null
    try {
      foreach($mutation in $mutations) {
      [void](Get-TaskPackageRemaining)
      $taskMutationStates[$mutation.name]='RUNNING'
      if (!$body.Contains($mutation.from)) { throw ('Missing mutation anchor '+$mutation.name) }
      $negativeRoot=Join-Path $EvidenceDir ('mutation-'+$mutation.name)
      [IO.Directory]::CreateDirectory((Join-Path $negativeRoot 'native'))|Out-Null
      [IO.Directory]::CreateDirectory((Join-Path $negativeRoot 'tools'))|Out-Null
      $mutantBody=$body.Replace($mutation.from,$mutation.to)
      # Adjacent generated literals do not contain the joined body verbatim.
      # Rebuild only their payload, keeping every mutated script character.
      $literalParts=New-Object 'System.Collections.Generic.List[string]'
      for ($offset=0; $offset -lt $mutantBody.Length;) {
        $partLength=[Math]::Min(2048,$mutantBody.Length-$offset)
        if ([char]::IsHighSurrogate($mutantBody[$offset+$partLength-1])) { --$partLength }
        $literalParts.Add('R"YMCCDECKYPKG('+$mutantBody.Substring($offset,$partLength)+')YMCCDECKYPKG"')
        $offset+=$partLength
      }
      $lastLiteral=$scriptLiteralMatches[$scriptLiteralMatches.Count-1]
      $mutantHeader=$header.Substring(0,$scriptLiteralMatches[0].Index)+($literalParts.ToArray() -join "`n")+$header.Substring($lastLiteral.Index+$lastLiteral.Length)
      [IO.File]::WriteAllText((Join-Path $negativeRoot 'native/decky_package_maintenance.ps1'),$mutantBody,(New-Object Text.UTF8Encoding($true)))
      [IO.File]::WriteAllText((Join-Path $negativeRoot 'native/decky_package_maintenance_script.h'),$mutantHeader,(New-Object Text.UTF8Encoding($false)))
      [IO.File]::Copy((Join-Path $root 'native/main.cpp'),(Join-Path $negativeRoot 'native/main.cpp'),$false)
      $test=Join-Path $negativeRoot 'tools/decky_package_maintenance_selftest.ps1'
      [IO.File]::Copy($PSCommandPath,$test,$false)
      $negativeOut=Join-Path $negativeRoot 'evidence'
      Write-TaskPackageProgress 'mutation-enter' @{name=$mutation.name;expectedCase=$mutation.case}
      $waitBudget=[Math]::Min(25000,(Get-TaskPackageRemaining))
      $cleanupForChild=Get-TaskCleanupRemaining
      if($cleanupForChild -le 0){$script:taskIncomplete=$true;throw 'DECKY_PACKAGE_TEST_CLEANUP_RESERVE_EXHAUSTED'}
      $childTotal=[Math]::Min($waitBudget+$cleanupForChild,$TotalBudgetMs-[int]$taskBudgetWatch.ElapsedMilliseconds)
      $cleanupForChild=[Math]::Max(0,$childTotal-$waitBudget)
      $eventName='Local\YMCC-DeckyPackageFixture-'+[guid]::NewGuid().ToString('N')
      $ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$eventName)
      $taskCommand='$ErrorActionPreference=''Stop'';$gate=[Threading.EventWaitHandle]::OpenExisting('+(Quote-TaskPackageLiteral $eventName)+');try{if(!$gate.WaitOne(1000)){exit 12}}finally{$gate.Dispose()}; & '+(Quote-TaskPackageLiteral $test)+' -EvidenceDir '+(Quote-TaskPackageLiteral $negativeOut)+' -SkipMutationProof -TotalBudgetMs '+$childTotal+' -RunBudgetMs '+$waitBudget+' -CleanupReserveMs '+$cleanupForChild+';if($?){exit 0}else{exit 1}'
      $info=New-Object Diagnostics.ProcessStartInfo
      $info.FileName=(Get-Process -Id $PID).Path
      $info.Arguments='-NoProfile -NonInteractive -ExecutionPolicy Bypass -EncodedCommand '+[Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($taskCommand))
      $info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
      $child=New-Object Diagnostics.Process;$child.StartInfo=$info
        $owned=[ordered]@{mutation=$mutation;negativeRoot=$negativeRoot;negativeOut=$negativeOut;ready=$ready;child=$child;info=$info;job=$null;exit=$null;timedOut=$false;assigned=$false;jobClosed=$null;childPid=$null;stdout=$null;stderr=$null;error=$null;exitObserved=$false;startedAt=[DateTimeOffset]::UtcNow.ToString('o');watch=[Diagnostics.Stopwatch]::StartNew();waitBudgetMs=$waitBudget}
        $ownedMutations.Add($owned)
        [void](Get-TaskPackageRemaining)
        $owned.job=New-Object YmccDeckyPackageFixtureJob
        if(!$child.Start()){throw 'Negative test process did not start'}
        $owned.childPid=$child.Id
        $owned.stdout=$child.StandardOutput.ReadToEndAsync();$owned.stderr=$child.StandardError.ReadToEndAsync()
        $owned.job.Assign($child.Handle);$owned.assigned=$true;[void]$ready.Set()
        Write-TaskPackageProgress 'mutation-process-started' @{name=$mutation.name;pid=$owned.childPid;file=$info.FileName;jobAssigned=$owned.assigned;waitBudgetMs=$waitBudget;elapsedTotalMs=$taskBudgetWatch.Elapsed.TotalMilliseconds;sharedDeadline=$true;independentRoot=$negativeRoot}
      }
      foreach($owned in $ownedMutations) {
        try {
          if(!$owned.child.HasExited){
            $actualWait=[Math]::Min($owned.waitBudgetMs-[int][Math]::Ceiling($owned.watch.Elapsed.TotalMilliseconds),(Get-TaskPackageRemaining))
            if($actualWait -le 0 -or !$owned.child.WaitForExit($actualWait)){
              $owned.timedOut=$true;$script:taskBudgetExhausted=$true;$script:taskIncomplete=$true
              throw 'DECKY_PACKAGE_TEST_RUN_BUDGET_EXHAUSTED'
            }
          }
          $owned.exit=$owned.child.ExitCode
        }catch{$owned.error=$_.Exception.Message;if($owned.error -eq 'DECKY_PACKAGE_TEST_RUN_BUDGET_EXHAUSTED'){$owned.timedOut=$true;$script:taskIncomplete=$true}}
      }
    }catch{$batchError=$_.Exception.Message;$script:taskIncomplete=$true}
    finally {
      # Retire every started child even when a sibling fails or launch throws.
      foreach($owned in $ownedMutations) {
        $script:taskCleanupCurrentWatch=[Diagnostics.Stopwatch]::StartNew()
        try {
          if($owned.job){$owned.jobClosed=$owned.job.Close();if($owned.jobClosed){$owned.job=$null}else{$script:taskIncomplete=$true;if(!$owned.error){$owned.error='Private Job close failed; ownership unresolved'}}}
          if($owned.childPid){
            if(!$owned.assigned -and !$owned.child.HasExited){$owned.child.Kill()}
            $owned.exitObserved=Wait-TaskOwnedChildExit $owned.child
            if($owned.exitObserved){$owned.exit=$owned.child.ExitCode}else{$script:taskIncomplete=$true}
          }
        }catch{$script:taskIncomplete=$true;if(!$owned.error){$owned.error='Private child retirement: '+$_.Exception.Message}}
        finally {
          $taskCleanupCurrentWatch.Stop();$script:taskCleanupUsedMs+=$taskCleanupCurrentWatch.Elapsed.TotalMilliseconds;$script:taskCleanupCurrentWatch=$null
          if($taskCleanupUsedMs -gt $CleanupReserveMs){$script:taskIncomplete=$true}
          $receipt=[ordered]@{name=$owned.mutation.name;expectedCase=$owned.mutation.case;startedAt=$owned.startedAt;finishedAt=[DateTimeOffset]::UtcNow.ToString('o');elapsedMs=$owned.watch.Elapsed.TotalMilliseconds;elapsedTotalMs=$taskBudgetWatch.Elapsed.TotalMilliseconds;totalBudgetMs=$TotalBudgetMs;runBudgetMs=$RunBudgetMs;cleanupReserveMs=$CleanupReserveMs;cleanupUsedMs=$taskCleanupUsedMs;pid=$owned.childPid;file=$owned.info.FileName;args=$owned.info.Arguments;waitBudgetMs=$owned.waitBudgetMs;jobAssigned=$owned.assigned;jobCloseSucceeded=$owned.jobClosed;timedOut=$owned.timedOut;exitCode=$owned.exit;exitObserved=$owned.exitObserved;error=$owned.error;sharedDeadline=$true;independentRoot=$owned.negativeRoot;allChildrenLaunchedBeforeWait=($ownedMutations.Count -eq $mutations.Count)}
          $taskMutationReceipts.Add([pscustomobject]$receipt)
          try {
            $receipt|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $owned.negativeRoot 'process.json') -Encoding UTF8
            if($owned.exitObserved -and $owned.stdout -and $owned.stderr -and $owned.stdout.IsCompleted -and $owned.stderr.IsCompleted){[IO.File]::WriteAllText((Join-Path $owned.negativeRoot 'run.log'),$owned.stdout.GetAwaiter().GetResult()+$owned.stderr.GetAwaiter().GetResult())}
            else{$script:taskIncomplete=$true}
            Write-TaskPackageProgress 'mutation-process-finished' $receipt
          }catch{
            $script:taskIncomplete=$true
            if(!$batchError){$batchError='Private mutation receipt write failed: '+$_.Exception.Message}
          }finally{$owned.child.Dispose();$owned.ready.Dispose()}
        }
        if($owned.timedOut){$taskMutationStates[$owned.mutation.name]='TIMEOUT'}elseif($owned.error -or !$owned.exitObserved){$taskMutationStates[$owned.mutation.name]='INCOMPLETE'}
      }
    }
    if($batchError){throw $batchError}
    foreach($owned in $ownedMutations) {
      if($owned.error){throw $owned.error}
      Need (!$owned.timedOut -and $owned.exitObserved -and $null -ne $owned.exit -and $owned.jobClosed -eq $true) 'Negative test requires exact non-timeout exit and owned Job close'
      $negative=Get-Content -LiteralPath (Join-Path $owned.negativeOut 'result.json') -Raw -Encoding UTF8|ConvertFrom-Json
      $expected=@($negative.cases|Where-Object{$_.name -ceq $owned.mutation.case})
      if($negative.complete -ne $true -or $negative.status -cne 'FAIL'){$script:taskIncomplete=$true;if($negative.status -ceq 'TIMEOUT'){$script:taskBudgetExhausted=$true};$taskMutationStates[$owned.mutation.name]=$negative.status}
      Need ($negative.complete -eq $true -and $negative.status -ceq 'FAIL') 'TIMEOUT or INCOMPLETE cannot witness the original mutation assertion'
      Need ($owned.exit -ne 0 -and $negative.pass -eq $false -and $expected.Count -eq 1 -and $expected[0].pass -eq $false) ('Mutation escaped original assertion '+$owned.mutation.name)
      $taskMutationStates[$owned.mutation.name]='EXPECTED_NONZERO'
    }
  }
}
$failures=@($cases | Where-Object { !$_.pass })
if((Get-TaskRunElapsed) -gt $RunBudgetMs -or $taskBudgetWatch.Elapsed.TotalMilliseconds -gt $TotalBudgetMs -or $taskCleanupUsedMs -gt $CleanupReserveMs){$taskBudgetExhausted=$true;$taskIncomplete=$true}
$taskMutationsComplete=$SkipMutationProof -or (@($taskMutationStates.Values|Where-Object{$_ -ne 'EXPECTED_NONZERO'}).Count -eq 0)
if(!$taskMutationsComplete){$taskIncomplete=$true}
$taskComplete=!$taskBudgetExhausted -and !$taskIncomplete -and $taskMutationsComplete
$taskStatus=if($taskBudgetExhausted){'TIMEOUT'}elseif(!$taskComplete){'INCOMPLETE'}elseif($failures.Count){'FAIL'}else{'PASS'}
$result=[ordered]@{pass=($failures.Count -eq 0 -and $taskComplete);status=$taskStatus;complete=$taskComplete;totalBudgetMs=$TotalBudgetMs;runBudgetMs=$RunBudgetMs;cleanupReserveMs=$CleanupReserveMs;elapsedTotalMs=$taskBudgetWatch.Elapsed.TotalMilliseconds;cleanupUsedMs=$taskCleanupUsedMs;mutations=$taskMutationStates;mutationRetirement=$taskMutationReceipts.ToArray();count=$cases.Count;passed=($cases.Count-$failures.Count);cases=$cases.ToArray();sourceSha256=(Get-YmccDeckyHash (Join-Path $root 'native/decky_package_maintenance.ps1'));embeddedSha256=(Get-YmccDeckyHash (Join-Path $root 'native/decky_package_maintenance_script.h'));realSteamActions=0;hardwareWrites=0;productProcessesLaunched=0;productionGlobalMutexAccessed=$false;fixtureMutexNamespaceRemapped=$fixtureMutexRemapped;scope='Exact embedded production PowerShell executes real file transactions in isolated evidence; inert Loader bytes, no installed/physical acceptance'}
$result | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath (Join-Path $EvidenceDir 'result.json') -Encoding UTF8
if (!$taskComplete){Write-Error ('DECKY_PACKAGE_TEST_'+$taskStatus) -ErrorAction Continue;exit 124}
if ($failures.Count) { $failures | Format-Table -Wrap; throw ('Decky package maintenance failed '+$failures.Count+'/'+$cases.Count) }
Write-Output ('DECKY_PACKAGE_MAINTENANCE_PASS cases='+$cases.Count)
