import {originalReadyConsumer} from './owned-run.mjs';
import {simulationCorePath,selectedNativeTools} from '../release-host-environment.mjs';
// Independent sidecar; no parent registration or source changes are performed.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
import {OWNED_SOURCES,OWNED_INPUTS,ownedToolDeclaration} from './owned-adapter.mjs';
import {lockBundledNodeTools,verifyBundledToolScope} from '../bundled-node-tool-authority.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..');
const rel='tools/ymcc_sim/global/decky-persistent-lifecycle';
const entry=rel+'/typed-legacy-runtime-regression';
const legacyRel='tools/decky_sidebar_runtime_selftest.cpp';
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const key=file=>path.resolve(file).toLowerCase();
const inside=(base,file)=>key(file).startsWith(key(base)+path.sep);
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const write=(file,data)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(data,null,2)+'\n');};
const pin=file=>({path:path.resolve(file),sha256:sha(file),bytes:fs.statSync(file).size});
export function originalAssertions(text) {
 const main=text.indexOf('int main()');assert(main>=0,'LEGACY_MAIN_CHANGED');
 const rows=[...text.slice(main).matchAll(/check\(([\s\S]*?),\s*"([^"\r\n]+)"\s*\);/g)].map((m,i)=>({
  ordinal:i+1,label:m[2],legacyCall:m[0],legacyLine:text.slice(0,main+m.index).split('\n').length,
  group:i<8?'environment':i<27?'no-launch':i<33?'cancellation':'handoff'}));
 assert.equal(rows.length,38,'ORIGINAL_ASSERTION_MEMBERSHIP_CHANGED');return rows;
}
function zero(s) {
 return s?.fullyDestroyed===true&&s.handles===0&&s.pendingIo===0&&s.activeCalls===0&&s.jobActiveProcesses===0&&s.childExited===true&&
  s.ownerHandles===0&&s.ownerPendingIo===0&&s.readerHandles===0&&s.readerPendingIo===0&&s.clientHandles===0;
}
export function validateRuntimeResult(raw,expected) {
 assert.equal(raw.schema,'ymcc.typed-legacy-runtime-regression.v1');assert.equal(raw.productCallbackMode,'typed-only; legacy macro absent');
 assert.equal(raw.fullPersistentAcceptance,false);assert.equal(raw.officialLoaderExecuted,false);assert.equal(raw.realSteamExecuted,false);assert.equal(raw.physicalSleepPerformed,false);
 assert.equal(raw.passed,true,'RAW_FAILED');assert.deepEqual(raw.assertions.map(r=>r.label),expected.map(r=>r.label),'ORDERED_ORIGINAL_ASSERTIONS_REQUIRED');
 assert(raw.assertions.every(r=>r.passed===true),'ORIGINAL_ASSERTION_FAILED');
 assert.equal(raw.elapsedMs<15000,true,'ORIGINAL_NATIVE_BUDGET');
 assert.equal(raw.parentHandlesAfter,raw.parentHandlesBefore,'PARENT_HANDLE_EQUALITY');assert.equal(raw.parentThreadsAfter,raw.parentThreadsBefore,'PARENT_THREAD_EQUALITY');
 assert(Array.isArray(raw.coldContextCalibration)&&raw.coldContextCalibration.length===2,'COLD_INITIALIZATION_PROOF_REQUIRED');
 const [cold,repeat]=raw.coldContextCalibration;
 assert.equal(cold.naturalExit,true);assert.equal(repeat.naturalExit,true);assert.equal(repeat.beforeHandles,repeat.afterHandles,'COLD_INITIALIZATION_NOT_STABLE');
 assert.equal(cold.afterHandles,repeat.beforeHandles);assert.equal(repeat.afterHandles,raw.parentHandlesBefore);
 const delta=cold.afterHandles-cold.beforeHandles;assert(delta===0||delta===2,'UNEXPLAINED_COLD_CONTEXT_RESOURCE');
 const identity=row=>[row.handle,row.type,row.access].join(':');
 const oldIdentities=new Set(cold.before.map(identity)),newIdentities=new Set(cold.after.map(identity));
 assert(cold.before.every(row=>newIdentities.has(identity(row))),'COLD_CONTEXT_LOST_BASELINE_HANDLES');
 const added=cold.after.filter(row=>!oldIdentities.has(identity(row)));
 assert.equal(added.length,delta);if(delta===2)assert.deepEqual(added.map(r=>[r.type,r.access]).sort(),[['EtwRegistration',2052],['Key',8]].sort(),'COLD_CONTEXT_DELTA_CHANGED');
 assert.deepEqual(raw.ownedContextExits.map(r=>r.scenario),['typed-cancellation','typed-handoff'],'CONTEXT_WITNESSES_REQUIRED');
 assert(raw.ownedContextExits.every(r=>r.exitedAfterOriginalContextDestructor===true),'OWNED_CONTEXT_STILL_ALIVE');
 assert(raw.scenarios.every(r=>r.passed!==false&&!r.error),'UNEXPECTED_SCENARIO_ERROR');
 const ids=['current.no-launch-environment-and-state','current.resource-and-real-owned-binding-cancellation','current.real-owned-generation-token-handoff'];
 assert.deepEqual(raw.scenarios.map(r=>r.id),ids,'SCENARIO_ERRORS_OR_MISSING');
 const cancellation=raw.scenarios[1].measurements,handoff=raw.scenarios[2].measurements;
 assert(cancellation.resourcePreparation.offMs<1000&&cancellation.typedOffMs<1000,'ORIGINAL_OFF_DEADLINE');
 assert(cancellation.typedBeforeOff.pendingIo>0&&cancellation.typedBeforeOff.jobActiveProcesses>0&&cancellation.typedBeforeOff.handles>0,'NO_REAL_PENDING_OWNED_OPERATION');
 assert.equal(cancellation.ownedChildExited,true);assert.equal(cancellation.fixtureReleaseUnsent,true);assert(cancellation.cancelApiCalls>0);
 assert(cancellation.newEpoch>cancellation.oldEpoch&&cancellation.plans===2,'CANCEL_FRESH_TOKEN');
 for(const name of ['entered','oneOperationCanceled','noLaunch','freshOperation','steamExitWait'])assert.equal(cancellation.resourcePreparation[name],true,name);
 assert.equal(handoff.cancelGateEntered,true);assert.equal(handoff.cancelGateTimedOut,false);assert.equal(handoff.forwardedCancelCalls,1);
 assert(handoff.forwardedCancelResult===true||(handoff.forwardedCancelResult===false&&handoff.forwardedCancelLastError===1168),'UNEXPECTED_FORWARDED_CANCEL_ERROR');
 assert.equal(handoff.predecessorExitedWhileCancelGated,true);assert.equal(handoff.startsWhileGated,1);assert.equal(handoff.serializationObservationMs,200);
 assert(handoff.newEpoch>handoff.oldEpoch);assert.equal(handoff.replacementAttached,true);assert.equal(handoff.requests.length,2);
 assert(handoff.requests[0].generation<handoff.requests[1].generation&&handoff.requests[0].id!==handoff.requests[1].id);
 assert(handoff.requests.every(r=>r.enabled===true&&r.canceled===false),'CANCELED_TOKEN_RENEWED');
 for(const s of [raw.scenarios[0].atExit,cancellation.typedAtExit,cancellation.resourcePreparation.atExit,handoff.atExit]){
  assert.equal(s.phase,'disabled');assert.equal(s.pid,0);assert.equal(s.ownedBindingDestroyed,true);assert.equal(s.ownedBindingCleanupError,0);assert(zero(s.client),'ACTUAL_RESOURCE_RESIDUE');
 }
 assert.deepEqual(raw.negativeControls.map(r=>r.id),['negative.original-enable-cancel-epoch-fences','negative.original-refresh-stop-off-epoch-sequence','negative.original-reject-wrong-id','negative.original-reject-wrong-generation']);
 for(const n of raw.negativeControls){assert.equal(n.passed,true,'NEGATIVE_FAILED:'+n.id);assert(zero(n.atExit??n),'NEGATIVE_RESOURCE_RESIDUE');if(n.id.includes('-reject-'))assert(n.reason&& !n.reason.includes('b'.repeat(64)),'NEGATIVE_REASON_REQUIRED');}
 return {originalAssertions:38,passedAssertions:38,negativeControls:4,orderedMapping:true,currentTypedRuntimeSemantics:true,legacyCallbackAbiExecuted:false,fullPersistentAcceptance:false};
}
export async function execute(argv) {
 const get=name=>{const i=argv.indexOf(name);return i<0?null:argv[i+1];};
 const core=path.resolve(simulationCorePath("")),out=path.resolve(get('--evidence-dir')??''),work=path.resolve(get('--workspace-dir')??'');
 assert(get('--evidence-dir')&&get('--workspace-dir'),'EXPLICIT_CORE_PATHS_REQUIRED');
 assert(inside(path.join(core,'Evidence/DeckyTypedLegacyRuntime-20261023'),out),'EVIDENCE_SCOPE');
 assert(inside(path.join(core,'Workspace/DeckyTypedLegacyRuntime-20261023'),work),'WORKSPACE_SCOPE');
 assert(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');assert(fs.statSync(work).isDirectory()&&fs.readdirSync(work).length===0,'EMPTY_OWNED_WORK_REQUIRED');
 assert.equal(key(fs.realpathSync(work)),key(work),'WORKSPACE_REPARSE');fs.mkdirSync(out,{recursive:true});assert.equal(key(fs.realpathSync(out)),key(out),'EVIDENCE_REPARSE');
 const started=Date.now(),sources=new Map(),tools=new Map(),frozen=new Map();
 const report={schemaVersion:1,id:'sidecar.decky-typed-legacy-runtime-regression',startedAt:new Date().toISOString(),status:'FAIL',passed:false,fullPersistentAcceptance:false,
  budgets:{compileMs:60000,nativeMs:15000,outerTypedGateMs:135000,reference:'original parent run.mjs runtime regression compile60/native15; original typed wrapper total135; no cumulative reset'},
  maxArtifactInputs:3072,legacyOriginalRegression:{source:legacyRel,status:'NOT_RUN',historicalVerdictsReclassified:false},
  excludedGaps:['health-close','AsyncRequest'],installedApplicationsStarted:false,physicalSleepPerformed:false,realSteamExecuted:false,officialLoaderExecuted:false,
  boundary:'Original source-built protocol peers, own inert context, actual current typed Runtime/Client/Owner. No external network, installed application, OS sleep, device or product Main.'};
 function bind(relative) {
  relative=relative.replaceAll('\\','/');if(sources.has(relative))return;const file=path.resolve(repo,relative);assert(inside(repo,file),'SOURCE_ESCAPE');
  assert.equal(key(fs.realpathSync(file)),key(file),'SOURCE_REPARSE');sources.set(relative,{path:relative,sha256:sha(file),bytes:fs.statSync(file).size});
 }
 function freeze(relative) {
  bind(relative);const target=path.join(work,'s',relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(repo,relative),target);
  assert.equal(sha(target),sources.get(relative).sha256,'FREEZE_CHANGED');frozen.set(key(target),pin(target));
 }
 function run(id,program,args,timeoutMs) {
  const remaining=135000-(Date.now()-started);assert(remaining>0,'OUTER_BUDGET_EXHAUSTED');const allotted=Math.min(timeoutMs,remaining);
  const begin=Date.now(),child=spawnSync(program,args,{cwd:work,env,windowsHide:true,timeout:allotted,maxBuffer:16*1024*1024});
  const stdout=path.join(out,id+'.stdout.log'),stderr=path.join(out,id+'.stderr.log');fs.writeFileSync(stdout,child.stdout??'');fs.writeFileSync(stderr,child.stderr??'');
  const row={id,program:pin(program),args,timeoutMs:allotted,originalBudgetMs:timeoutMs,exitCode:child.status,signal:child.signal,error:child.error?.message??null,elapsedMs:Date.now()-begin,stdout:pin(stdout),stderr:pin(stderr)};
  write(path.join(out,id+'.process.json'),row);(report.executions??=[]).push(row);console.log(JSON.stringify({phase:id,exitCode:row.exitCode,elapsedMs:row.elapsedMs,error:row.error}));
  assert(!child.error&&child.signal===null&&Number.isInteger(child.status),'PROCESS_NOT_NATURALLY_COMPLETED:'+id);return row;
 }
 let env;
 try {
  const declaration=ownedToolDeclaration(repo);assert(!declaration.blockedReason,declaration.blockedReason);const node=lockBundledNodeTools(repo);verifyBundledToolScope(node,repo);
  for(const relative of [...OWNED_INPUTS,legacyRel,rel+'/run.mjs',entry+'.mjs',entry+'.cpp',entry+'.test.mjs'])bind(relative);
  // Freeze the original producer closure, rather than rewriting a typed peer.
  for(const relative of [...OWNED_SOURCES,entry+'.cpp'])freeze(relative);
  const artifacts=[...new Set([...declaration.artifactInputs,process.execPath])];assert(artifacts.length<=3072,'ARTIFACT_LIMIT');for(const file of artifacts)tools.set(key(file),pin(file));
  report.nodeToolProof=node.proof;write(path.join(out,'SOURCE-PINS.json'),[...sources.values()]);write(path.join(out,'TOOL-PINS.json'),[...tools.values()]);
  const legacy=fs.readFileSync(path.join(repo,legacyRel),'utf8'),expected=originalAssertions(legacy);write(path.join(out,'ORIGINAL-ASSERTIONS.json'),expected);
  fs.copyFileSync(path.join(repo,legacyRel),path.join(out,'historical-legacy-runtime.cpp'));
  const original=fs.readFileSync(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'),'utf8'),end=original.indexOf('\nint main(int argc,char** argv){');
  assert(end>0,'ORIGINAL_HELPER_BOUNDARY_CHANGED');
  const pure=legacy.slice(legacy.indexOf('    using ymcc::deckysidebar::officialLoaderSearchPath;'),legacy.indexOf('    Runtime runtime;'));
  assert.equal((pure.match(/\bcheck\(/g)??[]).length,8,'PURE_ASSERTION_EXTRACTION');
  report.generated=[];
  for(const [name,text,origin] of [['helpers',original.slice(0,end)+'\n','tools/decky_binding_runtime_selftest.cpp'],['path-assertions',pure,legacyRel]]) {
   const file=path.join(work,'s/tools/typed-legacy-runtime-'+name+'.inc');fs.writeFileSync(file,text);frozen.set(key(file),pin(file));report.generated.push({...pin(file),origin,originSha256:sources.get(origin).sha256,bytePreserved:true});
  }
  const readyConsumer=originalReadyConsumer(path.join(work,'s'),{write:true});frozen.set(key(readyConsumer.path),readyConsumer);report.generated.push(readyConsumer);
  write(path.join(out,'GENERATED.json'),report.generated);
  const compiler=selectedNativeTools().native;
  env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(compiler.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=compiler.include.join(';');env.LIB=compiler.lib.join(';');
  for(const k of ['CL','_CL_','LINK','_LINK_'])env[k]='';
  for(const k of Object.keys(env))if(k.startsWith('YMCC_BINDING_')||k.startsWith('YMCC_DECKY_'))delete env[k];
  env.TEMP=path.join(work,'t');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
  const image=path.join(work,'steam.exe'),graph=path.join(work,'d.json'),object=path.join(work,'p.obj');
  const args=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/UYMCC_DECKY_RUNTIME_LEGACY_FIXTURE','/I'+path.join(work,'s/native'),'/I'+path.join(work,'s/deps/json'),'/I'+path.join(work,'s/tools'),path.join(work,'s',entry+'.cpp'),'/sourceDependencies',graph,'/Fo'+object,'/Fe'+image,'/link','advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib'];
  const build=run('compile',compiler.compiler,args,60000);assert.equal(build.exitCode,0,'COMPILE_FAILED');
  const dependencies=read(graph);assert.equal(dependencies.Version,'1.2');let count=0;
  for(const file of [dependencies.Data.Source,...dependencies.Data.Includes]){++count;const row=frozen.get(key(file))??tools.get(key(file));assert(row,'UNDECLARED_COMPILER_INPUT:'+file);assert.equal(sha(file),row.sha256,'COMPILER_INPUT_CHANGED:'+file);}
  report.compilerGraph={...pin(graph),inputs:count,allDeclared:true,osLoadedModuleGraph:false};
  const cef=path.join(work,'bin/cef/fixture/steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(image,cef);assert.equal(sha(image),sha(cef));
  report.nativeArtifacts=[pin(image),pin(cef),pin(object)];assert(tools.size+report.nativeArtifacts.length+report.generated.length<=3072,'ARTIFACT_LIMIT');
  const rawPath=path.join(out,'native-result.json'),native=run('native',image,[rawPath],15000);report.raw=fs.existsSync(rawPath)?pin(rawPath):null;
  assert(report.raw,'MISSING_NATIVE_RESULT');const raw=read(rawPath);assert.equal(native.exitCode,raw.passed?0:1,'NATIVE_EXIT_MISMATCH');
  // Retain the entire ordered map even when the native outcome fails.
  const map=expected.map(row=>{const actual=raw.assertions.find(r=>r.label===row.label);return {...row,status:actual?.passed===true?'PASS':actual?'FAIL':'INCOMPLETE',caseId:'legacy.runtime.'+String(row.ordinal).padStart(2,'0'),
   currentSource:'native/decky_sidebar_runtime.h',currentSourceSha256:sources.get('native/decky_sidebar_runtime.h').sha256,
   currentMechanism:row.group==='environment'?'same production pure function and exact historical equality':row.group==='no-launch'?'same no-launch state/attempt sequence with required ownedBinding Client':row.group==='cancellation'?'current prepareEnvironment(canContinue), plus real pending owned Client/Owner; original1000ms gate':'real typed ABI peer; actual cancellation API forwarded after original200ms gate; new Client epoch/request identity',
   legacyAbiDifference:row.group==='cancellation'||row.group==='handoff'?'Removed legacy Config callbacks are not executed; semantic invariants are measured through current typed consumer. Literal old regression remains independent.':null,
   raw:report.raw,measurements:actual?.measurements??null};});
  write(path.join(out,'ORDERED-MAPPING.json'),map);report.mapping=pin(path.join(out,'ORDERED-MAPPING.json'));
  report.summary=validateRuntimeResult(raw,expected);assert.equal(native.exitCode,0,'NATIVE_FAILED');report.status='PASS';report.passed=true;
 } catch(error) {report.error={message:error.message,stack:error.stack};}
 finally {
  report.sourceChanged=[...sources.values()].filter(r=>!fs.existsSync(path.join(repo,r.path))||sha(path.join(repo,r.path))!==r.sha256).map(r=>r.path);
  report.toolsChanged=[...tools.values()].filter(r=>!fs.existsSync(r.path)||sha(r.path)!==r.sha256).map(r=>r.path);
  if(report.sourceChanged.length||report.toolsChanged.length){report.status='FAIL';report.passed=false;}
  report.sourceInputs=[...sources.values()];report.toolArtifacts=[...tools.values()];report.finishedAt=new Date().toISOString();report.elapsedMs=Date.now()-started;
  write(path.join(out,'result.json'),report);console.log(JSON.stringify({status:report.status,summary:report.summary??null,error:report.error?.message??null,sourceChanged:report.sourceChanged,toolsChanged:report.toolsChanged,out}));
 }
 return report;
}
if(process.argv[1]&&pathToFileURL(path.resolve(process.argv[1])).href===import.meta.url){const report=await execute(process.argv.slice(2));process.exitCode=report.passed?0:1;}


