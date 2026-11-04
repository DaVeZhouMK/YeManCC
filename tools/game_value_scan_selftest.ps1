[CmdletBinding()]
param([switch]$Elevated,[string]$ValidationRoot='', [ValidateSet('Pipeline','Cancel','Lease','LuaFailure','SessionConfig','SessionLua')][string]$TestMode='Pipeline', [ValidatePattern('^[a-zA-Z0-9 ._-]+\.exe$')][string]$FixtureExeName='MappedFixture.exe', [string]$GameTitle='CE test fixture', [ValidatePattern('^[0-9]*$')][string]$SteamAppId='', [string]$CaseFilter='', [string]$LuaRuntimePath='', [string]$SessionSourcePath='')
if(($CaseFilter -or $SessionSourcePath -or $LuaRuntimePath) -and $TestMode -notin @('SessionConfig','SessionLua')){throw 'Selection options are supported only by inert SessionConfig/SessionLua'}
if($TestMode -in @('SessionConfig','SessionLua')){
 if(!$PSBoundParameters.ContainsKey('ValidationRoot') -or [string]::IsNullOrWhiteSpace($ValidationRoot) -or ![IO.Path]::IsPathRooted($ValidationRoot)){throw 'SessionConfig requires an explicit owned absolute output directory'}
 $sessionOutput=[IO.Path]::GetFullPath($ValidationRoot)
 if([IO.File]::Exists($sessionOutput) -or ([IO.Directory]::Exists($sessionOutput) -and [IO.Directory]::GetFileSystemEntries($sessionOutput).Length)){throw 'Preserve existing output; use a fresh owned directory'}
 for($parent=$sessionOutput;$parent;$parent=[IO.Path]::GetDirectoryName($parent)){if([IO.Directory]::Exists($parent) -and ([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::ReparsePoint)){throw 'SessionConfig output is redirected'}}
}
if(-not $ValidationRoot){$ValidationRoot=Join-Path (Split-Path (Split-Path $PSScriptRoot)) 'Build/Validation/CeEngineBridge-selftest'}
$ErrorActionPreference='Stop';$out=$ValidationRoot;[IO.Directory]::CreateDirectory($out)|Out-Null
$sourceRoot=Split-Path $PSScriptRoot
if($TestMode -eq 'SessionLua'){
 if([IntPtr]::Size -ne 4){throw 'SessionLua requires the existing x86 PowerShell host for the selected x86 Lua DLL'}
 if(!$LuaRuntimePath -or ![IO.File]::Exists($LuaRuntimePath)){throw 'SessionLua requires an explicit existing standalone Lua DLL'}
 function Lua-Pin([string]$Path){[pscustomobject]@{path=[IO.Path]::GetFullPath($Path);sha256=(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant();bytes=(Get-Item -LiteralPath $Path).Length}}
 function Lua-Literal([string]$Text){if($Text.Contains(']========]')){throw 'Unsupported literal delimiter'};return '[========['+$Text+']========]'}
 $luaStarted=[Diagnostics.Stopwatch]::StartNew();$luaSource=Join-Path $sourceRoot 'PowerControl/MemoryScan/CeScanEngine.lua'
 if($SessionSourcePath){$private=[IO.Path]::GetFullPath($SessionSourcePath);$privateRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($out))+[IO.Path]::DirectorySeparatorChar;if(!$private.StartsWith($privateRoot,[StringComparison]::OrdinalIgnoreCase)){throw 'Lua override must be private beside the owned output root'};$luaSource=$private}
 $luaInputPaths=@($luaSource,$PSCommandPath,[IO.Path]::GetFullPath($LuaRuntimePath),(Get-Process -Id $PID).Path);$luaInputPins=@($luaInputPaths|ForEach-Object {Lua-Pin $_})
 $dllBytes=[IO.File]::ReadAllBytes($LuaRuntimePath);$pe=[BitConverter]::ToInt32($dllBytes,60);if([BitConverter]::ToUInt16($dllBytes,$pe+4) -ne 0x014c){throw 'Selected Lua runtime must be x86'}
 $runtimeCopy=Join-Path $out 'lua5.1.dll';[IO.File]::Copy($LuaRuntimePath,$runtimeCopy,$false);$runtimeLiteral=$runtimeCopy.Replace('\','\\').Replace('"','\"')
 $luaAssembly=Join-Path $out 'ValueScan.InertLua.dll'
 $luaHost=@"
using System;using System.Runtime.InteropServices;using System.Text;
public static class ValueScanInertLua {
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr luaL_newstate();
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern void luaL_openlibs(IntPtr L);
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern int luaL_loadbuffer(IntPtr L,byte[] text,UIntPtr size,string name);
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern int lua_pcall(IntPtr L,int nargs,int results,int error);
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern IntPtr lua_tolstring(IntPtr L,int index,out UIntPtr size);
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern void lua_getfield(IntPtr L,int index,string key);
 [DllImport("$runtimeLiteral",CallingConvention=CallingConvention.Cdecl)] static extern void lua_close(IntPtr L);
 static string Text(IntPtr L){UIntPtr size;IntPtr p=lua_tolstring(L,-1,out size);if(p==IntPtr.Zero)throw new Exception("Lua result missing");byte[] bytes=new byte[(int)size];Marshal.Copy(p,bytes,0,bytes.Length);return Encoding.UTF8.GetString(bytes);}
 public static string Run(string chunk){var L=luaL_newstate();if(L==IntPtr.Zero)throw new Exception("Lua state allocation failed");try{luaL_openlibs(L);var bytes=Encoding.UTF8.GetBytes(chunk);int rc=luaL_loadbuffer(L,bytes,(UIntPtr)bytes.Length,"@owned-inert-consumer");if(rc==0)rc=lua_pcall(L,0,0,0);if(rc!=0)throw new Exception(Text(L));lua_getfield(L,-10002,"YMCC_TEST_RESULT");return Text(L);}finally{lua_close(L);}}
}
"@
 Add-Type -TypeDefinition $luaHost -OutputAssembly $luaAssembly -ErrorAction Stop
 [Reflection.Assembly]::LoadFrom($luaAssembly)|Out-Null
 $luaOwned=Join-Path $out 'owned-lua-files';[IO.Directory]::CreateDirectory($luaOwned)|Out-Null
 for($n=1;$n -le 64;$n++){[IO.Directory]::CreateDirectory((Join-Path $luaOwned ('session-{0:D3}' -f $n)))|Out-Null}
 $luaHarness=@'
local nativeIO,nativeOS=io,os
local cases,skipped,serial={}, {},0
local totals={reads=0,writes=0,flushes=0,renames=0,removes=0,forbidden=0,fakeAttach=0,fakeRegistry=0}
local function escape(s)return '"'..s:gsub('[%z\1-\31\\"]',function(c)if c=='"'then return '\\"'elseif c=='\\'then return '\\\\'else return string.format('\\u%04x',c:byte())end end)..'"'end
local function encode(v)
 if type(v)=='string'then return escape(v)end
 if type(v)=='number'or type(v)=='boolean'then return tostring(v)end
 if type(v)~='table'then return 'null'end
 local parts={};if v.__array then for i=1,#v do parts[#parts+1]=encode(v[i])end;return '['..table.concat(parts,',')..']'end
 for k,x in pairs(v)do if k~='__array'then parts[#parts+1]=escape(k)..':'..encode(x)end end;return '{'..table.concat(parts,',')..'}'
end
local function check(name,fn)
 if CASE_FILTER~=''and not name:find(CASE_FILTER,1,true)then skipped[#skipped+1]=name;return end
 local ok,err=pcall(fn);cases[#cases+1]={name=name,passed=ok,error=ok and nil or tostring(err)}
end
local function newSession()
 serial=serial+1;assert(serial<=64,'owned directory budget');local root=OWNED_ROOT..'\\session-'..string.format('%03d',serial)
 local token=string.rep('a',64);local timer=nil;local clock=0;local closed,forms=0,0;local attached=0
 local faults={};local statePath=root..'\\ce-state.json'
 local function safe(p)
  assert(type(p)=='string','path type');local n=p:gsub('/','\\');assert(n:sub(1,#root+1)==root..'\\','IO outside owned session')
  for part in n:gmatch('[^\\]+')do assert(part~='..'and part~='.'and not part:find('%z'),'unsafe component')end
  return n
 end
 local function put(p,text)local h=assert(nativeIO.open(safe(p),'wb'));assert(h:write(text));assert(h:flush());assert(h:close())end
 local function get(p)local h,err,code=nativeIO.open(safe(p),'rb');if not h then assert(code==2,err);return nil end;local v=h:read('*a');h:close();return v or ''end
 local function denied()totals.forbidden=totals.forbidden+1;error('real or fake scan/memory dispatch is forbidden in SessionLua')end
 local found={Count=0,Address={},Value={}}
 local mem={FoundList=found,waitTillDone=denied,getProgress=denied}
 local form={VarType={ItemIndex=3},ScanType={ItemIndex=0},cbPauseWhileScanning={Checked=false},btnNewScan={Enabled=true,doClick=denied},btnNextScan={Enabled=false,doClick=denied},ScanValue={},hide=function()end,OnShow=denied,findComponentByName=function()return{Enabled=false,doClick=denied}end}
 local env={YMCC_CE_SESSION={root=root,token=token,pid=1234},assert=assert,error=error,type=type,tostring=tostring,tonumber=tonumber,pairs=pairs,ipairs=ipairs,pcall=pcall,math=math,string=string,table=table,select=select,next=next}
 env.getMainForm=function()forms=forms+1;return form end;env.getCurrentMemscan=function()return mem end
 env.getTickCount=function()return clock end;env.reloadSettingsFromRegistry=function()totals.fakeRegistry=totals.fakeRegistry+1 end
 env.getAutoAttachList=function()return{clear=function()end}end
 env.openProcess=function(pid)assert(pid==1234);attached=pid;totals.fakeAttach=totals.fakeAttach+1 end
 env.getOpenedProcessID=function()return attached end;env.getCEVersion=function()return 'inert-no-CE'end
 env.readInteger=denied;env.writeInteger=denied;env.pause=denied;env.unpause=denied
 env.createTimer=function()timer={Enabled=false,Interval=0,destroyed=false};timer.destroy=function()timer.destroyed=true;timer.Enabled=false end;return timer end
 env.closeCE=function()closed=closed+1 end
 env.io={open=function(p,mode)
  p=safe(p);assert(mode=='rb'or mode=='wb','unsupported IO mode')
  if faults.readRefuse and mode=='rb'and p==statePath then return nil,'owned IO refusal',13 end
  local h,err,code=nativeIO.open(p,mode);if not h then return nil,err,code end
  return {read=function(_,...)totals.reads=totals.reads+1;return h:read(...)end,write=function(_,v)totals.writes=totals.writes+1;return h:write(v)end,flush=function()totals.flushes=totals.flushes+1;if faults.flushRefuse then return nil,'owned flush refusal'end;return h:flush()end,close=function()return h:close()end}
 end}
 env.os={remove=function(p)p=safe(p);assert(p~=statePath,'writer attempted primary deletion');totals.removes=totals.removes+1;return nativeOS.remove(p)end,rename=function(a,b)
  a,b=safe(a),safe(b);totals.renames=totals.renames+1
  if faults.promoteRefuse and a:sub(-4)=='.tmp'and b==statePath then return nil,'owned promotion refusal',13 end
  if faults.archiveRefuse and a==statePath then return nil,'owned archive refusal',13 end
  local ok,err,code=nativeOS.rename(a,b)
  if ok and faults.commandReplacement and a==root..'\\ce-command.txt'then put(a,faults.commandReplacement)end
  return ok,err,code
 end}
 local x={root=root,token=token,statePath=statePath,faults=faults,env=env,put=put,get=get}
 x.start=function(config)if config~=nil then env.YMCC_CE_SESSION=config end;local chunk=assert(loadstring(PRODUCTION_SOURCE,'@actual-CeScanEngine.lua'));setfenv(chunk,env);chunk()end
 x.tick=function(at)clock=at;assert(timer and timer.Enabled and not timer.destroyed,'timer not active');timer.OnTimer()end
 x.closed=function()return closed end;x.timer=function()return timer end;x.forms=function()return forms end
 return x
end
check('lua-file-actual-publish-and-write-readback-preserves-a-valid-idle-session',function()
 local x=newSession();x.start();assert(x.timer()and x.closed()==0);local first=assert(x.get(x.statePath));assert(first:find('"schemaVersion":1',1,true)and first:find('"stage":"idle"',1,true))
 x.put(x.root..'\\ce-lease.txt',x.token..'\n1');x.tick(1000);assert(x.closed()==0)
 x.put(x.root..'\\stop','stop');x.tick(1100);local last=assert(x.get(x.statePath));assert(last~=first and last:find('"stage":"closed"',1,true));assert(x.closed()==1 and x.timer().destroyed)
end)
check('lua-file-lease-exact-token-sequence-and-replay-expire-without-memory-authority',function()
 for _,bad in ipairs({'wrong-token','prefix-token','repeat','decrease','trailing'})do
  local x=newSession();x.start();x.put(x.root..'\\ce-lease.txt',x.token..'\n2');x.tick(1000)
  local nextLease=bad=='wrong-token'and string.rep('b',64)..'\n3'or bad=='prefix-token'and x.token..'extra\n3'or bad=='repeat'and x.token..'\n2'or bad=='decrease'and x.token..'\n1'or x.token..'\n3junk'
  x.put(x.root..'\\ce-lease.txt',nextLease);x.tick(7000);assert(x.closed()==0);x.tick(9001);assert(x.closed()==1 and x.timer().destroyed)
 end
 local x=newSession();x.start();x.put(x.root..'\\ce-lease.txt',x.token..'\n1');x.tick(1000);x.put(x.root..'\\ce-lease.txt',x.token..'\n2');x.tick(7000);x.tick(9001);assert(x.closed()==0);x.put(x.root..'\\stop','stop');x.tick(9002);assert(x.closed()==1)
end)
check('lua-file-promotion-rename-failure-restores-exact-original-and-explicit-new-session-recovers',function()
 local x=newSession();x.start();local original=x.get(x.statePath);x.faults.promoteRefuse=true;x.put(x.root..'\\stop','stop');x.tick(100);assert(x.closed()==1 and x.timer().destroyed);assert(x.get(x.statePath)==original,'promotion failure lost primary bytes')
 x.faults.promoteRefuse=false;local retry=newSession();retry.start();retry.put(retry.root..'\\stop','stop');retry.tick(100);assert(retry.get(retry.statePath):find('"stage":"closed"',1,true))
end)
check('lua-file-archive-rename-and-flush-failures-preserve-original-and-idle-cleanup',function()
 for _,fault in ipairs({'archiveRefuse','flushRefuse'})do local x=newSession();x.start();local original=x.get(x.statePath);x.faults[fault]=true;x.put(x.root..'\\stop','stop');x.tick(100);assert(x.closed()==1 and x.timer().destroyed);assert(x.get(x.statePath)==original,fault..' lost original')end
end)
check('lua-file-unknown-future-oversize-and-read-IO-preimages-stay-preserved-and-inactive',function()
 for _,kind in ipairs({'future','oversize','read-io'})do
  local x=newSession();local raw=kind=='oversize'and string.rep('x',131073)or '{"schemaVersion":99,"foreign":"keep"}';x.put(x.statePath,raw);if kind=='read-io'then x.faults.readRefuse=true end
  x.start();assert(x.closed()==1 and x.timer()==nil);x.faults.readRefuse=false;assert(x.get(x.statePath)==raw,'unknown preimage overwritten')
 end
 local x=newSession();x.start();local raw='{"schemaVersion":99,"replacement":true}';x.put(x.statePath,raw);x.put(x.root..'\\stop','stop');x.tick(100);assert(x.closed()==1 and x.get(x.statePath)==raw)
end)
check('lua-file-command-claim-keeps-concurrent-replacement-and-rejects-wrong-token',function()
 local x=newSession();x.start();local request=string.rep('c',32)..'\nwrong-token\nfirst\n100\n';local replacement='replacement-owned-by-another-request';x.put(x.root..'\\ce-command.txt',request);x.faults.commandReplacement=replacement;x.tick(100)
 assert(x.get(x.root..'\\ce-command.txt')==replacement,'old command deleted newer file');assert(x.get(x.root..'\\ce-command.txt.'..x.token..'.claim')==request,'rejected raw claim lost');x.tick(200);assert(x.closed()==1)
end)
check('lua-file-unknown-bootstrap-never-calls-CE-or-creates-session-state',function()
 for _,config in ipairs({false,'wrong',{root='relative',token=string.rep('a',64),pid=1234},{root=OWNED_ROOT,token='bad',pid=1234},{root=OWNED_ROOT,token=string.rep('a',64),pid='1234'}})do local x=newSession();x.start(config);assert(x.forms()==0 and x.timer()==nil and x.closed()==0 and x.get(x.statePath)==nil)end
end)
local passed,failed=0,0;for _,item in ipairs(cases)do if item.passed then passed=passed+1 else failed=failed+1 end end
assert(totals.forbidden==0,'forbidden scan/memory fake port was attempted')
cases.__array=true;skipped.__array=true
YMCC_TEST_RESULT=encode({passed=passed,failed=failed,cases=cases,skipped=skipped,operations=totals,luaVersion=_VERSION,actualProductionChunkExecuted=true,realCEStarts=0,realMemoryActions=0,realRegistryActions=0,sessionCount=serial})
'@
 $prefix='local OWNED_ROOT='+(Lua-Literal $luaOwned)+"`nlocal CASE_FILTER="+(Lua-Literal $CaseFilter)+"`nlocal PRODUCTION_SOURCE="+(Lua-Literal ([IO.File]::ReadAllText($luaSource)))+"`n"
 $luaRaw=[ValueScanInertLua]::Run($prefix+$luaHarness);$luaPayload=$luaRaw|ConvertFrom-Json
 $drift=@();for($i=0;$i -lt $luaInputPaths.Count;$i++){$now=Lua-Pin $luaInputPaths[$i];if($now.sha256 -cne $luaInputPins[$i].sha256){$drift+=@($now)}}
 $luaResult=@{status=$(if($luaPayload.failed -or $drift.Count){'FAIL'}else{'PASS'});mode='SessionLua';caseFilter=$CaseFilter;actual=$luaPayload;inputs=$luaInputPins;sourceDrift=$drift;loadedRuntime=Lua-Pin $runtimeCopy;loadedAssembly=Lua-Pin $luaAssembly;actualSource=Lua-Pin $luaSource;elapsedMs=$luaStarted.Elapsed.TotalMilliseconds;bitness=[IntPtr]::Size*8;scope='Actual unmodified production Lua chunk in existing isolated Lua 5.1 VM; fake CE API; real file IO limited to exclusive owned root; no CE/Host/game/process/memory/registry/network calls'}
 $luaResult|ConvertTo-Json -Depth 20|Set-Content -LiteralPath (Join-Path $out 'lua-session-config.json') -Encoding UTF8
 $luaResult|ConvertTo-Json -Depth 20
 if($luaResult.status -cne 'PASS'){exit 1};return
}

if($TestMode -eq 'SessionConfig'){
$started=[Diagnostics.Stopwatch]::StartNew()
$source=Join-Path $sourceRoot 'PowerControl/MemoryScan/MemoryScanHost.cs'
$assemblyPath=Join-Path $out 'MemoryScanHost.ConfigOnly.dll'
Add-Type -Path $source -OutputAssembly $assemblyPath -ReferencedAssemblies @('System.dll','System.Core.dll','System.Web.Extensions.dll') -ErrorAction Stop
$assembly=[Reflection.Assembly]::LoadFrom($assemblyPath);$adapterType=$assembly.GetType('MemoryScanHost',$true)
$script:scanAdapter=[Activator]::CreateInstance($adapterType,$true)
$binding=[Reflection.BindingFlags]'Instance,Static,NonPublic,Public'
function Call-SessionMethod([string]$Name,[object[]]$Arguments) {
    if($Name -notin @('Read','Save','Atomic','ValidateSessionConfig','ValidateLease','ValidateRequest','ValidateCeState','Refresh','Publish','ClaimRequest','FinishClaim')){throw 'Refusing non-file/process method'}
    $method=$adapterType.GetMethod($Name,$binding);if(!$method){throw ('Missing actual boundary '+$Name)}
    $parameters=$method.GetParameters();$unwrapped=New-Object object[] $Arguments.Count
    for($i=0;$i -lt $Arguments.Count;$i++){$item=$Arguments[$i];if($item -is [Management.Automation.PSObject]){$item=$item.PSObject.BaseObject};$type=$parameters[$i].ParameterType;if($type -eq [string]){$unwrapped[$i]=[string]$item}elseif($type -eq [int]){$unwrapped[$i]=[int]$item}elseif($type -eq [bool]){$unwrapped[$i]=[bool]$item}else{$unwrapped[$i]=$item}}
    try{return $method.Invoke($script:scanAdapter,$unwrapped)}catch{throw $_.Exception.InnerException}
}
function Field([string]$Name,$Value){$adapterType.GetField($Name,$binding).SetValue($script:scanAdapter,$Value)}
function Dict($Value){$d=New-Object 'Collections.Generic.Dictionary[string,object]';foreach($key in $Value.Keys){$d[$key]=$Value[$key]};return $d}
function Assert-Session([bool]$Value,[string]$Message){if(!$Value){throw $Message}}
function Reject-Session([scriptblock]$Action){$rejected=$false;try{& $Action|Out-Null}catch{$rejected=$true;$script:sessionRejection=$_.Exception.ToString()};if(!$rejected){throw 'Unknown/malformed file was accepted'}}
$rows=New-Object Collections.ArrayList
$script:sessionSkipped=@()
function Session-Case([string]$Name,[scriptblock]$Action){if($CaseFilter -and $Name.IndexOf($CaseFilter,[StringComparison]::OrdinalIgnoreCase) -lt 0){$script:sessionSkipped+=@($Name);return};try{& $Action;[void]$rows.Add(@{name=$Name;passed=$true})}catch{[void]$rows.Add(@{name=$Name;passed=$false;error=$_.ToString()})}}
$owned=Join-Path ([IO.Path]::GetFullPath($out)) 'owned-host-files';[IO.Directory]::CreateDirectory($owned)|Out-Null
$path=Join-Path $owned 'config.json';$utf8=New-Object Text.UTF8Encoding($false,$true);$token='a'*64;$id='b'*32
$valid=Dict @{schemaVersion=1;token=$token;pid=1234;processCreated='134365380584475513';path='R:/fake/never-opened.exe';ownerPid=55;ownerDirectory='R:/fake-owner';unknown='keep'}
Session-Case 'file-strict-root-duplicate-future-and-UTF-refuse-without-changing-bytes' {
    foreach($text in @('','null','[]','{','{"schemaVersion":2}','{"schemaVersion":2,"schemaVersion":1}','{"n":NaN}','{"n":01}','{"n":1e999}','{"u":"\ud800"}')){
        [IO.File]::WriteAllText($path,$text,$utf8);$before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));Reject-Session {Call-SessionMethod Read @($path,16384)};Assert-Session ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $before) 'Rejected bytes overwritten'
    }
    [IO.File]::WriteAllBytes($path,[byte[]]@(0xc3,0x28));Reject-Session {Call-SessionMethod Read @($path,16384)}
}
Session-Case 'file-config-permission-shapes-have-no-PID-or-path-coercion' {
    [void](Call-SessionMethod ValidateSessionConfig @($valid))
    foreach($key in @('pid','ownerPid','processCreated','path','token','schemaVersion')){
        $bad=Dict $valid;$bad[$key]=@{pid='1234';ownerPid=$true;processCreated=1234;path=42;token=$false;schemaVersion=2}[$key];Reject-Session {Call-SessionMethod ValidateSessionConfig @($bad)}
    }
}
Session-Case 'file-actual-writer-preserves-unknowns-roundtrips-and-retries-after-lock' {
    $path=Join-Path $owned 'writer.json';Call-SessionMethod Save @($path,$valid);$read=Call-SessionMethod Read @($path,16384);Assert-Session ($read['unknown'] -ceq 'keep' -and $read['pid'] -eq 1234) 'Actual producer readback changed legal fields'
    $before=[Convert]::ToBase64String([IO.File]::ReadAllBytes($path));$hold=[IO.File]::Open($path,'Open','ReadWrite','None')
    try{$bad=Dict $valid;$bad['unknown']='new';Reject-Session {Call-SessionMethod Save @($path,$bad)}}finally{$hold.Dispose()}
    Assert-Session ([Convert]::ToBase64String([IO.File]::ReadAllBytes($path)) -ceq $before) 'Failed replace lost original'
    $next=Dict $valid;$next['unknown']='retry';Call-SessionMethod Save @($path,$next);$read=Call-SessionMethod Read @($path,16384);Assert-Session ($read['unknown'] -ceq 'retry') 'Unlocked explicit save did not recover'
    [IO.File]::WriteAllText($path,'{"schemaVersion":2,"unknown":"future"}');$future=[IO.File]::ReadAllText($path)
    $atomicText='{"schemaVersion":1,"token":"'+$token+'"}'
    Reject-Session {Call-SessionMethod Atomic @($path,$atomicText)}
    Assert-Session ($script:sessionRejection -match '版本未知') 'Atomic negative did not reach the actual schema guard'
    Assert-Session ([IO.File]::ReadAllText($path) -ceq $future) 'Atomic layer overwrote a future preimage after caller validation'

}
Session-Case 'file-request-claim-cannot-delete-a-concurrent-replacement' {
    Field root $owned;Field token $token;Field revision 3
    $dir=Join-Path $owned 'requests';[IO.Directory]::CreateDirectory($dir)|Out-Null;$request=Join-Path $dir ($id+'.json')
    $value=Dict @{schemaVersion=1;id=$id;token=$token;action='first';value='100';revision=3};Call-SessionMethod Save @($request,$value)
    $claim=Call-SessionMethod ClaimRequest @($request);$read=Call-SessionMethod Read @($claim,16384);[void](Call-SessionMethod ValidateRequest @($read,$id))
    $value['value']='101';Call-SessionMethod Save @($request,$value);Call-SessionMethod FinishClaim @($claim,$true)
    $replacement=Call-SessionMethod Read @($request,16384);Assert-Session ($replacement['value'] -ceq '101') 'Old request completion deleted the replacement'
    foreach($key in @('token','revision','action','value')){$bad=Dict $value;$bad[$key]=@{token='wrong';revision=2;action='write';value=42}[$key];Reject-Session {Call-SessionMethod ValidateRequest @($bad,$id)}}
}
Session-Case 'file-CE-state-type-future-and-identity-faults-remain-inactive' {
    Field root $owned;Field token $token;Field processId 1234;Field created '134365380584475513';Field imagePath 'R:/fake/never-opened.exe'
    $ce=Dict @{schemaVersion=1;token=$token;engine='cheat-engine';stage='idle';message='safe';count=0;rows=@();scanRound=0;searchValue='';lastWriteCount=0;lastWriteValue='';scannedBytes=0;totalBytes=0;scanElapsedMs=0;temporarilyPaused=$false;pauseRestored=$true;dataType='int32';commandId=''}
    [void](Call-SessionMethod ValidateCeState @($ce))
    foreach($pair in @(@('count','0'),@('schemaVersion',2),@('token','wrong'),@('stage','future'),@('pid',1235),@('path','R:/other.exe'))){$bad=Dict $ce;$bad[$pair[0]]=$pair[1];Reject-Session {Call-SessionMethod ValidateCeState @($bad)}}
    $file=Join-Path $owned 'ce-state.json';Call-SessionMethod Save @($file,$ce);Assert-Session (Call-SessionMethod Refresh @()) 'Actual CE file consumer did not read valid state'
    Call-SessionMethod Publish @($true);[IO.File]::Copy((Join-Path $owned 'status.json'),(Join-Path $out 'actual-host-status.json'))
    [IO.File]::WriteAllText($file,'{"schemaVersion":2}');Reject-Session {Call-SessionMethod Refresh @()};$inactive=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter);Assert-Session ($inactive['stage'] -ceq 'closed' -and $inactive['count'] -eq 0) 'Unknown CE state retained activity or result authority'
    Assert-Session ([IO.File]::ReadAllText($file) -ceq '{"schemaVersion":2}') 'Future CE original overwritten'
}

Session-Case 'file-size-IO-lease-errors-are-not-missing-or-current' {
    [IO.File]::WriteAllBytes($path,(New-Object byte[] 16385));Reject-Session {Call-SessionMethod Read @($path,16384)}
    [IO.File]::Delete($path);Call-SessionMethod Save @($path,$valid);$hold=[IO.File]::Open($path,'Open','ReadWrite','None');try{Reject-Session {Call-SessionMethod Read @($path,16384)}}finally{$hold.Dispose()}
    Field token $token;$lease=Dict @{token=$token;at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();schemaVersion=1};[void](Call-SessionMethod ValidateLease @($lease))
    foreach($key in @('token','at')){$bad=Dict $lease;$bad[$key]=@{token='wrong';at='0'}[$key];Reject-Session {Call-SessionMethod ValidateLease @($bad)}}
}

Session-Case 'parser-boundary-UTF8-BOM-is-consumed-without-altering-original-or-unknowns' {
    $file=Join-Path $owned 'bom.json';$body='{"schemaVersion":1,"unknown":{"keep":"BOM"}}';$data=[byte[]](@(0xef,0xbb,0xbf)+$utf8.GetBytes($body));[IO.File]::WriteAllBytes($file,$data)
    $original=[Convert]::ToBase64String($data);$read=Call-SessionMethod Read @($file,16384);Assert-Session ($read['unknown']['keep'] -ceq 'BOM') 'BOM document or unknowns changed'
    Assert-Session ([Convert]::ToBase64String([IO.File]::ReadAllBytes($file)) -ceq $original) 'BOM original changed during read'
}
Session-Case 'parser-boundary-escaped-duplicate-keys-reject-at-root-and-nested-object' {
    $file=Join-Path $owned 'escaped-duplicate.json'
    foreach($text in @('{"schemaVersion":1,"schema\u0056ersion":1}','{"schemaVersion":1,"unknown":{"keep":1,"k\u0065ep":2}}')){
        [IO.File]::WriteAllText($file,$text,$utf8);Reject-Session {Call-SessionMethod Read @($file,16384)};Assert-Session ($script:sessionRejection -match 'Duplicate JSON field') 'Duplicate negative did not reach parser'
        Assert-Session ([IO.File]::ReadAllText($file) -ceq $text) 'Escaped duplicate original changed'
    }
    [IO.File]::WriteAllText($file,'{"schemaVersion":1,"unknown":{"k\u0065ep":"retry"}}',$utf8);$read=Call-SessionMethod Read @($file,16384);Assert-Session ($read['unknown']['keep'] -ceq 'retry') 'Valid escaped key explicit retry did not recover'
}
Session-Case 'parser-boundary-container-depth16-accepts-and-depth17-rejects' {
    $file=Join-Path $owned 'depth.json'
    foreach($shape in @('array','object')){foreach($levels in @(16,17)){
        $nested='"keep"';for($i=0;$i -lt $levels;$i++){if($shape -ceq 'array'){$nested='['+$nested+']'}else{$nested='{"next":'+$nested+'}'}}
        $text='{"schemaVersion":1,"unknown":'+$nested+'}';[IO.File]::WriteAllText($file,$text,$utf8)
        if($levels -eq 16){$read=Call-SessionMethod Read @($file,16384);$value=$read['unknown'];for($i=0;$i -lt $levels;$i++){if($shape -ceq 'array'){$value=$value[0]}else{$value=$value['next']}};Assert-Session ($value -ceq 'keep') 'Boundary depth/unknown field lost'}
        else{Reject-Session {Call-SessionMethod Read @($file,16384)};Assert-Session ($script:sessionRejection -match 'depth exceeded') 'Depth negative did not reach parser';Assert-Session ([IO.File]::ReadAllText($file) -ceq $text) 'Depth original changed'}
    }}
    [IO.File]::WriteAllText($file,'{"schemaVersion":1,"unknown":"retry"}',$utf8);$read=Call-SessionMethod Read @($file,16384);Assert-Session ($read['unknown'] -ceq 'retry') 'Depth explicit retry failed'
}

$result=@{caseFilter=$CaseFilter;skipped=$script:sessionSkipped;status=$(if(@($rows|Where-Object{!$_.passed}).Count){'FAIL'}else{'PASS'});checks=$rows.Count;passed=@($rows|Where-Object{$_.passed}).Count;failed=@($rows|Where-Object{!$_.passed}).Count;cases=@($rows.ToArray());elapsedMs=$started.Elapsed.TotalMilliseconds;mode='SessionConfig';actualSource=@{path=$source;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()};actualConsumer=@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()};loadedAssembly=@{path=$assemblyPath;sha256=(Get-FileHash -LiteralPath $assemblyPath -Algorithm SHA256).Hash.ToLowerInvariant()};powershell=@{path=(Get-Process -Id $PID).Path;version=$PSVersionTable.PSVersion.ToString()};scope='Actual production file/config functions loaded as library; no Main/Open/Run/Guard/PInvoke/CE/process/registry/memory invocation';processMemoryActions=0;ceLaunches=0;registryActions=0}
$result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $out 'host-session-config.json') -Encoding UTF8
$result|ConvertTo-Json -Depth 5
if($result.failed){exit 1};return

}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){$p=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Elevated','-ValidationRoot',('"'+$out+'"'),'-TestMode',$TestMode,'-FixtureExeName',('"'+$FixtureExeName+'"'),'-GameTitle',('"'+$GameTitle+'"'),'-SteamAppId',$(if($SteamAppId){$SteamAppId}else{'0'}));@{testPid=$p.Id;dispatched=$true}|ConvertTo-Json;return}
function Json([string]$p,$v){$temp=$p+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::WriteAllText($temp,($v|ConvertTo-Json -Depth 16 -Compress),[Text.UTF8Encoding]::new($false));if([IO.File]::Exists($p)){[IO.File]::Replace($temp,$p,[NullString]::Value)}else{[IO.File]::Move($temp,$p)}}
function Read([string]$p){$s=[IO.FileStream]::new($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));try{$r=[IO.StreamReader]::new($s,[Text.Encoding]::UTF8);try{return $r.ReadToEnd()|ConvertFrom-Json}finally{$r.Dispose()}}finally{$s.Dispose()}}
function SettingsSnapshot {
 $rows=[Collections.Generic.List[object]]::new();$queue=[Collections.Queue]::new();$queue.Enqueue('Software\Cheat Engine')
 while($queue.Count){$name=[string]$queue.Dequeue();$key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($name,$false);if(-not $key){continue};try{foreach($value in ($key.GetValueNames()|Sort-Object)){$data=$key.GetValue($value,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);$rows.Add(@{path=$name;name=$value;kind=[string]$key.GetValueKind($value);data=$data})};foreach($sub in ($key.GetSubKeyNames()|Sort-Object)){$queue.Enqueue($name+'\'+$sub)}}finally{$key.Dispose()}}
 return ,$rows.ToArray()
}
$run=Join-Path $out ('fixture-'+[guid]::NewGuid().ToString('N').Substring(0,8));$pc=Join-Path $run 'PowerControl';$session=Join-Path $run ([guid]::NewGuid().ToString('N'));foreach($dir in @((Join-Path $pc 'MemoryScan'),(Join-Path $session 'requests'),(Join-Path $session 'responses'))){[IO.Directory]::CreateDirectory($dir)|Out-Null}
$ceId=0;$report=@{ok=$false;mode=$TestMode;gameTitle=$GameTitle;steamAppId=$SteamAppId;realGameMemoryWrites=0;scope='Production CE plus adapter against owned 1MiB mapped fixture only';checks=@()};$fixture=$null;$helper=$null;$map=$null;$view=$null
try{
 $fixtureFile=Join-Path $run ('steamapps/common/'+$(if($SteamAppId){$SteamAppId}else{'fixture'})+'/bin/'+$FixtureExeName);[IO.Directory]::CreateDirectory((Split-Path $fixtureFile))|Out-Null;& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /optimize+ /target:winexe /platform:x64 /r:System.Web.Extensions.dll ('/out:'+$fixtureFile) (Join-Path $PSScriptRoot 'game_value_scan_fixture.cs');if($LASTEXITCODE){throw 'Fixture compile failed'}
 $dataFile=Join-Path $run 'fixture-memory.bin';$stream=[IO.File]::Create($dataFile);$stream.SetLength(1048576);$stream.Dispose();$sharedStream=[IO.FileStream]::new($dataFile,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite);$map=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateFromFile($sharedStream,[NullString]::Value,1048576,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite,$null,[IO.HandleInheritability]::None,$true);$view=$map.CreateViewAccessor();$view.Write(0,[int]731001);$view.Write(4,[int]731001)
 $ready=Join-Path $run 'fixture-ready.txt';$fixtureStop=Join-Path $run 'fixture-stop';$fixture=Start-Process -FilePath $fixtureFile -ArgumentList @(('"'+$dataFile+'"'),('"'+$ready+'"'),('"'+$fixtureStop+'"')) -WindowStyle Hidden -PassThru
 $until=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($ready)-and[DateTime]::UtcNow-lt$until){Start-Sleep -Milliseconds 50};if(-not[IO.File]::Exists($ready)){throw 'Fixture not ready'}
 $token=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N');$owner=Get-Process -Id $PID
 Json (Join-Path $session 'config.json') @{token=$token;pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;ownerPid=$PID;ownerPath=$owner.Path;ownerCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc()}
 function Pulse {Json (Join-Path $session 'lease.json') @{token=$token;at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()};Json (Join-Path $pc 'game-target.json') @{valid=$true;generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;title=$GameTitle;name=$GameTitle;steamAppId=$SteamAppId}}
 function WaitFile([string]$path,[int]$seconds=25){$until=[DateTime]::UtcNow.AddSeconds($seconds);while(-not[IO.File]::Exists($path)-and[DateTime]::UtcNow-lt$until){Pulse;if($helper-and$helper.HasExited){throw ('Adapter exited: '+(Read (Join-Path $session 'status.json')).message)};Start-Sleep -Milliseconds 100};if(-not[IO.File]::Exists($path)){throw ('Missing '+$path)}}
 & (Join-Path $PSScriptRoot 'build-game-value-scan-host.ps1') -OutputDirectory (Join-Path $out 'helper') | Out-Null
 foreach($name in @('MemoryScanHost.exe','CeScanEngine.lua')){Copy-Item -LiteralPath (Join-Path $out ('helper/'+$name)) -Destination (Join-Path $pc ('MemoryScan/'+$name))}
 $before=SettingsSnapshot;Json (Join-Path $run 'ce-settings-before.json') $before
 Pulse;$helper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru;WaitFile (Join-Path $session 'status.json');$status=Read (Join-Path $session 'status.json');if($status.stage-ne'idle'){throw $status.message};$report.initial=$status;$ceId=[int](Read (Join-Path $session 'ce-process.json')).pid;$report.checks+=@('CE starts hidden with current settings and no setup dialogs');$script:revision=0
 function Request([string]$action,[string]$value,[string]$expectError=''){Pulse;$id=[guid]::NewGuid().ToString('N');Json (Join-Path $session ('requests/'+$id+'.json')) @{token=$token;id=$id;action=$action;value=$value;revision=$script:revision};$response=Join-Path $session ('responses/'+$id+'.json');WaitFile $response;$result=Read $response;$script:revision=$result.state.revision;if($expectError){if($result.ok -or $result.error -notlike ('*'+$expectError+'*')){throw ('Expected '+$expectError+', got '+$result.error)}}elseif(-not$result.ok){throw ('CE '+$action+': '+$result.error)};return $result}
 if($TestMode -eq 'Cancel'){
  [IO.File]::WriteAllText((Join-Path $session 'cancel'),'cancel');$cancelled=Request first '731001' '取消';if($cancelled.state.count -ne 0){throw 'Cancelled scan published results'}
  if([IO.File]::Exists((Join-Path $session 'cancel'))){throw 'Cancellation marker leaked to later scans'};$report.checks+=@('Pending cancellation is consumed exactly once without dispatching a scan')
 }
 if($TestMode -eq 'Pipeline' -or $TestMode -eq 'Cancel'){
 $a=Request first '731001';if($a.state.count-lt2){throw 'Mapped values absent from CE first scan'};$report.first=$a.state;$report.checks+=@('CE first scan finds both fixture fields using saved scan defaults')
 $view.Write(0,[int]731002);$view.Write(4,[int]731002);$b=Request next '731002';if($b.state.count-ne2){throw ('Expected only 2 mapped values, got '+$b.state.count)};$report.next=$b.state;$report.checks+=@('CE next scan narrows to exactly two changed mapped fields')
 $c=Request writeAll '99999';if($view.ReadInt32(0)-ne99999-or$view.ReadInt32(4)-ne99999){throw 'CE write not observed in mapped fields'};$report.write=$c.state;$report.checks+=@('CE writeInteger changes both fixture values to 99999 and reads them back')

 }
 if($TestMode -eq 'LuaFailure'){
  [IO.File]::WriteAllText((Join-Path $session 'ce-command.txt'),([guid]::NewGuid().ToString('N')+"`n"+$token+"`nunsupported`n0`n"),[Text.UTF8Encoding]::new($false))
  $deadline=[DateTime]::UtcNow.AddSeconds(8);while(-not $helper.HasExited -and [DateTime]::UtcNow -lt $deadline){Pulse;Start-Sleep -Milliseconds 100}
  if(-not $helper.HasExited){throw 'Lua failure left adapter alive'};$report.checks+=@('Lua error closes owned CE and adapter without an active-loop leak')
 }elseif($TestMode -eq 'Lease'){
  if(-not $helper.WaitForExit(22000)){throw 'Expired frontend lease left adapter alive'};$report.checks+=@('Loss of YMCC heartbeat closes owned CE without touching other processes')
 }else{
  [IO.File]::WriteAllText((Join-Path $session 'stop'),'stop');if(-not $helper.WaitForExit(12000)){throw 'Adapter did not stop gracefully'}
 }
 if(Get-Process -Id $ceId -ErrorAction SilentlyContinue){throw 'Owned CE process leaked after adapter exit'};$report.ceExited=$true
 $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not$report.settingsPreserved){throw 'CE registry changed; inspect exact before/after values'};$report.checks+=@('CE registry settings remain byte-for-byte equivalent after scanning and exit');$report.ok=$true
}catch{$report.error=$_.Exception.Message;$report.stack=$_.ScriptStackTrace}
finally{if([IO.Directory]::Exists($session)){[IO.File]::WriteAllText((Join-Path $session 'stop'),'stop')};if($helper){$report.adapterExited=$helper.WaitForExit(12000)};if($fixture){[IO.File]::WriteAllText((Join-Path $run 'fixture-stop'),'stop');$report.fixtureExited=$fixture.WaitForExit(5000)};if($view){$view.Dispose()};if($map){$map.Dispose()};$report.run=$run;$report.at=[DateTimeOffset]::Now.ToString('o');Json (Join-Path $out ('ce-fixture-'+$TestMode.ToLowerInvariant()+'-result.json')) $report
 if(-not $report.ok){Write-Error $report.error -ErrorAction Continue;exit 1}
 $report|ConvertTo-Json -Depth 3}