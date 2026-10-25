// Bounded current-producer migration entry. This does not register or replace
// the full parent adapter, the 240s baseline, or historical legacy failures.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import assert from 'node:assert/strict';
import {spawnSync} from 'node:child_process';
import {pathToFileURL} from 'node:url';
import {OWNED_SOURCES,OWNED_INPUTS,ownedToolDeclaration} from './owned-adapter.mjs';
import {ORIGINAL_CASES,validateOriginalResult} from './owned-run.mjs';
import {resourceZero} from './typed-binding-gate.mjs';
import {lockBundledNodeTools,verifyBundledToolScope} from '../bundled-node-tool-authority.mjs';
const repo=path.resolve(import.meta.dirname,'../../../..');
const moduleRel='tools/ymcc_sim/global/decky-persistent-lifecycle';
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const within=(parent,file)=>key(file).startsWith(key(parent)+path.sep);
const args=process.argv.slice(2),get=name=>{const i=args.indexOf(name);return i<0?null:args[i+1];};
const write=(file,value)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,JSON.stringify(value,null,2)+'\n');};
const pin=file=>({path:path.resolve(file),sha256:sha(file)});
function checkedNative(file,digest){assert.equal(sha(file),digest,'CONSUMER_NATIVE_RECEIPT_CHANGED');const value=read(file);assert.equal(value.schemaVersion,1);assert(Array.isArray(value.cases));return value;}
if(get('--consumer-native')){
 const native=checkedNative(get('--consumer-native'),get('--consumer-native-sha256'));
 const {lifecycleConsumerTests}=await import('./consumer-selftest.mjs');
 const result=await lifecycleConsumerTests(native);write(get('--consumer-output'),result);
 console.log(JSON.stringify({scope:'unchanged production host-relay-client lifecycle consumer',cases:result.cases.length,failed:result.cases.filter(c=>!c.passed).map(c=>c.id)}));
 process.exitCode=result.passed?0:1;
}else{
 const out=path.resolve(get('--evidence-dir')??''),work=path.resolve(get('--workspace-dir')??'');
 const core=path.resolve('G:/YeManCC-Work/Testing/YMCC');
 assert(get('--evidence-dir')&&get('--workspace-dir'),'EXPLICIT_CORE_EVIDENCE_AND_WORKSPACE_REQUIRED');
 assert(within(path.join(core,'Evidence/DeckyParentMigration-20261023'),out),'EVIDENCE_SCOPE');
 assert(within(path.join(core,'Workspace/DeckyParentMigration-20261023'),work),'WORKSPACE_SCOPE');
 assert(!fs.existsSync(out),'FRESH_EVIDENCE_REQUIRED');assert(fs.statSync(work).isDirectory(),'OWNED_WORKSPACE_REQUIRED');
 assert.equal(key(fs.realpathSync(work)),key(work),'WORKSPACE_REPARSE');fs.mkdirSync(out,{recursive:true});
 const report={schemaVersion:1,id:'plugins.decky-current-parent-migration',status:'FAIL',passed:false,fullPersistentAcceptance:false,
  startedAt:new Date().toISOString(),cases:[],sourceInputs:[],toolArtifacts:[],physicalSleepPerformed:false,officialLoaderExecuted:false,
  installedApplicationsStarted:false,realSteamOrDeviceActions:0,networkScope:'unchanged owned source-built protocol peers only; production TS socket boundaries are in-memory',
  originalBudgets:{compileMs:60000,nativeMs:45000,typedWrapperMs:135000,fullBaselineMs:240000},maxArtifactInputs:3072,
  originalBaseline:{status:'NOT_RUN',reason:'First bounded current producer/parent migration; full update tree is independent'},
  requiredIntegration:'Main agent must separately register this entry and refresh parent sourceInputs/requiredComponents; adapter/contract remain unchanged',
  lifecycleComponent:{featureId:'plugins.decky',persistence:'persistent',parentAdapterId:'plugins.decky-persistent-lifecycle',fullPersistentAcceptance:false},
  historicalLegacyVerdict:'FAIL retained; original raw receipts and legacy source are not reclassified'};
 const sourcePins=new Map(),toolPins=new Map();
 function bind(relative){relative=relative.replaceAll('\\','/');if(sourcePins.has(relative))return;const file=path.join(repo,relative);assert(within(repo,file));assert.equal(key(fs.realpathSync(file)),key(file),'SOURCE_REPARSE');sourcePins.set(relative,{path:relative,sha256:sha(file)});}
 function run(id,program,argv,{timeout=45000,cwd=work,env=process.env}={}){
  const before=Date.now(),child=spawnSync(program,argv,{cwd,env,windowsHide:true,timeout,maxBuffer:16*1024*1024});
  const stdout=path.join(out,id+'.stdout.log'),stderr=path.join(out,id+'.stderr.log');fs.writeFileSync(stdout,child.stdout??'');fs.writeFileSync(stderr,child.stderr??'');
  const receipt={id,program:pin(program),arguments:argv,cwd,timeoutMs:timeout,exitCode:child.status,signal:child.signal,error:child.error?.message??null,elapsedMs:Date.now()-before,stdout:pin(stdout),stderr:pin(stderr)};
  write(path.join(out,id+'.process.json'),receipt);report.executions??=[];report.executions.push(receipt);console.log(JSON.stringify({phase:id,exitCode:receipt.exitCode,elapsedMs:receipt.elapsedMs,error:receipt.error}));
  assert.equal(child.signal,null,'NO_FORCED_PARENT_TERMINATION:'+id);assert(!child.error,'PROCESS_ERROR:'+id);assert(Number.isInteger(child.status)&&child.status>=0,'PROCESS_EXIT_REQUIRED:'+id);return receipt;
 }
 function validateGraph(file,frozen){
  const graph=read(file);assert.equal(graph.Version,'1.2');let count=0;
  for(const file of [graph.Data.Source,...graph.Data.Includes]){++count;const source=frozen.get(key(file)),tool=toolPins.get(key(file));assert(source||tool,'UNDECLARED_COMPILER_INPUT:'+file);assert.equal(sha(file),(source??tool).sha256,'COMPILER_INPUT_CHANGED:'+file);}
  return {path:file,sha256:sha(file),count,allDeclared:true,osLoadedModuleGraph:false};
 }
 try{
  const declaration=ownedToolDeclaration(repo);assert(!declaration.blockedReason,declaration.blockedReason);
  const nodeScope=lockBundledNodeTools(repo);verifyBundledToolScope(nodeScope,repo);
  for(const relative of [...OWNED_INPUTS,moduleRel+'/current-parent-native.mjs',moduleRel+'/current-parent-native.cpp',moduleRel+'/consumer-selftest.mjs',moduleRel+'/typed-binding-gate.mjs','native/main.cpp','tools/fixtures/decky_mirror_host_fixture.mjs','tools/fixtures/decky_game_memory_store.mjs','tools/decky_sidebar_crash_breaker_selftest.cpp'])bind(relative);
  // Capture every literal production TS/JSON input consumed by the existing
  // host/store loader. No source transformer or replacement consumer is added.
  for(const relative of [moduleRel+'/consumer-selftest.mjs','tools/fixtures/decky_mirror_host_fixture.mjs','tools/fixtures/decky_game_memory_store.mjs']){
   const text=fs.readFileSync(path.join(repo,relative),'utf8');
   for(const match of text.matchAll(/['"]((?:src\/bridge|decky-plugin\/src|PowerControl)\/[^'"\r\n]+\.(?:tsx?|js|json))['"]/g))bind(match[1]);
  }
  const declared=[...new Set([...declaration.artifactInputs,process.execPath])];assert(declared.length<=3072,'ORIGINAL_ARTIFACT_BUDGET');
  for(const file of declared)toolPins.set(key(file),pin(file));
  report.sourceInputs=[...sourcePins.values()];report.toolArtifacts=[...toolPins.values()];report.nodeToolProof=nodeScope.proof;
  write(path.join(out,'SOURCE-PINS.json'),report.sourceInputs);write(path.join(out,'TOOL-PINS.json'),report.toolArtifacts);
  const originalCpp=fs.readFileSync(path.join(repo,'tools/decky_binding_runtime_selftest.cpp'),'utf8');
  assert.deepEqual([...originalCpp.matchAll(/\btest\("([^"]+)"/g)].map(m=>m[1]),ORIGINAL_CASES.map(r=>r.registrationId),'ORIGINAL_SOURCE_REGISTRATIONS_CHANGED');
  const originalDir=path.join(work,'o');
  const originalRun=run('original-typed',process.execPath,[path.join(repo,'tools/decky_binding_runtime_selftest.mjs'),originalDir],{timeout:135000});
  const raw=read(path.join(originalDir,'typed-runtime-result.json')),originalResult=read(path.join(originalDir,'RESULT.json'));
  report.originalSummary=validateOriginalResult(raw,originalResult);assert.equal(originalRun.exitCode,raw.passed?0:1);
  const frozen=read(path.join(originalDir,'SOURCE-PINS.json'));
  assert.deepEqual(frozen.files.map(r=>r.path).sort(),[...OWNED_SOURCES].sort(),'ORIGINAL_FROZEN_SOURCE_MEMBERSHIP_CHANGED');
  const originalFrozen=new Map();for(const row of frozen.files){assert.equal(sourcePins.get(row.path)?.sha256,row.sha256);const p=path.join(frozen.source,row.path);assert.equal(sha(p),row.sha256);originalFrozen.set(key(p),row);}
  report.originalCompilerGraph=validateGraph(path.join(originalDir,'dependencies.json'),originalFrozen);
  const originalBuild=read(path.join(originalDir,'BUILD.json'));assert.equal(originalBuild.exit,0);assert.equal(originalBuild.error,null);assert.equal(originalBuild.compilerSha256,toolPins.get(key(originalBuild.compiler))?.sha256);
  assert.equal(sha(path.join(originalDir,'FixtureSteam/steam.exe')),originalResult.exeSha256);assert.equal(sha(path.join(originalDir,'FixtureSteam/bin/cef/fixture/steamwebhelper.exe')),originalResult.exeSha256);
  assert.equal(sha(path.join(originalDir,'binding-client.obj')),originalResult.objectSha256);
  report.originalProducer={raw:pin(path.join(originalDir,'typed-runtime-result.json')),result:pin(path.join(originalDir,'RESULT.json')),sourcePins:pin(path.join(originalDir,'SOURCE-PINS.json')),exe:pin(path.join(originalDir,'FixtureSteam/steam.exe'))};
  // The original six producer verdicts and all raw fields remain intact.
  report.cases.push(...raw.cases.map(row=>({id:row.id,passed:row.passed,measurements:Object.fromEntries(Object.entries(row).filter(([k])=>!['id','passed'].includes(k))),evidence:report.originalProducer.raw})));
  const nativeDir=path.join(work,'n'),source=path.join(nativeDir,'s');fs.mkdirSync(source,{recursive:true});const nativeFrozen=new Map();
  for(const relative of [...OWNED_SOURCES,moduleRel+'/current-parent-native.cpp']){
   const target=path.join(source,relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.copyFileSync(path.join(repo,relative),target);assert.equal(sha(target),sourcePins.get(relative).sha256);nativeFrozen.set(key(target),{path:target,sha256:sha(target)});
  }
  const mainStart=originalCpp.indexOf('\nint main(int argc,char** argv){');assert(mainStart>0&&originalCpp.indexOf('\nint main(',mainStart+1)<0,'ORIGINAL_PREAMBLE_BOUNDARY_CHANGED');
  const helper=path.join(source,'tools/current-runtime-helpers.inc');fs.writeFileSync(helper,originalCpp.slice(0,mainStart)+'\n');nativeFrozen.set(key(helper),pin(helper));
  const main=fs.readFileSync(path.join(repo,'native/main.cpp'),'utf8'),start=main.indexOf('    case WM_POWER_RESUME_COMMIT:'),begin=main.indexOf('\n',start)+1,end=main.indexOf('            ipc_emit("power.resumed"',begin);
  assert(start>=0&&end>begin,'CURRENT_READY_EXTRACTION_BOUNDARY_CHANGED');const slice=main.slice(begin,end);
  for(const fragment of ['PowerLifecycle::Ready','requestPreparationRefresh(currentPowerGeneration())','requestBindingRefresh(currentPowerGeneration())','requestConnectionCheck()'])assert(slice.includes(fragment),'CURRENT_READY_CONSUMER_CHANGED:'+fragment);
  const ready=path.join(source,'tools/current-committed-ready.inc');fs.writeFileSync(ready,'static void consumeCommittedReady(){\n'+slice+'        }\n}\n');nativeFrozen.set(key(ready),pin(ready));
  const crashPolicy=fs.readFileSync(path.join(repo,'tools/decky_sidebar_crash_breaker_selftest.cpp'),'utf8'),policyBegin=crashPolicy.indexOf('static void policy(){'),policyEnd=crashPolicy.indexOf('\nint wmain(',policyBegin);
  assert(policyBegin>=0&&policyEnd>policyBegin,'ORIGINAL_CRASH_POLICY_BOUNDARY_CHANGED');
  const policyText=crashPolicy.slice(policyBegin,policyEnd),policyInclude=path.join(source,'tools/current-crash-policy.inc');
  assert(policyText.includes('b.exited(10)')&&policyText.includes('LoaderCrashBudget::maxUnstableExits')&&policyText.includes('b.healthy(LoaderCrashBudget::stableUptimeMs)'),'ORIGINAL_CRASH_POLICY_CHANGED');
  fs.writeFileSync(policyInclude,policyText+'\n');nativeFrozen.set(key(policyInclude),pin(policyInclude));
  report.nativeExtractions={producer:{path:'tools/decky_binding_runtime_selftest.cpp',sha256:sourcePins.get('tools/decky_binding_runtime_selftest.cpp').sha256,beginByte:0,endByte:mainStart,generated:pin(helper),scope:'unchanged reusable producer helpers; original six cases execute separately through original mjs'},ready:{path:'native/main.cpp',sha256:sourcePins.get('native/main.cpp').sha256,beginByte:begin,endByte:end,sliceSha256:crypto.createHash('sha256').update(slice).digest('hex'),generated:pin(ready),scope:'unchanged current committed-Ready consumer; UI/OS events are not dispatched'}};
  report.nativeExtractions.crashPolicy={path:'tools/decky_sidebar_crash_breaker_selftest.cpp',sha256:sourcePins.get('tools/decky_sidebar_crash_breaker_selftest.cpp').sha256,beginByte:policyBegin,endByte:policyEnd,generated:pin(policyInclude),scope:'unchanged original pure policy() body; entire legacy fixture and its macro are not compiled'};
  write(path.join(out,'NATIVE-EXTRACTIONS.json'),report.nativeExtractions);
  const tools=read(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json')).native;
  const env=Object.fromEntries(Object.entries(process.env).map(([k,v])=>[k.toUpperCase(),v]));env.PATH=path.dirname(tools.compiler)+';'+process.env.SystemRoot+'/System32';env.INCLUDE=tools.include.join(';');env.LIB=tools.lib.join(';');for(const n of ['CL','_CL_','LINK','_LINK_'])env[n]='';env.TEMP=path.join(nativeDir,'t');env.TMP=env.TEMP;fs.mkdirSync(env.TEMP);
  const image=path.join(nativeDir,'steam.exe'),graph=path.join(nativeDir,'d.json');
  // YMCC and the public Windows Loader are GUI images. A console fixture can
  // add conhost.exe to its Job before the intended descendant starts, invalidating
  // the original two-process fallback proof. Keep main/redirected diagnostic IO.
  const compileArgs=['/nologo','/Bv','/std:c++20','/utf-8','/EHsc','/MT','/O2','/W4','/WX','/DNOMINMAX','/DUNICODE','/D_UNICODE','/I'+path.join(source,'native'),'/I'+path.join(source,'deps/json'),'/I'+path.join(source,'tools'),path.join(source,moduleRel,'current-parent-native.cpp'),'/sourceDependencies',graph,'/Fo'+path.join(nativeDir,'p.obj'),'/Fe'+image,'/link','/SUBSYSTEM:WINDOWS','/ENTRY:mainCRTStartup','/MAP:'+path.join(nativeDir,'native.map'),'advapi32.lib','ws2_32.lib','bcrypt.lib','winhttp.lib','user32.lib','userenv.lib','shell32.lib'];
  const compiled=run('current-native-compile',tools.compiler,compileArgs,{timeout:60000,cwd:nativeDir,env});assert.equal(compiled.exitCode,0,'CURRENT_PARENT_NATIVE_COMPILE_FAILED');
  const imageBytes=fs.readFileSync(image),peOffset=imageBytes.readUInt32LE(0x3c);
  assert(peOffset>=0x40&&peOffset+94<=imageBytes.length&&imageBytes.readUInt32LE(peOffset)===0x00004550,'CURRENT_NATIVE_PE_HEADER');
  assert.equal(imageBytes.readUInt16LE(peOffset+4),0x8664,'CURRENT_NATIVE_X64_REQUIRED');
  assert.equal(imageBytes.readUInt16LE(peOffset+24),0x20b,'CURRENT_NATIVE_PE32PLUS_REQUIRED');
  assert.equal(imageBytes.readUInt16LE(peOffset+24+68),2,'CURRENT_NATIVE_GUI_IMAGE_REQUIRED');
  report.currentNativeImage={...pin(image),machine:imageBytes.readUInt16LE(peOffset+4),subsystem:imageBytes.readUInt16LE(peOffset+24+68),sourceBuilt:true,officialLoaderExecuted:false};
  report.currentCompilerGraph=validateGraph(graph,nativeFrozen);const cef=path.join(nativeDir,'bin/cef/fixture/steamwebhelper.exe');fs.mkdirSync(path.dirname(cef),{recursive:true});fs.copyFileSync(image,cef);assert.equal(sha(image),sha(cef));
  const nativeRun=run('current-native',image,[path.join(nativeDir,'f')],{timeout:45000,cwd:nativeDir,env});
  assert([0,1].includes(nativeRun.exitCode),'CURRENT_NATIVE_NONTERMINAL_EXIT:'+nativeRun.exitCode+'; original process/fault/progress receipts retained; no result inferred');
  const current=read(path.join(nativeDir,'native-result.json'));assert.equal(current.schema,'ymcc.current-decky-parent-native.v1');assert.equal(current.productCallbackMode,'typed-only; legacy macro absent');assert.equal(nativeRun.exitCode,current.passed?0:1);
  const currentIds=['native.exit-clears-owned-process-job-worker-event','native.explicit-off-and-fatal-off-veto-late-wakes','native.late-actual-recovery-callback-after-exit','native.committed-ready-executes-original-rebind-consumer','native.job-stop-failure-falls-back-to-owned-job-close','native.crash-budget-cannot-renew-on-recovery-or-unknown'];assert.deepEqual(current.cases.map(r=>r.id),currentIds,'CURRENT_CASE_MEMBERSHIP_CHANGED');
  assert.equal(current.remainingTrackedJobHandles,0,'CURRENT_OWNED_JOB_HANDLE_LEDGER_NOT_EMPTY');assert.equal(current.resourceLedgerOverflow,false,'CURRENT_RESOURCE_LEDGER_OVERFLOW');
  report.currentProducer={raw:pin(path.join(nativeDir,'native-result.json')),exe:pin(image),cef:pin(cef)};report.cases.push(...current.cases.map(row=>({...row,evidence:report.currentProducer.raw})));
  const combined={schemaVersion:1,passed:raw.passed&&current.passed,cases:report.cases};const combinedFile=path.join(out,'native-result.json');write(combinedFile,combined);
  const consumerFile=path.join(out,'consumer-result.json');const consumerRun=run('current-consumer',process.execPath,[path.join(repo,moduleRel,'current-parent-native.mjs'),'--consumer-native',combinedFile,'--consumer-native-sha256',sha(combinedFile),'--consumer-output',consumerFile],{timeout:45000,cwd:repo});
  const consumer=read(consumerFile);assert.equal(consumerRun.exitCode,consumer.passed?0:1);assert.equal(consumer.productionHostExecuted,true);assert.equal(consumer.productionRelayExecuted,true);assert.equal(consumer.productionClientExecuted,true);report.cases.push(...consumer.cases.map(row=>({...row,evidence:pin(consumerFile)})));
  const c=id=>report.cases.find(r=>r.id===id),ids=ORIGINAL_CASES.map(r=>r.id),exit='consumer.exit-disposes-observers-and-delivered-late-recovery-callbacks';
  function requirement(name,caseIds,gaps=[]){const failed=caseIds.filter(id=>c(id)?.passed!==true);return {requirement:name,status:failed.length?'FAIL':gaps.length?'INCOMPLETE':'PASS',caseIds,failedOrMissingCaseIds:failed,gaps,observations:caseIds.map(id=>({caseId:id,evidence:c(id)?.evidence??null,measurements:c(id)?.measurements??null}))};}
  report.requirements=[
   requirement('destroyOnExit',[ids[0],currentIds[0],exit,'consumer.late-attach-resolution-cannot-recreate-stopped-host']),
   requirement('destroyWhenStopStalls',[ids[1],ids[2],currentIds[4],exit]),
   requirement('preventResurrectionAfterExit',[ids[1],ids[2],currentIds[2],exit,'consumer.late-attach-resolution-cannot-recreate-stopped-host']),
   requirement('explicitOffStaysOff',[ids[3],currentIds[1],exit]),
   requirement('recoverAfterSleep',[ids[4],ids[5],currentIds[3],'consumer.sleep-rejects-write-and-automatically-publishes-current-ready','negative.connected-without-ready-snapshot-is-not-health','negative.recorded-pre-sleep-ready-is-not-current-generation-health']),
   requirement('recoverAfterTransientFailure',['consumer.transient-read-exception-does-not-self-recover','consumer.transient-source-churn-recovers-through-original-two-retry-relay','consumer.exception-exhausts-original-two-retry-budget','consumer.exception-late-retry-cannot-revive-paused-demand','consumer.exception-late-retry-cannot-revive-detached-peer','consumer.exception-late-retry-cannot-revive-disposed']),
   requirement('respectRecoveryLimits',[currentIds[5],'negative.relay-source-churn-exhausts-original-two-retry-budget','consumer.exception-exhausts-original-two-retry-budget'])
  ];
  // Every returned consumer verdict, including negative controls, gates this run.
  report.selectedCasesPassed=report.cases.every(r=>r.passed===true);report.allSevenObservationsPassed=report.requirements.length===7&&report.requirements.every(r=>r.status==='PASS');report.status=report.selectedCasesPassed&&report.allSevenObservationsPassed?'INCOMPLETE':'FAIL';
 }catch(error){report.error=String(error.stack??error);report.status='FAIL';report.cases.push({id:'current-parent.execution-integrity',passed:false,error:report.error});}
 const changed=(rows,resolve)=>rows.filter(row=>{try{return sha(resolve(row))!==row.sha256;}catch{return true;}});
 report.sourceChanges=changed([...sourcePins.values()],r=>path.join(repo,r.path)).map(r=>r.path);report.toolChanges=changed([...toolPins.values()],r=>r.path).map(r=>r.path);
 report.sourceUnchanged=report.sourceChanges.length===0;report.toolsUnchanged=report.toolChanges.length===0;
 if(!report.sourceUnchanged||!report.toolsUnchanged)report.status='FAIL';
 report.summary={cases:report.cases.length,passed:report.cases.filter(r=>r.passed).length,failed:report.cases.filter(r=>!r.passed).map(r=>r.id),requirements:Object.fromEntries(['PASS','FAIL','INCOMPLETE'].map(s=>[s,(report.requirements??[]).filter(r=>r.status===s).length]))};
 report.finishedAt=new Date().toISOString();report.passed=false;write(path.join(out,'result.json'),report);
 write(path.join(out,'REQUIREMENTS.json'),{status:report.status,fullPersistentAcceptance:false,requirements:report.requirements??[],originalBaseline:report.originalBaseline,requiredIntegration:report.requiredIntegration});
 console.log(JSON.stringify({status:report.status,summary:report.summary,error:report.error??null,sourceUnchanged:report.sourceUnchanged,toolsUnchanged:report.toolsUnchanged,fullPersistentAcceptance:false,out}));
 process.exitCode=report.status==='INCOMPLETE'?2:1;
}