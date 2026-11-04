[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir,[Parameter(Mandatory=$true)][string]$ModuleConsumer,[ValidateRange(1,10)][int]$Cycles=3,[string]$CaseFilter='*',[string]$ModuleMaintenanceSource='',[string]$ModuleRecoverySource='',[switch]$CollectAfterFailure)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'));$evidence=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $evidence){throw 'FRESH_EVIDENCE_DIRECTORY_REQUIRED'}
[IO.Directory]::CreateDirectory($evidence)|Out-Null
. (Join-Path $repo 'native/decky_package_maintenance.ps1')
if(!$ModuleMaintenanceSource){$ModuleMaintenanceSource=Join-Path $repo 'native/module_package_maintenance.ps1'}
. $ModuleMaintenanceSource
if(!$ModuleRecoverySource){$ModuleRecoverySource=Join-Path $repo 'native/module_bootstrap_recovery.ps1'}
. $ModuleRecoverySource
. (Join-Path $repo 'tools/module_payload_packaging.ps1')
$script:bootstrapRequiredFailure=$null
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
function Invoke-ApplyConsumer($Case,[string]$CommitState='',[int]$OwnerPid=0){
 $timer=[Diagnostics.Stopwatch]::StartNew();$startedAt=[DateTime]::UtcNow.ToString('o')
 $consumer=[pscustomobject]@{path=[IO.Path]::GetFullPath($ModuleConsumer);sha256=(Get-FileHash -LiteralPath $ModuleConsumer -Algorithm SHA256).Hash}
 $arguments=@($ModuleConsumer,'apply',$Case.installed);if($CommitState){$arguments+=@($CommitState,[string]$OwnerPid)}
 $info=[Diagnostics.ProcessStartInfo]::new('dotnet');$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 foreach($arg in $arguments){$info.ArgumentList.Add($arg)}
 $child=[Diagnostics.Process]::Start($info);$output=$child.StandardOutput.ReadToEndAsync();$errors=$child.StandardError.ReadToEndAsync()
 try{
  if(!$child.WaitForExit(15000)){if(!$child.HasExited){$child.Kill()};throw 'BOOTSTRAP_CONSUMER_EXCEEDED_15S'}
  $exit=$child.ExitCode;$out=$output.GetAwaiter().GetResult();$err=$errors.GetAwaiter().GetResult()
  $log=Join-Path $Case.root ('apply-'+[guid]::NewGuid().ToString('N')+'.log');[IO.File]::WriteAllText($log,$out+$err);$timer.Stop()
  return [pscustomobject]@{consumer=$consumer;program=$info.FileName;arguments=$arguments;exitCode=$exit;stdout=$out;stderr=$err;startedAtUtc=$startedAt;milliseconds=$timer.ElapsedMilliseconds;budgetMilliseconds=15000;log=$log;logSha256=(Get-FileHash -LiteralPath $log -Algorithm SHA256).Hash}
 }finally{$child.Dispose()}
}
function Apply($Case,[string]$CommitState='',[int]$OwnerPid=0){
 $result=Invoke-ApplyConsumer $Case $CommitState $OwnerPid
 if($result.exitCode){throw ('APPLY_FAILED '+$result.exitCode+' '+$result.stderr)}
}
function Owned($Case){return @(Get-ChildItem -LiteralPath $Case.installed -Directory -Filter 'module-bootstrap-*' -Force|Where-Object{Test-YmccBootstrapStageOwned $_.FullName $Case.pc})}
function Current($Case){return Test-YmccBootstrapPayloadCurrent $Case.pc $Case.manifest}
function Check([string]$Name,[scriptblock]$Body,[System.Collections.IDictionary]$Observation=$null){
 if($Name -notlike $CaseFilter){return}
 if($script:bootstrapRequiredFailure -and -not $CollectAfterFailure){$cases.Add([pscustomobject]@{name=$Name;status='NOT_RUN_AFTER_REQUIRED_FAILURE';milliseconds=0;blockedBy=$script:bootstrapRequiredFailure});return}
 $timer=[Diagnostics.Stopwatch]::StartNew()
 try{&$Body;$row=[pscustomobject]@{name=$Name;status='PASS';milliseconds=$timer.ElapsedMilliseconds}}
 catch{if(-not $script:bootstrapRequiredFailure){$script:bootstrapRequiredFailure=$Name};$row=[pscustomobject]@{name=$Name;status='FAIL';error=$_.Exception.Message;stack=$_.ScriptStackTrace;milliseconds=$timer.ElapsedMilliseconds}}
 if($null -ne $Observation){$row|Add-Member -NotePropertyName observation -NotePropertyValue $Observation}
 $cases.Add($row)
}
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
# Reuse the production consumer's nonzero result; old Apply callers still throw.
$missingSourceObservation=[ordered]@{}
Check 'missing full source archive rejects stale modules without changing originals or backups'{
 $c=New-Case 'missing-full-source'
 $manifestPath=Join-Path $c.installed 'YeManCC/module-package.json';$scriptPath=Join-Path $c.pc 'redist/ymcc-input-payload-apply.ps1'
 $installedManifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
 Need ((Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash -ieq $installedManifest.compatibility.scriptSha256) 'missing-source case must reach the verified production bootstrap script'
 Need (!(Current $c)) 'missing-source case must start with stale installed modules'
 $previousReceipt=$c.manifest|ConvertTo-Json -Depth 20|ConvertFrom-Json;$previousReceipt.packageVersion='0.0.35'
 $receiptText=$previousReceipt|ConvertTo-Json -Depth 20
 Put $c.pc '.ymcc-input-managed.json' $receiptText|Out-Null
 $priorStage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($priorStage)|Out-Null
 Write-YmccBootstrapStage $priorStage $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path)
 Put $priorStage 'rollback/.ymcc-input-managed.json' $receiptText|Out-Null
 Put $priorStage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'retained-original-backup'|Out-Null
 Need ((Owned $c).Count -eq 1) 'missing-source case must retain a real owned backup stage'
 $archive=Join-Path $c.installed 'Packages/YeManCC.zip';Need ([IO.File]::Exists($archive)) 'missing-source fixture archive was not prepared'
 [IO.File]::Delete($archive);Need (![IO.File]::Exists($archive)) 'missing-source fixture archive still exists'
 # Only this fresh case-owned installation is read; logs stay outside it.
 $snapshot={@(Get-ChildItem -LiteralPath $c.installed -Recurse -Force|Sort-Object FullName|ForEach-Object{
  $relative=[IO.Path]::GetRelativePath($c.installed,$_.FullName)
  if($_.PSIsContainer){[pscustomobject]@{path=$relative;kind='directory'}}else{[pscustomobject]@{path=$relative;kind='file';bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}
 })}
 $before=@(&$snapshot);$missingSourceObservation['before']=$before
 $result=Invoke-ApplyConsumer $c
 $missingSourceObservation['consumer']=[ordered]@{input=$result.consumer;program=$result.program;arguments=$result.arguments;exitCode=$result.exitCode;startedAtUtc=$result.startedAtUtc;milliseconds=$result.milliseconds;budgetMilliseconds=$result.budgetMilliseconds;log=$result.log;logSha256=$result.logSha256}
 $after=@(&$snapshot);$missingSourceObservation['after']=$after
 $unchanged=(($before|ConvertTo-Json -Depth 6 -Compress) -ceq ($after|ConvertTo-Json -Depth 6 -Compress));$missingSourceObservation['installedTreeUnchanged']=$unchanged
 Need ($result.exitCode -ne 0) 'stale modules without a full source archive unexpectedly succeeded'
 Need (($result.stdout+"`n"+$result.stderr) -match '\bMODULE_SOURCE_FULL_PACKAGE_REQUIRED\b') 'missing source failed without the exact production source-required diagnostic'
 Need $unchanged 'missing source changed settings module files manifest receipt or retained backups'
} $missingSourceObservation
# Exercise the real size guard without allocating or hashing a large archive.
$oversizedSourceObservation=[ordered]@{}
Check 'oversized sparse full source archive rejects stale modules without changing originals or backups'{
 if(!('YmccBootstrapSparseArchive' -as [type])){Add-Type -TypeDefinition '
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class YmccBootstrapSparseArchive {
 [DllImport("kernel32.dll", SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 private static extern bool DeviceIoControl(SafeFileHandle file,uint code,ref byte input,uint inputBytes,IntPtr output,uint outputBytes,out uint returned,IntPtr overlapped);
 [DllImport("kernel32.dll", EntryPoint="GetCompressedFileSizeW", CharSet=CharSet.Unicode, ExactSpelling=true, SetLastError=true)]
 private static extern uint GetCompressedFileSize(string path,out uint high);
 [DllImport("kernel32.dll", EntryPoint="SetLastError", ExactSpelling=true)]
 private static extern void ClearLastError(uint code);
 public static void MarkSparse(SafeFileHandle file) {
  uint returned;byte setSparse=1; // FILE_SET_SPARSE_BUFFER contains one BOOLEAN.
  if(!DeviceIoControl(file,0x000900C4,ref setSparse,1,IntPtr.Zero,0,out returned,IntPtr.Zero))
   throw new Win32Exception(Marshal.GetLastWin32Error(),"FSCTL_SET_SPARSE failed; no large-file fallback permitted");
 }
 public static ulong AllocatedBytes(string path) {
  string full=Path.GetFullPath(path);
  string native=full.StartsWith(@"\\?\",StringComparison.Ordinal)?full:full.StartsWith(@"\\",StringComparison.Ordinal)?@"\\?\UNC\"+full.Substring(2):@"\\?\"+full;
  uint high;ClearLastError(0);
  uint low=GetCompressedFileSize(native,out high);int error=Marshal.GetLastWin32Error();
  if(low==UInt32.MaxValue && error!=0)throw new Win32Exception(error,"Sparse allocation query failed");
  return ((ulong)high<<32)|low;
 }
}
'}
 $c=New-Case 'oversized-full-source'
 $manifestPath=Join-Path $c.installed 'YeManCC/module-package.json';$scriptPath=Join-Path $c.pc 'redist/ymcc-input-payload-apply.ps1'
 $installedManifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
 Need ((Get-FileHash -LiteralPath $scriptPath -Algorithm SHA256).Hash -ieq $installedManifest.compatibility.scriptSha256) 'oversized-source case must reach the verified production bootstrap script'
 Need (!(Current $c)) 'oversized-source case must start with stale modules'
 $previousReceipt=$c.manifest|ConvertTo-Json -Depth 20|ConvertFrom-Json;$previousReceipt.packageVersion='0.0.35';$receiptText=$previousReceipt|ConvertTo-Json -Depth 20
 Put $c.pc '.ymcc-input-managed.json' $receiptText|Out-Null
 $priorStage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($priorStage)|Out-Null
 Write-YmccBootstrapStage $priorStage $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path)
 Put $priorStage 'rollback/.ymcc-input-managed.json' $receiptText|Out-Null
 Put $priorStage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'retained-original-backup'|Out-Null
 Need ((Owned $c).Count -eq 1) 'oversized-source case must retain a real owned backup stage'
 $archive=Join-Path $c.installed 'Packages/YeManCC.zip'
 $guardArchive={
  $caseRoot=[IO.Path]::GetFullPath($c.root);$expected=[IO.Path]::GetFullPath((Join-Path $c.installed 'Packages/YeManCC.zip'))
  Need ($caseRoot.StartsWith($evidence.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase) -and ([IO.Path]::GetFileName($caseRoot) -cmatch '^oversized-full-source-[a-f0-9]{32}$')) 'sparse source must stay in its fresh case-owned root'
  Need ([IO.Path]::GetFullPath($archive) -ieq $expected -and $expected.StartsWith($caseRoot+'\',[StringComparison]::OrdinalIgnoreCase)) 'sparse source path escaped its exact owned archive'
  $cursor=$expected
  while($cursor){if(Test-Path -LiteralPath $cursor){Need (!((Get-Item -LiteralPath $cursor -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)) 'sparse source or ancestor is redirected'};$parent=[IO.Path]::GetDirectoryName($cursor);if($parent -eq $cursor){break};$cursor=$parent}
 }
 &$guardArchive
 [IO.File]::Delete($archive);Need (![IO.File]::Exists($archive)) 'small fixture archive was not removed before exclusive sparse creation'
 $writer=$null;$reader=$null;$created=$false;$primaryFailure=$null
 try{
  $writer=[IO.File]::Open($archive,[IO.FileMode]::CreateNew,[IO.FileAccess]::ReadWrite,[IO.FileShare]::Read);$created=$true
  [YmccBootstrapSparseArchive]::MarkSparse($writer.SafeFileHandle)
  Need (([IO.File]::GetAttributes($archive) -band [IO.FileAttributes]::SparseFile) -ne 0) 'SPARSE_SOURCE_REQUIRED_BEFORE_LENGTH: no non-sparse fallback'
  $writer.SetLength([long]2147483649)
  # The production lease shares reads only, so retire the writer first.
  $writer.Dispose();$writer=$null
  &$guardArchive
  $reader=[IO.File]::Open($archive,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::Read)
  $inspectSparse={
   &$guardArchive
   [pscustomobject]@{path=$archive;logicalBytes=([IO.FileInfo]$archive).Length;sparse=(([IO.File]::GetAttributes($archive) -band [IO.FileAttributes]::SparseFile) -ne 0);allocatedBytes=[YmccBootstrapSparseArchive]::AllocatedBytes($archive)}
  }
  $sparseBefore=&$inspectSparse;$oversizedSourceObservation['sparseBefore']=$sparseBefore
  Need ($sparseBefore.sparse -and $sparseBefore.logicalBytes -eq 2147483649 -and $sparseBefore.allocatedBytes -le 1048576) 'SPARSE_SOURCE_ALLOCATION_REQUIRED: logical size must exceed 2GiB with at most 1MiB allocated'
  # Exclude ONLY this exact sparse path before any hashing; record its metadata.
  $snapshot={@(Get-ChildItem -LiteralPath $c.installed -Recurse -Force|Where-Object {$_.FullName -ine $archive}|Sort-Object FullName|ForEach-Object{
   $relative=[IO.Path]::GetRelativePath($c.installed,$_.FullName)
   if($_.PSIsContainer){[pscustomobject]@{path=$relative;kind='directory'}}else{[pscustomobject]@{path=$relative;kind='file';bytes=$_.Length;sha256=(Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash}}
  })}
  $before=@(&$snapshot);$oversizedSourceObservation['before']=$before
  $result=Invoke-ApplyConsumer $c
  $oversizedSourceObservation['consumer']=[ordered]@{input=$result.consumer;program=$result.program;arguments=$result.arguments;exitCode=$result.exitCode;startedAtUtc=$result.startedAtUtc;milliseconds=$result.milliseconds;budgetMilliseconds=$result.budgetMilliseconds;log=$result.log;logSha256=$result.logSha256}
  $after=@(&$snapshot);$oversizedSourceObservation['after']=$after;$sparseAfter=&$inspectSparse;$oversizedSourceObservation['sparseAfter']=$sparseAfter
  $unchanged=(($before|ConvertTo-Json -Depth 6 -Compress) -ceq ($after|ConvertTo-Json -Depth 6 -Compress));$oversizedSourceObservation['installedTreeUnchanged']=$unchanged
  Need ($result.exitCode -ne 0) 'oversized source unexpectedly succeeded'
  Need (($result.stdout+"`n"+$result.stderr) -match '\bMODULE_SOURCE_FULL_PACKAGE_REQUIRED\b') 'oversized source failed without the exact production source-required diagnostic'
  Need ($sparseAfter.sparse -and $sparseAfter.logicalBytes -eq 2147483649 -and $sparseAfter.allocatedBytes -le 1048576) 'sparse source metadata or bounded allocation changed during refusal'
  Need $unchanged 'oversized source changed settings module files manifest receipt or retained backups'
 }catch{$primaryFailure=$_;$oversizedSourceObservation['failure']=$_.Exception.Message;throw}
 finally{
  try{
   try{if($reader){$reader.Dispose()}}finally{try{if($writer){$writer.Dispose()}}finally{if($created){&$guardArchive;[IO.File]::Delete($archive);Need (![IO.File]::Exists($archive)) 'owned sparse source cleanup failed';$oversizedSourceObservation['sparseCleanup']='REMOVED_EXACT_OWNED_FILE'}}}
  }catch{$oversizedSourceObservation['cleanupError']=$_.Exception.Message;if($null -eq $primaryFailure){throw}}
 }
} $oversizedSourceObservation
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
 $foreign=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($foreign)|Out-Null;Write-YmccBootstrapStage $foreign $c.pc '0.0.35' ('a'*64) 'preparing' @($c.manifest.files.path);$foreignMarker=Join-Path $foreign '.ymcc-bootstrap-stage.json';$foreignRecord=Get-Content -LiteralPath $foreignMarker -Raw|ConvertFrom-Json;$foreignRecord.target=Join-Path $c.root 'other-target';[IO.File]::WriteAllText($foreignMarker,($foreignRecord|ConvertTo-Json -Depth 30))
 $personal=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($personal)|Out-Null;Write-YmccBootstrapStage $personal $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path);Put $personal 'personal.txt' 'keep-user-added'|Out-Null
 $nested=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($nested)|Out-Null;Write-YmccBootstrapStage $nested $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path);Put $nested 'payload/feature-assets/virtual-gamepad/personal-notes.txt' 'keep-nested-user-file'|Out-Null
 $outside=Join-Path $c.root 'outside';Put $outside 'keep.txt' 'outside-untouched'|Out-Null
 $junction=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));New-Item -ItemType Junction -Path $junction -Value $outside|Out-Null
 $owned=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($owned)|Out-Null;Write-YmccBootstrapStage $owned $c.pc '0.0.35' ('a'*64) 'applying' @($c.manifest.files.path);Put $owned 'payload/feature-assets/gyro-motion/runtime.json' 'owned-cache'|Out-Null
 $result=Invoke-ApplyConsumer $c;Need ($result.exitCode -ne 0) 'unknown stage was treated as settled by the startup entry'
 $null=Remove-YmccBootstrapStages $c.installed $c.pc;Need (!(Test-Path -LiteralPath $owned)) 'known abandoned stage not reclaimed after actual installed proof'
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
 $result=Invoke-ApplyConsumer $c;Need ($result.exitCode -ne 0) 'bad receipt was replaced without ownership proof'
 Need ([IO.File]::ReadAllText((Join-Path $c.pc '.ymcc-input-managed.json')) -ceq '{corrupt receipt') 'bad receipt original was lost'
 Put $c.root 'saved-corrupt-managed-original.json' '{corrupt receipt'|Out-Null
 Put $c.pc '.ymcc-input-managed.json' ($c.manifest|ConvertTo-Json -Depth 64 -Compress)|Out-Null
 Apply $c;Need (@((Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36').entries).Count -eq 0) 'explicit valid receipt repair could not recover'

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

# Journal storms reuse this original suite and its fake six-file payload. These
# probes never launch an installer, product Main or the payload apply script.
function Invoke-JournalReader($Case,[string]$Mode,[string]$State='',[int]$Owner=0){
  $info=[Diagnostics.ProcessStartInfo]::new('dotnet');$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
  $argv=@($ModuleConsumer,$Mode,$Case.installed);if($State){$argv+=@($State,[string]$Owner)}
  foreach($arg in $argv){$info.ArgumentList.Add($arg)}
  $child=[Diagnostics.Process]::Start($info);$out=$child.StandardOutput.ReadToEndAsync();$err=$child.StandardError.ReadToEndAsync()
  try{
    Need ($child.WaitForExit(5000)) 'JOURNAL_READER_EXCEEDED_5S'
    $code=$child.ExitCode;$stdout=$out.GetAwaiter().GetResult();$stderr=$err.GetAwaiter().GetResult()
    Put $Case.root ('probe-'+$Mode+'-'+[guid]::NewGuid().ToString('N')+'.log') ($stdout+"`n"+$stderr)|Out-Null
    return [pscustomobject]@{code=$code;stdout=$stdout;stderr=$stderr}
  }finally{if(!$child.HasExited){$child.Kill();$null=$child.WaitForExit(1500)};$child.Dispose()}
}
function New-JournalCase([string]$Name){
  $c=New-Case $Name
  foreach($entry in $c.manifest.files){Copy-Item -LiteralPath (Join-Path (Join-Path $c.source 'PowerControl') $entry.path) -Destination (Join-Path $c.pc $entry.path) -Force}
  Put $c.pc '.ymcc-input-managed.json' ($c.manifest|ConvertTo-Json -Depth 30)|Out-Null
  return $c
}
function Assert-JournalProbe($Case,[string]$Mode,[bool]$Expected,[string]$State='',[int]$Owner=0){
  $result=Invoke-JournalReader $Case $Mode $State $Owner
  Need ($result.code -in @(0,3)) ('reader crashed or bypassed the bounded probe: '+$result.code+' '+$result.stderr)
  Need (($result.code -eq 0) -eq $Expected) ('reader accepted an unconfirmed journal: '+$Mode+' '+$result.stdout)
}
$journalDefects=@('empty','truncated','wrong-root','null','schema-string','schema-future','owner-type','files-type','bytes-string','bytes-fraction','hash-type','duplicate-root','duplicate-node','oversized','payload-identity')
foreach($family in @('manifest','receipt')){foreach($defect in $journalDefects){
  Check ('journal-reader-'+$family+'-'+$defect){
    $c=New-JournalCase ('journal-'+$family+'-'+$defect)
    $doc=$c.manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json
    switch($defect){
      'empty'{$text=''} 'truncated'{$text='{'} 'wrong-root'{$text='[]'} 'null'{$text='null'}
      'schema-string'{$doc.schemaVersion='1'} 'schema-future'{$doc.schemaVersion=2} 'owner-type'{$doc.owner=@('YMCC')}
      'files-type'{$doc.files='all done'} 'bytes-string'{$doc.files[0].bytes=[string]$doc.files[0].bytes}
      'bytes-fraction'{$doc.files[0].bytes=1.5} 'hash-type'{$doc.files[0].sha256=@($doc.files[0].sha256)}
      'duplicate-root'{$text='{"schemaVersion":2,'+($doc|ConvertTo-Json -Depth 30 -Compress).Substring(1)}
      'duplicate-node'{$text=($doc|ConvertTo-Json -Depth 30 -Compress) -replace '"bytes":([0-9]+)','"bytes":0,"bytes":$1'}
      'oversized'{$text=' '*(4194304+1)} 'payload-identity'{$doc.compatibility.payloadSha256='b'*64}
    }
    if($defect -notin @('empty','truncated','wrong-root','null','duplicate-root','duplicate-node','oversized')){$text=$doc|ConvertTo-Json -Depth 30 -Compress}
    if($family -eq 'manifest'){$path=Put $c.installed 'YeManCC/module-package.json' $text;$mode='current'}else{$path=Put $c.pc '.ymcc-input-managed.json' $text;$mode='receipt'}
    $before=[IO.File]::ReadAllBytes($path)
    Assert-JournalProbe $c $mode $false
    Need ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq [Convert]::ToBase64String($before)) 'reader changed the original journal'
  }
}}
foreach($kind in @('missing-manifest-with-receipt','locked-manifest','locked-receipt','valid-historical','payload-changed','unknown-stage')){
 Check ('journal-reader-'+$kind){
  $c=New-JournalCase ('journal-'+$kind);$lock=$null
  try{
   switch($kind){
    'missing-manifest-with-receipt'{[IO.File]::Delete((Join-Path $c.installed 'YeManCC/module-package.json'));Assert-JournalProbe $c 'current' $false;Assert-JournalProbe $c 'receipt' $false}
    'locked-manifest'{$lock=[IO.File]::Open((Join-Path $c.installed 'YeManCC/module-package.json'),'Open','ReadWrite','None');Assert-JournalProbe $c 'current' $false;$lock.Dispose();$lock=$null;Assert-JournalProbe $c 'current' $true}
    'locked-receipt'{$lock=[IO.File]::Open((Join-Path $c.pc '.ymcc-input-managed.json'),'Open','ReadWrite','None');Assert-JournalProbe $c 'receipt' $false;$lock.Dispose();$lock=$null;Assert-JournalProbe $c 'receipt' $true}
    'valid-historical'{Assert-JournalProbe $c 'current' $true;Assert-JournalProbe $c 'receipt' $true}
    'payload-changed'{Put $c.pc $c.manifest.files[0].path 'changed'|Out-Null;Assert-JournalProbe $c 'receipt' $false}
    'unknown-stage'{$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));Put $stage '.ymcc-bootstrap-stage.json' '{'|Out-Null;Assert-JournalProbe $c 'stages' $true;Assert-JournalProbe $c 'current' $false;Assert-JournalProbe $c 'receipt' $false;Need ([IO.File]::ReadAllText((Join-Path $stage '.ymcc-bootstrap-stage.json')) -ceq '{') 'unknown stage original lost'}
   }
  }finally{if($lock){$lock.Dispose()}}
 }
}
foreach($kind in @('valid','wrong-version','wrong-root','schema-future','schema-string','duplicate','wrong-pid','pid-type','phase-type','empty','oversized')){
 Check ('journal-reader-commit-'+$kind){
  $c=New-JournalCase ('journal-commit-'+$kind);$state=[ordered]@{phase='committed';version='0.0.36';pid=$PID}
  switch($kind){
   'wrong-version'{$state.version='0.0.35'} 'wrong-root'{$state['installRoot']=Join-Path $c.root 'another-install'}
   'schema-future'{$state['schemaVersion']=2} 'schema-string'{$state['schemaVersion']='1'}
   'wrong-pid'{$state.pid=$PID+1} 'pid-type'{$state.pid=[string]$PID} 'phase-type'{$state.phase=@('committed')}
  }
  $text=$state|ConvertTo-Json -Compress
  if($kind -eq 'empty'){$text=''}
  if($kind -eq 'oversized'){$text=' '*(4194304+1)}
  if($kind -eq 'duplicate'){$text='{"phase":"preparing",'+$text.Substring(1)}
  $path=Put $c.root 'commit-state.json' $text
  Assert-JournalProbe $c 'commit-probe' ($kind -eq 'valid') $path $PID
  Need ([IO.File]::ReadAllText($path) -ceq $text) 'commit probe replaced original marker'
 }
}
foreach($kind in @('schema-string','duplicate-root','managed-path-type','missing-payload-identity')){
 Check ('journal-stage-authority-'+$kind){
  $c=New-JournalCase ('journal-stage-'+$kind);$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)
  $path=Join-Path $stage '.ymcc-bootstrap-stage.json';$doc=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
  switch($kind){'schema-string'{$doc.schemaVersion='2'} 'managed-path-type'{$doc.managedPaths=@(@{path=$c.manifest.files[0].path})} 'missing-payload-identity'{$doc.PSObject.Properties.Remove('payloadSha256')}}
  $text=$doc|ConvertTo-Json -Depth 30 -Compress;if($kind -eq 'duplicate-root'){$text='{"owner":"foreign",'+$text.Substring(1)}
  [IO.File]::WriteAllText($path,$text)
  Need (!(Test-YmccBootstrapStageOwned $stage $c.pc)) 'unconfirmed stage was granted owned/cleanup authority'
  Need ([IO.File]::ReadAllText($path) -ceq $text) 'stage original was lost'
 }
}


foreach($kind in @('schema-string','bytes-string','duplicate-root','payload-identity','locked','valid-unknown')){
 Check ('journal-plan-'+$kind){
  $c=New-JournalCase ('journal-plan-'+$kind);$receipt=Join-Path $c.pc '.ymcc-input-managed.json';$doc=$c.manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json
  switch($kind){'schema-string'{$doc.schemaVersion='1'} 'bytes-string'{$doc.files[0].bytes=[string]$doc.files[0].bytes} 'payload-identity'{$doc.compatibility.payloadSha256='b'*64} 'valid-unknown'{$doc|Add-Member futureAnnotation ([pscustomobject]@{keep='yes'})}}
  $text=$doc|ConvertTo-Json -Depth 30 -Compress;if($kind -eq 'duplicate-root'){$text='{"owner":"foreign",'+$text.Substring(1)}
  [IO.File]::WriteAllText($receipt,$text);$held=$null
  try{
   if($kind -eq 'locked'){$held=[IO.File]::Open($receipt,'Open','ReadWrite','None')}
   $refused=$false;try{$null=Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36'}catch{$refused=$true}
   Need ($refused -eq ($kind -ne 'valid-unknown')) 'actual module planner accepted an invalid managed receipt'
  }finally{if($held){$held.Dispose()}}
  Need ([IO.File]::ReadAllText($receipt) -ceq $text) 'planner overwrote the original managed receipt'
  if($kind -ne 'valid-unknown'){
   Put $c.pc '.ymcc-input-managed.json' ($c.manifest|ConvertTo-Json -Depth 30)|Out-Null
   $null=Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36'
  }
 }
}


Check 'journal-reader-rollback-without-stage-owner'{
 $c=New-JournalCase 'journal-orphaned-rollback';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'))
 $backup=Put $stage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'pending-original-return'
 Assert-JournalProbe $c 'stages' $true;Assert-JournalProbe $c 'current' $false;Assert-JournalProbe $c 'receipt' $false
 Need ([IO.File]::ReadAllText($backup) -ceq 'pending-original-return') 'orphaned rollback original was lost'
}


foreach($kind in @('legacy-no-compatibility','valid-extensions','explicit-storm','manifest-directory','legacy-development-root','stage-wrong-root','stage-wrong-payload','stage-schema-type')){
 Check ('journal-reader-extra-'+$kind){
  $c=New-JournalCase ('journal-extra-'+$kind);$manifestPath=Join-Path $c.installed 'YeManCC/module-package.json';$receiptPath=Join-Path $c.pc '.ymcc-input-managed.json'
  switch($kind){
   'legacy-no-compatibility'{$doc=$c.manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json;$doc.PSObject.Properties.Remove('compatibility');$text=$doc|ConvertTo-Json -Depth 30;[IO.File]::WriteAllText($manifestPath,$text);[IO.File]::WriteAllText($receiptPath,$text);Assert-JournalProbe $c 'current' $true;Assert-JournalProbe $c 'receipt' $true}
   'valid-extensions'{$doc=$c.manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json;$doc|Add-Member futureRoot ([pscustomobject]@{keep=1});$doc.files[0]|Add-Member futureFile 'keep';$text=$doc|ConvertTo-Json -Depth 30;[IO.File]::WriteAllText($manifestPath,$text);[IO.File]::WriteAllText($receiptPath,$text);Assert-JournalProbe $c 'current' $true;Assert-JournalProbe $c 'receipt' $true;Need ([IO.File]::ReadAllText($receiptPath) -ceq $text) 'valid extension data changed'}
   'explicit-storm'{$valid=[IO.File]::ReadAllText($manifestPath);[IO.File]::WriteAllText($manifestPath,'{');Assert-JournalProbe $c 'storm' $false;Need ([IO.File]::ReadAllText($manifestPath) -ceq '{') 'retry storm lost the corrupt original';Put $c.root 'storm-original.json' '{'|Out-Null;[IO.File]::WriteAllText($manifestPath,$valid);Assert-JournalProbe $c 'current' $true;Assert-JournalProbe $c 'receipt' $true}
   'manifest-directory'{[IO.File]::Delete($manifestPath);[IO.Directory]::CreateDirectory($manifestPath)|Out-Null;Assert-JournalProbe $c 'current' $false}
   'legacy-development-root'{[IO.File]::Delete($manifestPath);[IO.File]::Delete($receiptPath);Assert-JournalProbe $c 'current' $true;Assert-JournalProbe $c 'receipt' $true}
   default{
    $stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
    Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)
    $path=Join-Path $stage '.ymcc-bootstrap-stage.json';$record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
    if($kind -eq 'stage-wrong-root'){$record.target=Join-Path $c.root 'other/PowerControl'}
    if($kind -eq 'stage-wrong-payload'){$record.payloadSha256='b'*64}
    if($kind -eq 'stage-schema-type'){$record.schemaVersion='2'}
    $text=$record|ConvertTo-Json -Depth 30;[IO.File]::WriteAllText($path,$text);$backup=Put $stage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'pending-original'
    Assert-JournalProbe $c 'current' $false;Assert-JournalProbe $c 'receipt' $false;Assert-JournalProbe $c 'stages' $true
    Need ([IO.File]::ReadAllText($path) -ceq $text -and [IO.File]::ReadAllText($backup) -ceq 'pending-original') 'unknown stage or backup changed'
   }
  }
 }
}


foreach($kind in @('empty','truncated','schema-string','duplicate','oversized','wrong-target','wrong-payload','unknown-fields','locked')){
 Check ('journal-writer-stage-'+$kind){
  $c=New-JournalCase ('journal-write-stage-'+$kind);$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'preparing' @($c.manifest.files.path)
  $path=Join-Path $stage '.ymcc-bootstrap-stage.json';$doc=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;$text=$doc|ConvertTo-Json -Depth 30 -Compress
  switch($kind){
   'empty'{$text=''} 'truncated'{$text='{'} 'schema-string'{$doc.schemaVersion='2';$text=$doc|ConvertTo-Json -Depth 30 -Compress}
   'duplicate'{$text='{"owner":"foreign",'+$text.Substring(1)} 'oversized'{$text=' '*(4194304+1)}
   'wrong-target'{$doc.target=Join-Path $c.root 'other';$text=$doc|ConvertTo-Json -Depth 30 -Compress}
   'wrong-payload'{$doc.payloadSha256='b'*64;$text=$doc|ConvertTo-Json -Depth 30 -Compress}
   'unknown-fields'{$doc|Add-Member futureAnnotation ([pscustomobject]@{keep='yes'});$text=$doc|ConvertTo-Json -Depth 30 -Compress}
  }
  [IO.File]::WriteAllText($path,$text);$held=$null
  try{
   if($kind -eq 'locked'){$held=[IO.File]::Open($path,'Open','ReadWrite','None')}
   $refused=$false;try{Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)}catch{$refused=$true}
   Need ($refused -eq ($kind -ne 'unknown-fields')) 'stage writer replaced an unconfirmed existing marker'
  }finally{if($held){$held.Dispose()}}
  if($kind -eq 'unknown-fields'){$actual=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;Need ($actual.futureAnnotation.keep -ceq 'yes') 'stage transition dropped unknown fields'}
  else{Need ([IO.File]::ReadAllText($path) -ceq $text) 'stage writer lost original bytes/debt'}
 }
}
foreach($kind in @('empty','truncated','schema-string','duplicate','ownership-change','unknown-fields','locked','read-only','missing')){
 Check ('journal-writer-receipt-'+$kind){
  $c=New-JournalCase ('journal-write-receipt-'+$kind);$receipt=Join-Path $c.pc '.ymcc-input-managed.json';$doc=$c.manifest|ConvertTo-Json -Depth 30|ConvertFrom-Json
  if($kind -eq 'schema-string'){$doc.schemaVersion='1'}
  if($kind -eq 'read-only'){$doc.packageVersion='0.0.35'}
  if($kind -eq 'unknown-fields'){$doc|Add-Member futureAnnotation ([pscustomobject]@{keep='yes'});$doc.files[0]|Add-Member futureNode 'yes'}
  if($kind -eq 'ownership-change'){
   $extra=Put $c.pc 'feature-assets/gyro-motion/old-owned.dll' 'previous-owned-byte'
   $doc.files+=@([pscustomobject]@{path='feature-assets/gyro-motion/old-owned.dll';bytes=([IO.FileInfo]$extra).Length;sha256=(Get-YmccDeckyHash $extra)})
   [string[]]$rows=@($doc.files|ForEach-Object{$_.path+'|'+$_.bytes+'|'+$_.sha256});[Array]::Sort($rows,[StringComparer]::Ordinal);$sha=[Security.Cryptography.SHA256]::Create();try{$doc.compatibility.payloadSha256=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes(($rows -join "`n"))))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  }
  $text=$doc|ConvertTo-Json -Depth 30 -Compress;if($kind -eq 'empty'){$text=''};if($kind -eq 'truncated'){$text='{'};if($kind -eq 'duplicate'){$text='{"owner":"foreign",'+$text.Substring(1)}
  [IO.File]::WriteAllText($receipt,$text);if($kind -eq 'missing'){[IO.File]::Delete($receipt)};$held=$null
  try{
   if($kind -eq 'locked'){$held=[IO.File]::Open($receipt,'Open','ReadWrite','None')}
   if($kind -eq 'read-only'){[IO.File]::SetAttributes($receipt,[IO.FileAttributes]::ReadOnly)}
   $refused=$false;try{Sync-YmccBootstrapReceipt $c.pc $c.manifest}catch{$refused=$true}
   Need ($refused -eq ($kind -notin @('unknown-fields','missing'))) 'receipt sync replaced an unconfirmed ownership record'
  }finally{if($held){$held.Dispose()};if([IO.File]::Exists($receipt)){[IO.File]::SetAttributes($receipt,[IO.FileAttributes]::Normal)}}
  if($kind -eq 'unknown-fields'){$actual=Get-Content -LiteralPath $receipt -Raw|ConvertFrom-Json;Need ($actual.futureAnnotation.keep -ceq 'yes' -and $actual.files[0].futureNode -ceq 'yes') 'receipt sync lost unknown annotations';Assert-JournalProbe $c 'receipt' $true}
  elseif($kind -eq 'missing'){Assert-JournalProbe $c 'receipt' $true}
  else{Need ([IO.File]::ReadAllText($receipt) -ceq $text) 'receipt sync lost the unconfirmed original'}
  if($kind -eq 'ownership-change'){
   Need ([IO.File]::ReadAllText((Join-Path $c.pc 'feature-assets/gyro-motion/old-owned.dll')) -ceq 'previous-owned-byte') 'valid old ownership was erased'
   $plan=Get-YmccModulePlan (Join-Path $c.source 'PowerControl') $c.pc $c.manifest '0.0.36';Need (@($plan.entries|Where-Object {$_.path -ceq 'feature-assets/gyro-motion/old-owned.dll' -and $_.remove}).Count -eq 1) 'original transaction plan could not settle the ownership change'
  }
 }
}
Check 'journal-cleanup-committed-marker-cannot-substitute-for-current-bytes'{
 $c=New-JournalCase 'journal-fake-committed';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
 Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'committed' @($c.manifest.files.path)
 $backup=Put $stage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'return-original'
 Put $c.pc $c.manifest.files[0].path 'interrupted-installed-bytes'|Out-Null
 $result=Remove-YmccBootstrapStages $c.installed $c.pc
 Need ($result.removed -eq 0 -and [IO.File]::ReadAllText($backup) -ceq 'return-original') 'phase alone authorized deletion of unresolved rollback'
}
Check 'journal-cleanup-lock-preserves-marker-and-retry-finishes'{
 $c=New-JournalCase 'journal-locked-cleanup';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
 Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'committed' @($c.manifest.files.path)
 $backup=Put $stage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'return-original';$held=[IO.File]::Open($backup,'Open','Read','None')
 try{$result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 0 -and [IO.File]::Exists((Join-Path $stage '.ymcc-bootstrap-stage.json'))) 'clear failure lost responsibility marker'}finally{$held.Dispose()}
 $result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 1 -and ![IO.Directory]::Exists($stage)) 'clear retry did not finish after IO fault cleared'
}


foreach($kind in @('valid','wrong-version','wrong-root','wrong-payload','schema-string','schema-future','pid-string','duplicate','oversized')){
 Check ('journal-register-commit-'+$kind){
  $c=New-JournalCase ('journal-register-'+$kind);$state=[ordered]@{phase='committed';version='0.0.36';pid=$PID}
  switch($kind){'wrong-version'{$state.version='0.0.35'} 'wrong-root'{$state['installRoot']=Join-Path $c.root 'foreign'} 'wrong-payload'{$state['payloadSha256']='b'*64} 'schema-string'{$state['schemaVersion']='1'} 'schema-future'{$state['schemaVersion']=2} 'pid-string'{$state.pid=[string]$PID}}
  $text=$state|ConvertTo-Json -Compress;if($kind -eq 'duplicate'){$text='{"phase":"preparing",'+$text.Substring(1)};if($kind -eq 'oversized'){$text=' '*(4194304+1)}
  $path=Put $c.root 'commit-state.json' $text;$refused=$false
  try{$stage=Register-YmccPostCommitHcCleanup $c.installed $c.pc $c.manifest (Join-Path $c.installed 'YeManCC/module-package.json') $path $PID}catch{$refused=$true}
  Need ($refused -eq ($kind -ne 'valid')) 'postcommit writer accepted an unconfirmed commit marker'
  Need ([IO.File]::ReadAllText($path) -ceq $text) 'postcommit writer changed the original commit marker'
  if($kind -ne 'valid'){Need (@(Get-ChildItem -LiteralPath $c.installed -Directory -Filter 'module-bootstrap-*').Count -eq 0) 'rejected commit created cleanup authority'}
 }
}
foreach($kind in @('before-copy','after-copy','missing-backup','wrong-backup','duplicate-ledger')){
 Check ('journal-rollback-proof-'+$kind){
  $c=New-JournalCase ('journal-rollback-'+$kind);$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
  $relative='feature-assets/virtual-gamepad/YeManInputHost.dll';$file=Join-Path $c.pc $relative;$before=Get-YmccDeckyHash $file;$sha=[Security.Cryptography.SHA256]::Create();try{$after=([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes('changed')))).Replace('-','').ToLowerInvariant()}finally{$sha.Dispose()}
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'preparing' @($c.manifest.files.path)
  $plan=[pscustomobject]@{target=$c.pc;entries=@([pscustomobject]@{path=$relative;before=$before;after=$after;remove=$false})}
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path) -TransactionPlan $plan
  if($kind -eq 'after-copy'){$backup=Join-Path $stage ('rollback/'+$relative);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))|Out-Null;Copy-Item -LiteralPath $file -Destination $backup;[IO.File]::WriteAllText($file,'changed')}
  if($kind -eq 'missing-backup'){[IO.File]::WriteAllText($file,'changed')}
  if($kind -eq 'wrong-backup'){$null=Put $stage ('rollback/'+$relative) 'wrong original';[IO.File]::WriteAllText($file,'changed')}
  if($kind -eq 'duplicate-ledger'){$path=Join-Path $stage '.ymcc-bootstrap-stage.json';$record=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json;if(!$record.PSObject.Properties['rollbackLedger']){$record|Add-Member rollbackLedger ([pscustomobject]@{schemaVersion=1;entries=$plan.entries})};$text=$record|ConvertTo-Json -Depth 30 -Compress;[IO.File]::WriteAllText($path,$text.Replace('"rollbackLedger":','"rollbackLedger":{},"rollbackLedger":'))}
  $expected=$kind -in @('before-copy','after-copy');Need ((Test-YmccBootstrapStageOwned $stage $c.pc) -eq $expected) 'rollback evidence misclassified'
  if(!$expected){Assert-JournalProbe $c 'current' $false;$result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 0 -and [IO.File]::Exists((Join-Path $stage '.ymcc-bootstrap-stage.json'))) 'incomplete rollback was deleted'}
 }
}
Check 'journal-cleanup-storm-preserves-unknown-debt'{
 $c=New-JournalCase 'journal-cleanup-storm';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));$marker=Put $stage '.ymcc-bootstrap-stage.json' '{';$backup=Put $stage 'rollback/feature-assets/virtual-gamepad/YeManInputHost.dll' 'return-original'
 for($attempt=0;$attempt -lt 10;$attempt++){$result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 0) 'cleanup retry consumed an unknown stage'}
 Need ([IO.File]::ReadAllText($marker) -ceq '{' -and [IO.File]::ReadAllText($backup) -ceq 'return-original') 'cleanup storm lost original evidence'
}


foreach($kind in @('layout-schema-type','binding-count-type','host-schema-type','host-runtime-type','runtime-schema-type','shared-bytes-type')){
 Check ('journal-graph-'+$kind){
  $c=New-PostCommitCase ('journal-graph-'+$kind);$layoutPath=Join-Path $c.installed 'YeManCC/update-manifest.json';$layout=Get-Content -LiteralPath $layoutPath -Raw|ConvertFrom-Json
  if($kind -eq 'layout-schema-type'){$layout.schemaVersion='1'}
  if($kind -eq 'binding-count-type'){$layout.rules.fanHostPayload.fileCount='1'}
  if($kind -in @('host-schema-type','host-runtime-type')){
   $path=Join-Path $c.pc 'fan-host-v2/YeManFanHost.payload.json';$doc=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
   if($kind -eq 'host-schema-type'){$doc.schemaVersion='2'}else{$doc.runtimeId=@($doc.runtimeId)}
   [IO.File]::WriteAllText($path,($doc|ConvertTo-Json -Depth 30));$layout.rules.fanHostPayload.manifestSha256=Get-YmccDeckyHash $path
  }
  if($kind -in @('runtime-schema-type','shared-bytes-type')){
   $path=Join-Path $c.pc 'handheldcompanion-runtime/YMCC-CLEANUP-CURRENT/HandheldCompanion.runtime.json';$doc=Get-Content -LiteralPath $path -Raw|ConvertFrom-Json
   if($kind -eq 'runtime-schema-type'){$doc.schemaVersion='1'}else{$doc.sharedDependencies.files[0].bytes=[string]$doc.sharedDependencies.files[0].bytes}
   [IO.File]::WriteAllText($path,($doc|ConvertTo-Json -Depth 30));$layout.rules.fanHostPayload.runtimeManifestSha256=Get-YmccDeckyHash $path
  }
  [IO.File]::WriteAllText($layoutPath,($layout|ConvertTo-Json -Depth 30));$refused=$false;try{$null=Assert-YmccPostCommitHcLayout $c.installed $c.manifest}catch{$refused=$true}
  Need $refused 'typed payload graph error was accepted as verified layout'
  Need ([IO.File]::ReadAllText((Join-Path $c.old 'Old.dll')) -ceq 'obsolete-runtime') 'graph validation changed old HC ownership'
 }
}
Check 'journal-writer-stage-refuses-phase-rewind'{
 $c=New-JournalCase 'journal-rewind';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
 Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)
 $path=Join-Path $stage '.ymcc-bootstrap-stage.json';$before=[IO.File]::ReadAllText($path);$refused=$false;try{Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'preparing' @($c.manifest.files.path)}catch{$refused=$true}
 Need ($refused -and [IO.File]::ReadAllText($path) -ceq $before) 'explicit retry rewound durable stage debt'
}
Check 'journal-writer-stage-refuses-false-committed'{
 $c=New-JournalCase 'journal-false-commit';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
 Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)
 $path=Join-Path $stage '.ymcc-bootstrap-stage.json';$before=[IO.File]::ReadAllText($path);Put $c.pc $c.manifest.files[0].path 'interrupted'|Out-Null
 $refused=$false;try{Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'committed' @($c.manifest.files.path)}catch{$refused=$true}
 Need ($refused -and [IO.File]::ReadAllText($path) -ceq $before) 'unverified payload published committed marker'
}
Check 'journal-cleanup-directory-lock-preserves-owner'{
 if(!('YmccBootstrapDirectoryLock' -as [type])){Add-Type -TypeDefinition @"
using System;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
public static class YmccBootstrapDirectoryLock {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
 public static extern SafeFileHandle CreateFile(string path,uint access,uint share,IntPtr security,uint creation,uint flags,IntPtr template);
}
"@}
 $c=New-JournalCase 'journal-root-lock';$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
 Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'committed' @($c.manifest.files.path)
 $held=[YmccBootstrapDirectoryLock]::CreateFile($stage,1,3,[IntPtr]::Zero,3,0x02000000,[IntPtr]::Zero);Need (!$held.IsInvalid) 'owned stage directory lock unavailable'
 try{$result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 0 -and [IO.File]::Exists((Join-Path $stage '.ymcc-bootstrap-stage.json'))) 'directory deletion failure lost marker'}finally{$held.Dispose()}
 $result=Remove-YmccBootstrapStages $c.installed $c.pc;Need ($result.removed -eq 1) 'released directory lock failed to recover'
}

foreach($defect in @('stale-hash','changed-bytes')){
 Check ('journal-cleanup-legacy-checkpoint-'+$defect){
  $c=New-JournalCase ('journal-legacy-proof-'+$defect);$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path)
  $marker=Join-Path $stage '.ymcc-bootstrap-stage.json';$doc=Get-Content -LiteralPath $marker -Raw|ConvertFrom-Json
  $proof=@{schemaVersion=1;moduleManifestSha256=(Get-YmccDeckyHash (Join-Path $c.installed 'YeManCC/module-package.json'));managedReceiptSha256=(Get-YmccDeckyHash (Join-Path $c.pc '.ymcc-input-managed.json'))}
  if($defect -eq 'stale-hash'){$proof.moduleManifestSha256=('0'*64)}else{Put $c.pc $c.manifest.files[0].path 'changed-after-checkpoint'|Out-Null}
  $doc|Add-Member -NotePropertyName cleanupProof -NotePropertyValue $proof;[IO.File]::WriteAllText($marker,($doc|ConvertTo-Json -Depth 30));$before=[IO.File]::ReadAllText($marker)
  Need (!(Test-YmccBootstrapStageOwned $stage $c.pc)) 'legacy checkpoint bypassed current proof validation'
  Need ([IO.File]::ReadAllText($marker) -ceq $before) 'failed legacy checkpoint changed original marker'
 }
}
foreach($defect in @('remove-string','hash-type','path-escape')){
 Check ('journal-writer-plan-'+$defect){
  $c=New-JournalCase ('journal-plan-write-'+$defect);$stage=Join-Path $c.installed ('module-bootstrap-'+[guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($stage)|Out-Null
  Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'preparing' @($c.manifest.files.path)
  $marker=Join-Path $stage '.ymcc-bootstrap-stage.json';$before=[IO.File]::ReadAllText($marker);$entry=[pscustomobject]@{path=$c.manifest.files[0].path;before=$null;after=$c.manifest.files[0].sha256;remove=$false}
  switch($defect){'remove-string'{$entry.remove='false'} 'hash-type'{$entry.before=@('0'*64)} 'path-escape'{$entry.path='../unknown-original'}}
  $plan=[pscustomobject]@{target=$c.pc;entries=@($entry)};$refused=$false
  try{Write-YmccBootstrapStage $stage $c.pc '0.0.36' $c.manifest.compatibility.payloadSha256 'applying' @($c.manifest.files.path) -TransactionPlan $plan}catch{$refused=$true}
  Need ($refused -and [IO.File]::ReadAllText($marker) -ceq $before) 'invalid transaction plan published unreadable rollback debt'
 }
}

$failed=@($cases|Where-Object status -eq FAIL)
$inputs=@('GamepadPrerequisites/ModulePayloadBootstrap.cs','native/updater_hc_runtime.ps1','native/module_bootstrap_recovery.ps1','native/module_payload_bootstrap.ps1','native/module_package_maintenance.ps1','native/decky_package_maintenance.ps1','tools/module_payload_packaging.ps1')|ForEach-Object{[pscustomobject]@{path=$_;sha256=(Get-FileHash -LiteralPath (Join-Path $repo $_)).Hash}}
[pscustomobject]@{status=$(if(!$cases.Count){'NOT_RUN_NO_MATCHING_CASES'}elseif($failed.Count){'FAIL'}else{'PASS'});caseFilter=$CaseFilter;matchedCases=$cases.Count;executedCases=@($cases|Where-Object status -in @('PASS','FAIL')).Count;executionControl=@{stopAfterRequiredFailure=(-not [bool]$CollectAfterFailure);collectAfterFailure=[bool]$CollectAfterFailure;firstFailedCase=$script:bootstrapRequiredFailure;notRunCount=@($cases|Where-Object status -eq 'NOT_RUN_AFTER_REQUIRED_FAILURE').Count};cycles=$Cycles;cases=$cases;inputs=$inputs;executedModuleMaintenance=@{path=$ModuleMaintenanceSource;sha256=(Get-FileHash -LiteralPath $ModuleMaintenanceSource).Hash};executedModuleRecovery=@{path=$ModuleRecoverySource;sha256=(Get-FileHash -LiteralPath $ModuleRecoverySource).Hash};executedHarness=@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath).Hash};hardwareIo=$false;actualConsumer='actual bootstrap script/package functions and production C# ModulePayloadBootstrap';interruption='owned real PowerShell process killed at state/file-commit barriers; original script is used for recovery'}|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $evidence 'result.json') -Encoding UTF8
$cases|Format-Table -AutoSize
if(!$cases.Count){Write-Output 'NOT_RUN_NO_MATCHING_CASES: no bootstrap cases matched CaseFilter';exit 10}
if($failed.Count){throw ('MODULE_BOOTSTRAP_RECOVERY_FAILED '+$failed.Count)}
