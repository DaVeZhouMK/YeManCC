[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$PlanPath)
$ErrorActionPreference='Stop'
$plan=Get-Content -LiteralPath $PlanPath -Raw -Encoding UTF8|ConvertFrom-Json
$install=[IO.Path]::GetFullPath($plan.installRoot).TrimEnd('\')
$stage=[IO.Path]::GetFullPath($plan.stageRoot).TrimEnd('\')
$evidence=[IO.Path]::GetFullPath($plan.evidenceDir)
if(Test-Path -LiteralPath $evidence){throw 'Fresh lighting connection evidence directory required'}
if($stage -ieq $install -or $evidence.StartsWith($install+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Lighting evidence/stage must be separate from installation'}
New-Item -ItemType Directory -Path $evidence|Out-Null
$applied=[Collections.Generic.List[object]]::new();$mutex=$null;$ownsMutex=$false
$report=[ordered]@{status='FAIL';passed=$false;processId=$PID;engineVersion=$PSVersionTable.PSVersion.ToString();scriptPath=$PSCommandPath;planSha256=(Get-FileHash -LiteralPath $PlanPath -Algorithm SHA256).Hash.ToLowerInvariant();startedUtc=[DateTime]::UtcNow.ToString('o');hardwareIo=$false;appRestarted=$false;frontendReplaced=$false;nativeReplaced=$false;changedFiles=@();rollbackPerformed=$false;rollbackConflict=$null;rollbackError=$null}
function Hash([string]$File){(Get-FileHash -LiteralPath $File -Algorithm SHA256).Hash.ToLowerInvariant()}
function Scoped([string]$Root,[string]$Relative){
 if(-not $Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)'){throw 'Unsafe lighting path'}
 $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative));if(-not $path.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Lighting path escaped root'}
 $cursor=$path
 while($cursor.Length -ge $Root.Length){if(Test-Path -LiteralPath $cursor){if((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Reparse lighting path refused'}};if($cursor -ieq $Root){break};$cursor=Split-Path -Parent $cursor}
 return $path
}
function NoLightingConsumer{if(Get-Process -Name YeManLightSetter,LightSetter -ErrorAction SilentlyContinue){throw 'A live lighting helper is using the component'}}
function Acl([string]$Path){(Get-Acl -LiteralPath $Path).Sddl}
function ReplaceAtomically([string]$From,[string]$To){
 $temp=$To+'.ymcc-'+[guid]::NewGuid().ToString('N')+'.tmp'
 try{Copy-Item -LiteralPath $From -Destination $temp;[IO.File]::Replace($temp,$To,[Management.Automation.Language.NullString]::Value,$true)}
 finally{if(Test-Path -LiteralPath $temp){Remove-Item -LiteralPath $temp -Force}}
}
function CheckPreserved{
 foreach($record in $plan.preserved){$file=Scoped $install $record.relative;if((Hash $file) -cne $record.sha256 -or (Acl $file) -cne $record.sddl){throw ('Preserved lighting component changed: '+$record.relative)}}
}
try{
 . (Join-Path $PSScriptRoot 'verify-lighting-catalog-evidence.ps1')
 $report.admission=Assert-YmccLightingCatalogEvidence -Plan $plan
 if($plan.kind -ceq 'lighting-build-v4'){$report.matchingNativeRequired=$true;$report.nativeAlreadyMatched=$true;$report.nativeDeliveryScope='Native must be separately matched before this two-file transaction; native itself is never installed here.'}
 $expected=@('PowerControl/light-setter/YeManLightSetter.dll','YeManCC/export-identity.json')|Sort-Object
 $actual=@($plan.changes|ForEach-Object {$_.relative.Replace('\','/')})|Sort-Object
 if($actual.Count -ne 2 -or (Compare-Object $expected $actual)){throw 'Lighting plan must contain only helper and export identity'}
 if(@($plan.preserved|Group-Object relative|Where-Object Count -NE 1).Count -ne 0){throw 'Duplicate preserved lighting path'}
 foreach($required in @('YeManCC/YeManCC.exe','PowerControl/light-setter/YeManLightDeviceCore.dll','PowerControl/light-setter/YeManLightSetter.exe','PowerControl/light-setter/YeManLightSetter.deps.json','PowerControl/light-setter/YeManLightSetter.runtimeconfig.json','PowerControl/light-setter/YeManLightSetter.json','PowerControl/fan-host-v2/YeManFanHost.dll','PowerControl/fan-host-v2/YeManFanBackend.dll')){if(@($plan.preserved|Where-Object relative -CEQ $required).Count -ne 1){throw ('Required preserved component missing: '+$required)}}
 foreach($change in $plan.changes){if(@($plan.preserved|Where-Object relative -CEQ $change.relative).Count -ne 0){throw 'Changed lighting path also marked preserved'}}
 $native=Scoped $install 'YeManCC/YeManCC.exe';$core=Scoped $install 'PowerControl/light-setter/YeManLightDeviceCore.dll'
 if((Hash $native) -cne $plan.preservedNativeSha256 -or (Hash $core) -cne $plan.preservedCoreSha256){throw 'Preserved native/core changed'}
 $resolved=@()
 foreach($change in $plan.changes){
  $from=Scoped $stage $change.sourceRelative;$to=Scoped $install $change.relative
  if((Hash $from) -cne $change.afterSha256 -or (Hash $to) -cne $change.beforeSha256){throw ('Lighting preimage/input changed: '+$change.relative)}
  if((Acl $to) -cne $change.beforeSddl){throw ('Lighting preimage ACL changed: '+$change.relative)}
  $backup=Scoped $evidence ('preimage/'+$change.relative);New-Item -ItemType Directory -Force -Path (Split-Path -Parent $backup)|Out-Null
  Copy-Item -LiteralPath $to -Destination $backup
  if((Hash $backup) -cne $change.beforeSha256){throw 'Lighting backup differs from preimage'}
  $resolved+=[pscustomobject]@{from=$from;to=$to;backup=$backup;relative=$change.relative;before=$change.beforeSha256;after=$change.afterSha256;sddl=$change.beforeSddl}
 }
 $helperChange=@($resolved|Where-Object relative -CEQ 'PowerControl/light-setter/YeManLightSetter.dll')[0]
 if($helperChange.after -cne $plan.newHelperSha256){throw 'Stage helper differs from admission'}
 $exportChange=@($resolved|Where-Object relative -CEQ 'YeManCC/export-identity.json')[0]
 $beforeExport=Get-Content -LiteralPath $exportChange.to -Raw -Encoding UTF8|ConvertFrom-Json
 $afterExport=Get-Content -LiteralPath $exportChange.from -Raw -Encoding UTF8|ConvertFrom-Json
 $beforeRows=@($beforeExport.payload|Where-Object path -CEQ 'PowerControl/light-setter/YeManLightSetter.dll');$afterRows=@($afterExport.payload|Where-Object path -CEQ 'PowerControl/light-setter/YeManLightSetter.dll')
 if($beforeRows.Count -ne 1 -or $afterRows.Count -ne 1){throw 'Missing/duplicate lighting export row'}
 $beforeRows[0].sha256=$plan.newHelperSha256
 if($beforeRows[0].PSObject.Properties['bytes']){$beforeRows[0].bytes=(Get-Item -LiteralPath $helperChange.from).Length}
 if($beforeRows[0].PSObject.Properties['size']){$beforeRows[0].size=(Get-Item -LiteralPath $helperChange.from).Length}
 if(($beforeExport|ConvertTo-Json -Depth 80 -Compress) -cne ($afterExport|ConvertTo-Json -Depth 80 -Compress)){throw 'Lighting export edit changes unrelated identity data'}
 $sha=[Security.Cryptography.SHA256]::Create();try{$key=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($install.ToLowerInvariant())))).Replace('-','')}finally{$sha.Dispose()}
 $mutex=[Threading.Mutex]::new($false,('Local\YMCC-Lighting-Install-'+$key))
 try{$ownsMutex=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$ownsMutex=$true}
 if(-not $ownsMutex){throw 'Another lighting component transaction is active'}
 NoLightingConsumer;CheckPreserved
 foreach($change in $resolved){
  NoLightingConsumer;CheckPreserved
  if((Hash $change.to) -cne $change.before -or (Acl $change.to) -cne $change.sddl){throw ('Lighting preimage changed during connection: '+$change.relative)}
  ReplaceAtomically $change.from $change.to;$applied.Add($change)
  if((Hash $change.to) -cne $change.after -or (Acl $change.to) -cne $change.sddl){throw ('Lighting readback/ACL mismatch: '+$change.relative)}
 }
 CheckPreserved
 $report.status='PASS';$report.passed=$true;$report.helperSha256=$plan.newHelperSha256;$report.changedFiles=@($applied|ForEach-Object {$_.relative});$report.preservedFiles=@($plan.preserved).Count;$report.truePreimages=$resolved.Count;$report.allAclPreserved=$true
 $report.inventoryScope='Exact lighting helper and its export row; unrelated installed components and identity data preserved.'
}catch{
 $report.error=$_.Exception.Message
 for($i=$applied.Count-1;$i -ge 0;$i--){$row=$applied[$i];try{if((Hash $row.to) -ceq $row.after){ReplaceAtomically $row.backup $row.to;if((Hash $row.to) -cne $row.before -or (Acl $row.to) -cne $row.sddl){throw 'Rollback hash/ACL mismatch'}}else{$report.rollbackConflict=$row.relative}}catch{$report.rollbackError=$_.Exception.Message}}
 $report.rollbackPerformed=$applied.Count -gt 0
}finally{
 if($ownsMutex){$mutex.ReleaseMutex()};if($null -ne $mutex){$mutex.Dispose()}
 $report.finishedUtc=[DateTime]::UtcNow.ToString('o');[IO.File]::WriteAllText((Join-Path $evidence 'verification.json'),($report|ConvertTo-Json -Depth 12),[Text.UTF8Encoding]::new($false))
}
Write-Output ('INSTALLED_LIGHTING_CONNECTION '+$report.status)
if(-not $report.passed){exit 2}