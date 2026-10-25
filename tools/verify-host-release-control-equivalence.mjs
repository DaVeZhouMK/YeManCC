// Offline differential execution of actual extracted release code. Never builds
// or executes the production Host; only inert fixture projects are compiled.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const workspace=path.resolve(repo,'../../..');
const validation=path.join(workspace,'Build/Validation/VirtualInput-Win5-ROG-R101-20261016');
const options={out:path.join(validation,'R101/control-equivalence'),before:path.join(validation,'integration/Program.cs.before')};
for(let i=2;i<process.argv.length;i++){
 const a=process.argv[i];if(a==='--help'){console.log('node tools/verify-host-release-control-equivalence.mjs [--out DIR] [--before Program.cs.before]');process.exit(0)}
 if(a==='--out'||a==='--before'){assert(process.argv[i+1],a+' requires a path');options[a.slice(2)]=path.resolve(process.argv[++i]);}else throw Error('Unknown argument: '+a);
}
const out=options.out;fs.mkdirSync(out,{recursive:true});
const run=path.join(out,'runs',new Date().toISOString().replace(/[:.]/g,'-')+'-'+process.pid);fs.mkdirSync(run,{recursive:true});
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const json=(n,v)=>fs.writeFileSync(path.join(run,n),JSON.stringify(v,null,2)+'\n');
function source(file){const bytes=fs.readFileSync(file);return {file,bytes,text:bytes.toString('utf8'),sha256:hash(bytes)}}
const before=source(options.before),current=source(path.join(repo,'InputHost/Program.cs'));
const native=source(path.join(repo,'native/main.cpp'));
const fence=source(path.join(repo,'InputHost/MsiSteamDeckReleaseFence1726.cs'));
const template=source(path.join(repo,'tools/fixtures/host_release_control_equivalence.cs'));
const header=source(path.join(repo,'deps/json/json.hpp'));
const inputs=[before,current,native,fence,template,header];
// Blanks comments and ordinary C#/C++ string/character literals while retaining
// exact offsets and CR/LF. Selected members contain no raw/verbatim strings.
function mask(s){return s.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'/g,m=>m.replace(/[^\r\n]/g,' '));}
function range(s,name,start,end,kind='method'){
 const code=s.text.slice(start,end);
 return {name,kind,source:s.file,source_sha256:s.sha256,start_line:s.text.slice(0,start).split('\n').length,end_line:s.text.slice(0,end).split('\n').length,start_utf16:start,end_utf16_exclusive:end,start_utf8_byte:Buffer.byteLength(s.text.slice(0,start)),end_utf8_byte_exclusive:Buffer.byteLength(s.text.slice(0,end)),extracted_sha256:hash(Buffer.from(code)),code};
}
function extract(s,name,kind='method'){
 const rx=kind==='type'?new RegExp('^[ \\t]*(?:private|internal|public) (?:readonly record struct|enum) '+name+'\\b','m'):new RegExp('^[ \\t]*(?:private|internal|public|static)[^\\r\\n]*\\b'+name+'\\s*\\(','m');
 const m=rx.exec(s.text);assert(m,'Missing actual member '+name+' in '+s.file);
 const masked=mask(s.text),open=masked.indexOf('{',m.index);assert(open>=0,'No member body '+name);
 let depth=0,end=-1;for(let i=open;i<masked.length;i++){if(masked[i]==='{')depth++;else if(masked[i]==='}'&&--depth===0){end=i+1;break}}
 assert(end>open,'Unclosed actual member '+name);return range(s,name,m.index,end,kind);
}
function constant(s,name){const rx=new RegExp('^[ \\t]*private const int '+name+' = \\d+;','m'),m=rx.exec(s.text);assert(m,'Missing constant '+name);return range(s,name,m.index,m.index+m[0].length,'constant')}
function releaseCases(s){const marker='                case "RELEASE_TARGET":',start=s.text.indexOf(marker);assert(start>=0);assert.equal(s.text.indexOf(marker,start+1),-1,'Ambiguous release cases');const end=s.text.indexOf('                case "HIDHIDE_CYCLE_BEGIN":',start);assert(end>start);return range(s,'RELEASE_TARGET + SHUTDOWN switch cases',start,end,'switch-cases')}
const members=['ReleaseLocal','TryStopSteamDeckIdleForRelease','ReleaseLocalLocked','WaitForVirtualEndpointRemoval'];
const extracts={before:members.map(n=>extract(before,n)),current:members.map(n=>extract(current,n))};
for(const [label,s]of [['before',before],['current',current]]){
 extracts[label].push(extract(s,'VirtualEndpointObservation','type'),extract(s,'Phase','type'),constant(s,'VirtualEndpointRemovalTimeoutMs'),constant(s,'VirtualEndpointPollIntervalMs'),releaseCases(s));
}
for(const n of ['VirtualEndpointObservation','Phase','VirtualEndpointRemovalTimeoutMs','VirtualEndpointPollIntervalMs','WaitForVirtualEndpointRemoval'])assert.equal(extracts.before.find(x=>x.name===n).code,extracts.current.find(x=>x.name===n).code,n+' changed beyond timing scope');
const nativeFunction=extract(native,'inputHostReleaseTimingReceiptLog');
const exactExpected=`static void inputHostReleaseTimingReceiptLog(json& row, const json& response, const char* command) noexcept {
 try {
 if (!command || (std::strcmp(command, "RELEASE_TARGET") != 0 && std::strcmp(command, "SHUTDOWN") != 0)) return;
 const auto found = response.find("releaseTimingEvidence");
 if (found != response.end() && found->is_object()) row["releaseTimingEvidence"] = *found;
 } catch (...) { }
}`;
const compact=s=>s.replace(/\/\/[^\r\n]*|\/\*[\s\S]*?\*\//g,'').replace(/\s+/g,'');
assert.equal(compact(nativeFunction.code),compact(exactExpected),'Native diagnostic helper has unexpected actions/control consumers');
const callMatches=[...native.text.matchAll(/^[ \t]*inputHostReleaseTimingReceiptLog\((acceptedLine|rejectedLine), response, command\);/gm)];
assert.equal(callMatches.length,2,'Require both accepted/rejected diagnostic calls');assert.deepEqual(new Set(callMatches.map(m=>m[1])),new Set(['acceptedLine','rejectedLine']));
const calls=callMatches.map(m=>{const r=range(native,m[1],m.index,m.index+m[0].length,'call-site');assert(/^\s*inputHostCommandResultLog\(/.test(native.text.slice(m.index+m[0].length)),'Receipt append must be immediately followed by log sink');return r});
const hits=[];function nativeFiles(dir){for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.isDirectory()){if(!['bin','obj','deps','vendor','node_modules','.git'].includes(e.name))nativeFiles(path.join(dir,e.name));}else if(/\.(?:cpp|h|hpp)$/.test(e.name)){const f=path.join(dir,e.name),t=f===native.file?native.text:fs.readFileSync(f,'utf8');for(const m of t.matchAll(/\breleaseTimingEvidence\b|\binputHostReleaseTimingReceiptLog\b/g))hits.push({file:f,index:m.index,token:m[0],line:t.slice(0,m.index).split('\n').length});}}}
nativeFiles(path.join(repo,'native'));
for(const h of hits){assert.equal(h.file,native.file,'Unexpected native consumer in '+h.file);assert((h.index>=nativeFunction.start_utf16&&h.index<nativeFunction.end_utf16_exclusive)||calls.some(c=>h.index>=c.start_utf16&&h.index<c.end_utf16_exclusive),'Unexpected release-timing native consumer at '+h.line)}
const allRanges=[...extracts.before,...extracts.current,nativeFunction,...calls];
fs.mkdirSync(path.join(run,'extracted'),{recursive:true});
for(const [label,rs]of Object.entries(extracts))for(const r of rs)fs.writeFileSync(path.join(run,'extracted',label+'-'+r.name.replace(/[^a-zA-Z0-9_-]/g,'_')+'.cs.txt'),r.code);
fs.writeFileSync(path.join(run,'extracted/native-receipt.cpp.txt'),nativeFunction.code);
json('source-ranges.json',{inputs:inputs.map(({text,bytes,...s})=>({...s,bytes:bytes.length})),ranges:allRanges.map(({code,...r})=>r),substitutions:['Only Environment.TickCount64 -> Fixture.Now and Thread.Sleep -> Fixture.Sleep in extracted members/cases; exact original slices are retained.','Three release functions and real RELEASE/SHUTDOWN cases are otherwise unchanged.','Actual WaitForVirtualEndpointRemoval, endpoint observation record and shared actual MSI fence are executed against fake observations/clock.','SDK/controller/context, optional pre-existing diagnostics, report-chain writer, Ack/Error serialization and idempotent sink are inert action recorders.','HostReleaseTimingEvidence is a recording/null no-throw stub; actual collector correctness/serialization is not claimed.'],productionHostBuilt:false,productionHostExecuted:false,hardwareSdkDeviceServiceRegistryAccess:false});
const cases=[];const add=(Name,v={})=>cases.push({Name,...v});
for(const Msi of [false,true]){
 const prefix=Msi?'msi':'non-msi',base={Msi,Persona:Msi?'steamdeck':'elite'};
 for(const [name,v]of [
 ['success',{}],['no-objects',{Controller:false,Context:false}],['no-controller',{Controller:false}],['no-context',{Context:false}],
 ['neutral-false',{Neutral:'false'}],['neutral-blank',{Neutral:'false-blank'}],['neutral-throw',{Neutral:'throw'}],
 ['controller-dispose-throw',{ControllerThrow:true}],['remove1-throw',{Remove1Throw:true}],['remove2-throw',{Wait1:'present',Remove2Throw:true}],
 ['wait1-fail-retry-success',{Wait1:'present'}],['wait1-throw',{Wait1:'throw'}],['wait1-probe-failed',{Wait1:'probe-failed'}],
 ['wait2-fail',{Wait1:'present',Wait2:'present'}],['wait2-throw',{Wait1:'present',Wait2:'throw'}],['wait2-probe-failed',{Wait1:'present',Wait2:'probe-failed'}],
 ['context-dispose-throw',{ContextThrow:true}],['ordered-errors',{Neutral:'false',ControllerThrow:true,Remove1Throw:true,ContextThrow:true}],
 ['wait-retry-errors',{Neutral:'throw',Wait1:'present',Wait2:'present',ContextThrow:true}],['diagnostics-off',{Diagnostic:false,CostDiagnostic:false,Lifetime:false}],
 ['unexpected-report-close-throw',{ReportCloseThrow:true}],['internal-release',{Internal:true}],['shutdown-direct',{Command:'SHUTDOWN'}],
 ['release-then-shutdown',{Sequence:true}],['failed-release-then-shutdown',{Sequence:true,Wait1:'present',Wait2:'present'}],
 ['neutral-failed-then-shutdown',{Sequence:true,Neutral:'false'}]])add(prefix+'-'+name,{...base,...v});
}
for(const [name,v]of [
 ['steamdeck-idle-success',{}],['steamdeck-idle-failure',{IdleThrow:true}],['steamdeck-idle-failure-then-shutdown',{IdleThrow:true,Sequence:true}],
 ['steamdeck-no-controller',{Controller:false,IdleThrow:true}],['tuple-null',{Tuple:false}],['non-msi-steam-only-leftover',{OnlySteamRoots:true}],
 ['no-controller-context-throw',{Controller:false,ContextThrow:true}],['no-context-neutral-throw',{Context:false,Neutral:'throw'}],
 ['non-release-command-no-collector',{Command:'FRAME'}]])add(name,{Persona:'steamdeck',...v});
for(const Cache of ['success','failure'])for(const CachedObserveThrow of [false,true])add('msi-cached-'+Cache+(CachedObserveThrow?'-probe-throw':''),{Msi:true,Persona:'steamdeck',Cache,Controller:false,Context:false,CachedObserveThrow,Command:'SHUTDOWN'});
add('msi-idle-failure',{Msi:true,Persona:'steamdeck',IdleThrow:true});
add('non-msi-idle-failure',{Persona:'steamdeck',IdleThrow:true});
json('cases.json',cases);
function runProcess(label,command,args,cwd){
 const result=spawnSync(command,args,{cwd,encoding:'utf8',timeout:60000,env:{...process.env,DOTNET_CLI_TELEMETRY_OPTOUT:'1',DOTNET_SKIP_FIRST_TIME_EXPERIENCE:'1',DOTNET_NOLOGO:'1',NUGET_XMLDOC_MODE:'skip'}});
 fs.writeFileSync(path.join(run,label+'.stdout.txt'),result.stdout??'');fs.writeFileSync(path.join(run,label+'.stderr.txt'),result.stderr??'');
 json(label+'.command.json',{command,args,cwd,status:result.status,error:result.error?String(result.error):null});
 assert(!result.error&&result.status===0,label+' failed; see '+path.join(run,label+'.stdout.txt')+' '+(result.error??result.stderr??''));return result.stdout??'';
}
const escapeXml=s=>s.replaceAll('&','&amp;').replaceAll('"','&quot;').replaceAll('<','&lt;');
function generate(label){
 const dir=path.join(run,label);fs.mkdirSync(dir,{recursive:true});const rs=extracts[label],get=n=>rs.find(x=>x.name===n).code;
 const clock=s=>s.replaceAll('Environment.TickCount64','Fixture.Now').replaceAll('Thread.Sleep','Fixture.Sleep');
 let code=template.text;
 const parts={CONSTANTS:get('VirtualEndpointRemovalTimeoutMs')+'\n'+get('VirtualEndpointPollIntervalMs'),OBSERVATION:get('VirtualEndpointObservation'),PHASE:get('Phase'),WAIT:clock(get('WaitForVirtualEndpointRemoval')),RELEASE_METHODS:['ReleaseLocal','TryStopSteamDeckIdleForRelease','ReleaseLocalLocked'].map(n=>clock(get(n))).join('\n'),RELEASE_CASES:clock(get('RELEASE_TARGET + SHUTDOWN switch cases')),INVOKE_RELEASE:label==='before'?'ok=ReleaseLocal(out detail);':'ok=Fixture.C.Internal ? ReleaseLocal(out detail) : ReleaseLocal(out detail,e);'};
 for(const [k,v]of Object.entries(parts)){assert(code.includes('@@'+k+'@@'),'Missing template marker '+k);code=code.replace('@@'+k+'@@',()=>v)}assert(!/@@\w+@@/.test(code));
 fs.writeFileSync(path.join(dir,'Fixture.cs'),code);fs.writeFileSync(path.join(dir,'ActualMsiFence.cs'),fence.bytes);
 fs.writeFileSync(path.join(dir,'Fixture.csproj'),'<Project Sdk="Microsoft.NET.Sdk"><PropertyGroup><OutputType>Exe</OutputType><TargetFramework>net10.0</TargetFramework><ImplicitUsings>enable</ImplicitUsings><Nullable>enable</Nullable><EnableDefaultCompileItems>false</EnableDefaultCompileItems><NuGetAudit>false</NuGetAudit><RestoreIgnoreFailedSources>true</RestoreIgnoreFailedSources><NoWarn>CS0169;CS0414</NoWarn></PropertyGroup><ItemGroup><Compile Include="Fixture.cs"/><Compile Include="ActualMsiFence.cs"/></ItemGroup></Project>');
 fs.writeFileSync(path.join(dir,'NuGet.Config'),'<configuration><packageSources><clear /></packageSources></configuration>');
 runProcess(label+'-build','dotnet',['build','Fixture.csproj','-c','Release','--nologo'],dir);
 return {dir,dll:path.join(dir,'bin/Release/net10.0/Fixture.dll')};
}
const original=generate('before'),candidate=generate('current');
const baseline=JSON.parse(runProcess('before-execute','dotnet',[original.dll,path.join(run,'cases.json'),'recording'],original.dir));
const results={};
for(const mode of ['recording','unavailable']){
 const got=JSON.parse(runProcess('current-'+mode+'-execute','dotnet',[candidate.dll,path.join(run,'cases.json'),mode],candidate.dir));assert.equal(got.length,cases.length);
 const checks=[];
 for(let i=0;i<cases.length;i++){
  assert.equal(got[i].name,baseline[i].name);assert.deepEqual(got[i].business,baseline[i].business,'Control-flow mismatch '+got[i].name+' mode='+mode);
  const timeline=got[i].timing;const stack=[];let begin=0,end=0;
  for(const event of timeline){if(event.startsWith('begin:')){stack.push(event.slice(6));begin++}else if(event.startsWith('end:')){assert.equal(stack.pop(),event.slice(4),'Unbalanced timing finally '+got[i].name);end++}}
  assert.equal(stack.length,0,'Missing timing finally '+got[i].name);
  if(mode==='recording'){
   assert.equal(timeline.filter(x=>x==='finish').length,timeline.filter(x=>x==='create:recording').length,'Missing Finish '+got[i].name);
   if(cases[i].Internal||cases[i].Command==='FRAME')assert.equal(timeline.length,0,'Collector allocated outside release command');
  }else assert(timeline.every(x=>x==='create:unavailable'),'Unavailable collector called');
  checks.push({name:got[i].name,mode,passed:true,business_sha256:hash(JSON.stringify(got[i].business)),action_count:got[i].business.actions.length,timing_stages_started:begin,timing_stages_ended:end,step_results:got[i].business.steps});
 }
 results[mode]=checks;json('comparison-'+mode+'.json',checks);
}
// Explicit semantic witnesses ensure both sides did not merely skip a branch.
const byName=new Map(baseline.map(r=>[r.name,r]));
const direct=n=>byName.get(n).business.steps[0];
assert.equal(direct('non-msi-success').detail,'local-dispose-complete');
assert.match(direct('non-msi-steam-only-leftover').detail,/local-dispose-complete/,'Existing generic absence semantics changed');
assert.equal(direct('msi-cached-failure').ok,false);assert.equal(direct('msi-cached-success').ok,true);
assert.match(direct('non-msi-neutral-false').detail,/^neutral:fixture-neutral$/);
assert.equal(direct('non-msi-neutral-blank').detail,'neutral:failed');
assert.equal(direct('non-msi-ordered-errors').detail,'neutral:fixture-neutral;controller-dispose:InvalidOperationException;virtual-endpoint-reconcile:IOException;context-dispose:ApplicationException');
assert.equal(direct('non-msi-wait1-fail-retry-success').removes,2);assert.equal(direct('non-msi-wait1-fail-retry-success').clock,13500);
assert.equal(direct('non-msi-wait2-fail').removes,2);assert.equal(direct('non-msi-wait2-fail').clock,17000);
assert.equal(direct('msi-idle-failure').controller,true);assert.equal(direct('msi-idle-failure').context,true);assert.equal(direct('msi-idle-failure').removes,0);assert.equal(direct('msi-idle-failure').phase,'Quiesced');
assert.equal(byName.get('non-msi-release-then-shutdown').business.steps[1].shutdown,true);
assert.equal(byName.get('msi-failed-release-then-shutdown').business.steps[1].shutdown,true);
assert.equal(JSON.parse(byName.get('msi-failed-release-then-shutdown').business.steps[1].response).ok,false);
const nativeDir=path.join(run,'native');fs.mkdirSync(nativeDir,{recursive:true});fs.writeFileSync(path.join(nativeDir,'json.hpp'),header.bytes);
const cpp=`#include "json.hpp"
#include <cstring>
#include <iostream>
#include <vector>
#include <stdexcept>
#include <type_traits>
using json=nlohmann::json;
${nativeFunction.code}
static void check(bool ok){if(!ok)throw std::runtime_error("native fixture assertion");}
int main(){
 static_assert(noexcept(inputHostReleaseTimingReceiptLog(std::declval<json&>(),std::declval<const json&>(),nullptr)));
 static_assert(std::is_same_v<decltype(inputHostReleaseTimingReceiptLog(std::declval<json&>(),std::declval<const json&>(),nullptr)),void>);
 std::vector<const char*> commands={nullptr,"","RELEASE_TARGET","SHUTDOWN","FRAME","PREPARE_TARGET","release_target"};
 std::vector<json> payloads={nullptr,false,7,"text",json::array({1,2}),json::object(),json{{"totalUs",11},{"localReleaseOk",false},{"absentVerified",true}},json{{"nested",json::array({json{{"k","v"}}})}}};
 int tests=0;
 for(bool accepted:{false,true})for(const char* command:commands)for(bool present:{false,true})for(const json& payload:payloads){
  json response={{"ok",accepted},{"status",accepted?"accepted":"rejected"},{"targetPnpAdmitted",false}};
  if(present)response["releaseTimingEvidence"]=payload;
  const json unchanged=response;
  json row={{"ok",accepted},{"route",accepted?"acceptedLine":"rejectedLine"},{"detail","unchanged"}};const json original=row;
  inputHostReleaseTimingReceiptLog(row,response,command);
  bool shouldCopy=command&&(std::strcmp(command,"RELEASE_TARGET")==0||std::strcmp(command,"SHUTDOWN")==0)&&present&&payload.is_object();
  check(response==unchanged);check(row["ok"]==accepted);check(row["detail"]=="unchanged");
  if(shouldCopy){check(row["releaseTimingEvidence"]==payload);row.erase("releaseTimingEvidence");}
  check(row==original);tests++;
 }
 for(json row:{json(4),json::array({1,2}),json(nullptr)}){const json original=row;const json response={{"releaseTimingEvidence",json{{"totalUs",1}}}};inputHostReleaseTimingReceiptLog(row,response,"RELEASE_TARGET");if(original.is_null())check(row.contains("releaseTimingEvidence"));else check(row==original);tests++;}
 for(json response:{json(4),json::array({1,2}),json(nullptr)}){json row={{"ok",true}};const json original=row;inputHostReleaseTimingReceiptLog(row,response,"SHUTDOWN");check(row==original);tests++;}
 std::cout<<json{{"passed",true},{"tests",tests},{"actualExtractedFunction",true},{"acceptedAndRejectedRows",true},{"productExecuted",false}}.dump()<<std::endl;
}
`;
fs.writeFileSync(path.join(nativeDir,'fixture.cpp'),cpp);
const vcvars=process.env.YMCC_VCVARS64??'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Auxiliary/Build/vcvars64.bat';assert(fs.existsSync(vcvars),'MSVC vcvars64 not found; set YMCC_VCVARS64');
const quoted=s=>{assert(!/["\r\n]/.test(s));return '"'+s+'"'};
const cmdFile=path.join(nativeDir,'compile.cmd');fs.writeFileSync(cmdFile,'@echo off\r\ncall '+quoted(vcvars)+' >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /O2 '+quoted(path.join(nativeDir,'fixture.cpp'))+' /I'+quoted(nativeDir)+' /Fo'+quoted(path.join(nativeDir,'fixture.obj'))+' /Fe'+quoted(path.join(nativeDir,'fixture.exe'))+'\r\nexit /b %errorlevel%\r\n');
runProcess('native-build','cmd.exe',['/d','/c',cmdFile],nativeDir);
const nativeResult=JSON.parse(runProcess('native-execute',path.join(nativeDir,'fixture.exe'),[],nativeDir));assert(nativeResult.passed);
json('native-validation.json',{...nativeResult,function:(({code,...r})=>r)(nativeFunction),calls:calls.map(({code,...r})=>r),nativeSourceConsumers:hits,strictFunctionTokenWhitelist:true,noOtherTimingConsumersInNative:true,limits:['Extracted function is executed; production transport/caller function is not executed.','Both actual call sites are structurally verified as standalone logging-only calls immediately before the log sink.','Exception swallowing is exercised with invalid destination row types; allocator exhaustion is not injected.']});
const endHashes=inputs.map(s=>({file:s.file,initial:s.sha256,final:hash(fs.readFileSync(s.file))}));assert(endHashes.every(s=>s.initial===s.final),'Source changed concurrently; results are frozen but rerun required');
const summary={passed:true,run,case_count:cases.length,differential_comparisons:cases.length*2,collector_modes:['recording-no-side-effects','TryCreate-unavailable'],native_tests:nativeResult.tests,productionHostBuilt:false,productionHostExecuted:false,hardwareSdkDeviceServiceRegistryAccess:false,inputs:inputs.map(({text,bytes,...s})=>({...s,bytes:bytes.length})),source_stability:endHashes,coverage:cases.map(c=>c.Name),reports:{ranges:path.join(run,'source-ranges.json'),before:path.join(run,'before-execute.stdout.txt'),current:path.join(run,'current-recording-execute.stdout.txt'),comparisons:path.join(run,'comparison-recording.json'),native:path.join(run,'native-validation.json')},limits:['Conditional control-flow equivalence under inert SDK/PnP/diagnostic stand-ins and a no-throw collector API contract; not a formal proof for all possible external behaviors.','Actual collector timing/clock/serialization behavior is delegated to the parallel helper agent; this fixture uses a recording/null stub.','Real wall-clock durations, lock contention, SDK/OS thread races, hardware removal and physical return are not tested.','Ack/Error serialization is an inert shim; real release and shutdown switch branch selection/status strings are extracted.']};
json('summary.json',summary);fs.writeFileSync(path.join(out,'summary.json'),JSON.stringify(summary,null,2)+'\n');
const fileLink=(file,line)=>'['+path.basename(file)+(line?':'+line:'')+']('+file.replaceAll('\\','/')+(line?':'+line:'')+')';
const report=`R101 release 控制流离线对照：**PASS**。${cases.length} 个场景 × 2 个 collector 模式，共 ${cases.length*2} 次实际 C# 对照；native 提取函数 ${nativeResult.tests} 个本地 JSON 用例通过。未构建/运行生产 Host，无硬件、SDK、设备、服务或注册表操作。\n\n原始 Program SHA-256：\`${before.sha256}\`；当前 Program SHA-256：\`${current.sha256}\`；native/main.cpp SHA-256：\`${native.sha256}\`。每个实际提取成员的起止行、UTF-8 字节范围、片段 SHA-256 及源文件 SHA-256 见 ${fileLink(path.join(run,'source-ranges.json'))}。所有输入在结束时复核未变。\n\n提取执行：ReleaseLocal、TryStopSteamDeckIdleForRelease、ReleaseLocalLocked；另外执行原样的等待函数、端点 observation 类型、真实 RELEASE_TARGET/SHUTDOWN 分支及共同的实际 MSI fence。只将现有时钟/Sleep 替换为虚拟时钟；精确原始片段保存在 extracted/。\n\n覆盖非 MSI/MSI fence、有/无 controller/context、idle-stop 失败、neutral false/空 detail/throw、controller Dispose throw、Remove1/2 throw、wait1/2 失败/探针失败/throw、context Dispose throw、组合错误顺序、MSI 缓存成功/失败与失败后的复查、RELEASE 后无对象 SHUTDOWN、内部释放不分配 collector 等。逐项比较返回值、detail、异常、phase/pending、对象存在状态、fence 证据、动作顺序、重试次数及虚拟超时。一次等待 3500 ms，重试最多两次；失败两轮为 7000 ms。原有非 MSI 仅 Steam roots 残留的判定也保持，诊断未升级为物理归还判据。\n\n当前 collector 在 recording 和 unavailable 两模式均与原始业务输出完全相等。记录模式检查所有已 Begin 的 stage 均 End，成功/失败/异常退出均有 Finish。collector 是无副作用、无异常的记录替身；helper 自身时钟、异常隔离和序列化由并行 agent 验证，不能把此结果表述为实际 helper 已验证。\n\nNative 从实际函数提取，不重写实现。严格 token 白名单约束其仅按命令复制对象到日志行、void/noexcept 并捕获异常；实际 acceptedLine 与 rejectedLine 两处调用均检查，扫描 native C++/header 无其他消费者。本地 nlohmann JSON 测试涵盖两种行、两个 release 命令、无关/空命令、字段缺失/错误类型/嵌套对象、无效日志行引发的异常吞没；业务 ok/status 与 response 不变。实际 native 调用者仅作源检查，未执行传输流程；未注入分配器耗尽。\n\n完整结果：${fileLink(path.join(out,'summary.json'))}；C# 逐例对照：${fileLink(path.join(run,'comparison-recording.json'))}；原始实际轨迹：${fileLink(path.join(run,'before-execute.stdout.txt'))}；当前实际轨迹：${fileLink(path.join(run,'current-recording-execute.stdout.txt'))}；native 证据：${fileLink(path.join(run,'native-validation.json'))}。\n\n边界：这些是无硬件条件下的控制流回归，不覆盖真实墙钟开销、锁竞争、系统/SDK 并发竞态、物理端点消失或完整 Host serializer。\n`;
fs.writeFileSync(path.join(out,'REPORT.md'),report);fs.writeFileSync(path.join(run,'REPORT.md'),report);
console.log(JSON.stringify({passed:true,case_count:cases.length,comparisons:cases.length*2,native_tests:nativeResult.tests,summary:path.join(out,'summary.json'),report:path.join(out,'REPORT.md'),run},null,2));
