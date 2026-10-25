# Replace only plan-pinned runtime files and their export row after full regression.
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference='Stop'
$plan=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
$project=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..')).TrimEnd('\')
$install=[IO.Path]::GetFullPath($plan.installRoot).TrimEnd('\')
$stage=[IO.Path]::GetFullPath($plan.stageRoot).TrimEnd('\')
$evidence=[IO.Path]::GetFullPath($plan.evidenceDir)
if($install -ine 'C:\SOFT\YeMan'){throw 'This reviewed connector targets C:\SOFT\YeMan only'}
if(-not $evidence.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Evidence must stay inside the staged workspace'}
if(Test-Path -LiteralPath $evidence){throw 'Fresh runtime connection evidence required'}
New-Item -ItemType Directory -Path $evidence|Out-Null
$report=[ordered]@{status='FAIL';passed=$false;startedUtc=[DateTime]::UtcNow.ToString('o');applicationRestarted=$false;hardwareIo=$false;nativeReplaced=$false;frontendReplaced=$false;changedFiles=@();rollbackPerformed=$false}
$leases=@();$applied=[Collections.Generic.List[object]]::new();$temps=[Collections.Generic.List[string]]::new()
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Safe([string]$Root,[string]$Relative){
 if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'Unsafe runtime connection path'}
 $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
 if(-not $full.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Runtime connection path escaped its named root'}
 $node=$full
 while($node.Length -ge $Root.Length){if(Test-Path -LiteralPath $node){if((Get-Item -LiteralPath $node -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse runtime path refused'}};if($node -ieq $Root){break};$node=Split-Path -Parent $node}
 return $full
}
function NoConsumers{if(@(Get-Process -Name YeManFanHost,YeManLightSetter -ErrorAction SilentlyContinue).Count){throw 'A runtime consumer is active'}}
function ReplaceReviewed([string]$From,[string]$To){
 $target=[IO.Path]::GetFullPath($To)
 if(-not $target.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Atomic target is outside the reviewed installation'}
 $temporary=$target+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp'
 $temps.Add($temporary)
 Copy-Item -LiteralPath $From -Destination $temporary
 [IO.File]::Replace($temporary,$target,[Management.Automation.Language.NullString]::Value,$true)
}
try{
 $gates=@(Get-ChildItem -LiteralPath $plan.fullGateDirectory -Filter 'run-all-*.json' -File)
 if($gates.Count -ne 1){throw 'One completed full regression receipt required'}
 $gate=Get-Content -LiteralPath $gates[0].FullName -Raw -Encoding UTF8|ConvertFrom-Json
 $expectedSuites=@('fan_connection','tool_gate','invariants','extremes','regressions','gyro_core','signals','api_contract','cycle')|Sort-Object
 $actualSuites=@($gate.extra.suites|ForEach-Object {$_.id})|Sort-Object
 if($gate.verdict -ne 'ALL_PASS' -or $gate.exitCode -ne 0 -or $actualSuites.Count -ne 9 -or (Compare-Object $expectedSuites $actualSuites) -or @($gate.extra.suites|Where-Object {$_.exitCode -ne 0}).Count -or $gate.hardwareActionPerformed -ne $false -or $gate.realSleepPerformed -ne $false){throw 'The full nine-suite software regression did not pass'}
 $fan=@($gate.extra.suites|Where-Object {$_.id -eq 'fan_connection'})[0]
 $fanArgs=@($fan.args);$position=[Array]::IndexOf($fanArgs,'-EvidenceDir')
 if($position -lt 0){throw 'Fan proof path missing'}
 $fanRoot=[IO.Path]::GetFullPath($fanArgs[$position+1])
 if(-not $fanRoot.StartsWith([IO.Path]::GetFullPath($plan.fullGateDirectory).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fan proof is outside full regression root'}
 $fanRuns=@(Get-ChildItem -LiteralPath $fanRoot -File -Filter 'sim-fan-*.json')
 if($fanRuns.Count -ne 1){throw 'Exactly one fan receipt required'}
 $fanProof=Get-Content -LiteralPath $fanRuns[0].FullName -Raw -Encoding UTF8|ConvertFrom-Json
 $integrity=Get-Content -LiteralPath (Join-Path $fanRoot 'integrity/verification.json') -Raw -Encoding UTF8|ConvertFrom-Json
 $interop=Get-Content -LiteralPath (Join-Path $fanRoot 'interop/result.json') -Raw -Encoding UTF8|ConvertFrom-Json
 if($fanProof.verdict -ne 'ALL_PASS' -or $fanProof.counts.checksTotal -ne 5 -or $fanProof.identity.runtimeManifest.sha256 -ne $plan.newRuntimeManifestSha256 -or $fanProof.identity.host.sha256 -ne $plan.preservedHostSha256 -or $integrity.status -ne 'PASS' -or $integrity.runtime.profilePinnedDlls -lt 1 -or $interop.passed -ne $true -or $interop.runtimeManifestSha256 -cne $plan.newRuntimeManifestSha256){throw 'Full regression tested different inputs or omitted profile/interop verification'}
 foreach($name in @('Profile-pinned ordinary DLL derivative accepted','Unlisted DLL replacement rejected','Declared DLL replacement hash mismatch rejected','Declared DLL replacement size mismatch rejected')) {
  if(@($integrity.checks|Where-Object {$_.name -ceq $name -and $_.status -eq 'PASS'}).Count -ne 1){throw ('Runtime profile authorization check missing: '+$name)}
 }
 . (Join-Path $project 'tools/hc-device-runtime/verify-runtime.ps1')
 $profile=Get-Content -LiteralPath (Join-Path $project 'deps/ymcc-device-core-runtime/profile.json') -Raw -Encoding UTF8|ConvertFrom-Json
 if($plan.runtimeRelative -cne ('PowerControl/handheldcompanion-runtime/'+$profile.runtimeId)){throw 'Runtime target disagrees with the source profile'}
 $names=@($profile.interopAbiRepair.remainingSmallThirdPartyOriginals.files)
 if($names.Count -ne 23 -or @($names|Select-Object -Unique).Count -ne 23 -or @($names|Where-Object {$_ -notmatch '^[A-Za-z0-9.]+\.dll$'}).Count){throw 'Reviewed restoration file list changed'}
 $allowed=@($names|ForEach-Object {$plan.runtimeRelative+'/'+$_})+@(($plan.runtimeRelative+'/HandheldCompanion.runtime.json'),'YeManCC/export-identity.json')
 $allowed=@($allowed|Sort-Object);$planned=@($plan.changes|ForEach-Object {$_.relative.Replace('\','/')})|Sort-Object
 if($planned.Count -ne 25 -or (Compare-Object $allowed $planned)){throw 'Plan exceeds the exact 25-file runtime scope'}
 $stagedProof=Assert-YmccDeviceCoreRuntime -ProjectRoot $project -RuntimeRoot (Safe $stage $plan.runtimeRelative)
 if($stagedProof.manifestSha256 -ne $plan.newRuntimeManifestSha256){throw 'Staged runtime differs from tested runtime'}
 foreach($unchanged in $plan.preserved){if((Hash (Safe $install $unchanged.relative)) -ne $unchanged.sha256){throw ('Preserved component changed: '+$unchanged.relative)}}
 NoConsumers
 foreach($name in @('Global\YeManFanHost-v2','Local\YeManFanHost-v2')){
  $created=$false;$lease=[Threading.Mutex]::new($false,$name,[ref]$created)
  if(-not $created){$lease.Dispose();throw 'Host lifetime admission is already held'}
  $leases+=,$lease
 }
 NoConsumers
 $resolved=@()
 foreach($change in $plan.changes){
  $source=Safe $stage $change.sourceRelative;$target=Safe $install $change.relative
  if((Hash $source) -ne $change.afterSha256 -or (Hash $target) -ne $change.beforeSha256){throw ('Runtime input/preimage changed: '+$change.relative)}
  $backup=Safe $evidence ('preimage/'+$change.relative)
  New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup)|Out-Null
  Copy-Item -LiteralPath $target -Destination $backup
  $resolved+=@{source=$source;target=$target;backup=$backup;relative=$change.relative;before=$change.beforeSha256;after=$change.afterSha256}
 }
 foreach($change in $resolved){
  NoConsumers
  if((Hash $change.target) -ne $change.before){throw ('Runtime preimage drift during connection: '+$change.relative)}
  ReplaceReviewed $change.source $change.target
  $applied.Add($change)
  if((Hash $change.target) -ne $change.after){throw ('Runtime readback mismatch: '+$change.relative)}
 }
 $installedProof=Assert-YmccDeviceCoreRuntime -ProjectRoot $project -RuntimeRoot (Safe $install $plan.runtimeRelative)
 foreach($unchanged in $plan.preserved){if((Hash (Safe $install $unchanged.relative)) -ne $unchanged.sha256){throw ('Preserved component changed during connection: '+$unchanged.relative)}}
 $report.status='PASS';$report.passed=$true;$report.changedFiles=@($applied|ForEach-Object {$_.relative});$report.runtime=$installedProof;$report.preserved=@($plan.preserved);$report.gate=$gates[0].FullName
 $report.inventoryScope='Only changed runtime files and their export rows are verified; existing unrelated previews are preserved.'
}catch{
 $report.error=$_.Exception.Message
 for($i=$applied.Count-1;$i -ge 0;$i--){$change=$applied[$i];try{if((Hash $change.target) -eq $change.after){ReplaceReviewed $change.backup $change.target}else{$report.rollbackConflict=$change.relative}}catch{$report.rollbackError=$_.Exception.Message}}
 $report.rollbackPerformed=$applied.Count -gt 0
}finally{
 foreach($lease in $leases){$lease.Dispose()}
 foreach($temporary in $temps){
  $absolute=[IO.Path]::GetFullPath($temporary)
  if($absolute.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase) -and $absolute -match '\.ymcc-[0-9a-f]{32}\.tmp$' -and (Test-Path -LiteralPath $absolute)){Remove-Item -LiteralPath $absolute -Force}
 }
 $report.finishedUtc=[DateTime]::UtcNow.ToString('o')
 [IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($report|ConvertTo-Json -Depth 10),[Text.UTF8Encoding]::new($false))
}
Write-Output ('INSTALLED_RUNTIME_ORIGINALS '+$report.status)
if(-not $report.passed){exit 2}