[CmdletBinding()]
param([switch]$Elevated,[switch]$CurrentUserFixture,[string]$ValidationRoot='', [ValidateSet('Pipeline','Cancel','Lease','LuaFailure','SessionConfig','SessionLua','Startup','TargetChange','RendererRestart')][string]$TestMode='Pipeline', [ValidatePattern('^[a-zA-Z0-9 ._-]+\.exe$')][string]$FixtureExeName='MappedFixture.exe', [string]$GameTitle='CE test fixture', [ValidatePattern('^[0-9]*$')][string]$SteamAppId='', [string]$CaseFilter='', [string]$LuaRuntimePath='', [string]$SessionSourcePath='', [ValidateSet('Serial','DuplicateSession','HelperCrash','LegacyAdoption','UnconfirmedLaunch','UnconfirmedLive','UnconfirmedWithoutGate','LegacyUndispatched','PreCreateCrash','CancelWhileWaiting','TargetChangeWhileWaiting')][string]$OwnershipCase='Serial', [ValidateSet('Healthy','OwnerDirectory','OwnerMissing','MalformedCapability','FutureConfig')][string]$StartupCase='Healthy', [switch]$TargetChangeAfterResults,[switch]$SameExeTargetChange,[switch]$HeartbeatContention,[switch]$ExternalCeDuringRecovery,[switch]$LegacyOwnerGone,[switch]$ReconcileRelease,[switch]$ReleaseAfterGameExit,[ValidateSet('None','FutureClosed')][string]$ReleaseReceiptFault='None',[switch]$ConcurrentRelease,[switch]$ForeignReleaseImage)
if($ReconcileRelease -and ($TestMode -ne 'RendererRestart' -or $OwnershipCase -ne 'UnconfirmedLive')){throw 'ReconcileRelease extends only the exact live unknown CE boundary'}
if($ReleaseAfterGameExit -and -not $ReconcileRelease){throw 'ReleaseAfterGameExit requires explicit release reconciliation'}
if($ReleaseReceiptFault -ne 'None' -and -not $ReconcileRelease){throw 'ReleaseReceiptFault requires the explicit release boundary'}
if($ConcurrentRelease -and (-not $ReconcileRelease -or $ReleaseReceiptFault -ne 'None')){throw 'ConcurrentRelease extends only the valid explicit release boundary'}
if($ForeignReleaseImage -and (-not $ReconcileRelease -or $ConcurrentRelease -or $ReleaseReceiptFault -ne 'None')){throw 'ForeignReleaseImage is the exact release-image refusal boundary'}
if($LegacyOwnerGone -and ($TestMode -ne 'RendererRestart' -or $OwnershipCase -ne 'UnconfirmedWithoutGate')){throw 'LegacyOwnerGone extends only the no-gate migration boundary'}
if($ExternalCeDuringRecovery -and ($TestMode -ne 'RendererRestart' -or $OwnershipCase -notin @('LegacyUndispatched','UnconfirmedLaunch','UnconfirmedLive'))){throw 'ExternalCeDuringRecovery extends only the selected old-session recovery cases'}
if($TargetChangeAfterResults -and $TestMode -ne 'TargetChange'){throw 'TargetChangeAfterResults applies only to the original TargetChange fixture'}
if($SameExeTargetChange -and $TestMode -ne 'TargetChange'){throw 'SameExeTargetChange applies only to the original TargetChange fixture'}
if($HeartbeatContention -and $TestMode -ne 'Pipeline'){throw 'HeartbeatContention applies only to the original Pipeline fixture'}
if($StartupCase -ne 'Healthy' -and $TestMode -ne 'Startup'){throw 'StartupCase applies only to the existing Startup fixture'}
if($OwnershipCase -ne 'Serial' -and $TestMode -ne 'RendererRestart'){throw 'OwnershipCase applies only to the existing RendererRestart fixture'}
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
 $luaLongRoot=Join-Path $luaOwned ('long-'+('p'*[Math]::Max(1,175-$luaOwned.Length-6)))
 if($luaLongRoot.Length -gt 190){throw 'Selected Lua fixture output leaves no bounded legacy-path test space'}
 [IO.Directory]::CreateDirectory($luaLongRoot)|Out-Null
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
local function newSession(longRoot)
 serial=serial+1;assert(serial<=64,'owned directory budget');local root=longRoot and LONG_ROOT or OWNED_ROOT..'\\session-'..string.format('%03d',serial)
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
 -- CE file ports use real files inside the owned ASCII root. The separate
 -- actual-CE fixture proves Windows UTF-8 paths; CRT transport is forbidden.
 env.io={open=denied};env.os={remove=denied,rename=denied}
 env.fileExists=function(p)return get(safe(p))~=nil end
 env.createFileStream=function(p,mode)
  p=safe(p);assert(mode==0x40 or mode==0xff00,'unsupported CE stream mode')
  if faults.readRefuse and mode==0x40 and p==statePath then return nil,'owned IO refusal' end
  if faults.leaseReadRefuse and mode==0x40 and p==root..'\\ce-lease.txt' then error('owned lease sharing violation') end
  local h,err=nativeIO.open(p,mode==0x40 and 'rb'or 'wb');if not h then return nil,err end
  local stream={
   readString=function(n)totals.reads=totals.reads+1;return h:read(n)or ''end,
   writeString=function(v)totals.writes=totals.writes+1;assert(h:write(v))end,
   destroy=function()
    if mode==0xff00 then totals.flushes=totals.flushes+1;local ok,e=h:flush();local closed=h:close();if faults.flushRefuse then error('owned stream close refusal')end;assert(ok,e);assert(closed)
    else assert(h:close())end
   end}
  return setmetatable(stream,{__index=function(_,k)if k=='Position'then return h:seek()elseif k=='Size'then local at=h:seek();local size=h:seek('end');h:seek('set',at);return size end;error('unknown CE stream field '..k)end})
 end
 env.deleteFile=function(p)p=safe(p);assert(p~=statePath,'writer attempted primary deletion');totals.removes=totals.removes+1;return nativeOS.remove(p)end
 env.executeCodeLocalEx=function(api,source,destination,...)
  if api~='kernel32.MoveFileW'or select('#',...)~=0 or type(source)~='table'or source.type~=4 or type(destination)~='table'or destination.type~=4 then return denied()end
  local a,b=safe(source.value),safe(destination.value);totals.renames=totals.renames+1
  if faults.promoteRefuse and a:sub(-4)=='.tmp'and b==statePath then return 0,'owned promotion refusal' end
  if faults.archiveRefuse and a==statePath then return 0,'owned archive refusal' end
  if get(b)~=nil then return 0,'destination exists' end
  local ok,err=nativeOS.rename(a,b)
  if ok and faults.commandReplacement and a==root..'\\ce-command.txt'then put(a,faults.commandReplacement)end
  return ok and 1 or 0,err
 end
 local x={root=root,token=token,statePath=statePath,faults=faults,env=env,put=put,get=get}
 x.start=function(config)if config~=nil then env.YMCC_CE_SESSION=config end;local chunk=assert(loadstring(PRODUCTION_SOURCE,'@actual-CeScanEngine.lua'));setfenv(chunk,env);chunk()end
 x.tick=function(at)clock=at;assert(timer and timer.Enabled and not timer.destroyed,'timer not active');timer.OnTimer()end
 x.closed=function()return closed end;x.timer=function()return timer end;x.forms=function()return forms end
 return x
end
check('lua-file-long-root-keeps-state-and-command-claims-usable',function()
 local x=newSession(true);assert(#x.root>=170);x.start();assert(x.timer()and x.closed()==0,'long-root startup publication failed')
 x.put(x.root..'\\ce-lease.txt',x.token..'\n1');x.tick(100)
 local request=string.rep('c',32)..'\nwrong-token\nfirst\n100\n';x.put(x.root..'\\ce-command.txt',request);x.tick(200)
 assert(x.get(x.root..'\\ce-command.txt.'..x.token:sub(1,16)..'.claim')==request,'long-root claim was lost')
 x.tick(300);assert(x.closed()==1 and x.get(x.statePath):find('"stage":"closed"',1,true),'long-root shutdown publication failed')
end)
check('lua-file-actual-publish-and-write-readback-preserves-a-valid-idle-session',function()
 local x=newSession();x.start();assert(x.timer()and x.closed()==0);local first=assert(x.get(x.statePath));assert(first:find('"schemaVersion":1',1,true)and first:find('"stage":"idle"',1,true))
 x.put(x.root..'\\ce-lease.txt',x.token..'\n1');x.tick(1000);assert(x.closed()==0)
 x.put(x.root..'\\stop','stop');x.tick(1100);local last=assert(x.get(x.statePath));assert(last~=first and last:find('"stage":"closed"',1,true));assert(x.closed()==1 and x.timer().destroyed)
end)
check('lua-file-lease-transient-sharing-recovers-without-renewing-on-errors',function()
 local x=newSession();x.start();local p=x.root..'\\ce-lease.txt';x.put(p,x.token..'\n1');x.tick(1000)
 x.faults.leaseReadRefuse=true;x.tick(1100);x.tick(7000);assert(x.closed()==0,'brief lease IO fault ended CE')
 x.faults.leaseReadRefuse=false;x.put(p,x.token..'\n2');x.tick(7100);x.tick(9001);assert(x.closed()==0,'valid increasing heartbeat did not recover')
 x.faults.leaseReadRefuse=true;x.tick(15100);assert(x.closed()==0);x.tick(15101);assert(x.closed()==1 and x.timer().destroyed,'lease IO errors renewed or disabled finite expiry')
end)
check('lua-file-lease-sharing-never-hides-stop-or-command-errors',function()
 local x=newSession();x.start();x.put(x.root..'\\ce-lease.txt',x.token..'\n1');x.tick(1000);x.faults.leaseReadRefuse=true
 x.put(x.root..'\\stop','stop');x.tick(1100);assert(x.closed()==1 and x.timer().destroyed,'lease sharing blocked explicit stop')
 local y=newSession();y.start();y.put(y.root..'\\ce-lease.txt',y.token..'\n1');y.tick(1000);y.faults.leaseReadRefuse=true
 y.put(y.root..'\\ce-command.txt','invalid-frame');y.tick(1100);assert(y.closed()==0);y.tick(1200);assert(y.closed()==1 and y.timer().destroyed,'command error was swallowed as lease IO')
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
 assert(x.get(x.root..'\\ce-command.txt')==replacement,'old command deleted newer file');assert(x.get(x.root..'\\ce-command.txt.'..x.token:sub(1,16)..'.claim')==request,'rejected raw claim lost');x.tick(200);assert(x.closed()==1)
end)
check('lua-file-unknown-bootstrap-never-calls-CE-or-creates-session-state',function()
 for _,config in ipairs({false,'wrong',{root='relative',token=string.rep('a',64),pid=1234},{root=OWNED_ROOT,token='bad',pid=1234},{root=OWNED_ROOT,token=string.rep('a',64),pid='1234'}})do local x=newSession();x.start(config);assert(x.forms()==0 and x.timer()==nil and x.closed()==0 and x.get(x.statePath)==nil)end
end)
local passed,failed=0,0;for _,item in ipairs(cases)do if item.passed then passed=passed+1 else failed=failed+1 end end
assert(totals.forbidden==0,'forbidden scan/memory fake port was attempted')
cases.__array=true;skipped.__array=true
YMCC_TEST_RESULT=encode({passed=passed,failed=failed,cases=cases,skipped=skipped,operations=totals,luaVersion=_VERSION,actualProductionChunkExecuted=true,realCEStarts=0,realMemoryActions=0,realRegistryActions=0,sessionCount=serial})
'@
 $prefix='local OWNED_ROOT='+(Lua-Literal $luaOwned)+"`nlocal LONG_ROOT="+(Lua-Literal $luaLongRoot)+"`nlocal CASE_FILTER="+(Lua-Literal $CaseFilter)+"`nlocal PRODUCTION_SOURCE="+(Lua-Literal ([IO.File]::ReadAllText($luaSource)))+"`n"
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
if($SessionSourcePath){
    if(![IO.Path]::IsPathRooted($SessionSourcePath)){throw 'Host override requires an absolute owned source file'}
    $private=[IO.Path]::GetFullPath($SessionSourcePath);$privateRoot=[IO.Path]::GetDirectoryName([IO.Path]::GetFullPath($out))+[IO.Path]::DirectorySeparatorChar
    if(!$private.StartsWith($privateRoot,[StringComparison]::OrdinalIgnoreCase) -or [IO.Path]::GetExtension($private) -ine '.cs' -or ![IO.File]::Exists($private)){throw 'Host override must be a private C# file beside the owned output root'}
    for($parent=$private;$parent;$parent=[IO.Path]::GetDirectoryName($parent)){if(([IO.File]::Exists($parent) -or [IO.Directory]::Exists($parent)) -and ([IO.File]::GetAttributes($parent) -band [IO.FileAttributes]::ReparsePoint)){throw 'Host override source is redirected'}}
    $source=$private
}
$assemblyPath=Join-Path $out 'MemoryScanHost.ConfigOnly.dll'
Add-Type -Path $source -OutputAssembly $assemblyPath -ReferencedAssemblies @('System.dll','System.Core.dll','System.Web.Extensions.dll') -ErrorAction Stop
$assembly=[Reflection.Assembly]::LoadFrom($assemblyPath);$adapterType=$assembly.GetType('MemoryScanHost',$true)
$script:scanAdapter=[Activator]::CreateInstance($adapterType,$true)
$binding=[Reflection.BindingFlags]'Instance,Static,NonPublic,Public'
function Call-SessionMethod([string]$Name,[object[]]$Arguments) {
    if($Name -notin @('Read','Save','Atomic','Handoff','SaveHandoff','ValidateSessionConfig','ValidateLease','ValidateRequest','ValidateCeState','Refresh','Publish','ClaimRequest','FinishClaim','ResolveCE','ValidateCeRecord','Exclusive','SaveCeRecord','CloseState','HasSessionArgument','IsReleaseOnlyCommand','ExactReleasedReceipt','Heartbeat')){throw 'Refusing non-file/process method'}
    $method=if($Name -eq 'ResolveCE'){$adapterType.GetMethod($Name,$binding,$null,[type[]]@([string],[string]),$null)}else{$adapterType.GetMethod($Name,$binding)};if(!$method){throw ('Missing actual boundary '+$Name)}
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
Session-Case 'file-ce-discovery-falls-back-without-mutating-config-or-executing-files' {
    $ceDefault=Join-Path $owned 'ce-default';$ceSelected=Join-Path $owned 'ce-selected'
    [IO.Directory]::CreateDirectory($ceDefault)|Out-Null;[IO.Directory]::CreateDirectory($ceSelected)|Out-Null
    $defaultExe=Join-Path $ceDefault 'cheatengine-x86_64.exe';$selectedExe=Join-Path $ceSelected 'cheatengine-x86_64.exe';$gcm=Join-Path $owned 'gcm-settings.json'
    [IO.File]::WriteAllText($defaultExe,'inert path marker; never execute');[IO.File]::WriteAllText($selectedExe,'inert path marker; never execute')
    foreach($candidate in @('', 'Cheat Engine', 'old/cheatengine.exe', (Join-Path $owned 'removed-ce.exe'), $ceDefault, ('"'+$ceDefault+'"'), (Join-Path $ceDefault 'Cheat Engine.exe'))){
        $cfg=@{cePath=$candidate;unknown='preserve'}|ConvertTo-Json -Compress;[IO.File]::WriteAllText($gcm,$cfg,$utf8)
        if($candidate -like '*Cheat Engine.exe'){[IO.File]::WriteAllText($candidate,'inert architecture launcher; never execute')}
        $result=Call-SessionMethod ResolveCE @($gcm,$ceDefault)
        Assert-Session ($result -ceq $defaultExe) ('Default CE rejected for configured value: '+$candidate)
        Assert-Session ([IO.File]::ReadAllText($gcm) -ceq $cfg) 'CE discovery changed GCM settings'
    }
    [IO.File]::WriteAllText($gcm,(@{cePath=$ceSelected}|ConvertTo-Json -Compress),$utf8)
    Assert-Session ((Call-SessionMethod ResolveCE @($gcm,$ceDefault)) -ceq $selectedExe) 'Valid configured CE lost priority'
    [IO.File]::WriteAllText($gcm,'{"cePath":""}',$utf8)
    Assert-Session ((Call-SessionMethod ResolveCE @($gcm,(Join-Path $owned 'missing-ce'))) -ceq '') 'Missing CE was reported available'
    [IO.File]::WriteAllText($gcm,'{"schemaVersion":2,"cePath":"future"}',$utf8)
    Reject-Session {Call-SessionMethod ResolveCE @($gcm,$ceDefault)}
    Assert-Session ([IO.File]::ReadAllText($gcm) -ceq '{"schemaVersion":2,"cePath":"future"}') 'Future GCM settings changed'
}
Session-Case 'file-ce-ownership-lock-and-record-reject-unknown-and-concurrent-preimages' {
    $gatePath=Join-Path $owned '.ce-owner.lock';$recordPath=Join-Path $owned 'ce-owner.json'
    $gate=Call-SessionMethod Exclusive @($gatePath)
    try{
        Reject-Session {Call-SessionMethod Exclusive @($gatePath)}
        Field ceGate $gate;Field ceRecordPath $recordPath;Field ceRecordImage $null;Field root (Join-Path $owned ('b'*32));Field token $token;Field processId 1234;Field created '134365380584475513';Field imagePath 'R:/fake/never-opened.exe';Field ceIdentity $null
        Call-SessionMethod SaveCeRecord @('pending');$pending=Call-SessionMethod Read @($recordPath,16384);[void](Call-SessionMethod ValidateCeRecord @($pending))
        Assert-Session ($pending['phase'] -ceq 'pending' -and $pending['owner']['token'] -ceq $token) 'Pending launch lost its exact capability'
        $original=[IO.File]::ReadAllText($recordPath)
        foreach($field in @('phase','session','owner','ce','schemaVersion')){
            $bad=Dict $pending;$bad[$field]=@{phase='future';session='../other';owner=(Dict @{token='wrong'});ce='unknown';schemaVersion=2}[$field]
            Reject-Session {Call-SessionMethod ValidateCeRecord @($bad)}
        }
        $active=Dict $pending;$active['phase']='active';Reject-Session {Call-SessionMethod ValidateCeRecord @($active)}
        [IO.File]::WriteAllText($recordPath,'{"schemaVersion":2,"unknown":"preserve"}',$utf8)
        Reject-Session {Call-SessionMethod SaveCeRecord @('released')}
        Assert-Session ([IO.File]::ReadAllText($recordPath) -ceq '{"schemaVersion":2,"unknown":"preserve"}') 'Ownership writer replaced an unknown concurrent record'
        [IO.File]::WriteAllText($recordPath,$original,$utf8);Call-SessionMethod SaveCeRecord @('released')
        Assert-Session ((Call-SessionMethod Read @($recordPath,16384))['phase'] -ceq 'released') 'Exact preimage could not publish release'
    }finally{Field ceGate $null;$gate.Dispose()}
    $next=Call-SessionMethod Exclusive @($gatePath);$next.Dispose()
}
Session-Case 'file-cooperative-reader-allows-atomic-writer-sharing' {
    [IO.File]::WriteAllText($path,'{"schemaVersion":1,"unknown":"keep"}',$utf8)
    $writer=[IO.FileStream]::new($path,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete))
    try{$read=Call-SessionMethod Read @($path,16384);Assert-Session ($read['unknown'] -ceq 'keep') 'Concurrent reader changed valid bytes'}finally{$writer.Dispose()}
    $read=Call-SessionMethod Read @($path,16384);Assert-Session ($read['unknown'] -ceq 'keep') 'Closed writer changed bytes'
}
Session-Case 'file-long-primary-path-roundtrips-on-this-runtime' {
    $prefix=([IO.Path]::GetFullPath($owned)).Length+1
    $length=235-$prefix-10
    if($length -lt 1 -or $length -gt 180){throw 'Selected owned output cannot create the bounded long-path fixture'}
    $file=Join-Path $owned ('long-'+('p'*$length)+'.json')
    [IO.File]::WriteAllText($file,'{"schemaVersion":1,"unknown":"original"}',$utf8)
    [void](Call-SessionMethod Save @($file,(Dict @{schemaVersion=1;unknown='updated'})))
    $read=Call-SessionMethod Read @($file,16384)
    Assert-Session ($read['unknown'] -ceq 'updated') 'Long primary path did not retain a confirmed atomic replacement'
}
Session-Case 'file-handoff-is-one-publication-and-never-overwrites-an-existing-command' {
    $file=Join-Path $owned 'ce-command.txt';$claim=$file+'.claim';$frame=$id+"`n"+$token+"`nfirst`n731001`n"
    [void](Call-SessionMethod Handoff @($file,$frame));[IO.File]::Move($file,$claim)
    Assert-Session ([IO.File]::ReadAllText($claim,$utf8) -ceq $frame) 'Consumer did not receive the exact committed frame'
    [IO.File]::WriteAllText($file,'foreign-pending-frame',$utf8)
    Reject-Session {Call-SessionMethod Handoff @($file,$frame)}
    Assert-Session ([IO.File]::ReadAllText($file,$utf8) -ceq 'foreign-pending-frame') 'Handoff overwrote an unconfirmed publication'
}
Session-Case 'file-response-handoff-preserves-token-unknowns-and-refuses-republication' {
    $file=Join-Path $owned 'response.json';$response=Dict @{schemaVersion=1;token=$token;id=$id;ok=$true;unknown='keep'}
    [void](Call-SessionMethod SaveHandoff @($file,$response));$read=Call-SessionMethod Read @($file,16384)
    Assert-Session ($read['unknown'] -ceq 'keep' -and $read['token'] -ceq $token) 'Response publication lost its binding'
    Reject-Session {Call-SessionMethod SaveHandoff @($file,$response)}
    [IO.File]::Delete($file);[IO.File]::WriteAllText($file,'{"schemaVersion":99,"foreign":"keep"}',$utf8)
    Reject-Session {Call-SessionMethod SaveHandoff @($file,$response)}
    Assert-Session ([IO.File]::ReadAllText($file,$utf8) -ceq '{"schemaVersion":99,"foreign":"keep"}') 'Future response preimage was changed'
}
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
    $trainer='G:\任意游戏数据\old session\YMCC-Session.cetrainer'
    foreach($command in @(('"R:\different CE\renamed.exe" NOAUTORUN "'+$trainer+'"'),('"R:\different CE\renamed.exe" "'+$trainer.ToUpperInvariant().Replace('\','/')+'"'))){Assert-Session (Call-SessionMethod HasSessionArgument @($command,$trainer)) 'Exact renamed/moved CE session argument was not recognized'}
    foreach($command in @(('"'+$trainer+'"'),('ce.exe "'+$trainer+'.backup"'),('ce.exe "--file='+$trainer+'"'),('ce.exe "G:\other session\YMCC-Session.cetrainer"'))){Assert-Session (-not (Call-SessionMethod HasSessionArgument @($command,$trainer))) 'Filename/prefix/substrings were incorrectly treated as an exact session argument'}
    $session=Split-Path $trainer;$output=Join-Path $session ('release-'+('a'*32)+'.json');$release='"R:\different adapter\renamed.exe" --release "'+$session+'" "'+$output+'"'
    Assert-Session (Call-SessionMethod IsReleaseOnlyCommand @($release,$session)) 'Exact release-only command shape was not recognized'
    foreach($command in @(('helper.exe "'+$session+'"'),($release+' extra'),($release.Replace('--release','--release-other')),($release.Replace($output,('G:\elsewhere\release-'+('a'*32)+'.json'))),($release.Replace($output,($output+'.old'))),($release.Replace($output,(Join-Path $session ('release-'+('g'*32)+'.json')))))){Assert-Session (-not (Call-SessionMethod IsReleaseOnlyCommand @($command,$session))) 'Unknown command shape could bypass the live dispatcher fence'}
    $closedPath=Join-Path $owned 'peer-closed.json';$proof=Dict @{schemaVersion=1;engine='cheat-engine';stage='closed';token=$token;pid=$valid['pid'];processCreated=$valid['processCreated'];path=$valid['path'];ceExited=$true}
    Assert-Session (-not (Call-SessionMethod ExactReleasedReceipt @($closedPath,$valid))) 'Missing peer close proof was accepted'
    [IO.File]::WriteAllText($closedPath,($proof|ConvertTo-Json -Compress),$utf8);Assert-Session (Call-SessionMethod ExactReleasedReceipt @($closedPath,$valid)) 'Exact peer release proof was rejected'
    foreach($change in @(@('token','wrong'),@('pid',1235),@('processCreated','134365380584475514'),@('path','R:/other.exe'),@('ceExited',$false),@('ceExited','true'),@('schemaVersion',99))){$bad=Dict $proof;$bad[$change[0]]=$change[1];$raw=$bad|ConvertTo-Json -Compress;[IO.File]::WriteAllText($closedPath,$raw,$utf8);Assert-Session (-not (Call-SessionMethod ExactReleasedReceipt @($closedPath,$valid))) 'Foreign/future peer proof was granted release authority';Assert-Session ([IO.File]::ReadAllText($closedPath,$utf8) -ceq $raw) 'Rejected peer close original was changed'}
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
    $rejectedClaim=Call-SessionMethod ClaimRequest @($request);$archive=[IO.Path]::ChangeExtension($rejectedClaim,'rejected')
    [IO.File]::WriteAllText($archive,'unknown archive; preserve',$utf8)
    Reject-Session {Call-SessionMethod FinishClaim @($rejectedClaim,$false)}
    Assert-Session ([IO.File]::ReadAllText($archive) -ceq 'unknown archive; preserve' -and [IO.File]::Exists($rejectedClaim)) 'Rejected claim replaced unknown archive or lost its raw request'
    [IO.File]::Delete($archive);Call-SessionMethod FinishClaim @($rejectedClaim,$false)
    Assert-Session ((Call-SessionMethod Read @($archive,16384))['value'] -ceq '101') 'Rejected claim did not retain its original request'
    Reject-Session {Call-SessionMethod FinishClaim @($archive,$true)}
    Assert-Session ([IO.File]::Exists($archive)) 'Non-claim path was deleted'
}
Session-Case 'file-long-request-and-response-keep-intermediate-paths-bounded' {
    $long=Join-Path $owned 'long-requests';[IO.Directory]::CreateDirectory($long)|Out-Null
    $padding=200-($long.Length+1);if($padding -lt 1 -or $padding -gt 180){throw 'Selected owned root cannot create the 200-character session directory'}
    $sessionRoot=Join-Path $long ('r'*$padding);Field root $sessionRoot;Field token $token;Field revision 3
    $requests=Join-Path $sessionRoot 'requests';$responses=Join-Path $sessionRoot 'responses';[IO.Directory]::CreateDirectory($requests)|Out-Null;[IO.Directory]::CreateDirectory($responses)|Out-Null
    $request=Join-Path $requests ($id+'.json');$response=Join-Path $responses ($id+'.json');$value=Dict @{schemaVersion=1;id=$id;token=$token;action='first';value='731001';revision=3}
    Call-SessionMethod Save @($request,$value);$claim=Call-SessionMethod ClaimRequest @($request)
    Assert-Session ($claim.Length -lt 260 -and $claim.Length -le $request.Length+1) 'Actual claim name exceeded the published request path budget'
    $raw=Call-SessionMethod Read @($claim,16384);[void](Call-SessionMethod ValidateRequest @($raw,$id));Call-SessionMethod FinishClaim @($claim,$false)
    $rejected=[IO.Path]::ChangeExtension($claim,'rejected');Assert-Session ($rejected.Length -lt 260 -and [IO.File]::Exists($rejected)) 'Long rejected request was lost'
    Call-SessionMethod SaveHandoff @($response,(Dict @{schemaVersion=1;id=$id;token=$token;ok=$true;unknown='preserve'}))
    Assert-Session ((Call-SessionMethod Read @($response,16384))['unknown'] -ceq 'preserve') 'Long response did not publish and read back'
    Reject-Session {Call-SessionMethod SaveHandoff @($response,(Dict @{schemaVersion=1;id=$id;token=$token;ok=$false}))}
    Assert-Session ((Call-SessionMethod Read @($response,16384))['ok'] -eq $true) 'Response replay changed the original result'
}
Session-Case 'file-CE-state-type-future-and-identity-faults-remain-inactive' {
    Field root $owned;Field token $token;Field processId 1234;Field created '134365380584475513';Field imagePath 'R:/fake/never-opened.exe'
    $ce=Dict @{schemaVersion=1;token=$token;engine='cheat-engine';stage='idle';message='safe';count=0;rows=@();scanRound=0;searchValue='';lastWriteCount=0;lastWriteValue='';scannedBytes=0;totalBytes=0;scanElapsedMs=0;temporarilyPaused=$false;pauseRestored=$true;dataType='int32';commandId=''}
    [void](Call-SessionMethod ValidateCeState @($ce))
    foreach($pair in @(@('count','0'),@('schemaVersion',2),@('token','wrong'),@('stage','future'),@('pid',1235),@('path','R:/other.exe'))){$bad=Dict $ce;$bad[$pair[0]]=$pair[1];Reject-Session {Call-SessionMethod ValidateCeState @($bad)}}
    $file=Join-Path $owned 'ce-state.json';Call-SessionMethod Save @($file,$ce);Assert-Session (Call-SessionMethod Refresh @()) 'Actual CE file consumer did not read valid state'
    Field ownsSessionOutputs $true
    try{Call-SessionMethod Publish @($true)}finally{Field ownsSessionOutputs $false}
    [IO.File]::Copy((Join-Path $owned 'status.json'),(Join-Path $out 'actual-host-status.json'))
    [IO.File]::WriteAllText($file,'{"schemaVersion":2}');Reject-Session {Call-SessionMethod Refresh @()};$inactive=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter);Assert-Session ($inactive['stage'] -ceq 'closed' -and $inactive['count'] -eq 0) 'Unknown CE state retained activity or result authority'
    Assert-Session ([IO.File]::ReadAllText($file) -ceq '{"schemaVersion":2}') 'Future CE original overwritten'
    [IO.File]::Move($file,($file+'.future-retained'))
    $terminal=Dict $ce;$terminal['stage']='closed';$terminal['message']='exact CE terminal';$terminal['count']=1;$terminal['rows']=@((Dict @{address='0000000000001000';value='731001'}));$terminal['scanRound']=2;$terminal['searchValue']='731001';$terminal['lastWriteCount']=1;$terminal['lastWriteValue']='99999'
    [IO.File]::WriteAllText($file,($terminal|ConvertTo-Json -Depth 8 -Compress),$utf8);Field closing $false
    Reject-Session {Call-SessionMethod Refresh @()}
    $closed=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter);$stopped=$adapterType.GetField('closing',$binding).GetValue($script:scanAdapter)
    Assert-Session ($stopped -eq $true -and $closed['stage'] -ceq 'closed' -and $closed['message'] -ceq 'exact CE terminal') 'Exact terminal CE receipt did not stop backend admission before requests'
    Assert-Session ($closed['count'] -eq 0 -and $closed['rows'].Length -eq 0 -and $closed['scanRound'] -eq 0 -and $closed['searchValue'] -ceq '' -and $closed['lastWriteCount'] -eq 0 -and $closed['lastWriteValue'] -ceq '') 'Exact CE terminal retained result/write authority'
    Assert-Session ($closed['temporarilyPaused'] -eq $false -and $closed['pauseRestored'] -eq $true) 'Backend discarded CE own confirmed pause cleanup'
}
Session-Case 'file-close-clears-results-without-inventing-CE-resume' {
    $previous=Dict @{stage='scanning';count=2;rows=@('old-results');scanRound=3;searchValue='731001';lastWriteCount=2;lastWriteValue='99999';temporarilyPaused=$true;pauseRestored=$false;startupPhase='ce-handshake'}
    Field state $previous;Call-SessionMethod CloseState @('owned failure')
    $closed=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter)
    Assert-Session ($closed['stage'] -ceq 'closed' -and $closed['message'] -ceq 'owned failure' -and $closed['count'] -eq 0 -and $closed['rows'].Length -eq 0 -and $closed['scanRound'] -eq 0 -and $closed['searchValue'] -ceq '' -and $closed['lastWriteCount'] -eq 0 -and $closed['lastWriteValue'] -ceq '') 'Close retained old result/write authority'
    Assert-Session ($closed['temporarilyPaused'] -eq $true -and $closed['pauseRestored'] -eq $false -and $closed['startupPhase'] -ceq 'ce-handshake') 'Adapter close invented CE resume or lost its startup phase'
    foreach($fault in @('missing-recent','missing-expired','malformed','future','identity','sharing')){
        $faultRoot=Join-Path $owned ('refresh-'+$fault);[IO.Directory]::CreateDirectory($faultRoot)|Out-Null;$file=Join-Path $faultRoot 'ce-state.json';$hold=$null
        Field root $faultRoot;Field token $token;Field processId 1234;Field created '134365380584475513';Field imagePath 'R:/fake/never-opened.exe';Field state (Dict $previous);Field closing $false;Field lastCeSeen ([DateTime]::UtcNow)
        switch($fault){
            'missing-expired'{Field lastCeSeen ([DateTime]::UtcNow.AddSeconds(-9))}
            'malformed'{[IO.File]::WriteAllText($file,'HALF_WRITTEN_STATE',$utf8)}
            'future'{[IO.File]::WriteAllText($file,'{"schemaVersion":2}',$utf8)}
            'identity'{Call-SessionMethod Save @($file,(Dict @{schemaVersion=1;engine='cheat-engine';token=('f'*64)}))}
            'sharing'{[IO.File]::WriteAllText($file,'{"schemaVersion":1}',$utf8);$hold=[IO.File]::Open($file,'Open','ReadWrite','None')}
        }
        try{
            if($fault -eq 'missing-recent'){
                Assert-Session ((Call-SessionMethod Refresh @()) -eq $false) 'Brief missing state became current or fatal'
                $retained=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter);Assert-Session ($retained['stage'] -ceq 'scanning' -and $retained['temporarilyPaused'] -eq $true -and $retained['pauseRestored'] -eq $false -and $adapterType.GetField('closing',$binding).GetValue($script:scanAdapter) -eq $false) 'Brief missing state fabricated a terminal or pause result'
            }else{
                Reject-Session {Call-SessionMethod Refresh @()};$failed=$adapterType.GetField('state',$binding).GetValue($script:scanAdapter)
                Assert-Session ($failed['stage'] -ceq 'closed' -and $failed['count'] -eq 0 -and $failed['rows'].Length -eq 0 -and $failed['scanRound'] -eq 0 -and $failed['searchValue'] -ceq '' -and $failed['lastWriteCount'] -eq 0 -and $failed['lastWriteValue'] -ceq '') ($fault+': actual refresh retained stale result authority')
                Assert-Session ($failed['temporarilyPaused'] -eq $true -and $failed['pauseRestored'] -eq $false -and $failed['startupPhase'] -ceq 'ce-handshake') ($fault+': actual refresh invented CE pause restoration')
                Assert-Session ($adapterType.GetField('closing',$binding).GetValue($script:scanAdapter) -eq $true) ($fault+': failed refresh did not retire admission')
            }
        }finally{if($hold){$hold.Dispose()}}
    }
}

Session-Case 'file-heartbeat-sharing-recovers-and-permanent-lock-is-bounded' {
    $heartbeatRoot=Join-Path $owned 'heartbeat-sharing';[IO.Directory]::CreateDirectory($heartbeatRoot)|Out-Null
    Field root $heartbeatRoot;Field token $token
    Call-SessionMethod Heartbeat @();$heartbeatPath=Join-Path $heartbeatRoot 'ce-lease.txt';$original=[IO.File]::ReadAllText($heartbeatPath)
    $hold=[IO.File]::Open($heartbeatPath,'Open','ReadWrite','None')
    try{
        Call-SessionMethod Heartbeat @() # Exact Windows sharing fault; neither rename nor overwrite is permitted.
        $watch=[Diagnostics.Stopwatch]::StartNew();while($watch.ElapsedMilliseconds -lt 2100){Start-Sleep -Milliseconds 50}
        Reject-Session {Call-SessionMethod Heartbeat @()}
        Assert-Session ($script:sessionRejection -like '*CE心跳文件持续被占用*') 'Persistent lease lock did not fail with bounded, actionable error'
    }finally{$hold.Dispose()}
    Assert-Session ([IO.File]::ReadAllText($heartbeatPath) -ceq $original) 'Sharing retry modified the held heartbeat'
    Call-SessionMethod Heartbeat @();$recovered=[IO.File]::ReadAllText($heartbeatPath)
    Assert-Session ($recovered.StartsWith($token+"`n") -and $recovered -cne $original) 'Heartbeat did not recover after lock release'
    $badRoot=Join-Path $owned 'heartbeat-nonsharing';[IO.Directory]::CreateDirectory((Join-Path $badRoot 'ce-lease.txt'))|Out-Null;Field root $badRoot
    Reject-Session {Call-SessionMethod Heartbeat @()}
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

if($CaseFilter -and $rows.Count -eq 0){throw 'SessionConfig CaseFilter selected no original cases'}
$result=@{caseFilter=$CaseFilter;skipped=$script:sessionSkipped;status=$(if(@($rows|Where-Object{!$_.passed}).Count){'FAIL'}else{'PASS'});checks=$rows.Count;passed=@($rows|Where-Object{$_.passed}).Count;failed=@($rows|Where-Object{!$_.passed}).Count;cases=@($rows.ToArray());elapsedMs=$started.Elapsed.TotalMilliseconds;mode='SessionConfig';actualSource=@{path=$source;sha256=(Get-FileHash -LiteralPath $source -Algorithm SHA256).Hash.ToLowerInvariant()};actualConsumer=@{path=$PSCommandPath;sha256=(Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()};loadedAssembly=@{path=$assemblyPath;sha256=(Get-FileHash -LiteralPath $assemblyPath -Algorithm SHA256).Hash.ToLowerInvariant()};powershell=@{path=(Get-Process -Id $PID).Path;version=$PSVersionTable.PSVersion.ToString()};scope='Actual production file/config functions and Windows argument parser loaded as library; no Main/Open/Run/Guard/CE/process-query/registry/memory invocation';processMemoryActions=0;ceLaunches=0;registryActions=0}
$result|ConvertTo-Json -Depth 12|Set-Content -LiteralPath (Join-Path $out 'host-session-config.json') -Encoding UTF8
$result|ConvertTo-Json -Depth 5
if($result.failed){exit 1};return

}
$principal=[Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if(-not $CurrentUserFixture -and $TestMode -ne 'Startup' -and -not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)){$p=Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -PassThru -ArgumentList @('-NoProfile','-ExecutionPolicy','Bypass','-File',('"'+$PSCommandPath+'"'),'-Elevated','-ValidationRoot',('"'+$out+'"'),'-TestMode',$TestMode,'-FixtureExeName',('"'+$FixtureExeName+'"'),'-GameTitle',('"'+$GameTitle+'"'),'-SteamAppId',$(if($SteamAppId){$SteamAppId}else{'0'}));@{testPid=$p.Id;dispatched=$true}|ConvertTo-Json;return}
function Json([string]$p,$v){$temp=$p+'.'+[guid]::NewGuid().ToString('N')+'.tmp';[IO.File]::WriteAllText($temp,($v|ConvertTo-Json -Depth 16 -Compress),[Text.UTF8Encoding]::new($false));if([IO.File]::Exists($p)){[IO.File]::Replace($temp,$p,[NullString]::Value)}else{[IO.File]::Move($temp,$p)}}
function Read([string]$p){$s=[IO.FileStream]::new($p,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite-bor[IO.FileShare]::Delete));try{$r=[IO.StreamReader]::new($s,[Text.Encoding]::UTF8);try{return $r.ReadToEnd()|ConvertFrom-Json}finally{$r.Dispose()}}finally{$s.Dispose()}}
function SettingsSnapshot {
 $rows=[Collections.Generic.List[object]]::new();$queue=[Collections.Queue]::new();$queue.Enqueue('Software\Cheat Engine')
 while($queue.Count){$name=[string]$queue.Dequeue();$key=[Microsoft.Win32.Registry]::CurrentUser.OpenSubKey($name,$false);if(-not $key){continue};try{foreach($value in ($key.GetValueNames()|Sort-Object)){$data=$key.GetValue($value,$null,[Microsoft.Win32.RegistryValueOptions]::DoNotExpandEnvironmentNames);$rows.Add(@{path=$name;name=$value;kind=[string]$key.GetValueKind($value);data=$data})};foreach($sub in ($key.GetSubKeyNames()|Sort-Object)){$queue.Enqueue($name+'\'+$sub)}}finally{$key.Dispose()}}
 return ,$rows.ToArray()
}
$run=Join-Path $out ('fixture-'+[guid]::NewGuid().ToString('N').Substring(0,8));$pc=Join-Path $run 'PowerControl';$session=Join-Path $run ([guid]::NewGuid().ToString('N'));foreach($dir in @((Join-Path $pc 'MemoryScan'),(Join-Path $session 'requests'),(Join-Path $session 'responses'))){[IO.Directory]::CreateDirectory($dir)|Out-Null}
$ceId=0;$report=@{ok=$false;mode=$TestMode;gameTitle=$GameTitle;steamAppId=$SteamAppId;realGameMemoryWrites=0;scope='Production CE plus adapter against owned 1MiB mapped fixture only';checks=@()};$fixture=$null;$helper=$null;$map=$null;$view=$null;$secondSession=$null;$secondHelper=$null;$firstCeProcess=$null;$secondCeProcess=$null;$externalCe=$null;$externalStop=$null;$sameExeFixture=$null;$sameExeStop=$null
try{
 $fixtureFile=Join-Path $run ('steamapps/common/'+$(if($SteamAppId){$SteamAppId}else{'fixture'})+'/bin/'+$FixtureExeName);[IO.Directory]::CreateDirectory((Split-Path $fixtureFile))|Out-Null;& "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" /nologo /optimize+ /target:winexe /platform:x64 /r:System.Web.Extensions.dll ('/out:'+$fixtureFile) (Join-Path $PSScriptRoot 'game_value_scan_fixture.cs');if($LASTEXITCODE){throw 'Fixture compile failed'}
 $dataFile=Join-Path $run 'fixture-memory.bin';$stream=[IO.File]::Create($dataFile);$stream.SetLength(1048576);$stream.Dispose();$sharedStream=[IO.FileStream]::new($dataFile,[IO.FileMode]::Open,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite);$map=[IO.MemoryMappedFiles.MemoryMappedFile]::CreateFromFile($sharedStream,[NullString]::Value,1048576,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::ReadWrite,$null,[IO.HandleInheritability]::None,$true);$view=$map.CreateViewAccessor();$view.Write(0,[int]731001);$view.Write(4,[int]731001)
 $ready=Join-Path $run 'fixture-ready.txt';$fixtureStop=Join-Path $run 'fixture-stop';$fixture=Start-Process -FilePath $fixtureFile -ArgumentList @(('"'+$dataFile+'"'),('"'+$ready+'"'),('"'+$fixtureStop+'"')) -WindowStyle Hidden -PassThru
 $until=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($ready)-and[DateTime]::UtcNow-lt$until){Start-Sleep -Milliseconds 50};if(-not[IO.File]::Exists($ready)){throw 'Fixture not ready'}
 $token=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N');$owner=Get-Process -Id $PID
 Json (Join-Path $session 'config.json') @{token=$token;pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;ownerPid=$PID;ownerPath=$owner.Path;ownerCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc()}
 $script:targetOverride=$null
  function Pulse {Json (Join-Path $session 'lease.json') @{token=$token;at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()};$snapshot=if($script:targetOverride){$copy=$script:targetOverride.Clone();$copy.lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();$copy}else{@{valid=$true;generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;title=$GameTitle;name=$GameTitle;steamAppId=$SteamAppId}};Json (Join-Path $pc 'game-target.json') $snapshot}
 function WaitFile([string]$path,[int]$seconds=25){$until=[DateTime]::UtcNow.AddSeconds($seconds);while(-not[IO.File]::Exists($path)-and[DateTime]::UtcNow-lt$until){Pulse;if($helper-and$helper.HasExited){throw ('Adapter exited: '+(Read (Join-Path $session 'status.json')).message)};Start-Sleep -Milliseconds 100};if(-not[IO.File]::Exists($path)){throw ('Missing '+$path)}}
 & (Join-Path $PSScriptRoot 'build-game-value-scan-host.ps1') -OutputDirectory (Join-Path $out 'helper') | Out-Null
 foreach($name in @('MemoryScanHost.exe','CeScanEngine.lua')){Copy-Item -LiteralPath (Join-Path $out ('helper/'+$name)) -Destination (Join-Path $pc ('MemoryScan/'+$name))}
 $before=SettingsSnapshot;Json (Join-Path $run 'ce-settings-before.json') $before
 Pulse
 if($TestMode -eq 'Startup' -and $StartupCase -ne 'Healthy'){
  $report.startupCase=$StartupCase;$config=Read (Join-Path $session 'config.json');$expectedPhase=''
  switch($StartupCase){
   'OwnerDirectory'{$config|Add-Member ownerDirectory (Join-Path $run 'different-owner-directory');$expectedPhase='owner-identity'}
   'OwnerMissing'{$config.ownerPid=2147483647;$expectedPhase='target-session'}
   'MalformedCapability'{$config.token='not-an-owned-capability'}
   'FutureConfig'{$config|Add-Member schemaVersion 2}
  }
  Json (Join-Path $session 'config.json') $config
  $helper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru
  if(-not $helper.WaitForExit(5000) -or $helper.ExitCode -eq 0){throw 'Startup admission did not refuse the injected invalid owner/config'}
  $report.adapterExitCode=$helper.ExitCode;$report.ceStarts=0;$report.scanCommands=0
  if([IO.File]::Exists((Join-Path $session 'ce-process.json')) -or [IO.File]::Exists((Join-Path $run 'ce-owner.json')) -or [IO.File]::Exists((Join-Path $session 'YMCC-Session.cetrainer'))){throw 'Startup admission failure created CE state or took the shared CE record'}
  if($expectedPhase){
   if(-not [IO.File]::Exists((Join-Path $session 'status.json'))){throw 'Startup refusal lost its exact reason before CE creation; frontend would only time out'}
   $status=Read (Join-Path $session 'status.json')
   if($status.stage -cne 'closed' -or $status.engine -cne 'cheat-engine' -or $status.token -cne $token -or $status.pid -ne $config.pid -or $status.processCreated -cne $config.processCreated -or $status.path -cne $config.path -or $status.startupPhase -cne $expectedPhase -or -not $status.message -or $status.count -ne 0 -or $status.rows.Count -ne 0 -or $status.lastWriteCount -ne 0){throw 'Early startup refusal did not preserve the exact closed inactive identity and phase'}
   $report.startupRefusal=@{stage=$status.stage;phase=$status.startupPhase;message=$status.message;count=$status.count};$report.checks+=@('Valid-shape owner admission failure publishes a matching closed reason immediately, without launching CE or returning results')
   $released=Read (Join-Path $session 'closed.json');if($released.token -cne $token -or $released.ceExited -ne $true){throw 'No-CE startup refusal did not release its own exact capability'}
  }elseif([IO.File]::Exists((Join-Path $session 'status.json')) -or [IO.File]::Exists((Join-Path $session 'closed.json'))){throw 'Malformed/future capability acquired a publication or close authority'}
  else{$report.checks+=@('Malformed/future config remains unowned and cannot publish a misleading status or CE release')}
  $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not $report.settingsPreserved){throw 'Refused startup changed CE preferences'}
  $report.ok=$true;return
 }
 $helper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru;WaitFile (Join-Path $session 'status.json');$status=Read (Join-Path $session 'status.json');if($status.stage-ne'idle'){throw $status.message};$report.initial=$status;$ceId=[int](Read (Join-Path $session 'ce-process.json')).pid;$report.checks+=@('CE starts hidden with current settings and no setup dialogs');$script:revision=0
 if($TestMode -eq 'RendererRestart'){
  $firstCeProcess=[Diagnostics.Process]::GetProcessById($ceId);[void]$firstCeProcess.Handle
  $firstIdentity=Read (Join-Path $session 'ce-process.json');if([string]$firstCeProcess.StartTime.ToUniversalTime().ToFileTimeUtc() -cne $firstIdentity.processCreated){throw 'First CE birth changed'}
  $report.ownershipCase=$OwnershipCase;$report.scanCommands=0
  if($OwnershipCase -eq 'DuplicateSession'){
   $protected=Get-Content -LiteralPath (Join-Path $session 'ce-process.json') -Raw
   $secondHelper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru
   if(-not $secondHelper.WaitForExit(3000) -or $secondHelper.ExitCode -eq 0){throw 'Duplicate helper was not refused'}
   if($firstCeProcess.HasExited -or $helper.HasExited -or [IO.File]::Exists((Join-Path $session 'closed.json')) -or [IO.File]::Exists((Join-Path $session 'ce-stop')) -or (Get-Content -LiteralPath (Join-Path $session 'ce-process.json') -Raw) -cne $protected){throw 'Refused duplicate helper changed the first CE ownership'}
   $report.liveDuplicateRefused=$true;$report.duplicateExitCode=$secondHelper.ExitCode
   [IO.File]::WriteAllText((Join-Path $session 'stop'),'stop');if(-not $helper.WaitForExit(12000) -or -not $firstCeProcess.WaitForExit(1000)){throw 'First session did not close after duplicate refusal'}
   $protectedClosed=Get-Content -LiteralPath (Join-Path $session 'closed.json') -Raw
   $secondHelper.Dispose();$secondHelper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$session+'"') -WindowStyle Hidden -PassThru
   if(-not $secondHelper.WaitForExit(3000) -or $secondHelper.ExitCode -eq 0 -or (Get-Content -LiteralPath (Join-Path $session 'closed.json') -Raw) -cne $protectedClosed){throw 'Used capability was reused or its release receipt changed'}
   $report.usedSessionRefused=$true;$report.checks+=@('Duplicate live helper and later reused session both refuse without stopping or re-signing the original exact CE')
  }else{
  if($OwnershipCase -in @('HelperCrash','LegacyAdoption','UnconfirmedLive')){
   if($helper.Path -ine (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -or $helper.HasExited){throw 'Cannot bind the exact owned helper for injected crash'}
   [void]$helper.Handle;$report.crashedOwnedAdapter=@{pid=$helper.Id;processCreated=[string]$helper.StartTime.ToUniversalTime().ToFileTimeUtc();path=$helper.Path}
   $helper.Kill();if(-not $helper.WaitForExit(3000) -or $firstCeProcess.HasExited){throw 'Owned helper crash did not leave the expected live exact CE'}
   $report.ownedAdapterCrashInjected=$true
   if($OwnershipCase -eq 'UnconfirmedLive'){
    $journalPath=Join-Path $run 'ce-owner.json';$journal=Read $journalPath;$journal.phase='pending';$journal.ce=$null;Json $journalPath $journal
    Move-Item -LiteralPath (Join-Path $session 'ce-process.json') -Destination (Join-Path $session 'ce-process.fixture-retained.json')
    $report.unconfirmedLiveInjected=$true
   }else{
    Json (Join-Path $session 'closed.json') @{schemaVersion=1;engine='cheat-engine';stage='closed';token=$token;pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;ceExited=$true}
    $report.falseClosedReceiptInjected=$true
   }
   if($OwnershipCase -eq 'LegacyAdoption'){Move-Item -LiteralPath (Join-Path $run 'ce-owner.json') -Destination (Join-Path $run 'ce-owner.fixture-retained.json');$report.legacyJournalAbsentInjected=$true}
  }elseif($OwnershipCase -in @('UnconfirmedLaunch','UnconfirmedWithoutGate','LegacyUndispatched','PreCreateCrash')){
   [IO.File]::WriteAllText((Join-Path $session 'stop'),'stop');if(-not $helper.WaitForExit(12000) -or -not $firstCeProcess.WaitForExit(1000)){throw 'Cannot prepare inert unconfirmed record after real CE release'}
   $journalPath=Join-Path $run 'ce-owner.json';$journal=Read $journalPath;$journal.phase='pending';$journal.ce=$null;Json $journalPath $journal
   Move-Item -LiteralPath (Join-Path $session 'ce-process.json') -Destination (Join-Path $session 'ce-process.fixture-retained.json');Move-Item -LiteralPath (Join-Path $session 'closed.json') -Destination (Join-Path $session 'closed.fixture-retained.json')
   $report.unconfirmedRecordInjected=$true
   if($OwnershipCase -eq 'UnconfirmedWithoutGate'){
    Move-Item -LiteralPath (Join-Path $session '.host.lock') -Destination (Join-Path $session '.host.fixture-retained')
    if($LegacyOwnerGone){
     $priorOwner=Start-Process -FilePath $env:ComSpec -ArgumentList @('/d','/c','exit /b 0') -WindowStyle Hidden -PassThru
     try{
      [void]$priorOwner.Handle;$priorPid=$priorOwner.Id;$priorBirth=[string]$priorOwner.StartTime.ToUniversalTime().ToFileTimeUtc()
      if(-not $priorOwner.WaitForExit(3000)){throw 'Inert legacy dispatcher did not exit'}
      $priorConfig=Read (Join-Path $session 'config.json');$priorConfig.ownerPid=$priorPid;$priorConfig.ownerCreated=$priorBirth;$priorConfig.ownerPath=$env:ComSpec;Json (Join-Path $session 'config.json') $priorConfig
      $report.legacyDispatcherExited=@{pid=$priorPid;processCreated=$priorBirth;path=$env:ComSpec;actualExit=$true}
     }finally{$priorOwner.Dispose()}
    }
   }
   if($OwnershipCase -eq 'LegacyUndispatched'){
    Move-Item -LiteralPath $journalPath -Destination (Join-Path $run 'ce-owner.fixture-retained.json')
    Move-Item -LiteralPath (Join-Path $session 'ce-process.fixture-retained.json') -Destination (Join-Path $session 'ce-process.json');Move-Item -LiteralPath (Join-Path $session 'closed.fixture-retained.json') -Destination (Join-Path $session 'closed.json')
    $staged=Join-Path $run ([guid]::NewGuid().ToString('N'));[IO.Directory]::CreateDirectory($staged)|Out-Null
    $stagedConfig=Read (Join-Path $session 'config.json');$stagedConfig.token=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N');Json (Join-Path $staged 'config.json') $stagedConfig
    $report.stagedSession=$staged;$report.legacyUndispatchedInjected=$true
   }elseif($OwnershipCase -eq 'PreCreateCrash'){
    foreach($name in @('YMCC-Session.cetrainer','ce-lease.txt','ce-state.json','ce-stop','status.json')){if([IO.File]::Exists((Join-Path $session $name))){Move-Item -LiteralPath (Join-Path $session $name) -Destination (Join-Path $session ($name+'.fixture-retained'))}}
    $report.stagedSession=$session;$report.preCreateCrashInjected=$true
   }
  }
  if($ExternalCeDuringRecovery){
   $externalStop=Join-Path $run 'external-ce-stop';$externalReady=Join-Path $run 'external-ce-ready';$externalTrainer=Join-Path $run 'External-Fixture.cetrainer'
   function FixtureLuaString([string]$text){return '"'+$text.Replace('\','\\').Replace('"','\"')+'"'}
   $externalLua='local timer=createTimer(nil);timer.Interval=100;timer.OnTimer=function() getMainForm().hide();if fileExists('+ (FixtureLuaString $externalStop)+') then timer.destroy();closeCE();return end;local f=createFileStream('+(FixtureLuaString $externalReady)+',fmCreate);f.destroy() end'
   [IO.File]::WriteAllText($externalTrainer,('<?xml version="1.0" encoding="utf-8"?><CheatTable CheatEngineTableVersion="45"><LuaScript>'+[Security.SecurityElement]::Escape($externalLua)+'</LuaScript></CheatTable>'),[Text.UTF8Encoding]::new($false))
   $externalCe=Start-Process -FilePath $firstIdentity.path -ArgumentList ('NOAUTORUN "'+$externalTrainer+'"') -WindowStyle Hidden -PassThru;[void]$externalCe.Handle
   $externalUntil=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($externalReady) -and [DateTime]::UtcNow -lt $externalUntil -and -not $externalCe.HasExited){Start-Sleep -Milliseconds 50}
   if($externalCe.HasExited -or -not [IO.File]::Exists($externalReady)){throw 'Independent inert CE did not become available'}
   $report.externalCe=@{pid=$externalCe.Id;path=$externalCe.Path;birth=[string]$externalCe.StartTime.ToUniversalTime().ToFileTimeUtc()}
  }
  if($ReconcileRelease){
   if($ReleaseAfterGameExit){[IO.File]::WriteAllText((Join-Path $run 'fixture-stop'),'stop');if(-not $fixture.WaitForExit(5000)){throw 'Owned target did not exit'};$report.oldTargetExitedBeforeReconciliation=$true}
   if($ReleaseReceiptFault -eq 'FutureClosed'){$futureClosed='{"schemaVersion":99,"unknown":"retain"}';[IO.File]::WriteAllText((Join-Path $session 'closed.json'),$futureClosed,[Text.UTF8Encoding]::new($false))}
   $foreignRelease=$null
   if($ForeignReleaseImage){
    $foreignFile=Join-Path $run 'foreign/MemoryScanHost.exe';[IO.Directory]::CreateDirectory((Split-Path $foreignFile))|Out-Null;[IO.File]::Copy($fixtureFile,$foreignFile,$false)
    $foreignRelease=Start-Process -FilePath $foreignFile -ArgumentList @('--release',('"'+$session+'"'),('"'+(Join-Path $session ('release-'+('e'*32)+'.json'))+'"')) -WindowStyle Hidden -PassThru;[void]$foreignRelease.Handle
    $foreignReady=Join-Path $session 'foreign-release-ready';$foreignDeadline=[DateTime]::UtcNow.AddSeconds(5);while(-not [IO.File]::Exists($foreignReady) -and -not $foreignRelease.HasExited -and [DateTime]::UtcNow -lt $foreignDeadline){Start-Sleep -Milliseconds 25}
    if(-not [IO.File]::Exists($foreignReady) -or $foreignRelease.HasExited){throw 'Foreign same-name release fixture was not live'}
    $report.foreignReleaseImage=@{pid=$foreignRelease.Id;processCreated=[string]$foreignRelease.StartTime.ToUniversalTime().ToFileTimeUtc();path=$foreignFile;readinessObserved=$true}
   }
   $releaseOutput=Join-Path $session ('release-'+[guid]::NewGuid().ToString('N')+'.json')
   $releaseProcess=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList @('--release',('"'+$session+'"'),('"'+$releaseOutput+'"')) -WindowStyle Hidden -PassThru
   $releasePeer=$null;$peerOutput=$null
   try{
    [void]$releaseProcess.Handle;$releaseId=$releaseProcess.Id;$releaseBirth=[string]$releaseProcess.StartTime.ToUniversalTime().ToFileTimeUtc();$deadline=[DateTime]::UtcNow.AddSeconds(16)
    if($ConcurrentRelease){$peerOutput=Join-Path $session ('release-'+[guid]::NewGuid().ToString('N')+'.json');$releasePeer=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList @('--release',('"'+$session+'"'),('"'+$peerOutput+'"')) -WindowStyle Hidden -PassThru;[void]$releasePeer.Handle;$peerBirth=[string]$releasePeer.StartTime.ToUniversalTime().ToFileTimeUtc()}
    while(-not $releaseProcess.HasExited -and [DateTime]::UtcNow -lt $deadline){if($ReleaseReceiptFault -eq 'None' -and -not $firstCeProcess.HasExited -and [IO.File]::Exists((Join-Path $session 'closed.json'))){throw 'Release mode certified a still-live exact CE as exited'};Start-Sleep -Milliseconds 25}
    if($ForeignReleaseImage){
     if(-not $releaseProcess.HasExited -or $releaseProcess.ExitCode -ne 1 -or $foreignRelease.HasExited){throw 'Foreign image was skipped as a trusted release peer'}
     $releaseReceipt=Read $releaseOutput;if($releaseReceipt.released -ne $false -or [IO.File]::Exists((Join-Path $session 'closed.json'))){throw 'Unknown same-name image granted a release proof'}
     if($externalCe -and $externalCe.HasExited){throw 'Image refusal touched independent CE'}
     $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not $report.settingsPreserved){throw 'Image refusal changed CE settings'}
     $report.externalCeUntouched=[bool]$externalCe;$report.foreignImageRefused=$true;$report.releaseReceipt=$releaseReceipt;$report.checks+=@('A different exact image with the same MemoryScanHost.exe filename and valid --release argument shape cannot bypass old-session admission');$report.ok=$true;return
    }
    if($ReleaseReceiptFault -eq 'FutureClosed'){
     if(-not $releaseProcess.HasExited -or $releaseProcess.ExitCode -ne 1){throw 'Future close receipt was not refused'}
     $releaseReceipt=Read $releaseOutput
     if($releaseReceipt.released -ne $false -or [IO.File]::ReadAllText((Join-Path $session 'closed.json')) -cne $futureClosed){throw 'Unknown close record was changed or returned a release authority'}
     $report.futureClosedRetained=$true;$report.releaseReceipt=$releaseReceipt;$report.releaseProcess=@{pid=$releaseId;processCreated=$releaseBirth;exitCode=$releaseProcess.ExitCode}
     if($externalCe){if($externalCe.HasExited){throw 'Refused release touched independent CE'};$report.externalCeUntouched=$true}
     $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not $report.settingsPreserved){throw 'Refused release changed CE settings'}
     $report.checks+=@('Future closed schema remains unchanged and release-only mode returns false without authorizing another CE');$report.ok=$true;return
    }
    if($releasePeer){
     $remaining=[Math]::Max(0,[int]($deadline-[DateTime]::UtcNow).TotalMilliseconds);$peerEnded=$releasePeer.WaitForExit($remaining)
     $report.concurrentReleaseObservation=@{primary=@{pid=$releaseId;processCreated=$releaseBirth;exited=$releaseProcess.HasExited;exitCode=$(if($releaseProcess.HasExited){$releaseProcess.ExitCode}else{$null});receipt=$(if([IO.File]::Exists($releaseOutput)){Read $releaseOutput}else{$null})};peer=@{pid=$releasePeer.Id;processCreated=$peerBirth;exited=$peerEnded;exitCode=$(if($peerEnded){$releasePeer.ExitCode}else{$null});receipt=$(if([IO.File]::Exists($peerOutput)){Read $peerOutput}else{$null})}}
     if(-not $peerEnded -or $releasePeer.ExitCode -ne 0 -or $report.concurrentReleaseObservation.peer.receipt.released -ne $true){throw 'Concurrent release peer did not confirm its exact release within the original deadline'}
    }
    if(-not $releaseProcess.HasExited -or $releaseProcess.ExitCode -ne 0){throw 'Release reconciliation failed or did not exit'}
    $releaseReceipt=Read $releaseOutput;$closed=Read (Join-Path $session 'closed.json')
    if(-not $firstCeProcess.HasExited -or $releaseReceipt.released -ne $true -or $releaseReceipt.token -cne $token -or $releaseReceipt.pid -ne $fixture.Id -or $releaseReceipt.processCreated -cne [string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc() -or $closed.ceExited -ne $true -or $closed.releaseBasis -cne 'explicit-owned-session-reconciliation'){throw 'Release mode did not publish exact process-release proof'}
    $report.releaseProcess=@{pid=$releaseId;processCreated=$releaseBirth;exitCode=$releaseProcess.ExitCode};$report.releaseReceipt=$releaseReceipt;$report.closedReceipt=$closed;$report.ceExited=$true
   }finally{if($foreignRelease){[IO.File]::WriteAllText((Join-Path $session 'foreign-release-stop'),'stop');$report.foreignReleaseExitedAtCleanup=$foreignRelease.WaitForExit(3000);$foreignRelease.Dispose()};if($releasePeer){$report.releasePeerExitedAtCleanup=$releasePeer.HasExited;$releasePeer.Dispose()};$report.releaseProcessExitedAtCleanup=$releaseProcess.HasExited;$releaseProcess.Dispose()}
   if($externalCe){if($externalCe.HasExited){throw 'Release mode touched independent CE'};$report.externalCeUntouched=$true}
   $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after;$report.settingsPreserved=(($before|ConvertTo-Json -Depth 16 -Compress)-ceq($after|ConvertTo-Json -Depth 16 -Compress));if(-not $report.settingsPreserved){throw 'Release mode changed CE preferences'}
   $report.reconcileReleaseOnly=$true;$report.releaseOnlyStartedNewCE=$false;$report.checks+=@('Explicit --release waits for the exact unknown CE to really exit and restores only the original authenticated closed receipt, even after the old game has exited');$report.ok=$true;return
  }
  $secondSession=Join-Path $run ([guid]::NewGuid().ToString('N'));$secondToken=[guid]::NewGuid().ToString('N')+[guid]::NewGuid().ToString('N')
  foreach($name in @('requests','responses')){[IO.Directory]::CreateDirectory((Join-Path $secondSession $name))|Out-Null}
  Json (Join-Path $secondSession 'config.json') @{token=$secondToken;pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;ownerPid=$PID;ownerPath=$owner.Path;ownerCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc()}
  function ReplacementPulse {Json (Join-Path $secondSession 'lease.json') @{token=$secondToken;at=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds()};Json (Join-Path $pc 'game-target.json') @{valid=$true;generation=1;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$fixture.Id;processCreated=[string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile;title=$GameTitle;name=$GameTitle;steamAppId=$SteamAppId}}
  ReplacementPulse;$secondHelper=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$secondSession+'"') -WindowStyle Hidden -PassThru
  $report.firstCe=$firstIdentity;$report.firstAdapterPid=$helper.Id;$report.secondAdapterPid=$secondHelper.Id;$report.oldFrontendLeaseRenewed=$false;$report.scanCommands=0
  $handoffDeadline=[DateTime]::UtcNow.AddSeconds(25);$replacementProcessPath=Join-Path $secondSession 'ce-process.json';$overlap=$false
  if(($OwnershipCase -eq 'UnconfirmedWithoutGate' -and -not $LegacyOwnerGone) -or $OwnershipCase -in @('CancelWhileWaiting','TargetChangeWhileWaiting')){
   if($OwnershipCase -ne 'UnconfirmedWithoutGate'){
    $observe=[DateTime]::UtcNow.AddMilliseconds(500);while([DateTime]::UtcNow -lt $observe){ReplacementPulse;Start-Sleep -Milliseconds 50}
    if([IO.File]::Exists($replacementProcessPath) -or $firstCeProcess.HasExited){throw 'Waiting case created an overlapping CE or lost its original observation'}
    if($OwnershipCase -eq 'CancelWhileWaiting'){[IO.File]::WriteAllText((Join-Path $secondSession 'stop'),'stop')}
    else{Json (Join-Path $pc 'game-target.json') @{valid=$true;generation=2;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$PID;processCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc();path=$owner.Path}}
   }
   if(-not $secondHelper.WaitForExit(4000) -or $secondHelper.ExitCode -eq 0 -or [IO.File]::Exists($replacementProcessPath)){throw 'Unknown/cancelled/changed waiting authority created a CE or did not refuse'}
   $refused=Read (Join-Path $secondSession 'status.json');if($refused.stage -cne 'closed'){throw 'Waiting refusal remained active'}
   if($OwnershipCase -eq 'UnconfirmedWithoutGate'){
    if($refused.message -notlike '*缺少主机回执*'){throw 'Unconfirmed launch did not preserve the ownership reason'}
    $still=Read (Join-Path $run 'ce-owner.json');if($still.phase -cne 'pending' -or $null -ne $still.ce){throw 'Unconfirmed old record was replaced'}
    Move-Item -LiteralPath (Join-Path $session 'closed.fixture-retained.json') -Destination (Join-Path $session 'closed.json')
    $diagnostic=Read (Join-Path $run 'ce-session-diagnostics.json');$diagnosticText=[IO.File]::ReadAllText((Join-Path $run 'ce-session-diagnostics.json'))
    if($diagnostic.previousSession -cne (Split-Path $session -Leaf) -or $diagnostic.startupPhase -cne 'ce-ownership' -or $diagnosticText.Contains($token) -or $diagnosticText.Contains($secondToken) -or $diagnosticText.Contains('searchValue') -or $diagnosticText.Contains('rows')){throw 'Failure diagnostics lost exact phase/session or exposed capability/scan data'}
    $report.failureDiagnosticCaptured=$true
   }
   $report.refusedWithoutCE=$true;$report.refusalMessage=$refused.message;$report.checks+=@('Unconfirmed/cancelled/changed waiting session refuses without creating a replacement CE')
  }else{
  while(-not [IO.File]::Exists($replacementProcessPath) -and [DateTime]::UtcNow -lt $handoffDeadline){ReplacementPulse;if($secondHelper.HasExited){throw ('Replacement adapter exited: '+(Read (Join-Path $secondSession 'status.json')).message)};Start-Sleep -Milliseconds 50}
  if(-not [IO.File]::Exists($replacementProcessPath)){throw 'Replacement CE never became available after the old frontend lease ended'}
  $secondIdentity=Read $replacementProcessPath;$secondCeProcess=[Diagnostics.Process]::GetProcessById([int]$secondIdentity.pid);[void]$secondCeProcess.Handle
  if([string]$secondCeProcess.StartTime.ToUniversalTime().ToFileTimeUtc() -cne $secondIdentity.processCreated){throw 'Replacement CE birth changed'}
  $report.secondCe=$secondIdentity;$report.firstCeExitedBeforeReplacement=$firstCeProcess.HasExited;$report.firstAdapterExitedBeforeReplacement=$helper.HasExited
  if(-not $report.firstCeExitedBeforeReplacement){$report.overlapObserved=$true;throw 'Second CE was created while the previous exact owned CE was still alive'}
  $report.firstCeExitFileTime=[string]$firstCeProcess.ExitTime.ToUniversalTime().ToFileTimeUtc();$report.secondCeBirthFileTime=[string]$secondCeProcess.StartTime.ToUniversalTime().ToFileTimeUtc()
  if([long]$report.firstCeExitFileTime -gt [long]$report.secondCeBirthFileTime){$report.overlapObserved=$true;throw 'Exact CE exit timestamp is later than replacement creation'}
  $report.overlapObserved=$false
  if($OwnershipCase -in @('LegacyUndispatched','PreCreateCrash')){
   $recovered=Read (Join-Path $report.stagedSession 'admission-recovery.json');$stagedConfig=Read (Join-Path $report.stagedSession 'config.json')
   if($recovered.basis -cne 'never-dispatched' -or $recovered.token -cne $stagedConfig.token -or -not [IO.File]::Exists((Join-Path $report.stagedSession '.host.lock'))){throw 'Unlaunched session did not retain its bound recovery and late-dispatch gate'}
   $late=Start-Process -FilePath (Join-Path $pc 'MemoryScan/MemoryScanHost.exe') -ArgumentList ('"'+$report.stagedSession+'"') -WindowStyle Hidden -PassThru
   try{if(-not $late.WaitForExit(3000) -or $late.ExitCode -eq 0 -or [IO.File]::Exists((Join-Path $report.stagedSession 'ce-process.json'))){throw 'Late helper reused a recovered staged capability'}}finally{$late.Dispose()}
   $report.neverDispatchedRecovered=$true;$report.lateDispatchRefused=$true;$report.checks+=@('A staged/pre-create capability recovers only without launch artifacts, retains its original config, and refuses a late helper dispatch')
  }elseif($OwnershipCase -eq 'UnconfirmedLaunch' -or ($OwnershipCase -eq 'UnconfirmedWithoutGate' -and $LegacyOwnerGone)){
   $recovered=Read (Join-Path $session 'admission-recovery.json')
   if($recovered.token -cne $token -or $recovered.basis -cne 'sealed-session-no-live-command' -or $recovered.checkedProcesses -lt 1){throw 'Unknown ended CE did not retain its actual process-query recovery basis'}
   $report.orphanRecovery=$recovered;$report.checks+=@('A sealed unknown session recovers after exact process argument inspection proves its CE absent')
  }elseif($OwnershipCase -eq 'UnconfirmedLive'){
   $recovered=Read (Join-Path $session 'ce-process.json')
   if($recovered.token -cne $token -or $recovered.recoveryBasis -cne 'exact-session-argument' -or $recovered.pid -ne $firstIdentity.pid -or $recovered.processCreated -cne $firstIdentity.processCreated -or $recovered.path -cne $firstIdentity.path){throw 'Live unknown CE was not rebound to its exact original identity'}
   $report.orphanRecovery=$recovered;$report.checks+=@('Live CE without PID receipt is rediscovered by exact session argument and must really exit before replacement')
  }else{$oldClosed=Read (Join-Path $session 'closed.json');if($oldClosed.token -cne $token -or $oldClosed.ceExited -isnot [bool] -or -not $oldClosed.ceExited){throw 'Old CE exited without its matching release receipt'}}
  $statusDeadline=[DateTime]::UtcNow.AddSeconds(20);while(-not[IO.File]::Exists((Join-Path $secondSession 'status.json')) -and[DateTime]::UtcNow-lt$statusDeadline){ReplacementPulse;Start-Sleep -Milliseconds 100}
  $secondStatus=Read (Join-Path $secondSession 'status.json');if($secondStatus.token -cne $secondToken -or $secondStatus.stage -cne 'idle'){throw 'Replacement CE did not complete its own handshake'}
  [IO.File]::WriteAllText((Join-Path $secondSession 'stop'),'stop');if(-not $secondHelper.WaitForExit(12000) -or -not $secondCeProcess.WaitForExit(1000)){throw 'Replacement CE or adapter failed to close'}
  if($externalCe){
   if($externalCe.HasExited){throw 'Adapter recovery or shutdown closed the independently opened CE'}
   $report.externalCeUntouched=$true;[IO.File]::WriteAllText($externalStop,'stop');if(-not $externalCe.WaitForExit(5000)){throw 'Independent fixture CE did not close by its own marker'}
   $report.externalCeClosedByFixture=$true;$report.checks+=@('An independently opened real CE survives recovery and adapter shutdown, then closes only by its own fixture marker')
  }
  $report.secondCeExited=$true;$report.secondAdapterExited=$true;$report.checks+=@('A completely new session waits for exact old owned CE release after frontend lease loss, then starts and closes its own CE without replaying memory operations')
  }
  }
 }
 function Request([string]$action,[string]$value,[string]$expectError=''){Pulse;$id=[guid]::NewGuid().ToString('N');Json (Join-Path $session ('requests/'+$id+'.json')) @{token=$token;id=$id;action=$action;value=$value;revision=$script:revision};$response=Join-Path $session ('responses/'+$id+'.json');WaitFile $response;$result=Read $response;$script:revision=$result.state.revision;if($expectError){if($result.ok -or $result.error -notlike ('*'+$expectError+'*')){throw ('Expected '+$expectError+', got '+$result.error)}}elseif(-not$result.ok){throw ('CE '+$action+': '+$result.error)};return $result}
 if($HeartbeatContention){
  $leasePath=Join-Path $session 'ce-lease.txt';$report.heartbeatContentionBlocks=0
  for($block=0;$block -lt 3;$block++){
   $hold=$null;$deadline=[DateTime]::UtcNow.AddSeconds(2)
   while($null -eq $hold){Pulse;try{$hold=[IO.File]::Open($leasePath,'Open','ReadWrite','None')}catch [IO.IOException]{if([DateTime]::UtcNow -ge $deadline){throw};Start-Sleep -Milliseconds 10}}
   try{for($tick=0;$tick -lt 8;$tick++){Pulse;Start-Sleep -Milliseconds 100}}finally{$hold.Dispose()}
   for($tick=0;$tick -lt 5;$tick++){Pulse;Start-Sleep -Milliseconds 100}
   if($helper.HasExited -or (Read (Join-Path $session 'status.json')).stage -eq 'closed'){throw 'Brief real heartbeat contention ended the CE session'}
   $report.heartbeatContentionBlocks++
  }
  $report.checks+=@('Three actual Windows exclusive heartbeat locks recover through the original CE timer and adapter loop without session restart')
 }
 if($TestMode -eq 'Cancel'){
  [IO.File]::WriteAllText((Join-Path $session 'cancel'),'cancel');$cancelled=Request first '731001' '取消';if($cancelled.state.count -ne 0){throw 'Cancelled scan published results'}
  if([IO.File]::Exists((Join-Path $session 'cancel'))){throw 'Cancellation marker leaked to later scans'};$report.checks+=@('Pending cancellation is consumed exactly once without dispatching a scan')
 }
 if($TestMode -eq 'Pipeline' -or $TestMode -eq 'Cancel' -or ($TestMode -eq 'TargetChange' -and $TargetChangeAfterResults)){
 $a=Request first '731001';if($a.state.count-lt2){throw 'Mapped values absent from CE first scan'};$report.first=$a.state;$report.checks+=@('CE first scan finds both fixture fields using saved scan defaults')
 $view.Write(0,[int]731002);$view.Write(4,[int]731002);$b=Request next '731002';if($b.state.count-ne2){throw ('Expected only 2 mapped values, got '+$b.state.count)};$report.next=$b.state;$report.checks+=@('CE next scan narrows to exactly two changed mapped fields')
 $c=Request writeAll '99999';if($view.ReadInt32(0)-ne99999-or$view.ReadInt32(4)-ne99999){throw 'CE write not observed in mapped fields'};$report.write=$c.state;$report.checks+=@('CE writeInteger changes both fixture values to 99999 and reads them back')

 }
 if($TestMode -eq 'LuaFailure'){
  [IO.File]::WriteAllText((Join-Path $session 'ce-command.txt'),([guid]::NewGuid().ToString('N')+"`n"+$token+"`nunsupported`n0`n"),[Text.UTF8Encoding]::new($false))
  $deadline=[DateTime]::UtcNow.AddSeconds(8);while(-not $helper.HasExited -and [DateTime]::UtcNow -lt $deadline){Pulse;Start-Sleep -Milliseconds 100}
  if(-not $helper.HasExited){throw 'Lua failure left adapter alive'};$report.checks+=@('Lua error closes owned CE and adapter without an active-loop leak')
 }elseif($TestMode -eq 'TargetChange'){
  if($SameExeTargetChange){
   $sameExeReady=Join-Path $run 'same-exe-ready.txt';$sameExeStop=Join-Path $run 'same-exe-stop'
   $sameExeData=Join-Path $run 'same-exe-memory.bin';$secondData=[byte[]]::new(1048576);[BitConverter]::GetBytes([int]1234567).CopyTo($secondData,0);[BitConverter]::GetBytes([int]1234567).CopyTo($secondData,4);[IO.File]::WriteAllBytes($sameExeData,$secondData)
   $sameExeFixture=Start-Process -FilePath $fixtureFile -ArgumentList @(('"'+$sameExeData+'"'),('"'+$sameExeReady+'"'),('"'+$sameExeStop+'"')) -WindowStyle Hidden -PassThru
   $until=[DateTime]::UtcNow.AddSeconds(5);while(-not[IO.File]::Exists($sameExeReady)-and[DateTime]::UtcNow-lt$until){Pulse;Start-Sleep -Milliseconds 50};if(-not[IO.File]::Exists($sameExeReady)){throw 'Second same-exe fixture did not start'}
   if($sameExeFixture.Id -eq $fixture.Id -or $sameExeFixture.Path -cne $fixture.Path){throw 'Same-exe fixture did not have distinct PID and identical executable path'}
   $report.sameExeTargetChange=$true;$report.originalTargetPid=$fixture.Id;$report.replacementTargetPid=$sameExeFixture.Id;$report.targetExecutable=$fixtureFile
   $script:targetOverride=@{valid=$true;generation=2;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$sameExeFixture.Id;processCreated=[string]$sameExeFixture.StartTime.ToUniversalTime().ToFileTimeUtc();path=$fixtureFile};Pulse
  }else{$script:targetOverride=@{valid=$true;generation=2;lastSeen=[DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds();pid=$PID;processCreated=[string]$owner.StartTime.ToUniversalTime().ToFileTimeUtc();path=$owner.Path};Pulse}
  for($tick=0;$tick -lt 5;$tick++){Pulse;Start-Sleep -Milliseconds 100};if($helper.HasExited){throw 'Foreground recognition change cancelled the pinned PID'}
   if($TargetChangeAfterResults){$view.Write(0,[int]731003);$view.Write(4,[int]731003);$pinned=Request next '731003'}else{$pinned=Request first '731001'}
   if($pinned.state.pid -ne $fixture.Id -or $pinned.state.processCreated -cne [string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc() -or $pinned.state.path -cne $fixtureFile){throw 'Search rebound to the foreground or same-exe replacement PID'}
   $pinnedWrite=Request writeAll '999999';if($view.ReadInt32(0)-ne999999-or$view.ReadInt32(4)-ne999999){throw 'Pinned original fields were not written'}
   if($SameExeTargetChange){$bytes=[byte[]]::new(8);$checkStream=[IO.FileStream]::new($sameExeData,[IO.FileMode]::Open,[IO.FileAccess]::Read,([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete));try{if($checkStream.Read($bytes,0,8)-ne8){throw 'Replacement fixture read incomplete'}}finally{$checkStream.Dispose()};if([BitConverter]::ToInt32($bytes,0)-ne1234567-or[BitConverter]::ToInt32($bytes,4)-ne1234567){throw 'Replacement same-exe process memory was modified'}}
   $report.pinnedPidPreserved=$true;$report.foregroundDidNotCancel=$true;$report.replacementMemoryUntouched=[bool]$SameExeTargetChange;$report.pinnedState=$pinnedWrite.state
   [IO.File]::WriteAllText((Join-Path $run 'fixture-stop'),'stop');if(-not $fixture.WaitForExit(5000)){throw 'Pinned target did not exit'}
   if(-not $helper.WaitForExit(12000)){throw 'Original pinned PID exit left CE alive'}
  $closedStatus=Read (Join-Path $session 'status.json');if($closedStatus.stage -cne 'closed' -or $closedStatus.count -ne 0 -or $closedStatus.rows.Count -ne 0 -or $closedStatus.lastWriteCount -ne 0 -or $closedStatus.searchValue -cne '' -or $closedStatus.lastWriteValue -cne ''){throw 'Target change closed the helper but retained previous results or write authority'}
  $report.closedResultsCleared=$true;$report.targetChangeAfterResults=[bool]$TargetChangeAfterResults;$report.checks+=@('Foreground target changes preserve the original pinned PID and scan results; only actual original PID exit closes CE and clears result/write authority')
 }elseif($TestMode -eq 'Lease'){
  if(-not $helper.WaitForExit(22000)){throw 'Expired frontend lease left adapter alive'};$report.checks+=@('Loss of YMCC heartbeat closes owned CE without touching other processes')
 }else{
  [IO.File]::WriteAllText((Join-Path $session 'stop'),'stop');if(-not $helper.WaitForExit(12000)){throw 'Adapter did not stop gracefully'}
 }
 if(Get-Process -Id $ceId -ErrorAction SilentlyContinue){throw 'Owned CE process leaked after adapter exit'};$report.ceExited=$true
 $closed=if($OwnershipCase -eq 'UnconfirmedLive'){$null}else{Read (Join-Path $session $(if($OwnershipCase -in @('PreCreateCrash','UnconfirmedLaunch') -or ($OwnershipCase -eq 'UnconfirmedWithoutGate' -and $LegacyOwnerGone)){'closed.fixture-retained.json'}else{'closed.json'}))}
 if($OwnershipCase -ne 'UnconfirmedLive'){
 if($closed.engine -cne 'cheat-engine' -or $closed.token -cne $token -or $closed.stage -cne 'closed' -or $closed.ceExited -isnot [bool] -or -not $closed.ceExited -or $closed.pid -ne $fixture.Id -or $closed.processCreated -cne [string]$fixture.StartTime.ToUniversalTime().ToFileTimeUtc() -or $closed.path -cne $fixtureFile){throw 'Owned CE release receipt missing or mismatched'}
 $report.releaseReceipt=$closed
 }else{$report.releaseReceiptProvenance='Original helper crashed before its CE exit; exact held process exit and recovered identity verified independently'}
 if($TestMode -eq 'RendererRestart' -and $OwnershipCase -eq 'UnconfirmedLive'){$report.releaseReceiptProvenance='Original helper crashed; exact held CE exit and recovered identity independently verified'}
 elseif($TestMode -eq 'RendererRestart' -and $OwnershipCase -in @('HelperCrash','LegacyAdoption')){$report.releaseReceiptProvenance='Deliberately injected false closed receipt; exact held CE exit independently verified before replacement';$report.checks+=@('Live exact CE overrides the injected false closed receipt after its owned adapter crashes')}
 else{$report.releaseReceiptProvenance='Actual adapter after exact CE exit';$report.checks+=@('Matching closed receipt is emitted after the owned CE actually exits')}
 $after=SettingsSnapshot;Json (Join-Path $run 'ce-settings-after.json') $after
 $firstBefore=@($before|Where-Object {$_.path -eq 'Software\Cheat Engine' -and $_.name -eq 'First Time User'})
 $firstAfter=@($after|Where-Object {$_.path -eq 'Software\Cheat Engine' -and $_.name -eq 'First Time User'})
 if($firstAfter.Count -ne 1 -or $firstAfter[0].kind -ne 'DWord' -or $firstAfter[0].data -ne 0){throw 'CE first-run initialization was not confirmed'}
 $report.firstRunInitialized=($firstBefore.Count -eq 0 -or $firstBefore[0].data -ne 0)
 $otherBefore=@($before|Where-Object {-not ($_.path -eq 'Software\Cheat Engine' -and $_.name -eq 'First Time User')})
 $otherAfter=@($after|Where-Object {-not ($_.path -eq 'Software\Cheat Engine' -and $_.name -eq 'First Time User')})
 $report.settingsPreserved=(($otherBefore|ConvertTo-Json -Depth 16 -Compress)-ceq($otherAfter|ConvertTo-Json -Depth 16 -Compress));if(-not$report.settingsPreserved){throw 'CE preferences changed beyond the authorized first-run DWORD; inspect exact before/after values'};$report.checks+=@('CE preferences remain byte-for-byte equivalent; only the first-run DWORD can initialize');$report.ok=$true
}catch{$report.error=$_.Exception.Message;$report.stack=$_.ScriptStackTrace}
finally{if($sameExeFixture){[IO.File]::WriteAllText($sameExeStop,'stop');$report.sameExeFixtureExited=$sameExeFixture.WaitForExit(5000);$sameExeFixture.Dispose()};if($externalCe){[IO.File]::WriteAllText($externalStop,'stop');$report.externalCeExitedAtCleanup=$externalCe.WaitForExit(5000);$externalCe.Dispose()};if([IO.Directory]::Exists($session)){[IO.File]::WriteAllText((Join-Path $session 'stop'),'stop')};if($secondSession-and[IO.Directory]::Exists($secondSession)){[IO.File]::WriteAllText((Join-Path $secondSession 'stop'),'stop')};if($helper){$report.adapterExited=$helper.WaitForExit(12000)};if($secondHelper){$report.secondAdapterExited=$secondHelper.WaitForExit(12000)};if($firstCeProcess){$report.firstCeExitedAtCleanup=$firstCeProcess.WaitForExit(1000);$firstCeProcess.Dispose()};if($secondCeProcess){$report.secondCeExitedAtCleanup=$secondCeProcess.WaitForExit(1000);$secondCeProcess.Dispose()};if($fixture){[IO.File]::WriteAllText((Join-Path $run 'fixture-stop'),'stop');$report.fixtureExited=$fixture.WaitForExit(5000)};if($view){$view.Dispose()};if($map){$map.Dispose()};$report.run=$run;$report.at=[DateTimeOffset]::Now.ToString('o');Json (Join-Path $out ('ce-fixture-'+$TestMode.ToLowerInvariant()+'-result.json')) $report
 if(-not $report.ok){Write-Error $report.error -ErrorAction Continue;exit 1}
 $report|ConvertTo-Json -Depth 3}
