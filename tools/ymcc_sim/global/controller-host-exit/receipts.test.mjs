import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {test} from 'node:test';
import {REPO,contract,sourceInputs,getControllerHostExitAdapters,artifactRows} from './adapter.mjs';
import {toolInputs} from '../controller-signal-evidence/adapter.mjs';
import {readNativeExitTools,nativeExitEnvironment,nativeExitChildArgs} from './tool-profile.mjs';
import {validateHelper,validateParent,validateParentScenarios,validateGateProfiles,validateGates,validateNativeExit} from './receipts.mjs';
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
test('native exit registry names all three actual producers and preserves historical negative identity',()=>{const a=getControllerHostExitAdapters()[0];assert.equal(a.id,'controller.host-exit-owned');assert.equal(a.blockedReason,undefined);assert.equal(a.resultFiles[0],'host-exit-owned/result.json');assert.equal(new Set(sourceInputs).size,sourceInputs.length);assert(sourceInputs.every(n=>fs.existsSync(path.join(REPO,n))));assert(artifactRows().length<3072);assert.equal(read(contract.historicalFailure.path).status,'ACTUAL_NATIVE_EXIT_PARENT_CONTRACT_FAIL_REPRODUCED');});
test('native exit scoped tools bind the selected Python and ignore unrelated Git or inherited overrides',t=>{
 const tools=readNativeExitTools(REPO);
 assert.equal(tools.files.some(row=>/\\git\.exe$/i.test(row.path)),false);
 const env=nativeExitEnvironment({Path:'unreviewed',PYTHONHOME:'elsewhere',CL:'/DUNREVIEWED',ComSpec:'elsewhere'},tools,tools.primary.node);
 assert.equal(env.ComSpec,tools.primary.commandProcessor);
 assert.equal(env.PYTHONHOME,undefined);assert.equal(env.CL,undefined);assert(!env.PATH.includes('unreviewed'));
 assert.throws(()=>nativeExitEnvironment({},tools,tools.python),/NATIVE_EXIT_NODE_IDENTITY/);
 const output=path.join(REPO,'unused-test-output'), main='f'.repeat(64);
 for(const [name,route] of Object.entries(contract.routes)){
  assert.deepEqual(nativeExitChildArgs(REPO,route,output,name==='parent'?main:undefined),
   ['-I','-S','-B','-X','utf8',path.join(REPO,route.consumer),route.arg,output,...(name==='parent'?['--expected-main-sha256',main]:[])]);
  assert.deepEqual(nativeExitChildArgs(REPO,route,output,name==='parent'?main:undefined,{fixedBuild:true}),
   ['-I','-S','-B','-X','utf8',path.join(REPO,route.consumer),route.arg,output,'--fixed-native-build',...(name==='parent'?['--expected-main-sha256',main]:[])]);
 }
 const reads=fs.readFileSync;
 const mutate=t.mock.method(fs,'readFileSync',(f,...a)=>f===tools.python?Buffer.from('different Python'):reads(f,...a));
 assert.throws(()=>readNativeExitTools(REPO),/NATIVE_EXIT_TOOL_CHANGED:.*python\.exe/i);
 mutate.mock.restore();
 const stat=fs.lstatSync;
 const missing=t.mock.method(fs,'lstatSync',(f,...a)=>{if(f===tools.python)throw Object.assign(Error('mock missing Python'),{code:'ENOENT'});return stat(f,...a);});
 assert.throws(()=>readNativeExitTools(REPO),{code:'ENOENT',message:'mock missing Python'});
 missing.mock.restore();
 t.mock.method(fs,'lstatSync',(f,...a)=>/\\python312\._pth$/i.test(f)?stat(tools.python):stat(f,...a));
 assert.throws(()=>readNativeExitTools(REPO),/NATIVE_EXIT_PYTHON_STARTUP_OVERRIDE:python312\._pth/);
});
if(process.env.YMCC_NATIVE_HOST_EXIT_PARENT_CASE_RECEIPTS==='1')test('native exit parent case receipts bind historical raw and dead-process pipe debt',t=>{
 const original=contract.originalEvidence[1],parentBytes=fs.readFileSync(original.path),output=path.dirname(original.path);
 assert.equal(createHash('sha256').update(parentBytes).digest('hex'),original.sha256);
 const parent=JSON.parse(parentBytes.toString('utf8').replace(/^\uFEFF/,''));
 const readHistorical=r=>validateParentScenarios(r,output,{historical:true});
 assert.throws(()=>validateParentScenarios(parent,output),/NATIVE_EXIT_PARENT_MATRIX/);
 const raw=readHistorical(parent).scenarioResults;
 assert.deepEqual(raw.map(({profile,scenario})=>[profile,scenario]),['Release','Checked'].flatMap(profile=>contract.historicalParentScenarios.map(scenario=>[profile,scenario])));
 const release=parent.profiles.find(p=>p.name==='Release'),index=release.scenarios.findIndex(s=>s.name==='protected-primary-pipe');
 const draft=()=>{const copy=structuredClone(parent);return [copy,copy.profiles.find(p=>p.name==='Release').scenarios[index].result];};
 const [forged,edited]=draft();edited.actualLogs={replaced:true};
 assert.throws(()=>readHistorical(forged),/SCENARIO_RAW_MISMATCH:Release\/protected-primary-pipe/);
 for(const field of ['processExited','processOwnerClosed','primaryThreadExited']){
  const [copy,x]=draft();x.beforeFixtureFaultRelief[field]=false;
  assert.throws(()=>readHistorical(copy),/DEAD_PROCESS_WITH_PIPE_DEBT/);
 }
 const [debtGone,x]=draft();x.beforeFixtureFaultRelief.pipeState.ownedOriginals=0;
 assert.throws(()=>readHistorical(debtGone),/DEAD_PROCESS_WITH_PIPE_DEBT/);
 const readFile=fs.readFileSync,victim=raw.find(p=>p.profile==='Release'&&p.scenario==='protected-primary-pipe').path;
 const mock=t.mock.method(fs,'readFileSync',(file,...args)=>{if(file===victim)throw Object.assign(new Error('no scenario receipt'),{code:'ENOENT'});return readFile(file,...args);});
 assert.throws(()=>readHistorical(parent),/SCENARIO_RAW_UNREADABLE:Release\/protected-primary-pipe/);
 mock.mock.mockImplementation((file,...args)=>file===victim?Buffer.from('{incomplete'):readFile(file,...args));
 assert.throws(()=>readHistorical(parent),/SCENARIO_RAW_UNREADABLE:Release\/protected-primary-pipe/);
 mock.mock.restore();
 console.log('NATIVE_EXIT_PARENT_CASE_RECEIPTS '+JSON.stringify({historicalCaseResults:raw,rejectedControls:7,nativeRunsThisCheck:0}));
});
if(process.env.YMCC_NATIVE_HOST_EXIT_PARENT_CASE_RECEIPTS==='1')test('native exit parent current matrix distinguishes retained owners from readiness',t=>{
 const original=contract.originalEvidence[1],bytes=fs.readFileSync(original.path),output=path.dirname(original.path);
 assert.equal(createHash('sha256').update(bytes).digest('hex'),original.sha256);
 const parent=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
 const cases=[['capture-nonproducer-debt',false,true,true,false,false,true],['capture-worker-retained',false,false,false,true,true,false],
  ['capture-timer-retained',false,false,false,true,true,false],['capture-generation-mismatch',false,false,false,true,true,false],
  ['capture-stop-exception',false,false,false,true,true,false],['busy-host-budget',true,true,false,true,true,false],
  ['permanent-close-budget',true,true,false,false,true,true]];
 const common={scopedLocalOwnerPass:true,fullApplicationExitAccepted:false,assertionsPassed:1,endpointAndPhysicalReleaseProven:false,
  fixtureCleanup:{childKillOrEventBackstop:false,productClosedOriginalOwners:true,privateHandlesClosed:true,permanentFaultDestructionAccepted:false,faultReliefWasConditional:true}};
 const synthetic=new Map(); // Reader models only. No Native execution or lifecycle acceptance.
 for(const profile of parent.profiles){
  const add=(name,result)=>{
   const value={...structuredClone(common),scenario:name,...result};
   synthetic.set(path.join(output,profile.name,name+'-result.json'),Buffer.from(JSON.stringify(value)));
   profile.scenarios.push({name,exitCode:0,result:value});
  };
  for(const [name,captureReturned,producerReturned,localReturnedFlag,processOwnerRetained,primaryOwnerRetained,forceBudgetSpent] of cases){
   add(name,{actualEnsure:{returned:true,actualLocalGetter:true,wholeMainAndWmAppReadyExecuted:false},
    actualExitCleanup:{cleanupCompleted:true,readyPosts:1,cleanupCode:0,sameWorkerCompleted:true,originalTid:90},
    beforeFixtureFaultRelief:{workerStillRunning:true,workerTid:90,cleanupCompleted:false,readyPosts:0,checkpointLogCount:1,
     actualAwaitStageElapsedMs:31000,steppedSleepCalls:124,retainedReceipt:{observationEnded:false,continuationRetained:true,processExitAuthorized:false},
     captureReturned,producerReturned,ordinaryAttempted:forceBudgetSpent,localReturnedFlag,processOwnerRetained,primaryOwnerRetained,forceBudgetSpent,
     pipeState:{stopped:name!=='permanent-close-budget'},exceptionLogCount:name==='capture-stop-exception'?1:0}});
  }
  add('busy-pipe-independent-force',{realRuntimeMutexHeld:true,forceAttempts:1,retirementWallMsWhilePipeBusy:30,
   busyReceipt:{pipeOwnerAvailable:false,pipeReturned:false,forceAttempted:true}});
 }
 const readFile=fs.readFileSync;
 t.mock.method(fs,'readFileSync',(file,...args)=>synthetic.get(file)??readFile(file,...args));
 assert.equal(validateParentScenarios(parent,output).scenarioResults.length,22);
 const duplicate=structuredClone(parent);duplicate.profiles[0].scenarios[4]=duplicate.profiles[0].scenarios[3];
 assert.throws(()=>validateParentScenarios(duplicate,output),/NATIVE_EXIT_PARENT_MATRIX/);
 const reject=(name,mutate,error)=>{
  const draft=structuredClone(parent),result=draft.profiles[0].scenarios.find(row=>row.name===name).result;
  mutate(result);assert.throws(()=>validateParentScenarios(draft,output),error);
 };
 reject('capture-worker-retained',r=>{r.beforeFixtureFaultRelief.producerReturned=true;},/RETAINED_OWNER_BOUNDARY/);
 reject('busy-host-budget',r=>{r.beforeFixtureFaultRelief.processOwnerRetained=false;},/RETAINED_OWNER_BOUNDARY/);
 reject('permanent-close-budget',r=>{r.beforeFixtureFaultRelief.readyPosts=1;},/RETAINED_CHECKPOINT/);
 reject('capture-nonproducer-debt',r=>{r.actualExitCleanup.originalTid++;},/RETAINED_CHECKPOINT/);
 reject('busy-pipe-independent-force',r=>{r.busyReceipt.pipeReturned=true;},/BUSY_PIPE_RETAINS_DEBT/);
});
if(process.env.YMCC_NATIVE_HOST_EXIT_GATE_MATRIX==='1')test('native exit gate matrix preserves historical coverage and checks current exact selection',()=>{
 const original=contract.originalEvidence[2],bytes=fs.readFileSync(original.path);
 assert.equal(createHash('sha256').update(bytes).digest('hex'),original.sha256);
 const historical=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
 assert.equal(validateGateProfiles(historical,{sourceMode:'captured-execution-only'}).privateThreadsJoined,224);
 assert.throws(()=>validateGateProfiles(historical),/NATIVE_EXIT_GATES_SELECTION/);
 // Current rows below are deliberate models used only to exercise the JS reader.
 const model=structuredClone(historical);
 model.sourceHashes={};
 model.originalHostAndHistoricalControlsExecuted=true;
 const added=['ready:serial-not-returned','no-window-loop:serial-not-returned',
  'fast-completed-top:capture-not-returned','fast-completed-top:host-local-not-returned','fast-completed-top:serial-not-returned',
  'fast-completed-after-wait:capture-not-returned','fast-completed-after-wait:host-local-not-returned','fast-completed-after-wait:serial-not-returned'];
 for(const row of model.profiles){
  const current=row.route==='current';
  if(current)row.cases.push(...added.map(name=>({name,pass:true,processExitUsedAsCleanup:false,realPrivateResourcesZeroBeforeReturn:true})));
  if(row.route==='delete-no-window-host-gate'){
   row.failedChecks=row.failedChecks.filter(name=>!name.endsWith(':pending-loop-must-not-return-to-final-cleanup'));
   assert.equal(row.failedChecks.length,32);
   row.failures=row.failedChecks.length;
  }
  Object.assign(row,{checks:current?513:419,privateThreadsStarted:current?33:28,privateThreadsSignalledAndObserved:current?33:28,
   selection:current?'all':'legacy',expectedCases:row.cases.map(c=>c.name),serialOwnershipIsExplicitMemoryPort:true,
   realSerialWorkerExecuted:false,realSerialWaitOrJoinProven:false});
 }
 assert.deepEqual(validateGateProfiles(model),{profiles:2,currentCasesPerProfile:36,currentChecksPerProfile:513,
  specifiedNegativeBuilds:6,privateThreadsJoined:234,serialDeletionControls:'NOT_RUN',pass:true});
 const reordered=structuredClone(model);
 reordered.profiles.reverse();
 reordered.profiles[0].cases.reverse();
 reordered.profiles.at(-1).expectedCases.reverse();
 assert.deepEqual(validateGateProfiles(reordered),validateGateProfiles(model));
 for(const [edit,error] of [
  [r=>r.status='PASS_ACTUAL_SERIAL_UI_COMPLETION_GATES',/NATIVE_EXIT_GATES_SELECTION/],
  [r=>r.profiles.pop(),/NATIVE_EXIT_GATES_EIGHT_BUILDS/],
  [r=>r.profiles[0].checks=391,/NATIVE_EXIT_GATES_SPECIFIED_ASSERTIONS/],
  [r=>{const p=r.profiles[0];p.cases.at(-1).name=p.cases.at(-2).name;},/NATIVE_EXIT_GATES_EXACT_CASES/],
  [r=>r.profiles[0].expectedCases[0]='ready:wrong-case',/NATIVE_EXIT_GATES_EXACT_CASES/],
  [r=>r.profiles[0].privateThreadsSignalledAndObserved--,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
  [r=>r.profiles[0].cases.at(-1).realPrivateResourcesZeroBeforeReturn=false,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
  [r=>r.profiles[0].realSerialWaitOrJoinProven=true,/NATIVE_EXIT_GATES_NO_SCOPE_INFLATION/],
  [r=>{const p=r.profiles.find(q=>q.route==='delete-no-window-host-gate');p.failedChecks.pop();p.failedChecks.push('no-window-loop:host-process-retained:pending-loop-must-not-return-to-final-cleanup');},/NATIVE_EXIT_GATES_NEGATIVE_NOT_LOST/],
  [r=>{const p=r.profiles.find(q=>q.route==='delete-ready-host-gate');const i=p.failedChecks.indexOf('ready:host-local-not-returned:retain-cleanup-owner');p.failedChecks[i]='ready:host-local-not-returned:unrelated';},/NATIVE_EXIT_GATES_NEGATIVE_NOT_LOST/],
 ]){const draft=structuredClone(model);edit(draft);assert.throws(()=>validateGateProfiles(draft),error);}
 console.log('NATIVE_EXIT_GATE_MATRIX '+JSON.stringify({historicalRows:8,modeledCurrentRows:8,reorderedRowsAccepted:true,rejectedControls:11,nativeExecutions:0}));
});
const file=process.env.YMCC_NATIVE_HOST_EXIT_RECEIPT;
if(file){const r=read(file),fixed=r.schemaVersion===2,ctx={repo:REPO,sources:r.sourceHashes,tools:r.toolProfile?readNativeExitTools(REPO):toolInputs(REPO),sourceMode:'captured-execution-only',requireFixedBuild:fixed},p=read(r.children.parent.receipt.path),g=read(r.children.gates.receipt.path),h=read(r.children.helper.receipt.path);
 test('captured formal native exit snapshot executes all three actual consumers before parent fixture exit',()=>{const v=validateNativeExit(r,ctx);assert.equal(v.parent.scenariosPerProfile,contract.parentScenarios.length);assert.equal(v.gates.specifiedNegativeBuilds,6);assert.equal(v.gates.privateThreadsJoined,g.originalHostAndHistoricalControlsExecuted===true?234:224);assert.equal(v.fullLifecycleAcceptance,'INCOMPLETE');});
 for(const [name,change,expected] of [
 ['false whole lifecycle',x=>x.fullLifecycleAcceptance='PASS',/NATIVE_EXIT_AGGREGATE_SCOPE/],
 ['missing actual child',x=>delete x.children.parent,/NATIVE_EXIT_THREE_REAL_CHILDREN/],
 ['lost tool seal',x=>x.toolsAfter.pop(),/NATIVE_EXIT_COMPLETE_SOURCE_TOOL_SEALS/],
 ...(fixed?[
  ['historical archive claimed as current inputs',x=>x.compositionArchive.usedForCurrentBuild=true,/NATIVE_EXIT_HISTORICAL_REFERENCE_SCOPE/],
  ['current receipt downgraded to legacy',x=>{x.schemaVersion=1;delete x.toolEvidenceMode;},/NATIVE_EXIT_TOOL_EVIDENCE_MODE/],
  ['current command falls back to vcvars',x=>x.children.helper.args.pop(),/NATIVE_EXIT_ACTUAL_CONSUMER_COMMAND/],
  ['owned child workspace not bound',x=>x.children.helper.ownedWorkspace.jobIdentity.sha256='0'.repeat(64),/NATIVE_EXIT_OWNED_CHILD_PIN/],
 ]:[['lost full nested closure',x=>delete x.nestedAfter,/NATIVE_EXIT_NESTED_SEAL_PIN/]]),
 ['unsettled child execution',x=>x.children.parent.timedOut=true,/NATIVE_EXIT_ACTUAL_CHILD_COMPLETION/],
 ['changed executed receipt',x=>x.children.parent.receipt.sha256='0'.repeat(64),/NATIVE_EXIT_ACTUAL_CHILD_COMPLETION/],
 ])test('fresh native aggregate rejects '+name,()=>{const x=structuredClone(r);change(x);assert.throws(()=>validateNativeExit(x,ctx),expected);});
 for(const [name,change,expected] of [
 ['success before process join',x=>x.profiles[0].scenarios[1].result.productionResourceProof.processExitedAndJoined=false,/NATIVE_EXIT_PARENT_KERNEL_RETURN/],
 ['pending pipe owner',x=>x.profiles[0].scenarios[1].result.productionResourceProof.pipeRuntime.pending=1,/NATIVE_EXIT_PARENT_PIPE_RETURN/],
 ['fixture kill masquerades as product',x=>x.profiles[0].scenarios[1].result.fixtureCleanup.childKillOrEventBackstop=true,/NATIVE_EXIT_PARENT_NO_SALVAGE/],
 ['physical return invented',x=>x.profiles[0].scenarios[1].result.endpointAndPhysicalReleaseProven=true,/NATIVE_EXIT_PARENT_NO_SALVAGE/],
 ['READY during retained debt',x=>x.profiles[0].scenarios[2].result.beforeFixtureFaultRelief.readyPosts=1,/NATIVE_EXIT_PARENT_RETENTION/],
 ['repeat force budget',x=>x.profiles[0].scenarios[1].result.productionResourceProof.forceAttempts=2,/NATIVE_EXIT_PARENT_KERNEL_RETURN/],
 ])test('fresh full native owner rejects '+name,()=>{const x=structuredClone(p);change(x);assert.throws(()=>validateParent(x,ctx,r.children.parent.output),expected);});
 if(fixed)for(const [name,change] of [
  ['false execution marker',row=>{row.executedThisRun=false;}],
  ['missing execution marker',row=>{delete row.executedThisRun;}],
 ])test('fresh fixed native parent rejects '+name,()=>{
  const x=structuredClone(p),row=x.profiles.find(profile=>profile.name==='Release').scenarios.find(s=>s.name==='healthy-ordinary');
  change(row);
  assert.throws(()=>validateParent(x,ctx,r.children.parent.output),/NATIVE_EXIT_PARENT_CASE_NOT_EXECUTED:Release\/healthy-ordinary/);
 });
 for(const [name,change,expected] of [
 ['lost negative route',x=>x.profiles.pop(),/NATIVE_EXIT_GATES_EIGHT_BUILDS/],
 ['uncaught old completion gate',x=>x.profiles.find(p=>p.route==='historical-before-host-gate').failures=0,/NATIVE_EXIT_GATES_SPECIFIED_ASSERTIONS/],
 ['private handle remains',x=>x.profiles[0].privateHandlesRemaining=1,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
 ['process exit used as cleanup',x=>x.profiles[0].processExitUsedAsCleanup=true,/NATIVE_EXIT_GATES_BEFORE_EXIT_ZERO/],
 ])test('fresh completion consumers reject '+name,()=>{const x=structuredClone(g);change(x);assert.throws(()=>validateGates(x,ctx,r.children.gates.output),expected);});
 if(fixed)for(const [name,change,expected] of [
  ['missing graph',x=>x.fixedNativeBuild.graphs.pop(),/NATIVE_EXIT_FIXED_GRAPH_COUNT/],
  ['mutated graph pin',x=>x.fixedNativeBuild.graphs[0].sha256='0'.repeat(64),/NATIVE_EXIT_FIXED_PIN/],
  ['inherited linker search override',x=>x.fixedNativeBuild.environment.LIB='unreviewed',/NATIVE_EXIT_FIXED_ENV:LIB/],
  ['graph-only execution',x=>x.fixedNativeBuild.scenarioBinariesLaunched=0,/NATIVE_EXIT_FIXED_PROFILE_OR_LAUNCHES/],
 ])test('fresh fixed build reader rejects '+name,()=>{const x=structuredClone(h);change(x);assert.throws(()=>validateHelper(x,ctx,r.children.helper.output),expected);});
 test('fresh helper rejects a force request without actual private process destruction',()=>{const x=structuredClone(h);x.profiles[0].result.rows[0].evidence.processAndThreadJoined=false;assert.throws(()=>validateHelper(x,ctx,r.children.helper.output),/NATIVE_EXIT_HELPER_REAL_DESTRUCTION/);});
}
