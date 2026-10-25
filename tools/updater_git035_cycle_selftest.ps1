[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$Official035Zip,[Parameter(Mandatory=$true)][string]$Official035Native,[Parameter(Mandatory=$true)][string]$ProcessFixture,[string]$CurrentInputHost,[Parameter(Mandatory=$true)][string]$ModuleConsumer,[int]$Cycles=2,[string]$HelperEngine='', [string]$ProductSourceRoot='', [string]$CandidateZip='', [string]$CandidateSha256='', [string[]]$CaseModes=@('035-manual','035-auto-ready','035-hung','036-manual','036-auto-ready-hung','036-rollback-retry','035-036-037-manual','035-036-037-auto-ready','036-interrupt-repair','036-interrupt-failed-repair','036-interrupt-successor'))
$ErrorActionPreference='Stop'
$fixtureRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$repo=if($ProductSourceRoot){[IO.Path]::GetFullPath($ProductSourceRoot)}else{$fixtureRoot};$evidence=[IO.Path]::GetFullPath($EvidenceDir)
$executedHarness=[pscustomobject]@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash}
[IO.Directory]::CreateDirectory($evidence)|Out-Null
. (Join-Path $repo 'native/decky_package_maintenance.ps1')
. (Join-Path $repo 'native/module_package_maintenance.ps1')
. (Join-Path $repo 'tools/module_payload_packaging.ps1')
function Need([bool]$ok,[string]$why){if(!$ok){throw $why}}
function Put([string]$directory,[string]$relative,[string]$value){$file=Join-Path $directory $relative;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))|Out-Null;[IO.File]::WriteAllText($file,$value,[Text.UTF8Encoding]::new($false));return $file}
function Safe([string]$value){$full=[IO.Path]::GetFullPath($value);if(!$full.StartsWith($evidence+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'FIXTURE_PATH_ESCAPED'};return $full}
function Digest([string]$file){(Get-FileHash -LiteralPath $file -Algorithm SHA256).Hash.ToLowerInvariant()}
Need ((Digest $Official035Zip) -eq 'e4c52bfdf8e48542936d6fd23ef81a9c54154c3d1211a6038e27c7348cae1e3f') 'not the published 035 archive'
$original=Safe (Join-Path $evidence 'Official035')
Add-Type -AssemblyName System.IO.Compression.FileSystem
if (!(Test-Path -LiteralPath $original)) {
  $archive=[IO.Compression.ZipFile]::OpenRead($Official035Zip)
  try { foreach($entry in $archive.Entries){$dest=[IO.Path]::GetFullPath((Join-Path $original $entry.FullName));if(!$dest.StartsWith($original+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'OFFICIAL_ARCHIVE_TRAVERSAL'}} } finally {$archive.Dispose()}
  [IO.Compression.ZipFile]::ExtractToDirectory($Official035Zip,$original)
}
$identityFiles=@('native/main.cpp','native/decky_package_maintenance.ps1','native/module_package_maintenance.ps1','native/update_install_recovery.ps1','native/updater_hc_runtime.ps1','native/updater_hook_tools.ps1','tools/updater_helper_extract.mjs','tools/updater_git035_cycle_selftest.ps1')
$sourceHashes=@($identityFiles|ForEach-Object {@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash}})
$results=New-Object 'System.Collections.Generic.List[object]'
function Snapshot([string]$directory){$list=@{};foreach($file in Get-ChildItem -LiteralPath $directory -Recurse -File){$list[$file.FullName.Substring($directory.Length+1)]=Digest $file.FullName};return $list}
function Stop-Fixture([string]$executable){foreach($p in @(Get-Process -Name YeManCC -ErrorAction SilentlyContinue)){try{if($p.Path -and [IO.Path]::GetFullPath($p.Path) -ieq $executable -and !$p.HasExited){$null=$p.Handle;$p.Kill();$null=$p.WaitForExit(3000)}}finally{$p.Dispose()}}}
$candidateIdentity=$null
$candidateInventory=$null
$packageDerivations=[Collections.Generic.List[object]]::new()
if($CandidateZip){
  Need ($ProductSourceRoot -and $CandidateSha256 -cmatch '^[a-f0-9]{64}$') 'CANDIDATE_REQUIRES_FROZEN_SOURCE_AND_SHA256'
  Need ((Digest $CandidateZip) -ceq $CandidateSha256) 'CANDIDATE_ARCHIVE_SHA256_MISMATCH'
  $candidateRoot=Safe (Join-Path $evidence 'CandidateOriginal')
  Need (!(Test-Path -LiteralPath $candidateRoot)) 'CANDIDATE_EVIDENCE_ALREADY_EXISTS_USE_FRESH_DIRECTORY'
  $archive=[IO.Compression.ZipFile]::OpenRead($CandidateZip);$seen=@{}
  try {
    foreach($entry in $archive.Entries){
      $name=$entry.FullName.Replace('\','/')
      Need ($name -and $name -notmatch '(^/|:|(^|/)\.\.?(?:/|$)|[\x00-\x1f])') 'CANDIDATE_ARCHIVE_PATH_INVALID'
      $dest=[IO.Path]::GetFullPath((Join-Path $candidateRoot $name))
      Need ($dest.StartsWith($candidateRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'CANDIDATE_ARCHIVE_TRAVERSAL'
      Need (!$seen.ContainsKey($name.TrimEnd('/'))) 'CANDIDATE_ARCHIVE_DUPLICATE'
      $seen[$name.TrimEnd('/')]=$true
      Need (($entry.ExternalAttributes -band 0xF0000000) -ne 0xA0000000) 'CANDIDATE_ARCHIVE_SYMLINK'
    }
  }finally{$archive.Dispose()}
  [IO.Compression.ZipFile]::ExtractToDirectory($CandidateZip,$candidateRoot)
  $candidateInventory=Snapshot $candidateRoot
  foreach($name in @('YeManCC\YeManCC.exe','YeManCC\version.json','YeManCC\update-manifest.json','YeManCC\module-package.json','PowerControl\settings-baseline.json','PowerControl\settings-defaults-v1.json')){
    Need ($candidateInventory.ContainsKey($name)) ('CANDIDATE_REQUIRED_FILE '+$name)
  }
  $candidatePackageVersion=[string](Get-Content -LiteralPath (Join-Path $candidateRoot 'YeManCC/version.json') -Raw -Encoding UTF8|ConvertFrom-Json).version
  $frozenPackageVersion=[string](Get-Content -LiteralPath (Join-Path $repo 'version.json') -Raw -Encoding UTF8|ConvertFrom-Json).version
  Need ($candidatePackageVersion -ceq $frozenPackageVersion) 'CANDIDATE_SOURCE_VERSION_MISMATCH'
  $candidateIdentity=[pscustomobject]@{version=$candidatePackageVersion;path=[IO.Path]::GetFullPath($CandidateZip);sha256=$CandidateSha256;bytes=(Get-Item -LiteralPath $CandidateZip).Length;productSourceRoot=$repo;files=$candidateInventory.Count;originalInventory='candidate-original-inventory.json'}
  $candidateInventory|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $evidence 'candidate-original-inventory.json') -Encoding UTF8
}else{Need ([bool]$CurrentInputHost) 'SYNTHETIC_MODE_REQUIRES_INPUT_HOST'}
function Stage-CandidatePackage([string]$stage,[string]$version){
  Need (!(Test-Path -LiteralPath $stage)) 'CANDIDATE_STAGE_ALREADY_EXISTS'
  Copy-Item -LiteralPath $candidateRoot -Destination $stage -Recurse
  Copy-Item -LiteralPath $ProcessFixture -Destination (Join-Path $stage 'YeManCC/YeManCC.exe') -Force
  foreach($row in @(@('YeManCC/version.json','version'),@('YeManCC/update-manifest.json','packageVersion'),@('YeManCC/module-package.json','packageVersion'))){
    $file=Join-Path $stage $row[0];$doc=Get-Content -LiteralPath $file -Raw -Encoding UTF8|ConvertFrom-Json
    Need ([bool]$doc.PSObject.Properties[$row[1]]) ('CANDIDATE_VERSION_FIELD_MISSING '+$row[0])
    Need ([string]$doc.($row[1]) -ceq $candidateIdentity.version) ('CANDIDATE_INCONSISTENT_ORIGINAL_VERSION '+$row[0])
    # The real036 package needs only the inert executable port for035->036.
    # Future-version cases retain the original three explicit metadata ports.
    if($version -cne $candidateIdentity.version){
      $doc.($row[1])=$version
      $doc|ConvertTo-Json -Depth 32|Set-Content -LiteralPath $file -Encoding UTF8
    }
  }
  $derived=Snapshot $stage;$changes=@();$allowed=@('YeManCC\YeManCC.exe','YeManCC\version.json','YeManCC\update-manifest.json','YeManCC\module-package.json')
  Need ($derived.Count -eq $candidateInventory.Count) 'CANDIDATE_DERIVATION_FILE_COUNT_CHANGED'
  foreach($key in $candidateInventory.Keys){
    Need ($derived.ContainsKey($key)) ('CANDIDATE_DERIVATION_REMOVED '+$key)
    if($derived[$key] -cne $candidateInventory[$key]){Need ($key -in $allowed) ('CANDIDATE_UNDECLARED_DERIVATION '+$key);$changes+=@{path=$key;before=$candidateInventory[$key];after=$derived[$key]}}
  }
  $expectedChanges=if($version -ceq $candidateIdentity.version){1}else{4}
  Need ($changes.Count -eq $expectedChanges) 'CANDIDATE_DERIVATION_INCOMPLETE'
  $packageDerivations.Add([pscustomobject]@{version=$version;stage=$stage;originalFiles=$candidateInventory.Count;unchangedFiles=($derived.Count-$expectedChanges);expectedChangedFiles=$expectedChanges;changedFiles=$changes;subprocessHealth='CustomSteamLibrary process health port is inert; original release gate owns actual child health validation'})
}
function Assert-CandidatePayload([string]$install,[string]$template){
  if(!$CandidateZip){return}
  foreach($relative in @('PowerControl/settings-baseline.json','PowerControl/settings-defaults-v1.json','YeManCC/module-package.json')){
    Need ((Digest (Join-Path $install $relative)) -ceq (Digest (Join-Path $template $relative))) ('FINAL_PACKAGE_BASELINE_OR_MODULE_DRIFT '+$relative)
  }
  $manifest=Get-Content -LiteralPath (Join-Path $template 'YeManCC/module-package.json') -Raw -Encoding UTF8|ConvertFrom-Json
  foreach($entry in $manifest.files){Need ((Digest (Join-Path (Join-Path $install 'PowerControl') $entry.path)) -ceq $entry.sha256) ('FINAL_PACKAGE_INPUT_DRIFT '+$entry.path)}
}
function Assert-CandidateImmutable([string]$install,[string]$template){
  if(!$CandidateZip){return}
  $incoming=Join-Path $template 'PowerControl/handheldcompanion-runtime'
  foreach($runtime in @(Get-ChildItem -LiteralPath $incoming -Directory -ErrorAction Stop)){
    $target=Join-Path $install ('PowerControl/handheldcompanion-runtime/'+$runtime.Name)
    $expected=Snapshot $runtime.FullName;$actual=Snapshot $target
    Need ($actual.Count -eq $expected.Count) 'FINAL_PACKAGE_HC_INVENTORY_DRIFT'
    foreach($key in $expected.Keys){Need ($actual[$key] -ceq $expected[$key]) ('FINAL_PACKAGE_HC_BYTE_DRIFT '+$key)}
  }
  $expected=Snapshot (Join-Path $template 'PowerControl/fan-host-v2');$actual=Snapshot (Join-Path $install 'PowerControl/fan-host-v2')
  Need ($actual.Count -eq $expected.Count) 'FINAL_PACKAGE_FAN_HOST_INVENTORY_DRIFT'
  foreach($key in $expected.Keys){Need ($actual[$key] -ceq $expected[$key]) ('FINAL_PACKAGE_FAN_HOST_BYTE_DRIFT '+$key)}
  $expected=Snapshot (Join-Path $template 'PowerControl/ymcc-shared-runtime');$actual=Snapshot (Join-Path $install 'PowerControl/ymcc-shared-runtime')
  Need ($actual.Count -eq $expected.Count) 'FINAL_PACKAGE_SHARED_INVENTORY_DRIFT'
  foreach($key in $expected.Keys){Need ($actual[$key] -ceq $expected[$key]) ('FINAL_PACKAGE_SHARED_BYTE_DRIFT '+$key)}
}
function Assert-RollbackHostRuntime([string]$Install,[string]$CaseRoot){
  if(!$CandidateZip){return}
  $hostManifest=Join-Path $Install 'PowerControl/fan-host-v2/YeManFanHost.payload.json'
  Need (Test-Path -LiteralPath $hostManifest -PathType Leaf) 'ROLLBACK_HOST_MANIFEST_MISSING'
  $hostPayload=Get-Content -LiteralPath $hostManifest -Raw -Encoding UTF8|ConvertFrom-Json
  $runtimeManifest=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $hostManifest) ([string]$hostPayload.runtimeManifest)))
  $allowed=[IO.Path]::GetFullPath((Join-Path $Install 'PowerControl/handheldcompanion-runtime')).TrimEnd('\')+'\'
  Need ($runtimeManifest.StartsWith($allowed,[StringComparison]::OrdinalIgnoreCase)) 'ROLLBACK_HOST_RUNTIME_PATH_ESCAPED'
  $exists=Test-Path -LiteralPath $runtimeManifest -PathType Leaf
  [pscustomobject]@{hostManifest=$hostManifest;hostRuntimeId=$hostPayload.runtimeId;runtimeManifest=$runtimeManifest;runtimeManifestExists=$exists}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $CaseRoot 'intermediate-rollback-coherence.json') -Encoding UTF8
  Need $exists 'ROLLBACK_HOST_REFERENCES_REMOVED_HC_RUNTIME'
  $runtime=Get-Content -LiteralPath $runtimeManifest -Raw -Encoding UTF8|ConvertFrom-Json
  Need ($runtime.runtimeId -ceq $hostPayload.runtimeId) 'ROLLBACK_HOST_RUNTIME_ID_MISMATCH'
  $runtimeRoot=Split-Path -Parent $runtimeManifest
  foreach($entry in $runtime.files){
    $file=[IO.Path]::GetFullPath((Join-Path $runtimeRoot ([string]$entry.path)))
    Need ($file.StartsWith($runtimeRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'ROLLBACK_HC_FILE_PATH_ESCAPED'
    Need ((Test-Path -LiteralPath $file -PathType Leaf) -and (Digest $file) -ceq ([string]$entry.sha256).ToLowerInvariant()) ('ROLLBACK_HC_FILE_MISSING_OR_CHANGED '+$entry.path)
  }
}
function Assert-RollbackSnapshot($before,$after,[string]$install,[string]$template){
  if($CandidateZip){
    # HC, Host and their exact shared closure survive as one verified unit.
    Assert-CandidateImmutable $install $template
    # This is the user's no-old-runtime policy; all other old files stay exact.
    $ordinaryBefore=@{};$ordinaryAfter=@{}
    foreach($key in $before.Keys){if($key -notmatch '^PowerControl\\(handheldcompanion-runtime|fan-host-v2|ymcc-shared-runtime)\\'){$ordinaryBefore[$key]=$before[$key]}}
    foreach($key in $after.Keys){if($key -notmatch '^PowerControl\\(handheldcompanion-runtime|fan-host-v2|ymcc-shared-runtime)\\'){$ordinaryAfter[$key]=$after[$key]}}
    $before=$ordinaryBefore;$after=$ordinaryAfter
  }
  Need ($after.Count -eq $before.Count) ('rollback added/lost files: '+$before.Count+' -> '+$after.Count)
  foreach($key in $before.Keys){Need ($after[$key] -eq $before[$key]) ('rollback changed '+$key)}
}
function Stage-Package([string]$stage,[string]$version){
  if($CandidateZip){Stage-CandidatePackage $stage $version;return}
  [IO.Directory]::CreateDirectory((Join-Path $stage 'YeManCC'))|Out-Null
  [IO.Directory]::CreateDirectory((Join-Path $stage 'PowerControl'))|Out-Null
  Copy-Item -LiteralPath $ProcessFixture -Destination (Join-Path $stage 'YeManCC/YeManCC.exe')
  Put $stage 'YeManCC/YeMan-Support.html' '<html>fixture</html>'|Out-Null
  Put $stage 'YeManCC/version.json' ('{"version":"'+$version+'"}')|Out-Null
  Put $stage 'YeManCC/ui-fixture.html' ('<html>new YMCCUI '+$version+'</html>')|Out-Null
  Put $stage 'PowerControl/pawnio/YeManTdpCtl.exe' 'inert TDP fixture'|Out-Null
  Put $stage 'PowerControl/pawnio/_internal/python-fixture.dll' 'inert Python runtime fixture'|Out-Null
  Put $stage 'PowerControl/Sleep/system-blacklist.txt' 'new official rules'|Out-Null
  [IO.Directory]::CreateDirectory((Join-Path $stage 'PowerControl/feature-assets'))|Out-Null
  foreach($feature in @('virtual-gamepad','gyro-motion')){
    $source=Join-Path $repo ('..\..\Release\PowerControl\feature-assets\'+$feature)
    Copy-Item -LiteralPath $source -Destination (Join-Path $stage 'PowerControl/feature-assets') -Recurse -Force
  }
  foreach($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')){
    Copy-Item -LiteralPath (Join-Path $CurrentInputHost $name) -Destination (Join-Path $stage ('PowerControl/feature-assets/virtual-gamepad/'+$name)) -Force
  }
  Put $stage 'PowerControl/feature-assets/gyro-motion/package-layout.json' ('{"fixtureVersion":"'+$version+'"}')|Out-Null
  $decky=Join-Path $stage 'PowerControl/decky';foreach($name in Get-YmccDeckyFiles){Put $decky $name $(if($name -eq 'plugins/ymcc-sidebar/plugin.json'){'{"name":"ymcc-sidebar"}'}else{'inert-'+$name})|Out-Null}
  $files=@(Get-YmccDeckyFiles|ForEach-Object{[pscustomobject]@{path=$_;bytes=([IO.FileInfo](Join-Path $decky $_)).Length;sha256=(Digest (Join-Path $decky $_))}})
  $manifest=[pscustomobject]@{schemaVersion=1;owner='YMCC';origin='official-release';loaderVersion='v3.3.0';loaderSha256=(Digest (Join-Path $decky 'PluginLoader_noconsole.exe'));files=$files}
  $manifest|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $stage 'YeManCC/decky-package.json') -Encoding UTF8
  $layout=[pscustomobject]@{schemaVersion=1;packageId='yemancc-update';packageVersion=$version;requiredRoots=@('YeManCC','PowerControl');roots=@(@{source='YeManCC';target='YeManCC';mode='program'},@{source='PowerControl';target='PowerControl';mode='power-control'});rules=@{fanHostV2='preserve-existing'};deckyManaged=$manifest}
  $layout|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $stage 'YeManCC/update-manifest.json') -Encoding UTF8
  $null=Write-YmccInputCompatibilityPayload $stage $version
}
$template36=Safe (Join-Path $evidence 'Package036');Stage-Package $template36 '0.0.36'
$template37=Safe (Join-Path $evidence 'Package037');Stage-Package $template37 '0.0.37'
$templateArchives=@{}
foreach($template in @($template36,$template37)) {
 $archivePath=Safe ($template+'-full-source.zip');[IO.Compression.ZipFile]::CreateFromDirectory($template,$archivePath,[IO.Compression.CompressionLevel]::Fastest,$false);$templateArchives[$template]=$archivePath
}
$packageDerivations|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $evidence 'candidate-derivations.json') -Encoding UTF8
function Wait-FixtureReady($Process,[string]$Marker){
  $until=[DateTime]::UtcNow.AddSeconds(10)
  while([DateTime]::UtcNow -lt $until -and !$Process.HasExited){
    if(Test-Path -LiteralPath $Marker){
      try{$ready=Get-Content -LiteralPath $Marker -Raw -Encoding UTF8|ConvertFrom-Json}catch{Start-Sleep -Milliseconds 20;continue}
      if($ready.phase -eq 'ready' -and [int]$ready.pid -eq $Process.Id -and [string]$ready.created -ceq $Process.StartTime.ToUniversalTime().ToFileTimeUtc().ToString() -and [long]$ready.hwnd -gt 0){return}
    }
    Start-Sleep -Milliseconds 20
  }
  throw 'FIXTURE_NOT_READY_BEFORE_UPDATE'
}
function Run-Helper([string]$caseRoot,[string]$install,[string]$template,[string]$emitter,[string]$version,[string]$parentMode,[bool]$legacy,[string]$Interrupt="",[bool]$DenyRollbackChildStop=$false,[string]$DataRoot=""){
  $data=if($DataRoot){Safe $DataRoot}else{Join-Path $caseRoot ('data-'+[guid]::NewGuid().ToString('N'))};$stage=Join-Path $data 'staging';[IO.Directory]::CreateDirectory($data)|Out-Null
  if($DataRoot -and (Test-Path -LiteralPath $stage)){
    # The native prepare boundary clears fixed staging. Preserve those same
    # fixture-owned bytes by archiving them instead of recursively deleting.
    $safeStage=Safe $stage;$resolvedStage=(Resolve-Path -LiteralPath $safeStage).ProviderPath
    $archiveStage=Safe (Join-Path $data ('previous-staging-evidence-'+[guid]::NewGuid().ToString('N')))
    Need ($resolvedStage -ceq $safeStage -and [IO.Path]::GetDirectoryName($safeStage) -ceq $data -and [IO.Path]::GetDirectoryName($archiveStage) -ceq $data) 'persistent staging archive escaped its owned directory'
    for($ancestor=$safeStage;$ancestor;$ancestor=[IO.Path]::GetDirectoryName($ancestor)){
      Need (((Get-Item -LiteralPath $ancestor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'persistent fixture ancestor is a link'
      if($ancestor -ceq $evidence){break}
    }
    Move-Item -LiteralPath $safeStage -Destination $archiveStage -ErrorAction Stop
  }
  Copy-Item -LiteralPath $template -Destination $stage -Recurse
  $retainedPackage=Join-Path $data 'package.zip';Copy-Item -LiteralPath $templateArchives[$template] -Destination $retainedPackage
  Need (Test-Path -LiteralPath $retainedPackage) 'FULL_SOURCE_PACKAGE_NOT_RETAINED'
  $hookProcesses=[Collections.Generic.List[object]]::new();$hookPaths=@()
  if(!$legacy){
    $hookDirectory=Join-Path $caseRoot 'ExternalHookTools';[IO.Directory]::CreateDirectory($hookDirectory)|Out-Null
    foreach($name in @('OptiscalerClient.exe','LosslessScaling.exe')){
      $hookPath=Join-Path $hookDirectory $name;Copy-Item -LiteralPath $ProcessFixture -Destination $hookPath -Force
      $hookPaths+=$hookPath;$hookReady=Join-Path $caseRoot ('tool-ready-'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.json')
      $tool=Start-Process -FilePath $hookPath -ArgumentList @($(if($name -eq 'LosslessScaling.exe'){'hung'}else{'normal'}),'--fixture-ready',('"'+$hookReady+'"')) -PassThru -WindowStyle Hidden
      $hookProcesses.Add($tool);Wait-FixtureReady $tool $hookReady
    }
  }
  $exe=Join-Path $install 'YeManCC/YeManCC.exe';$parentReady=Join-Path $caseRoot ('app-ready-'+[guid]::NewGuid().ToString('N').Substring(0,8)+'.json');$parent=Start-Process -FilePath $exe -ArgumentList @($parentMode,'--fixture-ready',('"'+$parentReady+'"')) -PassThru -WindowStyle Hidden
  try {
    Wait-FixtureReady $parent $parentReady
    $helper=Join-Path $data 'update.ps1'
    $config=[ordered]@{zip=$retainedPackage;install=$install;staging=$stage;script=$helper;progress=(Join-Path $data 'update-progress.json');recoveryJournal=(Join-Path $data 'install-journal.json');hookToolPaths=$hookPaths;version=$version;parentPid=$parent.Id;parentCreationFileTime=$parent.StartTime.ToUniversalTime().ToFileTimeUtc().ToString()}
    # Native admission creates a protected journal in production. Only privileged
    # ACL IO is replaced for this non-admin isolated fixture.
    @{schema=1;owner='YMCC.FullInstall';phase='authorized';operationId='fixture-operation';version=$version;sha256=('0'*64);exePath=$exe;powerControl=(Join-Path $install 'PowerControl')}|ConvertTo-Json|Set-Content -LiteralPath $config.recoveryJournal -Encoding UTF8
    $configPath=Join-Path $data 'config.json';$config|ConvertTo-Json|Set-Content -LiteralPath $configPath -Encoding UTF8
    & node (Join-Path $repo 'tools/updater_helper_extract.mjs') $emitter $configPath | Out-Null
    if($LASTEXITCODE){throw 'PRODUCTION_HELPER_EXTRACTION_FAILED'}
    $text=[IO.File]::ReadAllText($helper)
    if($CandidateZip){
      # The installer is privileged ACL IO. Bind the exact incoming bytes and
      # owned target, then use an inert subprocess port in this non-admin suite.
      $aclNeedles=@('    Invoke-YmccFanPayloadInstaller $fanHostAclScript $fanHostTarget','    & powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File $fanHostAclScript -PayloadDirectory $fanHostTarget')
      $matchedAclPorts=@($aclNeedles|Where-Object{$text.Contains($_)})
      Need ($matchedAclPorts.Count -eq 1) 'exactly one original legacy/current Fan installer call must use the scoped ACL fixture port'
      foreach($aclNeedle in $matchedAclPorts){
        $expectedAclTarget=[IO.Path]::GetFullPath((Join-Path $install 'PowerControl/fan-host-v2'))
        $expectedAclHash=Digest (Join-Path $template 'PowerControl/fan-host-v2/install-fan-host-payload.ps1')
        $port='    if([IO.Path]::GetFullPath($fanHostTarget) -cne '+("'"+$expectedAclTarget.Replace("'","''")+"'")+'){throw "FIXTURE_FAN_ACL_TARGET_ESCAPED"}; if((Get-FileHash -LiteralPath $fanHostAclScript -Algorithm SHA256).Hash -ine '+("'"+$expectedAclHash+"'")+'){throw "FIXTURE_FAN_ACL_SCRIPT_CHANGED"}; [IO.File]::WriteAllText((Join-Path $staging "..\privileged-fan-acl-port.txt"),"fixture-only; original incoming script bytes verified; privileged ACL write not executed"); $global:LASTEXITCODE=0'
        $text=$text.Replace($aclNeedle,$port)
      }
      $errorNeedle='  $installError = $_.Exception.Message'
      $text=$text.Replace($errorNeedle,$errorNeedle+"`n"+'  try { [IO.File]::WriteAllText((Join-Path $staging "..\helper-original-error.txt"),$_.Exception.ToString()+[Environment]::NewLine+$_.ScriptStackTrace) } catch {}')
    }
    if($CandidateZip){
      # File deployment and production manifest validation remain real. This
      # suite does not launch the real CustomSteamLibrary/UI/Steam subprocess.
      # Its original successful/failed startup paths run in the release gate.
      $healthNeedle='Invoke-CustomSteamLibraryHealthCheck }'
      if($text.Contains($healthNeedle)){
        $text=$text.Replace($healthNeedle,"[IO.File]::WriteAllText((Join-Path `$staging '..\inert-child-health-port.txt'),'fixture-only; actual child health belongs to the release gate') }")
      }
    }
    # Only platform elevation and test clocks are injected. All install,
    # identity, copy, rollback and health control flow remains production code.
    if(!$legacy){
      $aclPort='function Assert-FixtureRecoveryAcl([string]$Path) { $full=[IO.Path]::GetFullPath($Path); if($full -ine $script:FixtureRecoveryRoot -and !$full.StartsWith($script:FixtureRecoveryRoot+"\",[StringComparison]::OrdinalIgnoreCase)){throw "FIXTURE_RECOVERY_PATH_ESCAPED"}; if((Get-Item -LiteralPath $full -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw "FIXTURE_RECOVERY_LINK"} }'+"`n"
      $aclPort='$script:FixtureRecoveryRoot = '+("'"+$data.Replace("'","''")+"'")+"`n"+$aclPort
      $text=$text.Replace('    Assert-YmccRecoveryAcl ','    Assert-FixtureRecoveryAcl ').Replace('        Assert-YmccRecoveryAcl ','        Assert-FixtureRecoveryAcl ')
      $text=$text.Replace("try {`n  `$moduleLease = Enter-YmccModuleMaintenance",$aclPort+"try {`n  `$moduleLease = Enter-YmccModuleMaintenance")
      $text=$text.Replace('Set-Acl -LiteralPath $temporary -AclObject $acl -ErrorAction Stop','# privileged ACL writer is an inert fixture port')
    }
    $text=$text.Replace('$isElevated = $currentPrincipal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)','$isElevated = $true # inert isolated fixture token')
    $inventory=@'
function Get-Process {
  [CmdletBinding()] param([int[]]$Id,[string[]]$Name)
  $arguments=@{};if($PSBoundParameters.ContainsKey('Id')){$arguments.Id=$Id};if($PSBoundParameters.ContainsKey('Name')){$arguments.Name=$Name}
  foreach($candidate in @(Microsoft.PowerShell.Management\Get-Process @arguments -ErrorAction SilentlyContinue)){
    try {$identity=Get-YmccUpdaterProcessIdentity -ProcessId $candidate.Id;$image=if($identity){$identity.Image}else{''};if($image -and $image.StartsWith($script:FixtureProcessRoot+'\',[StringComparison]::OrdinalIgnoreCase)){$candidate}}catch{}
  }
}
'@
    if($legacy){
      $identitySource=[IO.File]::ReadAllText((Join-Path $repo 'native/module_package_maintenance.ps1'),[Text.Encoding]::UTF8).TrimStart([char]0xFEFF)
      $identityEnd=$identitySource.IndexOf('function Stop-YmccUpdaterParent')
      Need ($identityEnd -gt 0) 'fixture read-only identity prefix missing'
      $inventory=$identitySource.Substring(0,$identityEnd)+"`n"+$inventory
    }
    $inventory='$script:FixtureProcessRoot = '+("'"+$install.Replace("'","''")+"'" )+"`n"+$inventory
    $text=$text.Replace("`$ErrorActionPreference = 'Stop'","`$ErrorActionPreference = 'Stop'`n"+$inventory)

    $text=$text.Replace('$parentExitTimeoutSeconds = 180','$parentExitTimeoutSeconds = 1').Replace('$handshakeTimeoutSeconds = 180','$handshakeTimeoutSeconds = 2').Replace('$gracefulStopSeconds = 30','$gracefulStopSeconds = 0').Replace('$childExitTimeoutSeconds = 180','$childExitTimeoutSeconds = 2')
    $text=$text.Replace('Stop-YmccUpdateHookTools $updateHookToolPaths','Stop-YmccUpdateHookTools $updateHookToolPaths 100 2000')
    $text=$text.Replace('Stop-YmccUpdaterParent $parentPid $exePath $parentCreationFileTime','Stop-YmccUpdaterParent $parentPid $exePath $parentCreationFileTime 300 2000')
    $exitReceipt=Join-Path $data 'parent-exit-mode.txt'
    if(!$legacy){
      $exitLine='  $parentExitMode = Stop-YmccUpdaterParent $parentPid $exePath $parentCreationFileTime 300 2000'
      $text=$text.Replace($exitLine,$exitLine+"`n"+'  [IO.File]::WriteAllText('+("'"+$exitReceipt.Replace("'","''")+"'")+', [string]$parentExitMode)')
    }

    $text=$text.Replace("(Join-Path `$rollbackRoot 'DeckySnapshot')", "(Join-Path `$rollbackRoot 'DeckySnapshot') -BackendPort 0").Replace('Restore-YmccDeckyPackage $deckyTransaction','Restore-YmccDeckyPackage $deckyTransaction -BackendPort 0')
    $text=$text.Replace('Start-Process -FilePath $exePath','Start-Process -WindowStyle Hidden -FilePath $exePath')
    # Observation is inserted at actual function entry; no debugger or new
    # command owner is involved. Keep the original outer timeout unchanged.
    if(!$legacy){
      $tracePath=Join-Path $data 'helper-steps.log'
      $traceTokens=$null;$traceErrors=$null;$traceAst=[Management.Automation.Language.Parser]::ParseInput($text,[ref]$traceTokens,[ref]$traceErrors)
      Need (!$traceErrors.Count) 'emitted helper parse failed before trace injection'
      $traceNames=@('Copy-TreeChecked','Install-YmccModulePackage','Install-YmccDeckyPackage','Assert-TreeMatch','Restore-OrdinaryFiles','Set-UpdateProgress','Stop-YmccUpdateHookTools','Stop-YmccUpdaterParent','Get-UpdateLayoutRoots','Get-YmccDeckyPlan','Stop-ResidentProcesses','Get-ResidentUpdaterProcesses','Get-YmccModulePlan','Register-TreeForRollback','Get-YmccUpdaterProcessIdentity','Get-AdditionalLayoutRootProcesses')
      $traceFunctions=@($traceAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -in $traceNames},$true)|Sort-Object {$_.Body.Extent.StartOffset} -Descending)
      foreach($function in $traceFunctions){
        $literal="'"+$tracePath.Replace("'","''")+"'"
        $line="`n  if (!(Test-Path -LiteralPath "+$literal+") -or (Get-Item -LiteralPath "+$literal+").Length -lt 65536) { [IO.File]::AppendAllText("+$literal+",([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()+' "+$function.Name+" '+[string]`$source+' '+[string]`$message+[Environment]::NewLine)) }`n"
        $offset=$function.Body.Extent.StartOffset+1;$text=$text.Insert($offset,$line)
      }
    }
    if(!$legacy){
      $traceLiteral="'"+$tracePath.Replace("'","''")+"'"
      $traceBefore='[IO.File]::AppendAllText('+$traceLiteral+',([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()+" parent-close-before "+$ProcessId+[Environment]::NewLine)); '
      $traceAfter='; [IO.File]::AppendAllText('+$traceLiteral+',([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()+" parent-close-after "+$ProcessId+[Environment]::NewLine))'
      $text=$text.Replace('$null=$process.CloseMainWindow()',$traceBefore+'$null=$process.CloseMainWindow()'+$traceAfter)
      $waitStart='[IO.File]::AppendAllText('+$traceLiteral+',([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()+" parent-wait-before "+$ProcessId+" "+$GraceMilliseconds+[Environment]::NewLine)); '
      $waitEnd='; [IO.File]::AppendAllText('+$traceLiteral+',([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds().ToString()+" parent-wait-after "+$ProcessId+[Environment]::NewLine)); '
      $text=$text.Replace('if ($process.WaitForExit($GraceMilliseconds)) { return', $waitStart+'$fixtureGraceExited=$process.WaitForExit($GraceMilliseconds)'+$waitEnd+'if ($fixtureGraceExited) { return')
    }
    $interruptionMarker=Join-Path $data 'interrupted-after-exe.json'
    if($Interrupt){
      $hook="[IO.File]::WriteAllText('"+$interruptionMarker.Replace("'","''")+"','executable-replaced'); while (`$true) { Start-Sleep -Milliseconds 100 }"
      $needle='  Move-Item -LiteralPath $newExe -Destination $exePath -Force'
      Need ($text.Contains($needle)) 'interruption anchor missing'
      $text=$text.Replace($needle,$needle+"`n"+$hook)
    }
    if($DenyRollbackChildStop){
      Need (!$legacy) 'rollback child stop fault belongs only to the current helper'
      # Fault ports only: the old PID command is denied, and the existing
      # identity guard receives a deliberately stale birth for the new child.
      # Neither port stops a foreign process or changes the product budgets.
      $denyTokens=$null;$denyErrors=$null;$denyAst=[Management.Automation.Language.Parser]::ParseInput($text,[ref]$denyTokens,[ref]$denyErrors)
      Need (!$denyErrors.Count) 'rollback denial helper parse failed'
      $denyFunctions=@($denyAst.FindAll({param($n)$n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq 'Stop-YmccUpdaterParent'},$true))
      Need ($denyFunctions.Count -eq 1) 'rollback denial needs the existing stop function'
      $denyLine='  if ($newProcess -and $ProcessId -eq $newProcess.Id) { $CreationFileTime = "fixture-stale-new-child-birth" }'
      $offset=$denyFunctions[0].Body.Extent.StartOffset+1
      $text=$text.Substring(0,$offset)+"`n"+$denyLine+"`n"+$text.Substring($offset)
      $text="function Stop-Process { [CmdletBinding()] param([int]`$Id,[switch]`$Force) throw 'FIXTURE_NEW_PROCESS_STOP_REFUSED' }`n"+$text
      $launches=[regex]::Matches($text,'(?m)^  \$newProcess = Start-Process [^\r\n]+')
      Need ($launches.Count -eq 1) 'rollback new child launch anchor missing'
      $newReceipt=Join-Path $data 'rollback-new-child.json'
      $probe='  [pscustomobject]@{pid=$newProcess.Id;image=$exePath;creation=(Get-YmccUpdaterProcessIdentity -Process $newProcess).Creation}|ConvertTo-Json -Compress|Set-Content -LiteralPath '+("'"+$newReceipt.Replace("'","''")+"'")+' -Encoding UTF8'
      $text=$text.Replace($launches[0].Value,$launches[0].Value+"`n"+$probe)
      [pscustomobject]@{scope='Owned inert child only; original functions/rollback control flow retained';oldStopCommand='denied';newStopIdentity='stale birth';productBudgetsChanged=$false}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $data 'rollback-stop-port.json') -Encoding UTF8
    }
    [IO.File]::WriteAllText($helper,$text,[Text.UTF8Encoding]::new($true))
    Copy-Item -LiteralPath $helper -Destination (Join-Path $data 'emitted-production-helper.txt')
    if($legacy -and $parentMode -eq 'normal'){$null=$parent.CloseMainWindow();$null=$parent.WaitForExit(3000)}
    $selectedEngine=if($HelperEngine){$HelperEngine}else{(Get-Process -Id $PID).Path};$info=[Diagnostics.ProcessStartInfo]::new($selectedEngine);$info.UseShellExecute=$false;$info.CreateNoWindow=$true
    # A native 035 launch normally does not inherit pwsh's module directory.
    # Keep that OS input explicit for the immutable old consumer. New consumers
    # inherit the polluted parent path and must repair it themselves.
    if($legacy -and $HelperEngine -and [IO.Path]::GetFileName($HelperEngine) -ieq 'powershell.exe'){
      $legacyModules=Join-Path ([IO.Path]::GetDirectoryName($HelperEngine)) 'Modules'
      $info.EnvironmentVariables['PSModulePath']=$legacyModules+';'+$info.EnvironmentVariables['PSModulePath']
    }
    # ProcessStartInfo.ArgumentList is a .NET Core interface. The outer
    # fixture may itself run in Windows PowerShell 5/.NET Framework.
    if($info.PSObject.Properties['ArgumentList']){
      foreach($arg in @('-NoProfile','-ExecutionPolicy','Bypass','-File',$helper)){$info.ArgumentList.Add($arg)}
    }else{
      if($helper -match '["\x00-\x1f]'){throw 'FIXTURE_HELPER_COMMAND_PATH_INVALID'}
      $info.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$helper+'"'
    }
    $child=[Diagnostics.Process]::Start($info)
    if($Interrupt){
      try{
        $until=[DateTime]::UtcNow.AddSeconds(15)
        while(!(Test-Path -LiteralPath $interruptionMarker) -and [DateTime]::UtcNow -lt $until -and !$child.HasExited){Start-Sleep -Milliseconds 50}
        Need (Test-Path -LiteralPath $interruptionMarker) 'did not reach executable replacement'
        Need (!$child.HasExited) 'helper exited before interruption'
        Need ((Get-Content -LiteralPath $exitReceipt -Raw) -eq 'graceful') 'normal running parent did not exit gracefully'
        Need (@($hookProcesses|Where-Object {!$_.HasExited}).Count -eq 0) 'external hook tool survived installation readiness'
        $child.Kill();Need ($child.WaitForExit(3000)) 'interrupted helper remained alive'
        Need ((Digest $exe) -eq (Digest (Join-Path $template 'YeManCC/YeManCC.exe'))) 'incoming executable was not installed'
        $journal=Get-Content -LiteralPath $config.recoveryJournal -Raw|ConvertFrom-Json
        Need ($journal.phase -eq 'applying') 'interrupted journal lost authorization'
        return [pscustomobject]@{state=(Get-Content -LiteralPath (Join-Path $data 'update-state.json') -Raw|ConvertFrom-Json);journal=$journal;data=$data;parentExited=$parent.HasExited;helper=(Join-Path $data 'emitted-production-helper.txt')}
      }finally{if(!$child.HasExited){$child.Kill();$null=$child.WaitForExit(3000)};$child.Dispose()}
    }
    try {if(!$child.WaitForExit(25000)){if(!$child.HasExited){$child.Kill()};throw 'HELPER_HUNG_EXCEEDED_25_SECONDS'}}finally{$child.Dispose()}
    Need (@($hookProcesses|Where-Object {!$_.HasExited}).Count -eq 0) 'external hook tool survived install helper'
    if(!$legacy){$modeReceipt=Get-Content -LiteralPath $exitReceipt -Raw;Need ($modeReceipt -eq $(if($parentMode -eq 'hung'){'forced-owned-parent'}else{'graceful'})) ('unexpected parent exit mode: '+$modeReceipt)}
    $state=Get-Content -LiteralPath (Join-Path $data 'update-state.json') -Raw|ConvertFrom-Json
    return [pscustomobject]@{state=$state;data=$data;parentExited=$parent.HasExited;helper=(Join-Path $data 'emitted-production-helper.txt')}
  }finally{foreach($tool in $hookProcesses){try{if(!$tool.HasExited){$tool.Kill();$null=$tool.WaitForExit(2000)}}finally{$tool.Dispose()}};if(!$parent.HasExited){$parent.Kill();$null=$parent.WaitForExit(2000)};$parent.Dispose()}
}
function Check([string]$name,[scriptblock]$body){$timer=[Diagnostics.Stopwatch]::StartNew();try{&$body;$results.Add([pscustomobject]@{name=$name;status='PASS';milliseconds=$timer.ElapsedMilliseconds})}catch{$results.Add([pscustomobject]@{name=$name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace;milliseconds=$timer.ElapsedMilliseconds})}}
for($round=0;$round -lt $Cycles;$round++){
 foreach($mode in $CaseModes){
  Check ($mode+'-cycle-'+$round) {
    $caseRoot=Safe (Join-Path $evidence ($mode+'-'+$round+'-'+[guid]::NewGuid().ToString('N')))
    [IO.Directory]::CreateDirectory($caseRoot)|Out-Null
    $install=Join-Path $caseRoot 'installed';Copy-Item -LiteralPath $original -Destination $install -Recurse
    $exe=Join-Path $install 'YeManCC/YeManCC.exe';Copy-Item -LiteralPath $ProcessFixture -Destination $exe -Force
    Put $install 'PowerControl/yeman-settings.json' '{"schemaVersion":1,"input":{"owner":"user"},"ui":{"theme":"dark"},"future":{"preserve":true}}'|Out-Null
    Put $install 'PowerControl/Sleep/player-blacklist.txt' 'user player rules'|Out-Null
    Put $install 'PowerControl/decky/settings/user.json' '{"user":true}'|Out-Null
    try{
      $before=Snapshot $install;$legacy=$mode.StartsWith('035');$hung=$mode.Contains('hung');$rollback=$mode.Contains('rollback')
      if($rollback){Put $install 'YeManCC/fail-handshake.flag' 'inert injected health failure'|Out-Null;$before=Snapshot $install}
      $emitter=if($legacy){$Official035Native}else{Join-Path $repo 'native/main.cpp'}
      $template=if($legacy){$template36}else{$template37};$version=if($legacy){'0.0.36'}else{'0.0.37'}
      if($mode -eq '036-immutable-locked-retry'){
        Need ([bool]$CandidateZip) 'LOCK_CYCLE_REQUIRES_ACTUAL_PACKAGE'
        $template=$template37;$version='0.0.37'
        $lockedFile=Join-Path $install 'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll';[IO.Directory]::CreateDirectory((Split-Path -Parent $lockedFile))|Out-Null
        Copy-Item -LiteralPath (Join-Path $template 'PowerControl/ymcc-shared-runtime/Newtonsoft.Json.dll') -Destination $lockedFile
        $lock=[IO.File]::Open($lockedFile,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);$wait=[Diagnostics.Stopwatch]::StartNew()
        try{$blocked=Run-Helper $caseRoot $install $template $emitter $version 'normal' $false}finally{$lock.Dispose()}
        Need ($wait.ElapsedMilliseconds -lt 20000) 'locked immutable copy exceeded its finite failure budget'
        Need ($blocked.state.phase -ceq 'failed' -and ([string]$blocked.state.error).Contains('IMMUTABLE_PAYLOAD_REPAIR_REQUIRED')) 'incomplete immutable repair was misreported as a successful rollback'
        Need (Test-Path -LiteralPath (Join-Path $blocked.data 'staging/PowerControl/fan-host-v2/YeManFanHost.payload.json')) 'failed immutable repair discarded its authenticated staging source'
        Need (Test-Path -LiteralPath (Join-Path $blocked.data 'package.zip')) 'failed immutable repair discarded the complete cached package'
        [pscustomobject]@{phase=$blocked.state.phase;error=$blocked.state.error;milliseconds=$wait.ElapsedMilliseconds;stagingRetained=$true;packageRetained=$true}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $caseRoot 'immutable-copy-lock.json') -Encoding UTF8
        $retry=Run-Helper $caseRoot $install $template $emitter $version 'hung' $false
        Need ($retry.state.phase -eq 'committed' -and $retry.parentExited) 'release of the immutable copy lock did not allow the next owned upgrade'
        Assert-CandidatePayload $install $template;Assert-CandidateImmutable $install $template;Assert-RollbackHostRuntime $install $caseRoot
        foreach($relative in @('PowerControl/yeman-settings.json','PowerControl/Sleep/player-blacklist.txt','PowerControl/decky/settings/user.json')){Need ((Digest (Join-Path $install $relative)) -ceq $before[$relative.Replace('/','\')]) ('locked copy retry changed user data '+$relative)}
        return
      }
      if($mode -eq '036-rollback-stop-retry'){
        $persistentData=Safe (Join-Path $caseRoot 'persistent-update')
        $blocked=Run-Helper $caseRoot $install $template $emitter $version 'normal' $false '' $true $persistentData
        $childReceipt=Get-Content -LiteralPath (Join-Path $blocked.data 'rollback-new-child.json') -Raw|ConvertFrom-Json
        $childIdentity=Get-YmccUpdaterProcessIdentity -ProcessId ([int]$childReceipt.pid)
        Need ($childIdentity -and [IO.Path]::GetFullPath($childIdentity.Image) -ieq [IO.Path]::GetFullPath($exe) -and $childIdentity.Creation -ceq [string]$childReceipt.creation) 'new owned child identity was not retained during refusal'
        $sourceManifest=Get-Content -LiteralPath (Join-Path $template 'YeManCC/module-package.json') -Raw|ConvertFrom-Json
        $moduleMatches=@($sourceManifest.files|Where-Object{ $target=Join-Path (Join-Path $install 'PowerControl') $_.path; (Test-Path -LiteralPath $target -PathType Leaf) -and (Digest $target) -ieq $_.sha256 }).Count
        $stageRetained=Test-Path -LiteralPath (Join-Path $blocked.data 'staging') -PathType Container
        $observed=[pscustomobject]@{phase=$blocked.state.phase;error=$blocked.state.error;ownedNewChildStillAlive=$true;moduleFilesMatchingIncoming=$moduleMatches;moduleFilesExpected=@($sourceManifest.files).Count;stagingRetained=$stageRetained;childIdentity=$childReceipt;hardwareIo=$false}
        $observed|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $caseRoot 'rollback-stop-observation.json') -Encoding UTF8
        Need ($blocked.state.phase -ceq 'failed' -and ([string]$blocked.state.error).Contains('UPDATE_ROLLBACK_CHILD_EXIT_UNCONFIRMED')) 'unconfirmed rollback child stop did not fail explicitly before rollback'
        Need $stageRetained 'unconfirmed child stop discarded recovery staging'
        Need ($moduleMatches -eq @($sourceManifest.files).Count) 'rollback changed incoming module files while the new child was alive'
        $authorization=Get-Content -LiteralPath (Join-Path $blocked.data 'install-journal.json') -Raw|ConvertFrom-Json
        Need ($authorization.phase -ceq 'repair-required') 'unconfirmed child stop lost repair authorization'
        Assert-CandidatePayload $install $template;Assert-CandidateImmutable $install $template
        # Preserve raw evidence before the same fixed journal/progress paths
        # are overwritten by the next original helper transaction.
        $firstAttempt=Safe (Join-Path $caseRoot 'first-attempt-evidence');[IO.Directory]::CreateDirectory($firstAttempt)|Out-Null
        foreach($name in @('update-state.json','install-journal.json','update-progress.json','emitted-production-helper.txt','helper-steps.log','rollback-new-child.json','rollback-stop-port.json')){
          $previous=Join-Path $blocked.data $name;if(Test-Path -LiteralPath $previous -PathType Leaf){Copy-Item -LiteralPath $previous -Destination (Join-Path $firstAttempt $name)}
        }
        $priorRollback=@(Get-ChildItem -LiteralPath $persistentData -Directory|Where-Object Name -cmatch '^rollback-[0-9a-f]{32}$'|ForEach-Object FullName)
        Stop-Fixture $exe;[IO.File]::Delete((Join-Path $install 'YeManCC/fail-handshake.flag'))
        $retry=Run-Helper $caseRoot $install $template $emitter $version 'hung' $false '' $false $persistentData
        Need ($retry.data -ceq $blocked.data) 'retry did not reuse the fixed update directory'
        Need ($retry.state.phase -ceq 'committed' -and $retry.parentExited) 'retry after child stop refusal did not commit'
        $committed=Get-Content -LiteralPath (Join-Path $retry.data 'install-journal.json') -Raw|ConvertFrom-Json
        Need ($committed.phase -ceq 'committed') 'shared recovery journal was not committed'
        Need (!(Test-Path -LiteralPath (Join-Path $retry.data 'staging'))) 'committed retry retained its current staging'
        $retainedRollback=@($priorRollback|Where-Object{Test-Path -LiteralPath $_ -PathType Container})
        $archivedStaging=@(Get-ChildItem -LiteralPath $persistentData -Directory|Where-Object Name -cmatch '^previous-staging-evidence-[0-9a-f]{32}$'|ForEach-Object FullName)
        Need ($archivedStaging.Count -eq 1) 'previous staging evidence was not preserved'
        [pscustomobject]@{status='PASS';sameUpdateDirectory=$true;firstPhase=$blocked.state.phase;nextPhase=$retry.state.phase;journalPhase=$committed.phase;stagingAfterCommit=$false;previousBackupRoots=$priorRollback;retainedPreviousBackupRoots=$retainedRollback;archivedPreviousStaging=$archivedStaging;firstAttemptEvidence=$firstAttempt;stageClearPort='Archive fixture-owned old staging; original native deletion not executed';authority='Existing scoped fixture ACL port; not administrator ProgramData IO';hardwareIo=$false}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $caseRoot 'persistent-recovery-observation.json') -Encoding UTF8
        Assert-CandidatePayload $install $template;Assert-CandidateImmutable $install $template
        foreach($relative in @('PowerControl/yeman-settings.json','PowerControl/Sleep/player-blacklist.txt','PowerControl/decky/settings/user.json')){Need ((Digest (Join-Path $install $relative)) -ceq $before[$relative.Replace('/','\')]) ('stop-refusal retry changed user data '+$relative)}
        return
      }
      if($mode.StartsWith('036-interrupt-')){
        $partial=Run-Helper $caseRoot $install $template36 (Join-Path $repo 'native/main.cpp') '0.0.36' 'normal' $false 'after-exe'
        Need $partial.parentExited 'old running YMCC did not exit before executable replacement'
        if($mode -eq '036-interrupt-failed-repair'){
          Put $install 'YeManCC/fail-handshake.flag' 'inert startup failure'|Out-Null
          $attempt=Run-Helper $caseRoot $install $template36 (Join-Path $repo 'native/main.cpp') '0.0.36' 'normal' $false
          Need ($attempt.state.phase -eq 'rolled-back') 'repair failure did not roll back other files'
          $authorization=Get-Content -LiteralPath (Join-Path $attempt.data 'install-journal.json') -Raw|ConvertFrom-Json
          Need ($authorization.phase -eq 'repair-required') 'failed same-version repair lost authorization'
          Stop-Fixture $exe;[IO.File]::Delete((Join-Path $install 'YeManCC/fail-handshake.flag'))
        }
        $repairVersion=if($mode -eq '036-interrupt-successor'){'0.0.37'}else{'0.0.36'}
        $repairTemplate=if($repairVersion -eq '0.0.37'){$template37}else{$template36}
        $repair=Run-Helper $caseRoot $install $repairTemplate (Join-Path $repo 'native/main.cpp') $repairVersion 'hung' $false
        Need ($repair.state.phase -eq 'committed') 'interrupted install could not recover'
        $authorization=Get-Content -LiteralPath (Join-Path $repair.data 'install-journal.json') -Raw|ConvertFrom-Json
        Need ($authorization.phase -eq 'committed') 'committed repair authorization was not retired'
        & dotnet $ModuleConsumer current $install|Out-Null;Need ($LASTEXITCODE -eq 0) 'repaired input module is incomplete'
        Assert-CandidatePayload $install $repairTemplate
        Assert-CandidateImmutable $install $repairTemplate
        Need ((Digest (Join-Path $install 'PowerControl/yeman-settings.json')) -eq $before['PowerControl\yeman-settings.json']) 'repair changed user configuration'
        return
      }
      $run=Run-Helper $caseRoot $install $template $emitter $version $(if($hung){'hung'}else{'normal'}) $legacy
      if($mode -eq '035-hung'){
        Need ($run.state.phase -eq 'failed') 'old helper did not report bounded parent timeout'
        Need (!$run.parentExited) 'legacy helper unexpectedly killed old parent'
        $after=Snapshot $install;Need ($after.Count -eq $before.Count) 'old timeout changed installation'
        foreach($key in $before.Keys){Need ($after[$key] -eq $before[$key]) ('old timeout changed '+$key)}
      }elseif($rollback){
        Need ($run.state.phase -eq 'rolled-back') ('rollback missing: '+($run.state|ConvertTo-Json -Compress))
        $after=Snapshot $install;Assert-RollbackSnapshot $before $after $install $template
        Assert-RollbackHostRuntime $install $caseRoot
        Stop-Fixture $exe;[IO.File]::Delete((Join-Path $install 'YeManCC/fail-handshake.flag'))
        $run=Run-Helper $caseRoot $install $template $emitter $version 'hung' $false
        Need ($run.state.phase -eq 'committed') ('retry failed: '+($run.state|ConvertTo-Json -Compress))
      }else{
        Need ($run.state.phase -eq 'committed') ('update failed: '+($run.state|ConvertTo-Json -Compress))
        Need $run.parentExited 'update committed before outgoing YMCC exit'
        if($legacy){
          & dotnet $ModuleConsumer current $install | Out-Null;Need ($LASTEXITCODE -eq 3) 'test did not reproduce old helper skipped modules'
          & dotnet $ModuleConsumer apply $install (Join-Path $run.data 'update-state.json') $run.state.pid | Out-Null
          Need ($LASTEXITCODE -eq 0) '035 compatibility bootstrap failed'
          if($CandidateZip){
            # The original consumer already receives the real commit context.
            # Its new postcommit cleanup must finish before a future update.
            Assert-CandidateImmutable $install $template
            $expectedIds=@(Get-ChildItem -LiteralPath (Join-Path $template 'PowerControl/handheldcompanion-runtime') -Directory|ForEach-Object Name)
            $obsolete=@()
            foreach($folder in @(Get-ChildItem -LiteralPath (Join-Path $install 'PowerControl/handheldcompanion-runtime') -Directory)){
              if($folder.Name -in $expectedIds -or $folder.Name -notmatch '^(YMCC-|HC-)[A-Za-z0-9_.-]+$'){continue}
              $manifest=Join-Path $folder.FullName 'HandheldCompanion.runtime.json'
              if(!(Test-Path -LiteralPath $manifest -PathType Leaf)){continue}
              $record=Get-Content -LiteralPath $manifest -Raw -Encoding UTF8|ConvertFrom-Json
              if($record.schemaVersion -eq 1 -and [string]$record.runtimeId -ceq $folder.Name){$obsolete+=$folder.Name}
            }
            Need ($obsolete.Count -eq 0) ('035 postcommit retained obsolete HC: '+($obsolete -join ','))
            [pscustomobject]@{status='PASS';currentRuntimeIds=$expectedIds;obsoleteManagedRuntimeIds=$obsolete;commitOwnerPid=$run.state.pid}|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $caseRoot 'legacy-postcommit-cleanup.json') -Encoding UTF8
          }
        }
      }
      if($mode.StartsWith('035-036-037')){
        Stop-Fixture $exe
        # The very installation just upgraded by 035 becomes the outgoing
        # installation for the new helper, with all receipts left in place.
        $next=Run-Helper $caseRoot $install $template37 (Join-Path $repo 'native/main.cpp') '0.0.37' 'hung' $false
        Need ($next.state.phase -eq 'committed') ('036 future upgrade failed: '+($next.state|ConvertTo-Json -Compress))
        Need $next.parentExited '036 parent survived successor commit'
      }
      if($mode -ne '035-hung'){
        Assert-CandidatePayload $install $(if($mode.StartsWith('035-036-037')){$template37}else{$template})
        if(!$legacy -or $mode.StartsWith('035-036-037')){Assert-CandidateImmutable $install $(if($mode.StartsWith('035-036-037')){$template37}else{$template})}
        & dotnet $ModuleConsumer current $install | Out-Null;Need ($LASTEXITCODE -eq 0) 'new input payload is mixed or stale'
        Need ((Digest (Join-Path $install 'PowerControl/yeman-settings.json')) -eq $before['PowerControl\yeman-settings.json']) 'user settings changed'
        Need ((Digest (Join-Path $install 'PowerControl/Sleep/player-blacklist.txt')) -eq $before['PowerControl\Sleep\player-blacklist.txt']) 'user player rules changed'
        Need ((Digest (Join-Path $install 'PowerControl/decky/settings/user.json')) -eq $before['PowerControl\decky\settings\user.json']) 'Decky settings changed'
      }
    }finally{Stop-Fixture $exe}
  }
 }
}
$stable=$true;foreach($identity in $sourceHashes){if((Get-FileHash -LiteralPath (Join-Path $repo $identity.path)).Hash -cne $identity.sha256){$stable=$false}}
if(!$stable){$results.Add([pscustomobject]@{name='source inputs stayed stable';status='FAIL';error='source changed during execution'})}
if($CandidateZip -and (Digest $CandidateZip) -cne $CandidateSha256){$results.Add([pscustomobject]@{name='candidate ZIP stayed stable';status='FAIL';error='candidate archive changed during execution'})}
if((Digest $executedHarness.path) -ine $executedHarness.sha256){$results.Add([pscustomobject]@{name='executed harness stayed stable';status='FAIL';error='executed harness changed during execution'})}
$failed=@($results|Where-Object status -eq FAIL)
[pscustomobject]@{status=$(if($failed.Count){'FAIL'}else{'PASS'});candidate=$candidateIdentity;packageDerivations=$packageDerivations;executedHarness=$executedHarness;cycles=$Cycles;helperEngine=$(if($HelperEngine){$HelperEngine}else{'caller PowerShell'});legacyPS5Environment='Native-host module path; current helper must self-heal inherited caller module path';sourceHashes=$sourceHashes;inputsStable=$stable;official035Sha256=(Digest $Official035Zip);cases=$results;hardwareIo=$false;outerZipVerification='Official 035 archive SHA checked; source native download/ZIP validation covered by separate 248-check fixture';candidateScope='Actual ZIP-derived payload when candidate is set; exactly4 version/EXE substitutions; inert process health for CustomSteamLibrary and scoped privileged FanHost ACL subprocess port; actual child health remains independent release-gate obligation;035 legacy HC cleanup is not claimed; new helper exact immutable HC/Host checked even after ordinary rollback';scope='Fixture-only process inventory, elevation and clocks injected; Actual 035/new helper PowerShell emission executed against full official 035 files; outgoing/new YMCC processes are inert Win32 fixtures; privileged recovery ACL IO, elevation and clocks injected; no live installed product or drivers'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$results|Format-Table -AutoSize
if($failed.Count){throw ('GIT035_CYCLE_SELFTEST_FAILED '+$failed.Count)}
