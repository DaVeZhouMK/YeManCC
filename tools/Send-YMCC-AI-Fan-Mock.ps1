[CmdletBinding()]
param(
  [Parameter(Mandatory=$true)][ValidateSet('probe','on','off','close','status')][string]$Action,
  [Parameter(Mandatory=$true)][string]$ExePath,
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-fA-F]{64}$')][string]$ExpectedSha256,
  [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ParentPid,
  [Parameter(Mandatory=$true)][string]$ParentCreationTimeUtc,
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$')][string]$SessionId,
  [ValidateSet('soft','balanced','aggressive')][string]$Preset='balanced',
  [ValidateRange(1,120)][int]$TimeoutSeconds=30,
  [Parameter(Mandatory=$true)][string]$OutDir,
  [switch]$ElevationHop,
  [switch]$NoAutoElevate
)
# AI-only mock file client; no settings/installation edits or direct Fan HTTP.
# This is NOT a full YMCC CPU/scenario certificate or an arbitrary-command broker.
$ErrorActionPreference='Stop'
$homeModules=Join-Path $PSHOME 'Modules'
if(($env:PSModulePath -split ';' )[0] -ine $homeModules){$env:PSModulePath=$homeModules+';'+$env:PSModulePath}
$scriptVersion='ymcc-ai-fan-client-v2-20261004'
if(-not [IO.Path]::IsPathRooted($ExePath) -or -not [IO.Path]::IsPathRooted($OutDir)){throw 'ABSOLUTE_PATH_REQUIRED'}
$ExePath=[IO.Path]::GetFullPath($ExePath)
$OutDir=[IO.Path]::GetFullPath($OutDir)
if([IO.Path]::GetFileName($ExePath) -notmatch '^(?i:YeManCC|YMCC)(?:[-_.A-Za-z0-9]+)?\.exe$'){throw 'TARGET_NOT_YMCC'}
if((Get-FileHash -LiteralPath $ExePath -Algorithm SHA256).Hash -ine $ExpectedSha256){throw 'TARGET_SHA256_MISMATCH'}
if((Test-Path -LiteralPath $OutDir) -and @(Get-ChildItem -LiteralPath $OutDir -Force).Count -gt 0 -and -not $ElevationHop){throw 'OUTPUT_DIRECTORY_NOT_EMPTY'}
New-Item -ItemType Directory -Path $OutDir -Force | Out-Null
function Quote([string]$value){
 if($value.Length -gt 0 -and $value -notmatch '[\s"]'){return $value}
 $builder=New-Object Text.StringBuilder
 [void]$builder.Append('"');$slashes=0
 foreach($char in $value.ToCharArray()){
  if($char -eq '\'){$slashes++;continue}
  if($char -eq '"'){[void]$builder.Append(('\' * ($slashes*2+1)));[void]$builder.Append('"');$slashes=0;continue}
  if($slashes){[void]$builder.Append(('\' * $slashes));$slashes=0};[void]$builder.Append($char)
 }
 if($slashes){[void]$builder.Append(('\' * ($slashes*2)))};[void]$builder.Append('"');$builder.ToString()
}
function Ensure-NativeType {
 if(-not ('YmccAiFanClient.Native' -as [type])){
  Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace YmccAiFanClient {
 public static class Native {
  [StructLayout(LayoutKind.Sequential)] struct FileTime { public uint Low; public uint High; }
  [DllImport("kernel32.dll",SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,uint pid);
  [DllImport("kernel32.dll",SetLastError=true)] static extern bool GetProcessTimes(IntPtr p,out FileTime creation,out FileTime exit,out FileTime kernel,out FileTime user);
  [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
  [DllImport("kernel32.dll")] static extern IntPtr GetCurrentProcess();
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool OpenProcessToken(IntPtr p,uint access,out IntPtr t);
  [DllImport("advapi32.dll",SetLastError=true)] static extern bool GetTokenInformation(IntPtr t,int kind,IntPtr buffer,int length,out int returned);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthorityCount(IntPtr sid);
  [DllImport("advapi32.dll")] static extern IntPtr GetSidSubAuthority(IntPtr sid,uint index);
  public static void ReplaceRequest(string source,string destination) { System.IO.File.Replace(source,destination,null); }
  public static string Creation(uint pid) {
   IntPtr h=OpenProcess(0x1000,false,pid); if(h==IntPtr.Zero) throw new InvalidOperationException("PROCESS_IDENTITY_QUERY_FAILED");
   try {FileTime c,e,k,u;if(!GetProcessTimes(h,out c,out e,out k,out u))throw new InvalidOperationException("PROCESS_CREATION_QUERY_FAILED");return (((ulong)c.High<<32)|c.Low).ToString(System.Globalization.CultureInfo.InvariantCulture);}finally{CloseHandle(h);}
  }
  // Both token elevation and integrity are required; UAC configuration is not evidence.
  public static bool HighToken() {
   IntPtr t; if(!OpenProcessToken(GetCurrentProcess(),8,out t))return false;
   try {
    int n;IntPtr e=Marshal.AllocHGlobal(4);bool elevated;
    try {if(!GetTokenInformation(t,20,e,4,out n))return false;elevated=Marshal.ReadInt32(e)!=0;}finally{Marshal.FreeHGlobal(e);}
    GetTokenInformation(t,25,IntPtr.Zero,0,out n);if(n<=0)return false;
    IntPtr b=Marshal.AllocHGlobal(n);try{if(!GetTokenInformation(t,25,b,n,out n))return false;IntPtr sid=Marshal.ReadIntPtr(b);byte count=Marshal.ReadByte(GetSidSubAuthorityCount(sid));if(count==0)return false;uint rid=(uint)Marshal.ReadInt32(GetSidSubAuthority(sid,(uint)(count-1)));return elevated&&rid>=0x3000&&rid<0x4000;}finally{Marshal.FreeHGlobal(b);}
   }finally{CloseHandle(t);}
  }
 }
}
"@
 }
}
Ensure-NativeType
$principal=New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
$elevated=$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator) -and [YmccAiFanClient.Native]::HighToken()
if(-not $elevated){
 if($NoAutoElevate -or $ElevationHop){[IO.File]::WriteAllText((Join-Path $OutDir 'client-result.json'),(@{ok=$false;errorCode='ADMIN_WORKER_REQUIRED';scriptVersion=$scriptVersion}|ConvertTo-Json),[Text.UTF8Encoding]::new($false));exit 6}
 $hostPath=(Get-Process -Id $PID).Path
 $args=@('-NoProfile','-ExecutionPolicy','Bypass','-File',$PSCommandPath,'-Action',$Action,'-ExePath',$ExePath,'-ExpectedSha256',$ExpectedSha256,'-ParentPid',[string]$ParentPid,'-ParentCreationTimeUtc',$ParentCreationTimeUtc,'-SessionId',$SessionId,'-Preset',$Preset,'-TimeoutSeconds',[string]$TimeoutSeconds,'-OutDir',$OutDir,'-ElevationHop')
 try{
  $worker=Start-Process -FilePath $hostPath -Verb RunAs -WindowStyle Hidden -ArgumentList (($args|ForEach-Object{Quote $_}) -join ' ') -PassThru
  $handle=$worker.Handle
  if(-not $worker.WaitForExit(($TimeoutSeconds+30)*1000) -or $null -eq $worker.ExitCode){throw 'ELEVATED_WORKER_TIMEOUT_OR_EXIT_MISSING'}
  exit ([int]$worker.ExitCode)
 }catch{[IO.File]::WriteAllText((Join-Path $OutDir 'client-result.json'),(@{ok=$false;errorCode='AUTO_ELEVATION_FAILED';scriptVersion=$scriptVersion;exceptionType=$_.Exception.GetType().FullName}|ConvertTo-Json),[Text.UTF8Encoding]::new($false));exit 6}
}
function Is-True([object]$x){return $x -is [bool] -and $x -eq $true}
function Is-False([object]$x){return $x -is [bool] -and $x -eq $false}
function Is-Integer([object]$x){return ($x -is [int] -or $x -is [long]) -and $x -ge 0 -and $x -le 9007199254740991L}
function Read-Json([string]$path){
 $reader=[IO.StreamReader]::new($path,[Text.Encoding]::UTF8,$true)
 try{$buffer=New-Object char[] 16385;$count=0;while($count -lt $buffer.Length){$n=$reader.Read($buffer,$count,$buffer.Length-$count);if($n -eq 0){break};$count+=$n};if($count -gt 16384){throw 'AI_EVIDENCE_TOO_LARGE'};return (-join $buffer[0..([math]::Max(0,$count-1))])|ConvertFrom-Json}finally{$reader.Dispose()}
}
function Utc([string]$s){
 if($s -notmatch '^\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d(?:\.\d{1,7})?(?:Z|[+-]\d\d:\d\d)$'){throw 'UTC_TIMESTAMP_REQUIRED'}
 return [DateTimeOffset]::Parse($s,[Globalization.CultureInfo]::InvariantCulture).UtcDateTime
}
$requestId=[guid]::NewGuid().ToString()
$started=[DateTime]::UtcNow
$result=[ordered]@{scriptVersion=$scriptVersion;scope='AI mock request/runtime evidence only; not whole CPU acceptance';sessionId=$SessionId;parentPid=$ParentPid;requestId=$requestId;action=$Action;workerPid=$PID;workerIsElevated=$elevated;workerIntegrity='high';timestampUtc=$started.ToString('o');ok=$false;errorCode=$null}
function Target {
 $row=Get-CimInstance Win32_Process -Filter ('ProcessId='+$ParentPid) -ErrorAction Stop
 if($null -eq $row -or [string]::IsNullOrWhiteSpace($row.ExecutablePath) -or [IO.Path]::GetFullPath($row.ExecutablePath) -ine $ExePath){throw 'TARGET_PATH_OR_PID_CHANGED'}
 $actual=([DateTime]$row.CreationDate).ToUniversalTime()
 if($actual -ne (Utc $ParentCreationTimeUtc)){throw 'TARGET_CREATION_TIME_CHANGED'}
 $owner=Invoke-CimMethod -InputObject $row -MethodName GetOwnerSid -ErrorAction Stop
 if($owner.ReturnValue -ne 0 -or $owner.Sid -ne [Security.Principal.WindowsIdentity]::GetCurrent().User.Value){throw 'TARGET_NOT_CURRENT_USER'}
 $flag='(?:^|\s)--ai-fan-mock-session\s+"?'+[regex]::Escape($SessionId)+'"?(?=\s|$)'
 if(-not [regex]::IsMatch([string]$row.CommandLine,$flag) -or ([regex]::Matches([string]$row.CommandLine,'--ai-fan-mock-session')).Count -ne 1){throw 'TARGET_AI_SESSION_NOT_ATTESTED'}
 $row
}
function Check-ZeroWrite([object]$e){
 if(-not (Is-False $e.hardwareWritesEnabled) -or -not (Is-False $e.hardwareWritesObserved) -or -not (Is-True $e.mockZeroHardwareEvidence) -or $e.hostMode -ne 'mock-handshake' -or [string]$e.protocolVersion -ne '2'){throw 'AI_RUNTIME_ZERO_WRITE_NOT_VERIFIED'}
 if($e.mockControlEnabled -isnot [bool]){throw 'AI_CONTROL_STATE_NOT_BOOLEAN'}
}
$mutex=$null;$locked=$false;$temporary=$null
try{
 $root=Target
 $stateDir=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) ('YeManCC\ai-fan-sessions\'+$SessionId+'\fan-host')
 if(-not (Test-Path -LiteralPath $stateDir -PathType Container)){throw 'AI_NATIVE_SESSION_DIRECTORY_NOT_READY'}
 # Reject namespace reparse redirects. No paths from a request/response are used.
 $dir=Get-Item -LiteralPath $stateDir -Force
 for($i=0;$i -lt 4 -and $null -ne $dir;$i++) {if(($dir.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'AI_SESSION_REPARSE_DIRECTORY'};$dir=$dir.Parent}
 $mutex=[Threading.Mutex]::new($false,('Local\YMCC-AI-Fan-Client-'+$ParentPid+'-'+$SessionId))
 try{$locked=$mutex.WaitOne(0)}catch [Threading.AbandonedMutexException]{$locked=$true}
 if(-not $locked){throw 'AI_CLIENT_SESSION_BUSY'}
 $requestPath=Join-Path $stateDir 'ai-request.json';$responsePath=Join-Path $stateDir 'ai-response.json'
 $sequence=1L
 foreach($p in @($responsePath,$requestPath)){
  if(Test-Path -LiteralPath $p){$previous=Read-Json $p;if($previous.sessionId -ne $SessionId -or $previous.parentPid -ne $ParentPid -or -not (Is-Integer $previous.sequence)){throw 'AI_PREVIOUS_SLOT_INVALID'};$sequence=[math]::Max($sequence,[long]$previous.sequence+1)}
 }
 if($sequence -le 0 -or $sequence -gt 9007199254740991L){throw 'AI_SEQUENCE_EXHAUSTED'}
 $request=[ordered]@{schemaVersion=1;sessionId=$SessionId;parentPid=$ParentPid;requestId=$requestId;sequence=$sequence;action=$Action}
 if($Action -eq 'on'){$request.preset=$Preset}
 $temporary=Join-Path $stateDir ('ai-request.'+$requestId+'.tmp')
 [IO.File]::WriteAllText($temporary,($request|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 [void](Target)
 # Replace is atomic on the same volume; single-client mutex prevents slot loss.
 if(Test-Path -LiteralPath $requestPath){[YmccAiFanClient.Native]::ReplaceRequest($temporary,$requestPath)}else{[IO.File]::Move($temporary,$requestPath)}
 $temporary=$null
 [IO.File]::WriteAllText((Join-Path $OutDir 'request.json'),($request|ConvertTo-Json -Depth 5),[Text.UTF8Encoding]::new($false))
 $deadline=[DateTime]::UtcNow.AddSeconds($TimeoutSeconds);$reply=$null
 while([DateTime]::UtcNow -lt $deadline){
  [void](Target)
  if(Test-Path -LiteralPath $responsePath){
   $observed=Read-Json $responsePath
   if((Is-Integer $observed.schemaVersion) -and $observed.schemaVersion -eq 1 -and (Is-Integer $observed.parentPid) -and $observed.sessionId -eq $SessionId -and $observed.parentPid -eq $ParentPid -and $observed.requestId -eq $requestId -and (Is-Integer $observed.sequence) -and $observed.sequence -eq $sequence -and $observed.action -eq $Action){$reply=$observed;break}
  }
  Start-Sleep -Milliseconds 200
 }
 if($null -eq $reply){throw 'AI_REQUEST_RESPONSE_TIMEOUT'}
 if(-not (Is-True $reply.ok)){throw 'AI_REQUEST_OR_RUNTIME_FAILED'}
 $at=Utc $reply.timestampUtc
 if($at -lt $started -or $at -gt [DateTime]::UtcNow.AddSeconds(1)){throw 'AI_RESPONSE_NOT_FRESH'}
 $e=$reply.evidence
 if($null -eq $e -or $e.source -ne 'mock' -or $e.mode -ne 'mock-handshake' -or -not (Is-Integer $e.hostPid)){throw 'AI_SOURCE_NOT_MOCK'}
 if($e.hostPid -gt 0){
  Check-ZeroWrite $e
  if(-not (Is-True $e.runtimeVerified) -or $e.hostCreationTime100ns -isnot [string] -or $e.hostCreationTime100ns -notmatch '^[1-9][0-9]*$'){throw 'AI_HOST_RUNTIME_NOT_VERIFIED'}
  $hostRow=Get-CimInstance Win32_Process -Filter ('ProcessId='+$e.hostPid) -ErrorAction Stop
  $hostPath=Join-Path ([IO.Path]::GetDirectoryName([IO.Path]::GetDirectoryName($ExePath))) 'PowerControl\fan-host-v2\YeManFanHost.exe'
  if($null -eq $hostRow -or [string]::IsNullOrWhiteSpace($hostRow.ExecutablePath) -or [IO.Path]::GetFullPath($hostRow.ExecutablePath) -ine $hostPath -or $hostRow.ParentProcessId -ne $ParentPid){throw 'AI_HOST_PATH_OR_PARENT_MISMATCH'}
  if([YmccAiFanClient.Native]::Creation([uint32]$e.hostPid) -ne $e.hostCreationTime100ns){throw 'AI_HOST_CREATION_TIME_CHANGED'}
 }elseif(-not (Is-False $e.runtimeVerified)) {throw 'AI_STOPPED_HOST_FALSE_RUNTIME'}
 if($Action -in @('probe','on') -and $e.hostPid -le 0){throw 'AI_OWNED_HOST_NOT_RUNNING'}
 if($Action -eq 'on' -and (-not (Is-True $e.mockControlEnabled) -or -not (Is-True $e.leaseHeld) -or -not (Is-Integer $e.mockControlSequence) -or $e.mockControlSequence -lt 1)){throw 'AI_CONTROL_NOT_ACTIVE'}
 if($Action -eq 'off' -and $e.hostPid -gt 0 -and -not (Is-False $e.mockControlEnabled)){throw 'AI_CONTROL_STILL_ACTIVE'}
 if($Action -eq 'close' -and $e.hostPid -gt 0){throw 'AI_CLOSE_CHILD_STILL_RUNNING'}
 if($Action -eq 'close' -and $null -ne $e.closeReceipt){
  Check-ZeroWrite $e.closeReceipt
  if(-not (Is-True $e.closeReceipt.mockCloseCompleted) -or -not (Is-False $e.closeReceipt.mockControlEnabled) -or -not (Is-True $e.childExited) -or -not (Is-Integer $e.closedHostPid) -or $e.closedHostPid -le 0 -or $e.closedHostCreationTime100ns -notmatch '^[1-9][0-9]*$'){throw 'AI_CLOSE_RECEIPT_NOT_CONFIRMED'}
 }
 $safeEvidence=[ordered]@{}
 foreach($key in @('phase','source','mode','hostPid','hostCreationTime100ns','runtimeVerified','state','powerState','hostMode','protocolVersion','hardwareWritesEnabled','hardwareWritesObserved','mockZeroHardwareEvidence','mockControlEnabled','mockCloseCompleted','mockWritesObserved','mockControlSequence','openCalled','openEventsCalled','leaseGeneration','leaseHeld','evidenceScope','closedHostPid','closedHostCreationTime100ns','childExited')){if($null -ne $e.PSObject.Properties[$key] -and ($e.$key -is [string] -or $e.$key -is [ValueType])){$safeEvidence[$key]=$e.$key}}
 if($null -ne $e.closeReceipt){$receipt=[ordered]@{};foreach($key in @('state','hostMode','protocolVersion','hardwareWritesEnabled','hardwareWritesObserved','mockZeroHardwareEvidence','mockControlEnabled','mockCloseCompleted','mockWritesObserved','mockControlSequence','openCalled','openEventsCalled','leaseHeld','evidenceScope')){if($null -ne $e.closeReceipt.PSObject.Properties[$key] -and ($e.closeReceipt.$key -is [string] -or $e.closeReceipt.$key -is [ValueType])){$receipt[$key]=$e.closeReceipt.$key}};$safeEvidence.closeReceipt=$receipt}
 [void](Target)
 $result.ok=$true;$result.sequence=$sequence;$result.stateReadAtUtc=$reply.timestampUtc;$result.rootCreationTimeUtc=([DateTime]$root.CreationDate).ToUniversalTime().ToString('o');$result.rootSha256=$ExpectedSha256.ToLowerInvariant();$result.evidence=$safeEvidence
}catch{$result.errorCode=$_.Exception.Message;if($result.errorCode -notmatch '^[A-Z0-9_]+$'){$result.errorCode='AI_CLIENT_EXCEPTION'};$result.exceptionType=$_.Exception.GetType().FullName}
finally{if($temporary -and (Test-Path -LiteralPath $temporary)){Remove-Item -LiteralPath $temporary -Force};if($locked){$mutex.ReleaseMutex()};if($mutex){$mutex.Dispose()}}
[IO.File]::WriteAllText((Join-Path $OutDir 'client-result.json'),($result|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
if(-not $result.ok){Write-Output ('AI fan client failed: '+$result.errorCode);exit 7}
Write-Output ('AI fan '+$Action+' verified; PID='+$ParentPid+' sequence='+$result.sequence)
exit 0