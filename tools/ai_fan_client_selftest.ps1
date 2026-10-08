[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$Client,[Parameter(Mandatory=$true)][string]$OutDir,[int]$QueryPid=0)
$ErrorActionPreference='Stop'
$env:PSModulePath=(Join-Path $PSHOME 'Modules')+';'+$env:PSModulePath
New-Item -ItemType Directory -Path $OutDir -Force|Out-Null
$t=$null;$errors=$null;$ast=[Management.Automation.Language.Parser]::ParseFile($Client,[ref]$t,[ref]$errors)
if($errors.Count){throw 'CLIENT_PARSE_FAILED'}
$names=@('Quote','Ensure-NativeType','Is-True','Is-False','Is-Integer','Read-Json','Utc','Check-ZeroWrite')
foreach($name in $names){$f=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -eq $name},$true));if($f.Count -ne 1){throw 'FUNCTION_EXTRACTION_FAILED'};Invoke-Expression $f[0].Extent.Text}
Ensure-NativeType
if($QueryPid -gt 0){$row=Get-CimInstance Win32_Process -Filter ('ProcessId='+$QueryPid);@{creationTime100ns=[YmccAiFanClient.Native]::Creation([uint32]$QueryPid);creationTimeUtc=([DateTime]$row.CreationDate).ToUniversalTime().ToString('o')}|ConvertTo-Json -Compress;exit 0}
Add-Type @"
using System;
using System.Runtime.InteropServices;
namespace AiFanQuoteTest {
 public static class Args {
  [DllImport("shell32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CommandLineToArgvW(string cmd,out int argc);
  [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
  public static string[] Parse(string cmd){int count;IntPtr p=CommandLineToArgvW(cmd,out count);if(p==IntPtr.Zero)throw new Exception("argv failed");try{string[] a=new string[count];for(int i=0;i<count;i++)a[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(p,i*IntPtr.Size));return a;}finally{LocalFree(p);}}
 }
}
"@
$script:checks=0
function Check([bool]$ok,[string]$msg){$script:checks++;if(-not $ok){throw $msg}}
$values=@('','plain','a b','中文路径','G:\a b\','a"b','\','\\','before\\"after',"`t",'path with space\"')
$r=[Random]::new(74151);$chars=@('a','中',' ','"','\',"`t")
for($n=0;$n -lt 1000;$n++){$v='';for($j=0;$j -lt $r.Next(1,65);$j++){$v+=$chars[$r.Next($chars.Count)]};$values+=$v}
foreach($v in $values){$a=[AiFanQuoteTest.Args]::Parse(('test.exe '+(Quote $v)));Check ($a.Length -eq 2 -and $a[1] -ceq $v) ('quote mismatch for length='+$v.Length)}
foreach($v in @('true','false',1,0,$null,@{},@(),[pscustomobject]@{x=1})){Check (-not (Is-True $v) -and -not (Is-False $v)) 'strict boolean accepted non-bool'}
Check ((Is-True $true) -and (Is-False $false)) 'boolean rejected'
foreach($v in @('1',1.0,1.5,-1,$null,9007199254740992L)){Check (-not (Is-Integer $v)) 'integer accepted wrong value/type'}
Check ((Is-Integer 1) -and (Is-Integer 9007199254740991L)) 'integer rejected'
$stamp=([DateTime]::UtcNow).ToString('o');Check ((Utc $stamp).ToString('o') -eq $stamp) 'UTC precision changed'
foreach($v in @('today','2026-10-04','2026-10-04T00:00:00','NaN')){$reject=$false;try{[void](Utc $v)}catch{$reject=$true};Check $reject 'ambiguous timestamp accepted'}
$valid=[pscustomobject]@{hardwareWritesEnabled=$false;hardwareWritesObserved=$false;mockZeroHardwareEvidence=$true;hostMode='mock-handshake';protocolVersion='2';mockControlEnabled=$false}
Check-ZeroWrite $valid
foreach($field in @('hardwareWritesEnabled','hardwareWritesObserved','mockZeroHardwareEvidence','mockControlEnabled')){$x=$valid|ConvertTo-Json|ConvertFrom-Json;$x.$field='false';$reject=$false;try{Check-ZeroWrite $x}catch{$reject=$true};Check $reject 'nonbool zero evidence accepted'}
$input=Join-Path $OutDir 'json-reader.json';[IO.File]::WriteAllText($input,'{"n":1,"text":"中文"}',[Text.UTF8Encoding]::new($true));$x=Read-Json $input;Check ($x.n -eq 1 -and $x.text -eq '中文') 'UTF8/BOM JSON read failed'
[IO.File]::WriteAllText($input,('a'*16385));$reject=$false;try{[void](Read-Json $input)}catch{$reject=$_.Exception.Message -eq 'AI_EVIDENCE_TOO_LARGE'};Check $reject 'oversized JSON accepted'
$dst=Join-Path $OutDir 'atomic-slot.json';$tmp=Join-Path $OutDir 'atomic-new.tmp';[IO.File]::WriteAllText($dst,'{"v":1}');[IO.File]::WriteAllText($tmp,'{"v":2}');[YmccAiFanClient.Native]::ReplaceRequest($tmp,$dst);Check ((Read-Json $dst).v -eq 2 -and -not (Test-Path -LiteralPath $tmp)) 'atomic replace failed'
$mutexName='Local\YMCC-AI-Fan-Client-test-'+[guid]::NewGuid().ToString();$m=[Threading.Mutex]::new($false,$mutexName);Check $m.WaitOne(0) 'mutex acquisition failed';$m.ReleaseMutex();$m.Dispose()
$c=[YmccAiFanClient.Native]::Creation([uint32]$PID);Check ($c -match '^[1-9][0-9]*$') 'actual creationTime query failed'
$result=@{status='PASS';checks=$checks;psVersion=$PSVersionTable.PSVersion.ToString();scope='client parser/Windows argv/strict types/atomic replace/native token and creation API; not product runtime or CPU';tokenIsHigh=[YmccAiFanClient.Native]::HighToken();creationTime100ns=$c;clientSha256=(Get-FileHash -LiteralPath $Client).Hash}
[IO.File]::WriteAllText((Join-Path $OutDir 'unit-results.json'),($result|ConvertTo-Json),[Text.UTF8Encoding]::new($false));$result|ConvertTo-Json