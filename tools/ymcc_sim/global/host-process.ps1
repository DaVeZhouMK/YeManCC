<# A job contains only the gated child created by this helper and its descendants.
   If the runner times out or crashes, Windows closes this job and reclaims its tests.
   The child cannot execute a test before successful job assignment. No name-based cleanup. #>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$ScriptPath,
 [Parameter(Mandatory=$true)][string]$ArgumentsPath,
 [switch]$Child,
 [string]$ReadyEvent=''
)
$ErrorActionPreference='Stop'
if($PSVersionTable.PSVersion.Major -lt 7){Write-Error 'POWERSHELL_7_REQUIRED_FOR_OWNED_TEST_JOB';exit 10}
if($Child){
 $ready=[Threading.EventWaitHandle]::OpenExisting($ReadyEvent)
 try {if(-not $ready.WaitOne(30000)){throw 'OWNED_TEST_JOB_NOT_ASSIGNED'}} finally {$ready.Dispose()}
 $testArguments=@(Get-Content -LiteralPath $ArgumentsPath -Raw -Encoding UTF8|ConvertFrom-Json)
 & (Join-Path $PSHOME 'pwsh.exe') -NoLogo -NoProfile -NonInteractive -File $ScriptPath @testArguments
 exit $LASTEXITCODE
}
Add-Type -TypeDefinition @"
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
public sealed class YmccGlobalOwnedJob : IDisposable {
 [StructLayout(LayoutKind.Sequential)] struct IO_COUNTERS { public ulong r,w,o,rb,wb,ob; }
 [StructLayout(LayoutKind.Sequential)] struct BASIC {
  public long perProcess,perJob; public uint flags;
  public UIntPtr minWorking,maxWorking; public uint active;
  public UIntPtr affinity; public uint priority,scheduling;
 }
 [StructLayout(LayoutKind.Sequential)] struct EXTENDED {
  public BASIC basic; public IO_COUNTERS io;
  public UIntPtr processMemory,jobMemory,peakProcess,peakJob;
 }
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateJobObject(IntPtr attrs,string name);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool SetInformationJobObject(IntPtr job,int type,ref EXTENDED info,uint length);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool AssignProcessToJobObject(IntPtr job,IntPtr process);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
 IntPtr handle;
 public YmccGlobalOwnedJob() {
  handle=CreateJobObject(IntPtr.Zero,null);
  if(handle==IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
  var info=new EXTENDED(); info.basic.flags=0x00002000;
  if(!SetInformationJobObject(handle,9,ref info,(uint)Marshal.SizeOf(typeof(EXTENDED)))) {
   var error=Marshal.GetLastWin32Error(); Dispose(); throw new Win32Exception(error);
  }
 }
 public void Assign(IntPtr process) {if(!AssignProcessToJobObject(handle,process)) throw new Win32Exception(Marshal.GetLastWin32Error());}
 public void Dispose(){if(handle!=IntPtr.Zero){CloseHandle(handle);handle=IntPtr.Zero;}}
}
"@
$eventName='Local\YMCC-Global-Owned-Test-'+[Guid]::NewGuid().ToString('N')
$ready=[Threading.EventWaitHandle]::new($false,[Threading.EventResetMode]::ManualReset,$eventName)
$job=$null;$process=$null;$code=10
try {
 $job=[YmccGlobalOwnedJob]::new()
 $info=[Diagnostics.ProcessStartInfo]::new()
 $info.FileName=Join-Path $PSHOME 'pwsh.exe'
 $info.UseShellExecute=$false;$info.CreateNoWindow=$true
 $info.RedirectStandardOutput=$true;$info.RedirectStandardError=$true
 $info.StandardOutputEncoding=[Text.Encoding]::UTF8;$info.StandardErrorEncoding=[Text.Encoding]::UTF8
 $info.WorkingDirectory=(Get-Location).ProviderPath
 foreach($arg in @('-NoLogo','-NoProfile','-NonInteractive','-File',$PSCommandPath,'-Child','-ReadyEvent',$eventName,'-ScriptPath',$ScriptPath,'-ArgumentsPath',$ArgumentsPath)){$info.ArgumentList.Add($arg)}
 $process=[Diagnostics.Process]::Start($info)
 $stdout=$process.StandardOutput.ReadToEndAsync()
 $stderr=$process.StandardError.ReadToEndAsync()
 $job.Assign($process.Handle)
 [void]$ready.Set()
 Write-Output ('YMCC_SIM_OWNED_JOB childPid='+$process.Id)
 $process.WaitForExit()
 $code=$process.ExitCode
 $job.Dispose();$job=$null
 [Console]::Out.Write($stdout.GetAwaiter().GetResult())
 [Console]::Error.Write($stderr.GetAwaiter().GetResult())
 $code=$process.ExitCode
} catch {
 Write-Error ('OWNED_TEST_JOB_FAILED: '+$_.Exception.GetType().Name) -ErrorAction Continue
 if($process -and -not $process.HasExited){$process.Kill()}
 $code=10
} finally {
 if($job){$job.Dispose()}
 if($process){$process.Dispose()}
 $ready.Dispose()
}
exit $code