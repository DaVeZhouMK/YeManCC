[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$ModuleConsumer,[ValidateRange(1,10)][int]$Cycles=3,[string]$CaseFilter='*')
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$evidence=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $evidence){throw 'FRESH_EVIDENCE_DIRECTORY_REQUIRED'}
[IO.Directory]::CreateDirectory($evidence)|Out-Null
. (Join-Path $repo 'native/decky_package_maintenance.ps1')
. (Join-Path $repo 'native/module_package_maintenance.ps1')
. (Join-Path $repo 'native/module_bootstrap_recovery.ps1')
. (Join-Path $repo 'tools/module_payload_packaging.ps1')
$cases=New-Object 'System.Collections.Generic.List[object]'
function Need([bool]$Condition,[string]$Why){if(!$Condition){throw $Why}}
function Put([string]$Root,[string]$Relative,[string]$Text){$file=Join-Path $Root $Relative;[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($file))|Out-Null;[IO.File]::WriteAllText($file,$Text,[Text.UTF8Encoding]::new($false));return $file}
function New-Case([string]$Name,[string]$SourceBarrier='',[string]$SourceRelease=''){
 $root=Join-Path $evidence ($Name+'-'+[guid]::NewGuid().ToString('N'));$source=Join-Path $root 'source';$installed=Join-Path $root 'installed'
 foreach($name in @('YeManInputHost.exe','YeManInputHost.dll','YeManInputHost.deps.json','YeManInputHost.runtimeconfig.json','HIDMaestro.Core.dll')){Put $source ('PowerControl/feature-assets/virtual-gamepad/'+$name) ('036-'+$name)|Out-Null}
 Put $source 'PowerControl/feature-assets/gyro-motion/runtime.json' '{"version":36}'|Out-Null
 [IO.Directory]::CreateDirectory((Join-Path $source 'YeManCC'))|Out-Null
 $manifest=Write-YmccInputCompatibilityPayload $source '0.0.36'
 if($SourceBarrier){
  $scriptPath=Join-Path $source 'PowerControl/redist/ymcc-input-payload-apply.ps1'
  $hook='$script:FixtureBarrier='+"'"+$SourceBarrier.Replace("'","''")+"'`n"+'$script:FixtureSourceRelease='+"'"+$SourceRelease.Replace("'","''")+"'`n"+'$script:FixturePhase='+"'source-read'`n"
  $hook+=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/updater-bootstrap-recovery/interrupt-hook.ps1'))
  $boundary='# This body is appended after the embedded maintenance functions by packaging.'
  $text=[IO.File]::ReadAllText($scriptPath);Need ($text.Contains($boundary)) 'bootstrap boundary missing'
  [IO.File]::WriteAllText($scriptPath,$text.Replace($boundary,$hook+"`n"+$boundary),[Text.UTF8Encoding]::new($true))
  $manifest.compatibility.scriptSha256=(Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash.ToLowerInvariant()
  [IO.File]::WriteAllText((Join-Path $source 'YeManCC/module-package.json'),($manifest|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 }
 Copy-Item -LiteralPath $source -Destination $installed -Recurse
 Need (!(Test-Path -LiteralPath (Join-Path $source 'PowerControl/redist/ymcc-input-payload.zip'))) 'duplicate inner module archive generated'
 $packages=Join-Path $installed 'Packages';[IO.Directory]::CreateDirectory($packages)|Out-Null
 [IO.Compression.ZipFile]::CreateFromDirectory($source,(Join-Path $packages 'YeManCC.zip'),[IO.Compression.CompressionLevel]::Optimal,$false)
 foreach($file in $manifest.files){Put (Join-Path $installed 'PowerControl') $file.path ('035-'+$file.path)|Out-Null}
 Put $installed 'PowerControl/yeman-settings.json' '{"owner":"user","keep":true}'|Out-Null
 return [pscustomobject]@{root=$root;source=$source;installed=$installed;manifest=$manifest;pc=(Join-Path $installed 'PowerControl')}
}
function Apply($Case,[string]$CommitState='',[int]$OwnerPid=0){
 $info=[Diagnostics.ProcessStartInfo]::new('dotnet');$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @($ModuleConsumer,'apply',$Case.installed)){$info.ArgumentList.Add($arg)}
 if($CommitState){$info.ArgumentList.Add($CommitState);$info.ArgumentList.Add([string]$OwnerPid)}
 $child=[Diagnostics.Process]::Start($info);$output=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
 try{if(!$child.WaitForExit(15000)){if(!$child.HasExited){$child.Kill()};throw 'BOOTSTRAP_CONSUMER_EXCEEDED_15S'};$exit=$child.ExitCode;$out=$output.GetAwaiter().GetResult();$err=$errors.GetAwaiter().GetResult();[IO.File]::WriteAllText((Join-Path $Case.root ('apply-'+[guid]::NewGuid().ToString('N')+'.log')),$out+$err);if($exit){throw ('APPLY_FAILED '+$exit+' '+$err)}}finally{$child.Dispose()}
}
function Owned($Case){return @(Get-ChildItem -LiteralPath $Case.installed -Directory -Filter 'module-bootstrap-*' -Force|Where-Object{Test-YmccBootstrapStageOwned $_.FullName $Case.pc})}
function Current($Case){return Test-YmccBootstrapPayloadCurrent $Case.pc $Case.manifest}
function Check([string]$Name,[scriptblock]$Body){if($Name -notlike $CaseFilter){return};$timer=[Diagnostics.Stopwatch]::StartNew();try{&$Body;$cases.Add([pscustomobject]@{name=$Name;status='PASS';milliseconds=$timer.ElapsedMilliseconds})}catch{$cases.Add([pscustomobject]@{name=$Name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace;milliseconds=$timer.ElapsedMilliseconds})}}
function Interrupt($Case,[ValidateSet('preparing','applying-middle','receipt-before','committed','cleanup-middle')][string]$Phase){
 $text=[IO.File]::ReadAllText((Join-Path $Case.pc 'redist/ymcc-input-payload-apply.ps1'));$barrier=Join-Path $Case.root ('barrier-'+$Phase+'.txt')
 $hook='$script:FixtureBarrier='+("'"+$barrier.Replace("'","''")+"'" )+"`n"+'$script:FixtureTarget='+("'"+$Case.pc.Replace("'","''")+"'" )+"`n"+'$script:FixturePhase='+("'"+$Phase+"'" )+"`n"
 $hook+=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'fixtures/updater-bootstrap-recovery/interrupt-hook.ps1'))
 $boundary='# This body is appended after the embedded maintenance functions by packaging.'
 Need ($text.Contains($boundary)) 'real bootstrap body boundary missing';$text=$text.Replace($boundary,$hook+"`n"+$boundary)
 $fixture=Join-Path $Case.root ('interrupted-'+$Phase+'.ps1');[IO.File]::WriteAllText($fixture,$text,[Text.UTF8Encoding]::new($true))
 $info=[Diagnostics.ProcessStartInfo]::new((Join-Path $env:WINDIR 'System32/WindowsPowerShell/v1.0/powershell.exe'));$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @('-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',$fixture,'-InstallRoot',$Case.installed)){$info.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::Start($info);$output=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
 try{
  $timer=[Diagnostics.Stopwatch]::StartNew();while(!(Test-Path -LiteralPath $barrier) -and !$child.HasExited -and $timer.ElapsedMilliseconds -lt 6000){Start-Sleep -Milliseconds 25}
  Need (Test-Path -LiteralPath $barrier) ('interruption barrier not reached '+$Phase)
  $child.Kill();Need ($child.WaitForExit(2500)) 'interrupted helper did not exit'
 }finally{if(!$child.HasExited){$child.Kill();$null=$child.WaitForExit(2500)};[IO.File]::WriteAllText((Join-Path $Case.root ('interruption-'+$Phase+'.log')),$output.GetAwaiter().GetResult()+$errors.GetAwaiter().GetResult());$child.Dispose()}
}
foreach($phase in @('preparing','applying-middle','receipt-before','committed','cleanup-middle')){for($round=0;$round -lt $Cycles;$round++){
 Check ('terminate-'+$phase+'-recover-'+$round){
  $c=New-Case $phase;Interrupt $c $phase;Need ((Owned $c).Count -eq 1) 'terminated stage lost ownership/recovery evidence'
  if($phase -in @('committed','cleanup-middle')){Need (Current $c) 'committed barrier was not really committed';[IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'))}
  Apply $c;Need (Current $c) 'retry left mixed input payload';Need (@((Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36').entries).Count -eq 0) 'recovery left a stale or missing managed receipt';Need ((Owned $c).Count -eq 0) 'successful retry leaked interrupted staging/rollback'
  Need ([IO.File]::ReadAllText((Join-Path $c.pc 'yeman-settings.json')) -eq '{"owner":"user","keep":true}') 'recovery changed user settings'
 }
}}
Check 'archive failure before mutation removes only its newly owned stage'{
 $c=New-Case 'bad-entry';$archive=Join-Path $c.installed 'Packages/YeManCC.zip'
 Add-Type -AssemblyName System.IO.Compression.FileSystem;$zip=[IO.Compression.ZipFile]::Open($archive,[IO.Compression.ZipArchiveMode]::Update)
 try{$entry=$zip.CreateEntry('../unexpected.txt');$stream=$entry.Open();$stream.WriteByte(1);$stream.Dispose()}finally{$zip.Dispose()}
 $rejected=$false;try{Apply $c}catch{$rejected=$true}
 Need $rejected 'unexpected archive entry accepted';Need ((Owned $c).Count -eq 0) 'pre-mutation failure leaked its archive extraction'
 Need ([IO.File]::ReadAllText((Join-Path $c.pc 'feature-assets/virtual-gamepad/YeManInputHost.dll')) -like '035-*') 'bad entry changed target'
}
Check 'owned cleanup preserves markerless stages wrong owners unknown files and junctions'{
 $c=New-Case 'ownership';Apply $c
 $unknown=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));Put $unknown 'personal.txt' 'keep-unknown'|Out-Null
 $foreign=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($foreign)|Out-Null;Write-YmccBootstrapStage $foreign (Join-Path $c.root 'other-target') '0.0.36' ('a'*64) 'preparing' @($c.manifest.files.path)
 $personal=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($personal)|Out-Null;Write-YmccBootstrapStage $personal $c.pc '0.0.36' ('a'*64) 'applying' @($c.manifest.files.path);Put $personal 'personal.txt' 'keep-user-added'|Out-Null
 $nested=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($nested)|Out-Null;Write-YmccBootstrapStage $nested $c.pc '0.0.36' ('a'*64) 'applying' @($c.manifest.files.path);Put $nested 'payload/feature-assets/virtual-gamepad/personal-notes.txt' 'keep-nested-user-file'|Out-Null
 $outside=Join-Path $c.root 'outside';Put $outside 'keep.txt' 'outside-untouched'|Out-Null
 $junction=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Junction -Path $junction -Value $outside|Out-Null
 $owned=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($owned)|Out-Null;Write-YmccBootstrapStage $owned $c.pc '0.0.36' ('a'*64) 'applying' @($c.manifest.files.path);Put $owned 'payload/feature-assets/gyro-motion/runtime.json' 'owned-cache'|Out-Null
 Apply $c;Need (!(Test-Path -LiteralPath $owned)) 'known abandoned stage not reclaimed'
 foreach($folder in @($unknown,$foreign,$personal,$nested,$junction)){Need (Test-Path -LiteralPath $folder) 'cleanup erased unknown/foreign/redirected content'}
 Need ([IO.File]::ReadAllText((Join-Path $outside 'keep.txt')) -eq 'outside-untouched') 'cleanup followed junction'
}
Check 'cleanup is bounded per startup and resumes without needing an archive'{
 $c=New-Case 'bounded';Apply $c
 for($index=0;$index -lt 11;$index++){$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null;Write-YmccBootstrapStage $stage $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path);Put $stage 'payload/feature-assets/gyro-motion/runtime.json' ('old-'+$index)|Out-Null}
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));Apply $c;Need ((Owned $c).Count -eq 3) 'cleanup did not enforce eight-stage budget'
 Apply $c;Need ((Owned $c).Count -eq 0) 'next startup did not finish reclaiming known stages'
}
Check 'healthy direct manual package adopts and repairs only its owned receipt without an archive'{
 $c=New-Case 'manual-receipt'
 foreach($file in $c.manifest.files){Copy-Item -LiteralPath (Join-Path (Join-Path $c.source 'PowerControl') $file.path) -Destination (Join-Path $c.pc $file.path) -Force}
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'))
 Apply $c;Need (@((Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36').entries).Count -eq 0) 'manual installation did not acquire its paired receipt'
 [IO.File]::WriteAllText((Join-Path $c.pc '.ymcc-input-managed.json'),'{corrupt receipt')
 Apply $c;Need (@((Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36').entries).Count -eq 0) 'known exact payload could not repair its managed receipt'
}

Check 'full source archive is pinned before extraction and released after bootstrap'{
 $barrier=Join-Path $evidence 'source-read-barrier.txt';$release=Join-Path $evidence 'source-read-release.txt'
 $c=New-Case 'source-lease' $barrier $release;$archive=Join-Path $c.installed 'Packages/YeManCC.zip'
 $original=[IO.File]::ReadAllBytes($archive);$originalHash=(Get-FileHash -LiteralPath $archive).Hash;$replacement=Join-Path $c.root 'replacement.zip';[IO.File]::WriteAllBytes($replacement,$original)
 $info=[Diagnostics.ProcessStartInfo]::new('dotnet');$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in @($ModuleConsumer,'apply',$c.installed)){$info.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::Start($info);$output=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
 $deleteRejected=$false;$replaceRejected=$false
 try{
  $wait=[Diagnostics.Stopwatch]::StartNew();while(![IO.File]::Exists($barrier) -and !$child.HasExited -and $wait.ElapsedMilliseconds -lt 6000){[Threading.Thread]::Sleep(20)}
  Need ([IO.File]::Exists($barrier)) 'real consumer did not reach before-archive-open barrier'
  try{[IO.File]::Delete($archive)}catch [IO.IOException]{$deleteRejected=$true}
  if(!$deleteRejected){[IO.File]::WriteAllBytes($archive,$original)}
  try{[IO.File]::Move($replacement,$archive,$true)}catch [IO.IOException]{$replaceRejected=$true}catch [UnauthorizedAccessException]{$replaceRejected=$true}
  Need ((Get-FileHash -LiteralPath $archive).Hash -eq $originalHash) 'source archive changed during sharing check'
  [IO.File]::WriteAllText($release,'release')
  Need ($child.WaitForExit(10000)) 'real consumer did not exit after source release'
  Need ($child.ExitCode -eq 0) 'bootstrap failed after concurrent source maintenance'
  Need $deleteRejected 'cache deletion removed the sole recovery archive before extraction'
  Need $replaceRejected 'successor download replaced the sole recovery archive before extraction'
  Need (Current $c) 'source lease test did not finish the real payload recovery'
  [IO.File]::Delete($archive);Need (![IO.File]::Exists($archive)) 'bootstrap leaked its source archive handle after exit'
 }finally{
  [IO.File]::WriteAllText($release,'release')
  if(!$child.HasExited){$child.Kill();$null=$child.WaitForExit(2500)}
  [IO.File]::WriteAllText((Join-Path $c.root 'source-lease.log'),$output.GetAwaiter().GetResult()+$errors.GetAwaiter().GetResult());$child.Dispose()
 }
}
function New-PostCommitCase([string]$Name){
 $c=New-Case $Name
 $current='YMCC-CLEANUP-CURRENT';$runtime='PowerControl/handheldcompanion-runtime/'+$current
 $runtimeFile=Put $c.installed ($runtime+'/HandheldCompanion.dll') 'incoming-runtime'
 $sharedFile=Put $c.installed 'PowerControl/ymcc-shared-runtime/Shared.dll' 'incoming-shared'
 $hostFile=Put $c.installed 'PowerControl/fan-host-v2/YeManFanHost.dll' 'incoming-host'
 $runtimeManifest=@{schemaVersion=1;runtimeId=$current;files=@(@{path='HandheldCompanion.dll';sha256=(Get-FileHash -LiteralPath $runtimeFile).Hash.ToLowerInvariant()});sharedDependencies=@{directory='PowerControl/ymcc-shared-runtime';files=@(@{path='Shared.dll';bytes=([IO.FileInfo]$sharedFile).Length;sha256=(Get-FileHash -LiteralPath $sharedFile).Hash.ToLowerInvariant()})}}
 $runtimePath=Put $c.installed ($runtime+'/HandheldCompanion.runtime.json') ($runtimeManifest|ConvertTo-Json -Depth 8)
 $hostManifest=@{schemaVersion=2;runtimeId=$current;runtimeManifest=('../handheldcompanion-runtime/'+$current+'/HandheldCompanion.runtime.json');files=@(@{path='YeManFanHost.dll';sha256=(Get-FileHash -LiteralPath $hostFile).Hash.ToLowerInvariant()})}
 $hostPath=Put $c.installed 'PowerControl/fan-host-v2/YeManFanHost.payload.json' ($hostManifest|ConvertTo-Json -Depth 8)
 $layout=@{schemaVersion=1;packageId='yemancc-update';packageVersion='0.0.36';rules=@{fanHostPayload=@{schemaVersion=2;directory='fan-host-v2';fileCount=1;manifestSha256=(Get-FileHash -LiteralPath $hostPath).Hash.ToLowerInvariant();runtimeDirectory=('handheldcompanion-runtime/'+$current);runtimeManifestSha256=(Get-FileHash -LiteralPath $runtimePath).Hash.ToLowerInvariant()}}}
 Put $c.installed 'YeManCC/update-manifest.json' ($layout|ConvertTo-Json -Depth 8)|Out-Null
 $old=Join-Path $c.pc 'handheldcompanion-runtime/HC-OLD-OWNED'
 Put $old 'HandheldCompanion.runtime.json' '{"schemaVersion":1,"runtimeId":"HC-OLD-OWNED","files":[]}'|Out-Null
 Put $old 'Old.dll' 'obsolete-runtime'|Out-Null
 Copy-Item -LiteralPath (Join-Path $c.installed 'Packages/YeManCC.zip') -Destination (Join-Path $c.root 'package.zip')
 $state=Put $c.root 'update-state.json' (@{phase='committed';pid=$PID;version='0.0.36'}|ConvertTo-Json)
 $c|Add-Member -NotePropertyName old -NotePropertyValue $old
 $c|Add-Member -NotePropertyName currentFile -NotePropertyValue $runtimeFile
 $c|Add-Member -NotePropertyName sharedFile -NotePropertyValue $sharedFile
 $c|Add-Member -NotePropertyName commitState -NotePropertyValue $state
 return $c
}
Check 'postcommit normal full update cleans old HC through the original module consumer'{
 $c=New-PostCommitCase 'postcommit-normal';Apply $c $c.commitState $PID
 Need (Current $c) 'postcommit failed to prepare input payload'
 Need (!(Test-Path -LiteralPath $c.old)) 'postcommit retained obsolete HC runtime'
 Need ((Owned $c).Count -eq 0) 'successful cleanup left a pending marker'
 Need ([IO.File]::ReadAllText($c.currentFile) -ceq 'incoming-runtime') 'cleanup changed current HC'
 Need ([IO.File]::ReadAllText((Join-Path $c.pc 'yeman-settings.json')) -eq '{"owner":"user","keep":true}') 'cleanup changed user settings'
}
Check 'postcommit manual autonomous module preparation never requests full HC cleanup'{
 $c=New-PostCommitCase 'postcommit-autonomous';Apply $c
 Need (Current $c) 'autonomous module preparation failed'
 Need (Test-Path -LiteralPath $c.old) 'autonomous input route performed full-update HC cleanup'
 Need ((Owned $c).Count -eq 0) 'autonomous route created full-update cleanup authority'
}
Check 'postcommit locked old library retains ownership then retries without an archive'{
 $c=New-PostCommitCase 'postcommit-locked';$clock=[Diagnostics.Stopwatch]::StartNew()
 $locked=[IO.File]::Open((Join-Path $c.old 'Old.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{Apply $c $c.commitState $PID}finally{$locked.Dispose()}
 Need ($clock.ElapsedMilliseconds -lt 15000) 'locked cleanup blocked module preparation'
 Need (Current $c) 'deferred HC cleanup blocked the input module'
 Need (Test-Path -LiteralPath (Join-Path $c.old 'HandheldCompanion.runtime.json')) 'failed cleanup lost old runtime ownership'
 Need ((Owned $c).Count -eq 1) 'locked cleanup lost its retry marker'
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));Apply $c
 Need (!(Test-Path -LiteralPath $c.old)) 'unlock retry did not remove old HC'
 Need ((Owned $c).Count -eq 0) 'unlock retry leaked pending marker'
}
Check 'postcommit corrupted incoming graph defers all old HC deletion and self heals'{
 foreach($kind in @('runtime','shared')){
  $c=New-PostCommitCase ('postcommit-bad-'+$kind)
  $file=if($kind -eq 'runtime'){$c.currentFile}else{$c.sharedFile};$bytes=[IO.File]::ReadAllBytes($file);[IO.File]::WriteAllText($file,'broken')
  Apply $c $c.commitState $PID
  Need (Test-Path -LiteralPath (Join-Path $c.old 'Old.dll')) 'bad incoming graph allowed destructive cleanup'
  Need ((Owned $c).Count -eq 1) 'bad graph did not retain retry authority'
  [IO.File]::WriteAllBytes($file,$bytes);[IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));Apply $c
  Need (!(Test-Path -LiteralPath $c.old)) 'repaired graph did not resume pending cleanup'
  Need ((Owned $c).Count -eq 0) 'repaired graph left pending cleanup'
 }
}
Check 'postcommit healthy receipt cannot bypass full update cleanup'{
 $c=New-PostCommitCase 'postcommit-fastpath';Apply $c
 Need (Test-Path -LiteralPath $c.old) 'setup unexpectedly cleaned HC before full commit'
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));Apply $c $c.commitState $PID
 Need (!(Test-Path -LiteralPath $c.old)) 'healthy receipt bypassed postcommit cleanup'
}
Check 'postcommit mismatched version is rejected before module mutation'{
 $c=New-PostCommitCase 'postcommit-wrong-version';[IO.File]::WriteAllText($c.commitState,(@{phase='committed';pid=$PID;version='0.0.37'}|ConvertTo-Json))
 $rejected=$false;try{Apply $c $c.commitState $PID}catch{$rejected=$true}
 Need $rejected 'wrong-version commit was accepted'
 Need (!(Current $c)) 'wrong commit changed module files'
 Need (Test-Path -LiteralPath (Join-Path $c.old 'Old.dll')) 'wrong commit removed old HC'
}
Check 'postcommit old directory handle preserves its manifest and recovers after release'{
 if(!('YmccBootstrapDirectoryLock' -as [type])){Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
public static class YmccBootstrapDirectoryLock {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
}
"@}
 $c=New-PostCommitCase 'postcommit-directory-lock'
 $directoryLock=[YmccBootstrapDirectoryLock]::CreateFile($c.old,1,3,[IntPtr]::Zero,3,0x02000000,[IntPtr]::Zero)
 Need (!$directoryLock.IsInvalid) 'owned directory lock was not acquired'
 try{Apply $c $c.commitState $PID}finally{$directoryLock.Dispose()}
 Need (Current $c) 'old directory handle blocked module preparation'
 Need (Test-Path -LiteralPath (Join-Path $c.old 'HandheldCompanion.runtime.json')) 'directory failure lost runtime ownership after file removal'
 Need ((Owned $c).Count -eq 1) 'directory failure lost pending cleanup'
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));[IO.File]::Delete((Join-Path $c.root 'package.zip'));Apply $c
 Need (!(Test-Path -LiteralPath $c.old)) 'directory release did not self heal'
 Need ((Owned $c).Count -eq 0) 'directory release leaked pending authority'
}
Check 'postcommit HC cleanup enforces an eight-directory budget and continues next startup'{
 $c=New-PostCommitCase 'postcommit-budget'
 foreach($index in 1..10){$id='HC-BUDGET-'+$index;$folder=Join-Path $c.pc ('handheldcompanion-runtime/'+$id);Put $folder 'HandheldCompanion.runtime.json' (@{schemaVersion=1;runtimeId=$id;files=@()}|ConvertTo-Json)|Out-Null;Put $folder 'Old.dll' 'obsolete'|Out-Null}
 Apply $c $c.commitState $PID
 $remaining=@(Get-ChildItem -LiteralPath (Join-Path $c.pc 'handheldcompanion-runtime') -Directory|Where-Object Name -like 'HC-*')
 Need ($remaining.Count -eq 3) 'HC cleanup did not honor its eight-directory budget'
 Need ((Owned $c).Count -eq 1) 'budget exhaustion lost the retry marker'
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));[IO.File]::Delete((Join-Path $c.root 'package.zip'));Apply $c
 Need (@(Get-ChildItem -LiteralPath (Join-Path $c.pc 'handheldcompanion-runtime') -Directory|Where-Object Name -like 'HC-*').Count -eq 0) 'next startup did not finish budgeted cleanup'
 Need ((Owned $c).Count -eq 0) 'completed budgeted cleanup leaked authority'
}
Check 'postcommit successor package resumes pending cleanup against its current graph'{
 $c=New-PostCommitCase 'postcommit-successor'
 $locked=[IO.File]::Open((Join-Path $c.old 'Old.dll'),[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
 try{Apply $c $c.commitState $PID}finally{$locked.Dispose()}
 Need ((Owned $c).Count -eq 1) 'predecessor did not retain pending cleanup'
 foreach($relative in @('YeManCC/module-package.json','YeManCC/update-manifest.json')){
  $file=Join-Path $c.installed $relative;$doc=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json;$doc.packageVersion='0.0.37'
  [IO.File]::WriteAllText($file,($doc|ConvertTo-Json -Depth 20),[Text.UTF8Encoding]::new($false))
 }
 [IO.File]::Delete((Join-Path $c.installed 'Packages/YeManCC.zip'));[IO.File]::Delete((Join-Path $c.root 'package.zip'));Apply $c
 Need (!(Test-Path -LiteralPath $c.old)) 'successor failed to retire predecessor pending HC'
 Need ((Owned $c).Count -eq 0) 'successor retained obsolete cleanup authority'
 Need ([IO.File]::ReadAllText($c.currentFile) -ceq 'incoming-runtime') 'successor cleanup changed its verified runtime'
}
$failed=@($cases|Where-Object status -eq FAIL)
$inputs=@('GamepadPrerequisites/ModulePayloadBootstrap.cs','native/updater_hc_runtime.ps1','native/module_bootstrap_recovery.ps1','native/module_payload_bootstrap.ps1','native/module_package_maintenance.ps1','native/decky_package_maintenance.ps1','tools/module_payload_packaging.ps1')|ForEach-Object{[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash}}
[pscustomobject]@{status=$(if($failed.Count){'FAIL'}else{'PASS'});cycles=$Cycles;cases=$cases;inputs=$inputs;executedHarness=@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash};hardwareIo=$false;actualConsumer='actual bootstrap script/package functions and production C# ModulePayloadBootstrap';interruption='owned real PowerShell process killed at state/file-commit barriers; original script is used for recovery'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$cases|Format-Table -AutoSize
if($failed.Count){throw ('MODULE_BOOTSTRAP_RECOVERY_FAILED '+$failed.Count)}
