import test from 'node:test';
import assert from 'node:assert/strict';
import { ORIGINAL_NATIVE_BUDGET_MS, readCompletedNativeReport, createNativeObservationBudget } from './r3-terminal22-observation-terminal.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { resolveTerminal22PreparationLayout } from './r3-terminal22-prepare.mjs';
import { terminal22ExecutionFromProcess,parseTerminal22ObservationArguments,terminal22LifecyclePreparationPins,validateTerminal22LifecycleLayout,verifyTerminal22LifecycleCapture,terminal22LifecycleManagedDllPin,finalizeTerminal22LifecycleExecution,terminal26JointFixturePreflightBytes } from './r3-terminal22-qualified-observe.mjs';
import {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';
import {requireCurrentPreparedContext} from './r3-current-prepared.mjs';
import { validateSourceSnapshotRoots,createCapturedSourceSnapshot } from '../source-snapshot.mjs';
import {createHash as lifecycleRev2CreateHash} from 'node:crypto';
import { runProcess } from '../runner.mjs';

const complete = (change = {}) => ({timedOut:false,launchError:null,signal:null,
  outputTruncated:false,durationMs:59000,exitCode:0,...change});
function rejectedBeforeReport(terminal, code, expectedExit=0) {
  let reads=0;
  assert.throws(()=>readCompletedNativeReport(terminal,expectedExit,()=>{
    ++reads;throw new SyntaxError('Unexpected end of JSON input');
  }),error=>error.code===code);
  assert.equal(reads,0,'unadmitted native execution must not parse partial evidence');
}
test('recorded timeout wins over empty-report parser failure',()=>
  rejectedBeforeReport(complete({timedOut:true,signal:'SIGTERM',exitCode:null,durationMs:60038}),
    'NATIVE22_ORIGINAL_BUDGET_TIMEOUT'));
test('timeout cannot be concealed by a forged complete PASS report',()=>{
  let reads=0;assert.throws(()=>readCompletedNativeReport(complete({timedOut:true}),0,
    ()=>{++reads;return {status:'PASS',checks:6457};}),
    error=>error.code==='NATIVE22_ORIGINAL_BUDGET_TIMEOUT');assert.equal(reads,0);
});
for(const [name,change,code] of [
 ['missing timeout fact',{timedOut:undefined},'NATIVE22_TERMINAL_METADATA_INCOMPLETE'],
 ['string timeout fact',{timedOut:'false'},'NATIVE22_TERMINAL_METADATA_INCOMPLETE'],
 ['launch failure',{launchError:'ENOENT'},'NATIVE22_PROCESS_LAUNCH_UNPROVEN'],
 ['missing launch fact',{launchError:undefined},'NATIVE22_PROCESS_LAUNCH_UNPROVEN'],
 ['abnormal process signal',{signal:'SIGTERM'},'NATIVE22_NATURAL_EXIT_UNPROVEN'],
 ['missing signal fact',{signal:undefined},'NATIVE22_NATURAL_EXIT_UNPROVEN'],
 ['truncated output',{outputTruncated:true},'NATIVE22_OUTPUT_INCOMPLETE'],
 ['missing output fact',{outputTruncated:undefined},'NATIVE22_OUTPUT_INCOMPLETE'],
 ['negative duration',{durationMs:-1},'NATIVE22_DURATION_UNPROVEN'],
 ['missing duration',{durationMs:undefined},'NATIVE22_DURATION_UNPROVEN'],
 ['nonfinite duration',{durationMs:Infinity},'NATIVE22_DURATION_UNPROVEN'],
 ['unreported deadline overrun',{durationMs:60001},'NATIVE22_ORIGINAL_BUDGET_EXCEEDED'],
 ['unexpected successful exit',{exitCode:1},'NATIVE22_ORIGINAL_EXIT_MISMATCH'],
 ['missing exit code',{exitCode:null},'NATIVE22_ORIGINAL_EXIT_MISMATCH']
])test(name+' cannot parse a report',()=>rejectedBeforeReport(complete(change),code));
test('original positive admits one complete report read',()=>{
  let reads=0;const raw={status:'PASS',checks:6457};
  assert.equal(readCompletedNativeReport(complete(),0,()=>{++reads;return raw;}),raw);
  assert.equal(reads,1);
});
test('specified semantic-negative exit is admitted without relabelling FAIL',()=>{
  const raw={status:'FAIL',failures:[{assertion:'original negative'}]};
  assert.equal(readCompletedNativeReport(complete({exitCode:1}),1,()=>raw),raw);
});
test('exact original budget boundary remains 60000ms',()=>{
  assert.equal(ORIGINAL_NATIVE_BUDGET_MS,60000);
  const raw={};assert.equal(readCompletedNativeReport(complete({durationMs:60000}),0,()=>raw),raw);
});
test('natural exit does not convert malformed report to pass',()=>{
  assert.throws(()=>readCompletedNativeReport(complete(),0,()=>JSON.parse('')),SyntaxError);
});
test('natural exit does not conceal a missing final report',()=>{
  const missing=new Error('complete native raw report missing');
  assert.throws(()=>readCompletedNativeReport(complete(),0,()=>{throw missing;}),error=>error===missing);
});
test('report content and original semantic validation are left to caller',()=>{
  const raw={status:'FAIL',checks:0};
  assert.equal(readCompletedNativeReport(complete(),0,()=>raw),raw);
});
test('unsupported terminal contract is rejected',()=>{
  for(const code of [-1,2,null,undefined])assert.throws(
    ()=>readCompletedNativeReport(complete(),code,()=>({})),
    error=>error.code==='NATIVE22_TERMINAL_INPUT_INVALID');
});

function producerWiringFixture(t){
 const requested=process.env.YMCC_TERMINAL22_TEST_TEMP,core=path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
 assert(requested===undefined||(typeof requested==='string'&&path.isAbsolute(requested)),'Absolute G-core task temp root required when explicitly set');
 // Standalone discovery owns a fresh child of Workspace; explicit overrides retain the strict child-root gate.
 const temp=requested===undefined?core:path.resolve(requested),relative=path.relative(core,temp);
 assert(requested===undefined?relative==='':relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));assert(!fs.lstatSync(temp).isSymbolicLink());assert.equal(fs.realpathSync(temp).toLowerCase(),temp.toLowerCase());
 const base=fs.mkdtempSync(path.join(temp,'terminal22-producer-'));
 t.after(()=>{const resolved=path.resolve(base);assert.equal(path.dirname(resolved),temp);assert(path.basename(resolved).startsWith('terminal22-producer-'));assert(!fs.lstatSync(resolved).isSymbolicLink());assert.equal(fs.realpathSync(resolved).toLowerCase(),resolved.toLowerCase());fs.rmSync(resolved,{recursive:true,force:true});});
 const primaryRoot=path.join(base,'Mainline','YeManCC-source','YeManCC'),coreWorkspace=path.join(base,'Testing','YMCC','Workspace'),evidence=path.join(base,'Testing','YMCC','Evidence');
 fs.mkdirSync(primaryRoot,{recursive:true});fs.mkdirSync(coreWorkspace,{recursive:true});fs.mkdirSync(evidence,{recursive:true});
 return {base,primaryRoot,coreWorkspace,evidence};
}
test('terminal22 producer wiring: core owner and legacy evidence layouts use the original roots gate without capture',t=>{
 const f=producerWiringFixture(t),owner=path.join(f.coreWorkspace,'owner-a','run-1');
 const core=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
 assert.equal(core.layoutMode,'G_CORE_WORKSPACE_OWNER');assert.equal(core.workspaceRoot,owner);assert.equal(core.sourceRoot,path.join(owner,'Mainline','YeManCC-source','YeManCC'));assert.equal(core.authorizedBuildRoot,path.join(owner,'Build','Validation','CurrentCoordinatorTerminal22'));
 assert(!fs.existsSync(owner),'Layout resolution must not create directories or start capture');fs.mkdirSync(path.dirname(core.sourceRoot),{recursive:true});assert.deepEqual(validateSourceSnapshotRoots(f.primaryRoot,core.sourceRoot),{sourceRoot:f.primaryRoot,snapshotRoot:core.sourceRoot});assert(!fs.existsSync(core.sourceRoot));
 const legacy=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:path.join(f.evidence,'legacy-run')});assert.equal(legacy.layoutMode,'LEGACY_EVIDENCE_SIBLING');assert.equal(path.dirname(legacy.sourceRoot),path.dirname(f.primaryRoot));assert(path.basename(legacy.sourceRoot).startsWith('YeManCC-terminal22-'));assert.deepEqual(validateSourceSnapshotRoots(f.primaryRoot,legacy.sourceRoot),{sourceRoot:f.primaryRoot,snapshotRoot:legacy.sourceRoot});assert(!fs.existsSync(legacy.sourceRoot));assert(!fs.existsSync(legacy.workspaceRoot));
});
test('terminal22 producer wiring: fresh owner and reparse violations reject before capture',t=>{
 const f=producerWiringFixture(t),resolve=workspaceRoot=>resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot});
 for(const target of [f.coreWorkspace,f.evidence,path.join(f.base,'foreign-owner'),path.resolve(f.coreWorkspace,'owner','..','..','escaped-owner')])assert.throws(()=>resolve(target));
 const reused=path.join(f.coreWorkspace,'reused');fs.mkdirSync(reused);fs.writeFileSync(path.join(reused,'sentinel.txt'),'unchanged');assert.throws(()=>resolve(reused),/FRESH_ABSOLUTE_WORKSPACE_REQUIRED/);assert.equal(fs.readFileSync(path.join(reused,'sentinel.txt'),'utf8'),'unchanged');
 const actual=path.join(f.base,'junction-target'),alias=path.join(f.coreWorkspace,'alias-owner');fs.mkdirSync(actual);fs.symlinkSync(actual,alias,'junction');
 try{assert.throws(()=>resolve(path.join(alias,'run')),/TERMINAL22_PREPARATION_REPARSE_PATH/);assert.deepEqual(fs.readdirSync(actual),[]);}finally{fs.unlinkSync(alias);}
 assert.deepEqual(fs.readdirSync(f.primaryRoot),[],'Rejected layout must not create or capture product files');
});
test('terminal22 producer wiring: actual process outcomes populate execution without granting qualification',async t=>{
 const f=producerWiringFixture(t);
 for(const exitCode of [0,1]){
  const {stdout,stderr,...terminal}=await runProcess(process.execPath,['--eval','process.exit('+exitCode+')'],{cwd:f.base,env:process.env,timeoutMs:5000});
  const execution=terminal22ExecutionFromProcess(terminal);assert.equal(execution.exitCode,exitCode);assert.equal(execution.signal,terminal.signal);assert.equal(execution.outerTimedOut,terminal.timedOut);assert.equal(execution.outerTimedOut,false);assert.equal(execution.naturalExit,true);assert.equal(execution.observerCliExitObserved,false);assert.equal(execution.supervisionScope,'original-runProcess-native-child-deadline-and-close');
  let reads=0;const raw={status:exitCode?'FAIL':'PASS'};assert.equal(readCompletedNativeReport(terminal,exitCode,()=>{reads++;return raw;}),raw);assert.equal(reads,1);assert.equal(execution.exitCode,exitCode,'A semantic-negative exit must remain1');
 }
 const {stdout,stderr,...timed}=await runProcess(process.execPath,['--eval','setTimeout(()=>{},60000)'],{cwd:f.base,env:process.env,timeoutMs:250});
 const execution=terminal22ExecutionFromProcess(timed);assert.equal(timed.timedOut,true);assert.equal(execution.outerTimedOut,true);assert.equal(execution.exitCode,timed.exitCode);assert.equal(execution.signal,timed.signal);assert.equal(execution.naturalExit,false);let reads=0;
 assert.throws(()=>readCompletedNativeReport(timed,0,()=>{reads++;return {status:'PASS'};}),error=>error.code==='NATIVE22_ORIGINAL_BUDGET_TIMEOUT');assert.equal(reads,0);
 const missing=terminal22ExecutionFromProcess({});assert.equal(missing.exitCode,undefined);assert.equal(missing.signal,undefined);assert.equal(missing.outerTimedOut,undefined);assert.equal(missing.naturalExit,false);
});

// Pure API samples below are not native preparation, source or runtime qualification receipts.
const lifecycleRaw=change=>({status:'PASS',forbiddenIo:0,fullTranslationUnit:true,normalProductMainExecuted:false,realDeviceIo:false,checks:1,failures:[],cases:[],...change});
test('terminal22 lifecycle qualification: one shared original16 plan preserves order and negative labels',()=>{
 const plan=currentLifecycleModePlan();assert.equal(plan.length,16);
 assert.deepEqual(plan.slice(0,12).map(row=>row.mode),['entry','mutation1','mutation2','rollback','queued-exception','queued-exception-common','healthy-log-exception','start-protection','recovery-protection','record-read','switch-recovery','tuple-protection']);
 assert(plan.slice(0,12).every(row=>row.role==='positive'&&row.mutation===null));assert.deepEqual(plan.slice(0,3).map(row=>row.expectedExit),[0,1,1]);
 assert.deepEqual(plan.slice(12).map(currentLifecycleModeLabel),['negative-bypass-admission','negative-rollback-recovery-inheritance','negative-rollback-switch-return-rearm','negative-rollback-switch-fault-owner']);assert(plan.slice(12).every(row=>row.expectedExit===1));
 plan[0].mode='forged';assert.equal(currentLifecycleModePlan()[0].mode,'entry');assert.throws(()=>currentLifecycleModeLabel(plan[0]),/ORIGINAL_LIFECYCLE_PLAN_MEMBER_REQUIRED/);
});
test('terminal22 lifecycle qualification: entry requires all56 and exact retained failure polarity without runtime authority',()=>{
 const plan=currentLifecycleModePlan(),entry=plan[0],lines=Array.from({length:56},(_,i)=>'PASS original-entry-'+i).join('\n');
 const proof=verifyCurrentLifecycleMode(entry,lifecycleRaw(),{entryText:lines});assert.equal(proof.preparedContextGranted,false);assert.equal(proof.runtimeAuthorityGranted,false);assert.throws(()=>requireCurrentPreparedContext(proof),/LIVE_BRANDED_CONTEXT_REQUIRED/);
 assert.throws(()=>verifyCurrentLifecycleMode(entry,lifecycleRaw(),{entryText:lines.split('\n').slice(1).join('\n')}),/ORIGINAL56_ENTRY_CASES_REQUIRED/);
 const wrong=Array.from({length:56},(_,i)=>i?'PASS original-entry-'+i:'FAIL different-entry').join('\n');assert.throws(()=>verifyCurrentLifecycleMode(plan[1],lifecycleRaw({status:'FAIL'}),{entryText:wrong}),/ORIGINAL_ENTRY_POLARITY_REQUIRED/);
});
test('terminal22 lifecycle qualification: designated negatives and return tuple validators remain mandatory',()=>{
 for(const row of currentLifecycleModePlan().slice(12))assert.throws(()=>verifyCurrentLifecycleMode(row,lifecycleRaw({status:'FAIL',failures:[{assertion:'unrelated failure'}]})),/ORIGINAL_LIFECYCLE_SEMANTIC_NEGATIVE_REQUIRED/);
 const plan=currentLifecycleModePlan();assert.throws(()=>verifyCurrentLifecycleMode(plan.find(row=>row.mode==='switch-recovery'&&!row.mutation),lifecycleRaw()),/R3_ALL43_NORMAL_RETURN_921_REQUIRED/);assert.throws(()=>verifyCurrentLifecycleMode(plan.find(row=>row.mode==='tuple-protection'),lifecycleRaw()),/R3_EXACT_TUPLE_CONSUMER_REQUIRED/);
 assert.throws(()=>verifyCurrentLifecycleMode(plan[3],lifecycleRaw({forbiddenIo:1})),/OFFLINE_COMPLETE_NATIVE_MODE_REQUIRED/);
});
test('terminal22 lifecycle qualification: original45 mode and90 group budget never resets remaining time',()=>{
 assert.deepEqual(currentLifecycleRemainingBudget(1000,1000),{originalModeBudgetMs:45000,originalGroupBudgetMs:90000,groupWallMs:0,remainingGroupMs:90000,timeoutMs:45000});assert.equal(currentLifecycleRemainingBudget(1000,90999).timeoutMs,1);
 for(const now of [91000,91001,999,NaN,Infinity])assert.throws(()=>currentLifecycleRemainingBudget(1000,now),/ORIGINAL_GROUP90_BUDGET_EXCEEDED/);
});
test('terminal22 lifecycle qualification: explicit lifecycle mode is mutually exclusive and pipe CLI remains compatible',()=>{
 const input='G:/unit-inputs/lifecycle.json',out='G:/unit-inputs/observe';assert.equal(parseTerminal22ObservationArguments(['--lifecycle-preparations',input,'--out',out])['--lifecycle-preparations'],input);
 assert.equal(parseTerminal22ObservationArguments(['--native-preparation','relative-native.json','--out','relative-out'])['--native-preparation'],'relative-native.json');
 for(const args of [[],['--out',out],['--native-preparation',input,'--lifecycle-preparations',input,'--out',out],['--lifecycle-preparations',input,'--out',out,'--out',out],['--lifecycle-preparations','relative.json','--out',out],['--lifecycle-preparations',input,'--out',out,'--unknown','value']])assert.throws(()=>parseTerminal22ObservationArguments(args));
});
test('terminal22 lifecycle qualification: declaration pins are syntax only and missing duplicate or foreign roles reject',()=>{
 const roles=[...new Set(currentLifecycleModePlan().map(row=>row.role))],declaration={schemaVersion:1,kind:'TERMINAL22_LIFECYCLE_COMPILED_INPUTS',preparations:roles.map(role=>({role,path:'G:/unit-inputs/'+role+'/NATIVE-PREPARATION.json',sha256:'a'.repeat(64)}))};
 const pins=terminal22LifecyclePreparationPins(declaration);assert.deepEqual(pins.map(row=>row.role),roles);assert.throws(()=>requireCurrentPreparedContext(pins),/LIVE_BRANDED_CONTEXT_REQUIRED/);
 for(const mutate of [d=>d.kind='QUALIFIED',d=>d.preparations.pop(),d=>d.preparations[1]=d.preparations[0],d=>d.preparations.reverse(),d=>d.preparations[0].path='relative.json',d=>d.preparations[0].sha256='PASS',d=>d.preparations[0].role='cancellation',d=>d.accepted=true]){const bad=structuredClone(declaration);mutate(bad);assert.throws(()=>terminal22LifecyclePreparationPins(bad));}
});

// REV2 uses the existing owned-temp fixture and actual local validation APIs only.
const lifecycleRev2Sha=bytes=>lifecycleRev2CreateHash('sha256').update(bytes).digest('hex');
function lifecycleRev2Layout(f,ownerName='rev2-owner'){
 const owner=path.join(f.coreWorkspace,ownerName),sourceRoot=path.join(owner,'Mainline','YeManCC-source','YeManCC');fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});
 return {primaryRoot:f.primaryRoot,sourceRoot,workspaceRoot:owner,authorizedBuildRoot:path.join(owner,'Build','Validation','CurrentCoordinatorTerminal22')};
}
test('terminal22 lifecycle qualification rev2: trusted verifier rejects repinned captured code before any import',t=>{
 const f=producerWiringFixture(t),layout=lifecycleRev2Layout(f),verifierBytes=fs.readFileSync(new URL('../source-snapshot.mjs',import.meta.url)),runnerBytes=fs.readFileSync(new URL('../runner.mjs',import.meta.url));
 const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);};
 write('native/main.cpp','// synthetic owned source, never compiled\n');write('tools/ymcc_sim/global/source-snapshot.mjs',verifierBytes);write('tools/ymcc_sim/global/runner.mjs',runnerBytes);
 const capture=createCapturedSourceSnapshot({repoRoot:f.primaryRoot,outDir:layout.sourceRoot});assert.equal(capture.status,'PASS');layout.manifestSha256=lifecycleRev2Sha(fs.readFileSync(capture.manifestFile));layout.verifierCodeSha256=lifecycleRev2Sha(verifierBytes);
 assert.equal(verifyTerminal22LifecycleCapture(layout,f.primaryRoot).status,'FROZEN_SOURCE_VERIFIED');
 const marker=path.join(f.base,'untrusted-module-executed.txt'),payload='import fs from "node:fs";fs.writeFileSync('+JSON.stringify(marker)+',"must not execute");export const forged=true;\n',copiedVerifier=path.join(layout.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs');
 fs.writeFileSync(copiedVerifier,payload);layout.verifierCodeSha256=lifecycleRev2Sha(Buffer.from(payload));assert.throws(()=>verifyTerminal22LifecycleCapture(layout,f.primaryRoot),/differs from trusted local implementation/);assert(!fs.existsSync(marker));
 fs.writeFileSync(copiedVerifier,verifierBytes);layout.verifierCodeSha256=lifecycleRev2Sha(verifierBytes);fs.writeFileSync(path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs'),payload);
 assert.throws(()=>verifyTerminal22LifecycleCapture(layout,f.primaryRoot),/SNAPSHOT_CAPTURED_VERSION_CHANGED/);assert(!fs.existsSync(marker));
});
test('terminal22 lifecycle qualification rev2: original producer owner and exact build relationships cannot be self declared',t=>{
 const f=producerWiringFixture(t),layout=lifecycleRev2Layout(f);assert.equal(validateTerminal22LifecycleLayout(layout,f.primaryRoot).runtimeAuthorityGranted,false);
 const legacyOwner=path.join(f.evidence,'legacy-rev2'),legacy={...layout,workspaceRoot:legacyOwner,authorizedBuildRoot:path.join(legacyOwner,'Build','Validation','CurrentCoordinatorTerminal22'),sourceRoot:path.join(path.dirname(f.primaryRoot),'YeManCC-terminal22-00000000-0000-4000-8000-000000000000')};
 assert.equal(validateTerminal22LifecycleLayout(legacy,f.primaryRoot).runtimeAuthorityGranted,false);
 const foreignOwner='G:/UnrelatedOwner',foreign={...layout,workspaceRoot:foreignOwner,authorizedBuildRoot:path.join(foreignOwner,'Build','Validation','CurrentCoordinatorTerminal22')};assert.throws(()=>validateTerminal22LifecycleLayout(foreign,f.primaryRoot),/owner outside trusted project/);
 assert.throws(()=>validateTerminal22LifecycleLayout({...layout,sourceRoot:path.join(f.coreWorkspace,'other-owner','Mainline','YeManCC-source','YeManCC')},f.primaryRoot),/capture does not belong to exact core owner/);
 assert.throws(()=>validateTerminal22LifecycleLayout({...layout,authorizedBuildRoot:path.join(layout.workspaceRoot,'Build','Validation','ForeignSubRoot')},f.primaryRoot),/exact original lifecycle authorized build root/);
 assert.throws(()=>validateTerminal22LifecycleLayout({...layout,primaryRoot:path.join(f.base,'UnrelatedPrimary')},f.primaryRoot),/primary differs from trusted observer/);
 assert(!fs.existsSync(layout.authorizedBuildRoot),'Relation rejection must not create observation/build outputs');
});
test('terminal22 lifecycle qualification rev2: actual managed DLL must be scoped unaliased and an exact runtime member',t=>{
 const f=producerWiringFixture(t),layout=lifecycleRev2Layout(f),folder=path.join(layout.authorizedBuildRoot,'managed'),dll=path.join(folder,'Fixture.dll');fs.mkdirSync(folder,{recursive:true});fs.writeFileSync(dll,'unit test bytes, never loaded');
 const proof={path:dll,sha256:lifecycleRev2Sha(fs.readFileSync(dll))},managed={fixtureDll:dll,runtimeArtifacts:[proof]};assert.deepEqual(terminal22LifecycleManagedDllPin(managed,proof,layout.authorizedBuildRoot),proof);
 assert.throws(()=>terminal22LifecycleManagedDllPin({...managed,runtimeArtifacts:[]},proof,layout.authorizedBuildRoot),/must be one exact runtime artifact/);
 const outsideAlias=path.join(f.base,'root-outside-dll-alias'),insideAlias=path.join(layout.authorizedBuildRoot,'inside-dll-alias');fs.symlinkSync(folder,outsideAlias,'junction');fs.symlinkSync(folder,insideAlias,'junction');
 try{for(const [alias,pattern]of[[outsideAlias,/managed DLL escaped authority/],[insideAlias,/lifecycle (?:reparse path|path redirection) rejected/]]){const pathAlias=path.join(alias,'Fixture.dll');assert.throws(()=>terminal22LifecycleManagedDllPin({...managed,fixtureDll:pathAlias},{...proof,path:pathAlias},layout.authorizedBuildRoot),pattern);}}finally{fs.unlinkSync(outsideAlias);fs.unlinkSync(insideAlias);}
 fs.appendFileSync(dll,'changed');assert.throws(()=>terminal22LifecycleManagedDllPin(managed,proof,layout.authorizedBuildRoot),/managed DLL changed/);
});
test('terminal22 lifecycle qualification rev2: one final wall sample decides deadline status exit and recorded time',()=>{
 const modeResults=currentLifecycleModePlan().map(test=>({label:currentLifecycleModeLabel(test),semanticAccepted:true,nativeProcess:{timedOut:false,signal:null}}));
 const before=finalizeTerminal22LifecycleExecution({started:0,ended:89999,complete:true,modeResults});assert.equal(before.execution.groupWallMs,89999);assert.equal(before.execution.exitCode,0);assert.equal(before.execution.outerTimedOut,false);assert.equal(before.qualificationAccepted,false);
 const after=finalizeTerminal22LifecycleExecution({started:0,ended:90001,complete:true,modeResults});assert.equal(after.status,'INCOMPLETE');assert.equal(after.deadlineExceeded,true);assert.equal(after.execution.groupWallMs,90001);assert.equal(after.execution.exitCode,1);assert.equal(after.execution.outerTimedOut,true);assert.equal(after.finishedAt,new Date(90001).toISOString());assert.equal(after.execution.observerCliExitObserved,false);
 const boundary=finalizeTerminal22LifecycleExecution({started:0,ended:90000,complete:true,modeResults});assert.equal(boundary.execution.exitCode,0);assert.equal(boundary.execution.groupWallMs,90000);
});

import {verifyTerminal23PipeSource,validateTerminalLifecycleNativeReceipt} from './r3-terminal22-qualified-observe.mjs';

test('decoder23 pipeline: mismatched native or managed source cannot pass the pre-import capture barrier',t=>{
 const f=producerWiringFixture(t),layout=lifecycleRev2Layout(f),verifierBytes=fs.readFileSync(new URL('../source-snapshot.mjs',import.meta.url)),runnerBytes=fs.readFileSync(new URL('../runner.mjs',import.meta.url));
 const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);};
 write('native/main.cpp','// synthetic data fixture, never compiled\n');write('tools/ymcc_sim/global/source-snapshot.mjs',verifierBytes);write('tools/ymcc_sim/global/runner.mjs',runnerBytes);
 const capture=createCapturedSourceSnapshot({repoRoot:f.primaryRoot,outDir:layout.sourceRoot});assert.equal(capture.status,'PASS');layout.manifestSha256=lifecycleRev2Sha(fs.readFileSync(capture.manifestFile));layout.verifierCodeSha256=lifecycleRev2Sha(verifierBytes);
 const n={sourceRoot:layout.sourceRoot},preparation={sourceRoot:layout.sourceRoot};assert.equal(verifyTerminal23PipeSource(n,preparation,layout,f.primaryRoot).status,'FROZEN_SOURCE_VERIFIED');
 const foreign=path.join(f.base,'unverified-code-root'),marker=path.join(f.base,'foreign-import-marker');fs.mkdirSync(foreign);fs.writeFileSync(path.join(foreign,'oracle.mjs'),'import fs from "node:fs";fs.writeFileSync('+JSON.stringify(marker)+',"must not run");');
 assert.throws(()=>verifyTerminal23PipeSource({...n,sourceRoot:foreign},preparation,layout,f.primaryRoot),/source roots must match verified layout/);
 assert.throws(()=>verifyTerminal23PipeSource(n,{...preparation,sourceRoot:foreign},layout,f.primaryRoot),/source roots must match verified layout/);assert(!fs.existsSync(marker));
});
test('decoder23 pipeline: actual lifecycle receipt count follows its exact contract and retains every compile-only guard',()=>{
 const receipt=count=>({managedAdmissions:count,native:{status:'COMPILED_NOT_EXECUTED',compileOnly:true,compilerExitCode:0,compilerSignal:null,compilerError:null,runBranchReached:false,fixtureExecuted:false,nativeExecuted:false,normalProductMainExecuted:false,realDeviceIo:false,mutation:null}});
 for(const [id,count]of[['current105-terminal22-v1',22],['current105-terminal23-sd-decoder-v1',23]]){const good=receipt(count),proof=validateTerminalLifecycleNativeReceipt(good,id,null);assert.equal(proof.managedSources,count);assert.equal(proof.runtimeAuthorityGranted,false);assert.throws(()=>validateTerminalLifecycleNativeReceipt(receipt(count===22?23:22),id,null));
  for(const [key,value]of[['status','FAILED'],['compileOnly',false],['compilerExitCode',1],['compilerSignal','SIGTERM'],['compilerError','compiler failed'],['runBranchReached',true],['fixtureExecuted',true],['nativeExecuted',true],['normalProductMainExecuted',true],['realDeviceIo',true],['mutation','different']]){const bad=receipt(count);bad.native[key]=value;assert.throws(()=>validateTerminalLifecycleNativeReceipt(bad,id,null));}}
});


import {terminalNativeImplementationRoot,terminal26LocalNativeToolFiles} from './r3-terminal22-native-prepare.mjs';
import {terminalManagedRuntimeEnvironment,terminalRuntimeOracleRoot} from './r3-terminal22-qualified-observe.mjs';
import {fileURLToPath as native26FileURLToPath} from 'node:url';
test('signal26 native routing: trusted fixture implementation keeps original frozen source root separate',()=>{
 const primary=path.resolve(path.dirname(native26FileURLToPath(import.meta.url)),'../../../..');
 const captured=path.resolve('G:/owned-frozen-source-sample');
 const layout={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',primaryRoot:primary,sourceRoot:captured};
 assert.equal(terminalNativeImplementationRoot(layout),primary);assert.equal(terminalRuntimeOracleRoot(layout),path.join(primary,'tools/ymcc_sim/global/controller-coordinator-pipe'));
 const files=terminal26LocalNativeToolFiles(layout);
 assert(files.length>20&&files.length<64);assert.equal(new Set(files).size,files.length);
 assert(files.every(file=>path.isAbsolute(file)&&path.relative(primary,file)&&!path.relative(primary,file).startsWith('..')));
 assert(files.some(file=>file.endsWith('input_coordinator_terminal26_source_profile.mjs')));
 assert(files.some(file=>file.endsWith('r3-terminal26-compile-source-profile.json')));for(const name of ['r3-current105-oracle.mjs','r3-current105-ordered-observation.mjs','r3-current105-original-groups.mjs','r3-terminal26-runtime-expectations.json','r3-terminal22-qualified-observe.mjs','r3-terminal26-compile-source-profile-v2.json','pipe_cases.inc','prepare_ack_cases.inc'])assert.equal(files.filter(file=>file.endsWith(name)).length,1,'exact paired consumer/data member '+name);
 assert.equal(layout.sourceRoot,captured);
 for(const id of ['current105-terminal22-v1','current105-terminal23-sd-decoder-v1']){
  assert.equal(terminalNativeImplementationRoot({...layout,managedInputContract:id}),captured);assert.equal(terminalRuntimeOracleRoot({...layout,managedInputContract:id}),path.join(captured,'tools/ymcc_sim/global/controller-coordinator-pipe'));
  assert.deepEqual(terminal26LocalNativeToolFiles({...layout,managedInputContract:id}),[]);
 }
 assert.throws(()=>terminalNativeImplementationRoot({...layout,primaryRoot:path.resolve('G:/foreign-implementation')}),/trusted primary/);assert.throws(()=>terminalRuntimeOracleRoot({...layout,primaryRoot:path.resolve('G:/foreign-implementation')}),/trusted primary/);
 assert.throws(()=>terminalNativeImplementationRoot({...layout,managedInputContract:'current105-terminal27-v1'}),/UNKNOWN/);
});
test('signal26 native routing: original compile receipt rejects wrong count or executed/native/device flags',()=>{
 const receipt=()=>({managedAdmissions:26,native:{status:'COMPILED_NOT_EXECUTED',compileOnly:true,compilerExitCode:0,compilerSignal:null,compilerError:null,runBranchReached:false,fixtureExecuted:false,nativeExecuted:false,normalProductMainExecuted:false,realDeviceIo:false,mutation:null}});
 const id='current105-terminal26-sdk-lifetime-compact-v1';
 assert.equal(validateTerminalLifecycleNativeReceipt(receipt(),id,null).managedSources,26);
 for(const count of [21,22,23,25,27]){const bad=receipt();bad.managedAdmissions=count;assert.throws(()=>validateTerminalLifecycleNativeReceipt(bad,id,null));}
 for(const [field,value] of [['status','PASS'],['compilerExitCode',1],['compilerSignal','SIGTERM'],['compileOnly',false],['runBranchReached',true],['fixtureExecuted',true],['nativeExecuted',true],['normalProductMainExecuted',true],['realDeviceIo',true],['mutation','foreign']]){
  const bad=receipt();bad.native[field]=value;assert.throws(()=>validateTerminalLifecycleNativeReceipt(bad,id,null));
 }
 assert.equal(validateTerminalLifecycleNativeReceipt(receipt(),id,null).runtimeAuthorityGranted,false);
});

test('signal26 managed runtime: original launcher receives the pinned project runtime and refuses drift',()=>{
 const repo=path.resolve(path.dirname(native26FileURLToPath(import.meta.url)),'../../../..');
 const spec=JSON.parse(fs.readFileSync(path.join(repo,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json'),'utf8'));
 const program=path.join(spec.dotnetRoot,'dotnet.exe'),sha256=lifecycleRev2Sha(fs.readFileSync(program));
 const inherited={YMCC_FIXTURE_DOTNET:'C:/foreign/dotnet.exe',DOTNET_ROOT:'C:/foreign',sentinel:'kept'};
 const env=terminalManagedRuntimeEnvironment({dotnet:{path:program,sha256}},inherited);
 assert.equal(env.YMCC_FIXTURE_DOTNET,path.resolve(program));assert.equal(env.DOTNET_ROOT,path.dirname(path.resolve(program)));
 assert.equal(env.DOTNET_MULTILEVEL_LOOKUP,'0');assert.equal(env.sentinel,'kept');
 assert.equal(inherited.YMCC_FIXTURE_DOTNET,'C:/foreign/dotnet.exe');
 for(const dotnet of [undefined,{path:'dotnet.exe',sha256},{path:program,sha256:'0'.repeat(64)},{path:repo,sha256}])
  assert.throws(()=>terminalManagedRuntimeEnvironment({dotnet},inherited));
});

test('assessed native fixture budget admits only explicit26 bounded natural completion',()=>{
 const b=createNativeObservationBudget('current105-terminal26-sdk-lifetime-compact-v1',180000),raw={status:'PASS'};
 assert.equal(readCompletedNativeReport(complete({durationMs:120000}),0,()=>raw,b),raw);
 assert.equal(readCompletedNativeReport(complete({durationMs:180000}),0,()=>raw,b),raw);
 let reads=0;assert.throws(()=>readCompletedNativeReport(complete({durationMs:180001}),0,()=>{reads++;return raw;},b),e=>e.code==='NATIVE22_ASSESSED_FIXTURE_BUDGET_EXCEEDED');assert.equal(reads,0);
 for(const value of [59999,180001,Infinity,NaN,'180000',60000.5])assert.throws(()=>createNativeObservationBudget('current105-terminal26-sdk-lifetime-compact-v1',value));
 assert.throws(()=>createNativeObservationBudget('legacy-contract',180000));
});
test('assessed native fixture budget cannot admit old timeout or serialized budget',()=>{
 const b=createNativeObservationBudget('current105-terminal26-sdk-lifetime-compact-v1',180000);let reads=0;
 assert.throws(()=>readCompletedNativeReport(complete({timedOut:true,durationMs:60054}),0,()=>{reads++;return {status:'PASS'};},b),e=>e.code==='NATIVE22_ASSESSED_FIXTURE_BUDGET_TIMEOUT');
 for(const fake of [{ms:180000},JSON.parse(JSON.stringify(b))])assert.throws(()=>readCompletedNativeReport(complete({durationMs:120000}),0,()=>{reads++;},fake),e=>e.code==='NATIVE22_ASSESSED_BUDGET_UNPROVEN');
 assert.equal(reads,0);rejectedBeforeReport(complete({durationMs:60001}),'NATIVE22_ORIGINAL_BUDGET_EXCEEDED');
});
test('assessed native fixture budget option does not change lifecycle budgets',()=>{
 const args=['--native-preparation','G:/input/native.json','--out','G:/output/observe'];
 assert.equal(parseTerminal22ObservationArguments([...args,'--native-budget-ms','180000'])['--native-budget-ms'],'180000');
 for(const value of ['59999','180001','NaN','180000.5','0180000'])assert.throws(()=>parseTerminal22ObservationArguments([...args,'--native-budget-ms',value]));
 assert.throws(()=>parseTerminal22ObservationArguments(['--lifecycle-preparations','G:/input/lifecycle.json','--out','G:/output/observe','--native-budget-ms','180000']));
});

import {TERMINAL26_RUNTIME_LAYOUT_KIND,terminal26RuntimeLayout,terminal26PreparedRuntimeLayout,terminal26RuntimePaths,TERMINAL26_MSI_PARAMETER_LAYOUT,TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT,terminal26PreparedRuntimeGenerationOptions} from './r3-terminal26-runtime-layout.mjs';
const terminal26UnitRoot=path.resolve('terminal26-layout-unit','qualification-runtime');
const terminal26UnitPreparation=()=>({runtimeRoot:terminal26UnitRoot,runtimeLayout:terminal26RuntimeLayout(terminal26UnitRoot)});

test('terminal26 runtime layout: exact17 unique paths keep pipe separate from original16',()=>{
 const layout=terminal26RuntimeLayout(terminal26UnitRoot),paths=terminal26RuntimePaths(terminal26UnitRoot);
 assert.equal(TERMINAL26_RUNTIME_LAYOUT_KIND,'EXACT26_PIPE_AND_ORIGINAL16_RUNTIME_PATHS_V1');
 assert.equal(layout.kind,TERMINAL26_RUNTIME_LAYOUT_KIND);assert.equal(layout.root,path.resolve(terminal26UnitRoot));
 assert.equal(layout.pipe,path.join(layout.root,'pipe'));assert.equal(layout.lifecycle.length,16);
 assert.deepStrictEqual(paths,[layout.pipe,...layout.lifecycle.map(row=>row.path)]);
 assert.equal(paths.length,17);assert.equal(new Set(paths).size,17);
 for(const row of layout.lifecycle){assert.equal(path.dirname(row.path),layout.root);assert.equal(path.basename(row.path),row.label);assert.notEqual(row.path,layout.pipe);assert(!path.relative(layout.pipe,row.path).startsWith('pipe'+path.sep));}
 assert(layout.lifecycle.every(row=>path.relative(layout.pipe,row.path).startsWith('..'+path.sep)));
});
test('terminal26 runtime layout: order role mode mutation exit and label come from original plan',()=>{
 const plan=currentLifecycleModePlan(),before=structuredClone(plan),layout=terminal26RuntimeLayout(terminal26UnitRoot);
 assert.deepStrictEqual(layout.lifecycle,plan.map(row=>({...row,label:currentLifecycleModeLabel(row),path:path.join(layout.root,currentLifecycleModeLabel(row))})));
 assert.equal(new Set(layout.lifecycle.map(row=>row.label)).size,16);
 for(const row of layout.lifecycle){assert(!/[\\/\0\r\n:]/.test(row.label));assert(!row.label.includes('..'));}
 layout.lifecycle[0].role='polluted';assert.deepStrictEqual(currentLifecycleModePlan(),before);
});
test('terminal26 runtime layout: root resolves normalized absolute qualification-runtime',()=>{
 const absolute=path.join(terminal26UnitRoot,'unused','..');
 assert.deepStrictEqual(terminal26RuntimeLayout(absolute),terminal26RuntimeLayout(terminal26UnitRoot));
 const n={runtimeRoot:absolute,runtimeLayout:terminal26RuntimeLayout(absolute)};
 assert.deepStrictEqual(terminal26PreparedRuntimeLayout(n),n.runtimeLayout);
});
test('terminal26 runtime layout: relative malformed or wrong-basename roots reject every API',()=>{
 const badRoots=[undefined,null,0,{},'', 'qualification-runtime','relative/qualification-runtime','G:qualification-runtime',path.dirname(terminal26UnitRoot),path.join(terminal26UnitRoot,'foreign'),terminal26UnitRoot+'\0',terminal26UnitRoot+'\r',terminal26UnitRoot+'\n'];
 for(const runtimeRoot of badRoots){assert.throws(()=>terminal26RuntimeLayout(runtimeRoot));assert.throws(()=>terminal26RuntimePaths(runtimeRoot));assert.throws(()=>terminal26PreparedRuntimeLayout({runtimeRoot,runtimeLayout:terminal26RuntimeLayout(terminal26UnitRoot)}));}
 for(const n of [undefined,null,{}, {runtimeLayout:terminal26RuntimeLayout(terminal26UnitRoot)}])assert.throws(()=>terminal26PreparedRuntimeLayout(n));
});
test('terminal26 runtime layout: prepared layout missing unknown kind and extra fields reject',()=>{
 for(const mutate of [n=>delete n.runtimeLayout,n=>n.runtimeLayout=null,n=>delete n.runtimeLayout.kind,n=>n.runtimeLayout.kind='UNKNOWN',n=>n.runtimeLayout.kind='PASS',n=>n.runtimeLayout.accepted=true,n=>delete n.runtimeLayout.pipe,n=>delete n.runtimeLayout.root,n=>delete n.runtimeLayout.lifecycle]){
  const n=terminal26UnitPreparation();mutate(n);const before=structuredClone(n);assert.throws(()=>terminal26PreparedRuntimeLayout(n));assert.deepStrictEqual(n,before);
 }
});
test('terminal26 runtime layout: repeated omitted added reordered or foreign members reject',()=>{
 for(const mutate of [rows=>rows.pop(),rows=>rows.push({...rows[0]}),rows=>rows[1]={...rows[0]},rows=>rows.reverse(),rows=>rows[0]={...rows[0],role:'foreign'},rows=>rows[0]={...rows[0],mode:'foreign'},rows=>rows[0]={...rows[0],mutation:'foreign'},rows=>rows[0]={...rows[0],expectedExit:99},rows=>rows[0]={...rows[0],foreign:true},rows=>delete rows[0]]){
  const n=terminal26UnitPreparation();mutate(n.runtimeLayout.lifecycle);assert.throws(()=>terminal26PreparedRuntimeLayout(n));
 }
});
test('terminal26 runtime layout: root pipe mode paths and dotdot label injections reject',()=>{
 const mutations=[n=>n.runtimeLayout.root=path.dirname(n.runtimeLayout.root),n=>n.runtimeLayout.pipe=n.runtimeLayout.root,n=>n.runtimeLayout.pipe=n.runtimeLayout.lifecycle[0].path,n=>n.runtimeLayout.lifecycle[0].path=n.runtimeLayout.pipe,n=>n.runtimeLayout.lifecycle[0].path=path.join(n.runtimeLayout.root,'foreign'),n=>n.runtimeLayout.lifecycle[0].path=path.join(n.runtimeLayout.root,'..','outside'),n=>n.runtimeLayout.lifecycle[0].path=n.runtimeLayout.root+path.sep+'unused'+path.sep+'..'+path.sep+n.runtimeLayout.lifecycle[0].label,n=>n.runtimeRoot=path.join(path.dirname(terminal26UnitRoot),'other','qualification-runtime')];
 for(const label of ['..','../outside','..\\outside','foreign/child','foreign\\child','pipe','a..b','foreign\0','foreign\r','foreign\n'])mutations.push(n=>{n.runtimeLayout.lifecycle[0].label=label;n.runtimeLayout.lifecycle[0].path=path.join(n.runtimeLayout.root,label);});
 for(const mutate of mutations){const n=terminal26UnitPreparation();mutate(n);assert.throws(()=>terminal26PreparedRuntimeLayout(n));}
});
test('terminal26 runtime layout: frozen preparation stays sealed and returned copies cannot pollute it',()=>{
 const n=terminal26UnitPreparation(),before=structuredClone(n);
 n.runtimeLayout.lifecycle.forEach(Object.freeze);Object.freeze(n.runtimeLayout.lifecycle);Object.freeze(n.runtimeLayout);Object.freeze(n);
 const first=terminal26PreparedRuntimeLayout(n),second=terminal26PreparedRuntimeLayout(n);
 assert.deepStrictEqual(first,n.runtimeLayout);assert.notEqual(first,n.runtimeLayout);assert.notEqual(first,second);
 assert.notEqual(first.lifecycle,n.runtimeLayout.lifecycle);assert.notEqual(first.lifecycle[0],n.runtimeLayout.lifecycle[0]);
 first.pipe='polluted';first.lifecycle[0].label='polluted';first.lifecycle.pop();
 assert.deepStrictEqual(n,before);assert.deepStrictEqual(terminal26PreparedRuntimeLayout(n),second);
});
test('terminal26 runtime layout: independent layouts and paths have no shared mutable state',()=>{
 const layout=terminal26RuntimeLayout(terminal26UnitRoot),before=structuredClone(layout),paths=terminal26RuntimePaths(terminal26UnitRoot);
 paths[0]='foreign';paths.pop();layout.lifecycle[0].mutation='foreign';layout.lifecycle.length=0;
 assert.deepStrictEqual(terminal26RuntimeLayout(terminal26UnitRoot),before);
 assert.deepStrictEqual(terminal26RuntimePaths(terminal26UnitRoot),[before.pipe,...before.lifecycle.map(row=>row.path)]);
});


// Synthetic validator controls only; no coordinator, managed process or Evidence input.
const terminal26TupleSpec=JSON.parse(fs.readFileSync(new URL('./r3-terminal26-tuple-expectations.json',import.meta.url),'utf8'));
function terminal26SyntheticTuple(){
 const spec=terminal26TupleSpec,assertionTrace=[];
 const cases=spec.cases.map((row,i)=>{
  const host=i%3!==0,owner={epoch:i+1,runId:'synthetic-owner-'+i};
  const c={name:row.name,sourceOrdinal:i+1,nameOccurrence:1,events:[
   {event:'input-host-started',detail:{...owner,transportUsable:true}}
  ]};
  if(!host)c.events.push({event:'input-host-process-retirement',detail:{...owner,handleClosed:true,hostHandleRetained:false,closeAttempted:true,hostExitConfirmed:true,waitState:0,closeError:0}});
  let probe=false;
  if(row.hasProbe){
   c.oldRequestId=100+i;
   c.offeredRuntime={serial:200+i,oldRequestId:c.oldRequestId,pending:true,spent:false,recoveryRequestId:0};
   c.observedRuntime={serial:200+i,hostEpoch:owner.epoch,runId:owner.runId,pending:false,spent:true,recoveryRequestId:1000+i};
   if(row.parameters.condition==='cooldown'){
    for(const stage of ['restart','start-returned'])c.events.push({event:'input-host-runtime-recovery',detail:{serial:200+i,stage,recoveryRequestId:1000+i,spent:true,pending:false,nextProbeTick:0}});
   }else{
    probe=true;c.nextScheduledProbe={beforeTick:10000,originalNextProbe:10500,advanceMs:500,oldRequestId:c.oldRequestId,recoveryRequestBefore:0,recoveryRequestAfter:1000+i,pendingAfter:false};
   }
  }
  const expand=nodes=>nodes.forEach(n=>{
   if(Number.isInteger(n))assertionTrace.push({case:row.name,assertion:spec.labels[n],sourceOrdinal:i+1,nameOccurrence:1,checkIndex:assertionTrace.length+1,passed:true});
   else expand((n.fact==='host'?host:probe)?n.then:n.else);
  });expand(row.grammar);return c;
 });
 const bytes=fs.readFileSync(new URL('../../../fixtures/input_coordinator_lifecycle/tuple_recovery_cases.inc',import.meta.url));
 return {raw:{status:'PASS',forbiddenIo:0,fullTranslationUnit:true,normalProductMainExecuted:false,realDeviceIo:false,failures:[],cases,assertionTrace,checks:assertionTrace.length},
  options:{managedInputContract:spec.managedInputContract,tupleSource:{path:path.resolve('synthetic-compiled-input','tuple_recovery_cases.inc'),sha256:spec.sourceBindings.compiledTupleSha256,bytes}}};
}
const terminal26TupleMode=currentLifecycleModePlan().find(t=>t.role==='positive'&&t.mode==='tuple-protection');
test('terminal26 tuple consumer: exact synthetic source grammar and independently replayed branches',()=>{
 const {raw,options}=terminal26SyntheticTuple();
 assert.deepEqual(verifyCurrentLifecycleMode(terminal26TupleMode,raw,options),{originalSemanticChecksVerified:true,preparedContextGranted:false,runtimeAuthorityGranted:false});
 assert.equal(raw.cases.length,45);
});
for(const [name,change]of [
 ['missing positive owner',r=>{r.cases[1].events=[];}],
 ['foreign retirement epoch',r=>{r.cases[0].events[1].detail.epoch++;}],
 ['foreign retirement run',r=>{r.cases[0].events[1].detail.runId='foreign';}],
 ['conflicting retained closed fields',r=>{r.cases[0].events[1].detail.hostHandleRetained=true;}],
 ['missing closure receipt',r=>{delete r.cases[0].events[1].detail.closeAttempted;}],
 ['duplicate live owner',r=>{r.cases[1].events.push(structuredClone(r.cases[1].events[0]));}],
 ['foreign retained owner',r=>{r.cases[1].events.push({event:'input-host-session-retained',detail:{epoch:999,runId:'foreign',hostHandleRetained:true}});}],
 ['foreign final owner',r=>{r.cases[25].observedRuntime.runId='foreign';}],
 ['post-finish cleanup event',r=>{r.cases[1].events.push({event:'input-host-stop-request',detail:{reason:'offline-owned-cleanup-final'}});}],
 ['missing positive probe marker',r=>{delete r.cases[25].nextScheduledProbe;}],
 ['untyped probe guard',r=>{r.cases[25].nextScheduledProbe.beforeTick='10000';}],
 ['wrong probe delta',r=>{r.cases[25].nextScheduledProbe.advanceMs++;}],
 ['probe beyond original bound',r=>{Object.assign(r.cases[25].nextScheduledProbe,{advanceMs:1001,originalNextProbe:11001});}],
 ['wrong probe old request',r=>{r.cases[25].nextScheduledProbe.oldRequestId++;}],
 ['foreign probe incident',r=>{r.cases[25].observedRuntime.serial++;}],
 ['wrong probe successor',r=>{r.cases[25].nextScheduledProbe.recoveryRequestAfter++;}],
 ['foreign probe marker',r=>{r.cases[0].nextScheduledProbe=structuredClone(r.cases[25].nextScheduledProbe);}],
 ['cooldown marker conflict',r=>{r.cases[39].nextScheduledProbe=structuredClone(r.cases[25].nextScheduledProbe);}],
 ['missing cooldown successor',r=>{r.cases[39].observedRuntime.spent=false;}],
 ['missing cooldown restart',r=>{r.cases[39].events=r.cases[39].events.filter(e=>e.detail.stage!=='restart');}],
 ['duplicate cooldown restart',r=>{r.cases[39].events.push(structuredClone(r.cases[39].events.find(e=>e.detail.stage==='restart')));}],
 ['conflicting cooldown receipt',r=>{r.cases[39].events.find(e=>e.detail.stage==='start-returned').detail.nextProbeTick=1;}],
 ['missing scenario',r=>{r.cases.pop();}],
 ['reordered scenarios',r=>{[r.cases[0],r.cases[1]]=[r.cases[1],r.cases[0]];}],
 ['wrong scenario name',r=>{r.cases[0].name='foreign';}],
 ['duplicate scenario occurrence',r=>{r.cases[0].nameOccurrence=2;}],
 ['wrong source ordinal',r=>{r.cases[0].sourceOrdinal=2;}],
 ['negative assertion',r=>{r.assertionTrace[0].passed=false;}],
 ['duplicate assertion',r=>{r.assertionTrace.splice(1,0,structuredClone(r.assertionTrace[0]));r.checks++;}],
 ['wrong assertion label',r=>{r.assertionTrace[0].assertion='foreign';}],
 ['reordered assertions',r=>{const a=r.assertionTrace;[a[0].assertion,a[1].assertion]=[a[1].assertion,a[0].assertion];}],
 ['noncontinuous check index',r=>{r.assertionTrace[0].checkIndex=0;}],
 ['wrong trace owner',r=>{r.assertionTrace[0].sourceOrdinal=2;}],
 ['wrong total',r=>{r.checks++;}],
 ['forbidden IO',r=>{r.forbiddenIo=1;}]
])test('terminal26 tuple consumer: rejects '+name,()=>{
 const {raw,options}=terminal26SyntheticTuple();change(raw);
 assert.throws(()=>verifyCurrentLifecycleMode(terminal26TupleMode,raw,options));
});
for(const [name,change]of [
 ['missing compiled tuple source',o=>{delete o.tupleSource;}],
 ['wrong source pin',o=>{o.tupleSource.sha256='0'.repeat(64);}],
 ['changed actual tuple bytes',o=>{o.tupleSource.bytes=Buffer.from(o.tupleSource.bytes);o.tupleSource.bytes[0]^=1;}],
 ['relative source path',o=>{o.tupleSource.path='tuple_recovery_cases.inc';}],
 ['foreign source basename',o=>{o.tupleSource.path=path.resolve('foreign.inc');}],
 ['unknown contract',o=>{o.managedInputContract='unknown26';}],
 ['old default contract',o=>{delete o.managedInputContract;}],
 ['explicit old21 contract',o=>{o.managedInputContract='current105-exact21-v1';}],
 ['explicit old22 contract',o=>{o.managedInputContract='current105-terminal22-v1';}],
 ['explicit old23 contract',o=>{o.managedInputContract='current105-terminal23-sd-decoder-v1';}]
])test('terminal26 tuple consumer: rejects '+name,()=>{
 const {raw,options}=terminal26SyntheticTuple();change(options);
 assert.throws(()=>verifyCurrentLifecycleMode(terminal26TupleMode,raw,options));
});


test('MSI parameter layouts preserve old r2 and version split-process exit paths',()=>{
 const original=terminal26RuntimeLayout(terminal26UnitRoot,{runtimeParameterLayout:TERMINAL26_MSI_PARAMETER_LAYOUT});
 const split=terminal26RuntimeLayout(terminal26UnitRoot,{runtimeParameterLayout:TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT});
 assert.equal(Object.hasOwn(original.parameters.cases.find(row=>row.boundary==='exit'),'phases'),false);
 assert.deepEqual(split.parameters.cases.find(row=>row.boundary==='exit').phases.map(row=>row.phase),['pending','success']);
 assert.equal(terminal26RuntimePaths(terminal26UnitRoot,{runtimeParameterLayout:TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT}).length,
  terminal26RuntimePaths(terminal26UnitRoot,{runtimeParameterLayout:TERMINAL26_MSI_PARAMETER_LAYOUT}).length+2);
 const prepare=runtimeLayout=>({runtimeRoot:terminal26UnitRoot,runtimeParameterLayout:runtimeLayout.parameters.id,runtimeLayout,
  native:{productPolicySuccessor:{id:'msi-real-policy-return-r8-r5-20261117'}},
  fixtureInputSuccessor:{id:'msi-policy-fixture-input-r2-20261115'}});
 for(const layout of [original,split]){
  const input=prepare(layout),before=structuredClone(input);
  assert.deepEqual(terminal26PreparedRuntimeLayout(input),layout);assert.deepEqual(input,before);
 }
 const historical=structuredClone(split);historical.parameters.id=TERMINAL26_MSI_PARAMETER_LAYOUT;
 const input=prepare(historical);
 assert.deepEqual(terminal26PreparedRuntimeLayout(input),historical);
 assert.equal(terminal26PreparedRuntimeGenerationOptions(input).runtimeParameterLayout,TERMINAL26_MSI_EXIT_PARAMETER_LAYOUT);
 for(const mutate of [
  n=>n.runtimeLayout.parameters.cases.find(row=>row.boundary==='exit').phases[0].path=path.join(terminal26UnitRoot,'other'),
  n=>n.runtimeLayout.parameters.cases.find(row=>row.boundary==='exit').phases.push({phase:'other',path:path.join(terminal26UnitRoot,'other')}),
  n=>n.native.productPolicySuccessor.id='msi-real-policy-return-r7-20261115'
 ]){const bad=structuredClone(input);mutate(bad);assert.throws(()=>terminal26PreparedRuntimeLayout(bad));}
});

test('MSI parameter actual receipts remain scoped references and reject missing cancellation evidence',
 {skip:!process.env.YMCC_MSI_CANCELLATION_INTAKE},async t=>{
 const input=process.env.YMCC_MSI_CANCELLATION_INTAKE,digest=process.env.YMCC_MSI_CANCELLATION_INTAKE_SHA256;
 assert(path.isAbsolute(input)&&/^[a-f0-9]{64}$/.test(digest??''),'explicit exact actual intake required');
 const sha=b=>lifecycleRev2CreateHash('sha256').update(b).digest('hex');
 const bytes=fs.readFileSync(input);assert.equal(sha(bytes),digest);
 const intake=JSON.parse(bytes);assert.equal(intake.status,'FIVE_EXISTING_NONSEAM_PARAMETERS_SCOPED_ACCEPTED_GLOBAL_OPEN');
 const role={role:'positive',mode:'recovery-protection',mutation:null,expectedExit:0};
 const contract='current105-terminal26-sdk-lifetime-compact-v1',positive=[];
 for(const row of intake.configurations){
  assert(fs.lstatSync(row.receipt.path).isFile()&&!fs.lstatSync(row.receipt.path).isSymbolicLink());
  const b=fs.readFileSync(row.receipt.path);assert.equal(b.length,row.receipt.bytes);assert.equal(sha(b),row.receipt.sha256);
  const actual=JSON.parse(b);assert.equal(actual.terminal.exitCode,0);
  const options={boundary:row.boundary,...(row.phase?{phase:row.phase}:{})};
  const result=verifyCurrentLifecycleMode(role,actual.rawData,{managedInputContract:contract,msiCancellation:options});
  assert.equal(result.originalSemanticChecksVerified,false);assert.equal(result.selectedParameterSemanticChecksVerified,true);
  assert.equal(result.msiCancellation.fullParentVerified,false);assert.equal(result.msiCancellation.executedByInspection,false);
  assert.equal(result.msiCancellation.currentPrimaryAccepted,false);
  positive.push({options,raw:actual.rawData});
 }
 assert.equal(positive.length,6);
 assert.equal(positive.reduce((sum,row)=>sum+row.raw.checks,0),320);
 const check=(entry,mutate,pattern)=>{const raw=structuredClone(entry.raw);mutate(raw);
  assert.throws(()=>verifyCurrentLifecycleMode(role,raw,{managedInputContract:contract,msiCancellation:entry.options}),pattern);};
 const normal=positive.find(row=>row.options.boundary==='off'),exit=positive.find(row=>row.options.boundary==='exit');
 await t.test('unfired input cannot borrow a complete PASS report',()=>check(normal,
  r=>r.cases[0].policyBoundary.hookFired=false,/ACTUAL_NONSEAM_INPUT_REQUIRED/));
 await t.test('late live permission or a new Host cannot be concealed by raw PASS',()=>{
  check(normal,r=>r.cases[0].jointCancellationResult.lateForwardPermissionAfterCleanup=true,/LATE_PERMISSION_AND_RETURN_REQUIRED/);
  check(normal,r=>r.cases[0].jointCancellationResult.newHostSpawns=1,/LATE_PERMISSION_AND_RETURN_REQUIRED/);
 });
 await t.test('a successful old Start is not a valid cancellation result',()=>check(normal,
  r=>r.cases[0].jointCancellationResult.actualReturnResult.startReturned=true,/ORIGINAL_NEGATIVE_START_RECEIPT_REQUIRED/));
 await t.test('dropping a necessary assertion and fixing counts still refuses',()=>check(normal,r=>{
  r.assertionTrace=r.assertionTrace.filter(row=>!(row.case===r.cases[0].name&&row.assertion==='MSI-joint-selected-cancellation-must-actually-fire-once-on-current-path'));
  r.assertionTrace.forEach((row,index)=>row.checkIndex=index+1);r.checks=r.assertionTrace.length;
 },/NECESSARY_ASSERTION_MISSING/));
 await t.test('failed assertion cannot be disguised by an empty failures list',()=>check(normal,
  r=>r.assertionTrace[0].passed=false,/ASSERTION_TRACE_CONFLICT/));
 await t.test('exit fence-only receipt cannot impersonate actual capture and Host retirement',()=>check(exit,
  r=>delete r.cases[0].jointCancellationResult.actualExitOwner,/ACTUAL_CAPTURE_AND_HOST_EXIT_OWNER_REQUIRED/));
 await t.test('parameter evidence cannot be routed as an unrelated or complete mode',()=>{
  assert.throws(()=>verifyCurrentLifecycleMode({...role,mode:'start-protection'},normal.raw,{managedInputContract:contract,msiCancellation:normal.options}),
   /EXPLICIT26_POSITIVE_RECOVERY_PARAMETER_ONLY/);
  assert.throws(()=>verifyCurrentLifecycleMode(role,normal.raw,{managedInputContract:contract,msiCancellation:{boundary:'unknown'}}),/KNOWN_PARAMETER_REQUIRED/);
 });
});


test('MSI compiled fixture inverse rejects altered bytes and unreviewed edit metadata',
 {skip:!process.env.YMCC_MSI_CANCELLATION_INTAKE},async t=>{
 const input=process.env.YMCC_MSI_CANCELLATION_INTAKE,digest=process.env.YMCC_MSI_CANCELLATION_INTAKE_SHA256;
 assert(path.isAbsolute(input)&&/^[a-f0-9]{64}$/.test(digest??''));
 const sha=b=>lifecycleRev2CreateHash('sha256').update(b).digest('hex');
 const bytes=fs.readFileSync(input);assert.equal(sha(bytes),digest);
 const intake=JSON.parse(bytes),actual=[];
 for(const row of intake.epochs){
  const b=fs.readFileSync(row.preparation.path);assert.equal(b.length,row.preparation.bytes);assert.equal(sha(b),row.preparation.sha256);
  const native=JSON.parse(b),source=fs.readFileSync(native.native.sourceManifest.path);
  assert.equal(sha(source),native.native.sourceManifest.sha256);
  const manifest=JSON.parse(source),compiled=fs.readFileSync(row.generatedFixture.path);
  assert.equal(sha(compiled),row.generatedFixture.sha256);
  const inverse=terminal26JointFixturePreflightBytes(native,manifest,compiled);
  assert.equal(sha(inverse),native.generationPreflight.fixtureSha256);
  actual.push({native,manifest,compiled});
 }
 assert.equal(actual.length,2);const row=actual[1];
 await t.test('extra compiled source cannot hide outside inverse spans',()=>{
  assert.throws(()=>terminal26JointFixturePreflightBytes(row.native,row.manifest,
   Buffer.concat([row.compiled,Buffer.from('\n// injected unreviewed source\n')])));
 });
 await t.test('unknown joint edit roles refuse',()=>{
  const manifest=structuredClone(row.manifest);manifest.policyIoProvenance.joint.cancellation.edits.fixture[0].role='unreviewed';
  assert.throws(()=>terminal26JointFixturePreflightBytes(row.native,manifest,row.compiled),/unknown or missing joint fixture edit role/);
 });
 await t.test('a claimed inverse with changed actual span refuses',()=>{
  const manifest=structuredClone(row.manifest),edit=manifest.policyIoProvenance.joint.cancellation.edits.fixture[0];
  edit.afterBase64=Buffer.concat([Buffer.from(edit.afterBase64,'base64'),Buffer.from('tampered')]).toString('base64');
  assert.throws(()=>terminal26JointFixturePreflightBytes(row.native,manifest,row.compiled),/one actual joint edit span required/);
 });
 await t.test('foreign preflight identity cannot borrow the actual compiled graph',()=>{
  const native=structuredClone(row.native);native.generationPreflight.fixtureSha256='0'.repeat(64);
  assert.throws(()=>terminal26JointFixturePreflightBytes(native,row.manifest,row.compiled),/actual preflight fixture/);
 });
});


const sourcePhaseEntry = new URL('./r3-terminal26-lifecycle-observe.mjs',import.meta.url);
test('terminal26 lifecycle source-phase: cost recorder returns the exact original proof once',async()=>{
 const {measureTerminal26LifecycleSourceCheck:measure}=await import(sourcePhaseEntry);
 const proof={status:'FROZEN_SOURCE_VERIFIED',identity:{untouched:true}},timings=[];let calls=0;
 assert.equal(measure(()=>{calls++;return proof;},'before-modes',timings),proof);
 assert.equal(calls,1);assert.equal(timings.length,1);assert.equal(timings[0].phase,'before-modes');
 assert.equal(timings[0].status,'COMPLETED');assert(Number.isFinite(timings[0].wallMs)&&timings[0].wallMs>=0);
});
test('terminal26 lifecycle source-phase: a failed complete check keeps its first error and measured cost',async()=>{
 const {measureTerminal26LifecycleSourceCheck:measure}=await import(sourcePhaseEntry);
 const first=new Error('frozen source drift'),timings=[];let calls=0;
 assert.throws(()=>measure(()=>{calls++;throw first;},'after-modes',timings),error=>error===first);
 assert.equal(calls,1);assert.equal(timings.length,1);assert.equal(timings[0].phase,'after-modes');
 assert.equal(timings[0].status,'FAILED');assert(Number.isFinite(timings[0].wallMs)&&timings[0].wallMs>=0);
});
test('terminal26 lifecycle source-phase: actual observer has one complete barrier before launch and one after modes',()=>{
 const text=fs.readFileSync(sourcePhaseEntry,'utf8');
 const start=text.indexOf('async function observeTerminal22Lifecycle('),end=text.indexOf('export async function observeTerminal26Lifecycle(',start);
 assert(start>=0&&end>start);const body=text.slice(start,end);
 assert.equal((body.match(/verifyTerminal22LifecycleCapture\(layout\)/g)||[]).length,1);
 assert(!body.includes('sourceCheck();'),'no unmeasured redundant early complete scan');
 const before=body.indexOf("result.sourceBefore=measureTerminal26LifecycleSourceCheck(sourceCheck,'before-modes',sourceCheckTimings);");
 const launch=body.indexOf('processResult=await runProcess(');
 const after=body.indexOf("result.sourceAfter=measureTerminal26LifecycleSourceCheck(sourceCheck,'after-modes',sourceCheckTimings);");
 const final=body.indexOf('result.postInputsStable=true;');
 assert(before>=0&&launch>before&&after>launch&&final>after);
 assert(body.includes("sourceCheck=()=>verifyTerminal22LifecycleCapture(layout);"));
 assert(body.includes('for(const row of inputs.values())'),'all exact input checks remain');
 assert(body.includes('sourceCheckTimings,preflight:'));
});
test('terminal26 lifecycle source-phase: source failure cannot produce a successful final lifecycle receipt',async()=>{
 const {measureTerminal26LifecycleSourceCheck:measure,finalizeTerminal22LifecycleExecution:finish}=await import(sourcePhaseEntry);
 const modes=currentLifecycleModePlan().map(row=>({label:currentLifecycleModeLabel(row),semanticAccepted:true,nativeProcess:{timedOut:false,signal:null}}));
 for(const phase of ['before-modes','after-modes']){
  const timings=[];let failed=false;
  try{measure(()=>{throw Error('source mismatch');},phase,timings);}catch{failed=true;}
  const result=finish({started:1000,ended:1100,complete:true,modeResults:modes,failed,groupBudgetMs:90000});
  assert.equal(result.execution.exitCode,1);assert.equal(result.status,'INCOMPLETE');assert.equal(result.qualificationAccepted,false);
 }
});
