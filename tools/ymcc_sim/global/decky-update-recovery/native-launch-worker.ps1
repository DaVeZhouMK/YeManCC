[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Request,[Parameter(Mandatory=$true)][string]$ExpectedSha256,[switch]$GuardianLauncher,[int]$LauncherPid=0)
$ErrorActionPreference='Stop'
function Sha([string]$File){$stream=[IO.File]::OpenRead($File);$algorithm=[Security.Cryptography.SHA256]::Create();try{return ([BitConverter]::ToString($algorithm.ComputeHash($stream))).Replace('-','').ToLowerInvariant()}finally{$stream.Dispose();$algorithm.Dispose()}}
function Need($Value,[string]$Why){if(!$Value){throw ('NATIVE_LAUNCH_OWNER_'+$Why)}}
function Write-Json([string]$File,$Value){Need (!(Test-Path -LiteralPath $File)) 'FRESH_RECEIPT';[IO.File]::WriteAllText($File,($Value|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))}
Need ($PSVersionTable.PSVersion.Major -eq 5) 'WINDOWS_POWERSHELL_5_REQUIRED'
Need ((Sha $Request) -ceq $ExpectedSha256) 'REQUEST_PIN'
$r=Get-Content -LiteralPath $Request -Raw -Encoding UTF8|ConvertFrom-Json
$separator=[IO.Path]::DirectorySeparatorChar
$root=[IO.Path]::GetFullPath('G:/YeManCC-Work/Testing/YMCC/Workspace/Nl').TrimEnd($separator)+$separator
$work=[IO.Path]::GetFullPath($r.work)
Need ($work.StartsWith($root,[StringComparison]::OrdinalIgnoreCase) -and [IO.Path]::GetDirectoryName($work) -eq $root.TrimEnd($separator)) 'EXACT_OWNED_SHORT_ROOT'
Need (([IO.FileInfo]$Request).Directory.FullName -eq $work) 'REQUEST_IN_OWNED_ROOT'
Need ($r.kind -ceq 'private-native-launch-worker') 'REQUEST_KIND'
Need ($r.nonce -match '^[a-f0-9]{64}$') 'NONCE_FORMAT'
$now=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()
Need ($r.deadlineMs -gt $now -and $r.deadlineMs-$now -le 240000 -and $r.cleanupMs -eq 1500 -and $r.runDeadlineMs -eq $r.deadlineMs-1500) 'ABSOLUTE_SHARED_DEADLINE'
$receipt=[ordered]@{schemaVersion=1;status='FAIL';mode=$r.mode;helperPid=$PID;helperBirthTicks=[Diagnostics.Process]::GetCurrentProcess().StartTime.ToUniversalTime().Ticks.ToString();mainParent=$r.parent;mainParentExitObserved=$false;startedAt=[DateTimeOffset]::UtcNow.ToString('o');deadlineMs=$r.deadlineMs;runDeadlineMs=$r.runDeadlineMs;compiler=$null;childPid=$null;childBirthTicks=$null;jobAssigned=$false;killOnJobClose=$false;nonceSent=$false;jobCloseSucceeded=$false;childExitObserved=$false;jobActiveProcessesAtClose=-1;ownedHandlesAfter=0;sourceChanges=@();error=$null}
$parent=$null;$launcher=$null;$compiler=$null;$child=$null;$job=$null;$stopTask=$null;$childStdout=$null;$childStderr=$null;$exitCode=2
function Remaining([long]$Deadline){return [Math]::Max(0,[Math]::Min(2147483647,$Deadline-[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()))}
function Validate-Pins {
 $changes=New-Object Collections.Generic.List[string]
 foreach($row in @($r.sources)+@($r.tools)){try{if((Sha $row.path) -cne $row.sha256){$changes.Add($row.path)}}catch{$changes.Add($row.path)}}
 return $changes.ToArray()
}
function Stop-Requested {
 if($stopTask -and $stopTask.IsCompleted){$line=$stopTask.GetAwaiter().GetResult();if($null -eq $line -or $line -ceq ('STOP '+$r.nonce)){return $true};throw 'NATIVE_LAUNCH_OWNER_INVALID_CONTROL_NONCE'}
 return $false
}
function Parent-Alive {return !$parent.HasExited}
# Wait on exact process handles and the pending stdin task. No name search,
# periodic worker or arbitrary process attachment is used for ownership.
function Wait-Owned([Diagnostics.Process]$Process,[long]$Deadline){
 $parentWait=New-Object Threading.EventWaitHandle($false,[Threading.EventResetMode]::ManualReset)
 $processWait=New-Object Threading.EventWaitHandle($false,[Threading.EventResetMode]::ManualReset)
 try {
  $parentWait.SafeWaitHandle=New-Object Microsoft.Win32.SafeHandles.SafeWaitHandle($parent.Handle,$false)
  $processWait.SafeWaitHandle=New-Object Microsoft.Win32.SafeHandles.SafeWaitHandle($Process.Handle,$false)
  $taskWait=([IAsyncResult]$stopTask).AsyncWaitHandle
  $index=[Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]@($parentWait,$processWait,$taskWait),[int](Remaining $Deadline))
  if($index -eq 0){$receipt.mainParentExitObserved=$true;return 'PARENT_EXIT'}
  if($index -eq 1){return 'EXIT'}
  if($index -eq 2){if(Stop-Requested){return 'STOPPED'}}
  return 'TIMEOUT'
 }finally{$parentWait.Dispose();$processWait.Dispose()}
}
# A Node-spawned non-detached PS is subject to that Node's own lifetime Job.
# Direct detached PS5 did not execute on this fixed host (sealed raw controls).
# The .NET-created, hidden PS guardian is not uv_spawn's direct child. It owns
# the private worker Job and watches the actual calling Node, not this relay.
if($GuardianLauncher){
 $ownerProcess=$null;$launcherParent=$null;$ownerOut=$null;$ownerErr=$null
 $launch=[ordered]@{schemaVersion=1;status='FAIL';launcherPid=$PID;ownerPid=$null;ownerBirthTicks=$null;ownerExitObserved=$false;ownerExitCode=$null;ownedProcessHandlesAfter=0;error=$null}
 try{
  Need (@(Validate-Pins).Count -eq 0) 'LAUNCHER_PINS'
  $launcherParent=[Diagnostics.Process]::GetProcessById([int]$r.parent.pid)
  Need ($launcherParent.StartTime.ToUniversalTime().Ticks.ToString() -ceq $r.parent.birthTicks -and $launcherParent.MainModule.FileName -ieq $r.parent.image -and !$launcherParent.HasExited) 'LAUNCHER_EXACT_MAIN'
  $launcherHandle=$launcherParent.Handle
  $startInfo=New-Object Diagnostics.ProcessStartInfo
  $startInfo.FileName=$r.powershell.path
  $startInfo.Arguments='-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "'+$PSCommandPath+'" -Request "'+$Request+'" -ExpectedSha256 '+$ExpectedSha256+' -LauncherPid '+$PID
  $startInfo.UseShellExecute=$false;$startInfo.CreateNoWindow=$true;$startInfo.WorkingDirectory=$r.repo
  $startInfo.RedirectStandardInput=$true;$startInfo.RedirectStandardOutput=$true;$startInfo.RedirectStandardError=$true
  $ownerProcess=New-Object Diagnostics.Process;$ownerProcess.StartInfo=$startInfo
  Need ($ownerProcess.Start()) 'GUARDIAN_START'
  $launch.ownerPid=$ownerProcess.Id;$launch.ownerBirthTicks=$ownerProcess.StartTime.ToUniversalTime().Ticks.ToString()
  $ownerOut=$ownerProcess.StandardOutput.ReadToEndAsync();$ownerErr=$ownerProcess.StandardError.ReadToEndAsync()
  Write-Json (Join-Path $work 'guardian-started.json') ([ordered]@{launcherPid=$PID;launcherBirthTicks=[Diagnostics.Process]::GetCurrentProcess().StartTime.ToUniversalTime().Ticks.ToString();launcherImage=$r.powershell.path;ownerPid=$ownerProcess.Id;ownerBirthTicks=$launch.ownerBirthTicks;ownerImage=$r.powershell.path;mainParent=$r.parent;requestSha256=$ExpectedSha256;createdAt=[DateTimeOffset]::UtcNow.ToString('o')})
  $ownerProcess.StandardInput.WriteLine('GUARDIAN '+$r.nonce)
  $parent=$launcherParent
  $relayReader=New-Object IO.StreamReader([Console]::OpenStandardInput());$stopTask=$relayReader.ReadLineAsync()
  $reason=Wait-Owned $ownerProcess ([long]$r.runDeadlineMs)
  if($reason -ne 'EXIT'){
   $ownerProcess.StandardInput.WriteLine('STOP '+$r.nonce);$ownerProcess.StandardInput.Close()
   $launch.ownerExitObserved=$ownerProcess.WaitForExit([int](Remaining $r.deadlineMs))
  }else{$launch.ownerExitObserved=$true}
  Need $launch.ownerExitObserved 'GUARDIAN_EXIT_UNOBSERVED'
  $launch.ownerExitCode=$ownerProcess.ExitCode;$launch.status='EXIT_OBSERVED'
 }catch{$launch.error=$_.Exception.Message}finally{
  if($ownerProcess){try{if(!$ownerProcess.HasExited){$ownerProcess.Kill()};if(!$launch.ownerExitObserved){$launch.ownerExitObserved=$ownerProcess.WaitForExit([int](Remaining $r.deadlineMs))};if($launch.ownerExitObserved){$launch.ownerExitCode=$ownerProcess.ExitCode};$ownerProcess.Dispose();$ownerProcess=$null}catch{$launch.error=$_.Exception.Message;$launch.status='CLEANUP_INCOMPLETE'}}
  foreach($stream in @(@{task=$ownerOut;file='guardian.stdout.log'},@{task=$ownerErr;file='guardian.stderr.log'})){if($stream.task -and $stream.task.Wait([int](Remaining $r.deadlineMs))){[IO.File]::WriteAllText((Join-Path $work $stream.file),$stream.task.Result)}}
  if($launcherParent){$launcherParent.Dispose();$launcherParent=$null;$parent=$null}
  $launch.ownedProcessHandlesAfter=[int]($null -ne $ownerProcess)+[int]($null -ne $launcherParent)
  Write-Json (Join-Path $work 'guardian-launcher-result.json') $launch
 }
 if($launch.status -eq 'EXIT_OBSERVED' -and $launch.ownerExitObserved -and $launch.ownedProcessHandlesAfter -eq 0){exit $launch.ownerExitCode}
 exit 2
}
try {
 Need ((Sha $r.powershell.path) -ceq $r.powershell.sha256) 'POWERSHELL_PIN'
 Need ($LauncherPid -gt 0) 'GUARDIAN_LAUNCHER_REQUIRED'
 $controlReader=New-Object IO.StreamReader([Console]::OpenStandardInput())
 $handoff=$controlReader.ReadLineAsync()
 Need ($handoff.Wait([int][Math]::Min(5000,(Remaining $r.runDeadlineMs))) -and $handoff.GetAwaiter().GetResult() -ceq ('GUARDIAN '+$r.nonce)) 'GUARDIAN_HANDOFF_NONCE'
 $launchIdentity=Get-Content -LiteralPath (Join-Path $work 'guardian-started.json') -Raw|ConvertFrom-Json
 Need ($launchIdentity.launcherPid -eq $LauncherPid -and $launchIdentity.ownerPid -eq $PID -and $launchIdentity.requestSha256 -ceq $ExpectedSha256) 'GUARDIAN_HANDOFF_IDENTITY'
 $launcher=[Diagnostics.Process]::GetProcessById($LauncherPid)
 Need ($launcher.StartTime.ToUniversalTime().Ticks.ToString() -ceq $launchIdentity.launcherBirthTicks -and $launcher.MainModule.FileName -ieq $r.powershell.path) 'EXACT_LAUNCHER_IDENTITY'
 $launcherHandle=$launcher.Handle
 $receipt.launcherPid=$LauncherPid;$receipt.launcherBirthTicks=$launchIdentity.launcherBirthTicks
 $parent=[Diagnostics.Process]::GetProcessById([int]$r.parent.pid)
 Need ($parent.StartTime.ToUniversalTime().Ticks.ToString() -ceq $r.parent.birthTicks -and $parent.MainModule.FileName -ieq $r.parent.image -and (Parent-Alive)) 'EXACT_MAIN_PARENT_IDENTITY'
 $stopTask=$controlReader.ReadLineAsync()
 $changes=@(Validate-Pins);Need ($changes.Count -eq 0) 'SOURCE_OR_TOOL_CHANGED_BEFORE_COMPILE'
 Need ((Sha $r.packageManifest.path) -ceq '2ba2e58c4c1367375cd265192972a371e993544880623f4b51ca412cfe85746a') 'ORIGINAL_PACKAGE_MANIFEST_PIN'
 $authority=Get-Content -LiteralPath $r.packageManifest.path -Raw|ConvertFrom-Json
 Need ($authority.files.Count -eq 42) 'ORIGINAL_42_COMPILER_TOOLS'
 foreach($row in $authority.files){Need ((Sha $row.path) -ceq $row.sha256) ('COMPILER_TOOL_PIN:'+ $row.path)}
 $cs=Join-Path $work 'owned-job.cs';$dll=Join-Path $work 'owned-job.dll'
 # Existing package fixture Job layout/constructor/assign/close, with bounded
 # accounting/termination and exact OS parent queries. Compiler is explicit and
 # held as a separate exact owned Process before the class is available.
 [IO.File]::WriteAllText($cs,@"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class YmccNativeLaunchWorkerJob : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong r,w,o,rb,wb,ob; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC { public long perProcess,perJob; public uint flags; public UIntPtr minWorking,maxWorking; public uint active; public UIntPtr affinity; public uint priority,scheduling; }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED { public BASIC basic; public IO_COUNTERS io; public UIntPtr processMemory,jobMemory,peakProcess,peakJob; }
 [StructLayout(LayoutKind.Sequential)] struct ACCOUNTING { public long user,kernel,periodUser,periodKernel; public uint faults,total,active,terminated; }
 [StructLayout(LayoutKind.Sequential)] struct PBI { public IntPtr reserved,peb,reserved2,reserved3; public UIntPtr processId,parentProcessId; }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attrs,string name);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref EXTENDED info,uint length);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool TerminateJobObject(IntPtr job,uint exit);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool QueryInformationJobObject(IntPtr job,int type,out ACCOUNTING info,uint length,IntPtr returned);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool CloseHandle(IntPtr handle);
 [DllImport("ntdll.dll")] static extern int NtQueryInformationProcess(IntPtr process,int kind,out PBI info,int size,out int returned);
 IntPtr handle;
 public YmccNativeLaunchWorkerJob(){handle=CreateJobObject(IntPtr.Zero,null);if(handle==IntPtr.Zero)throw new Win32Exception(Marshal.GetLastWin32Error());var info=new EXTENDED();info.basic.flags=0x00002000;if(!SetInformationJobObject(handle,9,ref info,(uint)Marshal.SizeOf(typeof(EXTENDED)))){var error=Marshal.GetLastWin32Error();Close();throw new Win32Exception(error);}}
 public void Assign(IntPtr process){if(!AssignProcessToJobObject(handle,process))throw new Win32Exception(Marshal.GetLastWin32Error());}
 public void Terminate(){if(!TerminateJobObject(handle,1223))throw new Win32Exception(Marshal.GetLastWin32Error());}
 public uint Active(){ACCOUNTING info;if(!QueryInformationJobObject(handle,1,out info,(uint)Marshal.SizeOf(typeof(ACCOUNTING)),IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());return info.active;}
 public static uint ParentId(IntPtr process){PBI info;int returned;int status=NtQueryInformationProcess(process,0,out info,Marshal.SizeOf(typeof(PBI)),out returned);if(status!=0)throw new Exception("Parent query NTSTATUS "+status);return info.parentProcessId.ToUInt32();}
 public bool Close(){if(handle==IntPtr.Zero)return true;if(!CloseHandle(handle))return false;handle=IntPtr.Zero;return true;}
 public void Dispose(){Close();}
}
"@,[Text.UTF8Encoding]::new($false))
 $compileInfo=New-Object Diagnostics.ProcessStartInfo
 $compileInfo.FileName=$authority.compiler
 $references=@($authority.referencedAssemblies|ForEach-Object {'/reference:"'+$_+'"'}) -join ' '
 $compileInfo.Arguments='/nologo /noconfig /nostdlib+ /target:library /out:"'+$dll+'" '+$references+' "'+$cs+'"'
 $compileInfo.WorkingDirectory=$work;$compileInfo.UseShellExecute=$false;$compileInfo.CreateNoWindow=$true;$compileInfo.RedirectStandardOutput=$true;$compileInfo.RedirectStandardError=$true
 $compiler=New-Object Diagnostics.Process;$compiler.StartInfo=$compileInfo
 Need ($compiler.Start()) 'COMPILER_START'
 $compileOut=$compiler.StandardOutput.ReadToEndAsync();$compileErr=$compiler.StandardError.ReadToEndAsync()
 $compileDeadline=[Math]::Min([long]$r.runDeadlineMs,[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()+15000)
 $receipt.compiler=[ordered]@{pid=$compiler.Id;birthTicks=$compiler.StartTime.ToUniversalTime().Ticks.ToString();image=$authority.compiler;args=$compileInfo.Arguments;deadlineMs=$compileDeadline;budgetMaximumMs=15000;exitObserved=$false;exitCode=$null;reason=$null;sourceSha256=(Sha $cs)}
 $compileReason=Wait-Owned $compiler $compileDeadline;$receipt.compiler.reason=$compileReason
 if($compileReason -ne 'EXIT'){$receipt.status=$compileReason;if(!$compiler.HasExited){$compiler.Kill()};Need ($compiler.WaitForExit([int](Remaining $r.deadlineMs))) 'COMPILER_REAP'}
 $receipt.compiler.exitObserved=$compiler.HasExited;$receipt.compiler.exitCode=$compiler.ExitCode
 Need ($compileOut.Wait(500) -and $compileErr.Wait(500)) 'COMPILER_STREAM_DRAIN'
 [IO.File]::WriteAllText((Join-Path $work 'compile.stdout.log'),$compileOut.Result);[IO.File]::WriteAllText((Join-Path $work 'compile.stderr.log'),$compileErr.Result)
 Need ($compileReason -eq 'EXIT' -and $compiler.ExitCode -eq 0) 'COMPILER_NONZERO_OR_CANCELLED'
 $compiler.Dispose();$compiler=$null
 Add-Type -Path $dll
 Need ([YmccNativeLaunchWorkerJob]::ParentId([Diagnostics.Process]::GetCurrentProcess().Handle) -eq [uint32]$LauncherPid -and [YmccNativeLaunchWorkerJob]::ParentId($launcherHandle) -eq [uint32]$r.parent.pid) 'REAL_OS_MAIN_PARENT_CHAIN'
 Need ((Parent-Alive) -and !(Stop-Requested) -and (Remaining $r.runDeadlineMs) -gt 0) 'CANCELLED_BEFORE_WORKER'
 $job=New-Object YmccNativeLaunchWorkerJob;$receipt.killOnJobClose=$true
 $info=New-Object Diagnostics.ProcessStartInfo;$info.FileName=$r.node.path
 Need ($r.node.path.IndexOf('"') -lt 0 -and $r.bootstrap.IndexOf('"') -lt 0 -and $Request.IndexOf('"') -lt 0) 'LITERAL_ARGUMENT_PATHS'
 $info.Arguments='"'+$r.bootstrap+'" "'+$Request+'" '+$ExpectedSha256+' '+$PID
 $info.WorkingDirectory=$r.repo;$info.UseShellExecute=$false;$info.CreateNoWindow=$true;$info.RedirectStandardInput=$true;$info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $child=New-Object Diagnostics.Process;$child.StartInfo=$info
 Need ($child.Start()) 'BOOTSTRAP_START'
 $receipt.childPid=$child.Id;$receipt.childBirthTicks=$child.StartTime.ToUniversalTime().Ticks.ToString()
 $childStdout=$child.StandardOutput.ReadToEndAsync();$childStderr=$child.StandardError.ReadToEndAsync()
 $job.Assign($child.Handle);$receipt.jobAssigned=$true
 Write-Json (Join-Path $work 'assigned.json') ([ordered]@{childPid=$child.Id;childBirthTicks=$receipt.childBirthTicks;helperPid=$PID;helperBirthTicks=$receipt.helperBirthTicks;mainParent=$r.parent;jobAssigned=$true;killOnJobClose=$true;assignedAt=[DateTimeOffset]::UtcNow.ToString('o');childImage=$r.node.path})
 $child.StandardInput.WriteLine($r.nonce);$child.StandardInput.Close();$receipt.nonceSent=$true
 $reason=Wait-Owned $child ([long]$r.runDeadlineMs)
 $receipt.status=if($reason -eq 'EXIT'){'COMPLETED'}else{$reason}
 if($reason -eq 'EXIT'){$receipt.childExitCode=$child.ExitCode;if($child.ExitCode -ne 0){$receipt.status='CHILD_NONZERO'}}
}catch{
 $receipt.error=$_.Exception.Message
 if($receipt.status -notin @('PARENT_EXIT','STOPPED','TIMEOUT')){$receipt.status='FAIL'}
}finally{
 $cleanupStart=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();$cleanupDeadline=[Math]::Min([long]$r.deadlineMs,$cleanupStart+1500)
 if($compiler){try{if(!$compiler.HasExited){$compiler.Kill()};$receipt.compiler.exitObserved=$compiler.WaitForExit([int](Remaining $cleanupDeadline));if($receipt.compiler.exitObserved){$receipt.compiler.exitCode=$compiler.ExitCode};$compiler.Dispose();$compiler=$null}catch{$receipt.error='Compiler cleanup: '+$_.Exception.Message;$receipt.status='CLEANUP_INCOMPLETE'}}
 if($job){try{
  if($job.Active() -gt 0){$job.Terminate();$receipt.jobTerminationAttempted=$true}
  if($child){$receipt.childExitObserved=$child.WaitForExit([int](Remaining $cleanupDeadline));if($receipt.childExitObserved){$receipt.childExitCode=$child.ExitCode}}
  while($job.Active() -gt 0 -and (Remaining $cleanupDeadline) -gt 0){[Threading.Thread]::Sleep([Math]::Min(10,[int](Remaining $cleanupDeadline)))}
  $receipt.jobActiveProcessesAtClose=$job.Active()
  $receipt.jobCloseSucceeded=$job.Close();if($receipt.jobCloseSucceeded){$job=$null}
 }catch{$receipt.error='Job cleanup: '+$_.Exception.Message;$receipt.status='CLEANUP_INCOMPLETE';if($job){$receipt.jobCloseSucceeded=$job.Close();if($receipt.jobCloseSucceeded){$job=$null}}}}
 if($child){try{if(!$receipt.jobAssigned -and !$child.HasExited){$child.Kill()};if(!$receipt.childExitObserved){$receipt.childExitObserved=$child.WaitForExit([int](Remaining $cleanupDeadline))};$child.Dispose();$child=$null}catch{$receipt.status='CLEANUP_INCOMPLETE';$receipt.error=$_.Exception.Message}}
 foreach($entry in @(@{task=$childStdout;file='bootstrap.stdout.log'},@{task=$childStderr;file='bootstrap.stderr.log'})){if($entry.task){if($entry.task.Wait([int](Remaining $cleanupDeadline))){[IO.File]::WriteAllText((Join-Path $work $entry.file),$entry.task.Result)}else{$receipt.status='CLEANUP_INCOMPLETE';$receipt.error='Child stream drain unobserved'}}}
 if($parent){$receipt.mainParentExitObserved=$parent.WaitForExit(0);if($receipt.mainParentExitObserved -and $receipt.status -eq 'STOPPED'){$receipt.status='PARENT_EXIT'};$parent.Dispose();$parent=$null}
 if($launcher){$launcher.Dispose();$launcher=$null}
 $receipt.ownedHandlesAfter=[int]($null -ne $job)+[int]($null -ne $child)+[int]($null -ne $compiler)+[int]($null -ne $parent)+[int]($null -ne $launcher)
 $receipt.sourceChanges=@(Validate-Pins)
 if($receipt.sourceChanges.Count -gt 0){$receipt.status='SOURCE_OR_TOOL_CHANGED'}
 if($receipt.jobAssigned -and (!$receipt.jobCloseSucceeded -or !$receipt.childExitObserved -or $receipt.jobActiveProcessesAtClose -ne 0 -or $receipt.ownedHandlesAfter -ne 0)){$receipt.status='CLEANUP_INCOMPLETE'}
 if([DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() -gt [long]$r.deadlineMs){$receipt.status='TIMEOUT'}
 $receipt.cleanupElapsedMs=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()-$cleanupStart;$receipt.finishedAt=[DateTimeOffset]::UtcNow.ToString('o')
 Write-Json (Join-Path $work 'owner-result.json') $receipt
 if($receipt.status -eq 'COMPLETED'){$exitCode=0}
}
exit $exitCode