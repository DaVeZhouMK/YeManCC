import test from 'node:test';
import assert from 'node:assert/strict';
import { ORIGINAL_NATIVE_BUDGET_MS, readCompletedNativeReport } from './r3-terminal22-observation-terminal.mjs';
import fs from 'node:fs';
import path from 'node:path';
import { resolveTerminal22PreparationLayout } from './r3-terminal22-prepare.mjs';
import { terminal22ExecutionFromProcess,parseTerminal22ObservationArguments,terminal22LifecyclePreparationPins,validateTerminal22LifecycleLayout,verifyTerminal22LifecycleCapture,terminal22LifecycleManagedDllPin,finalizeTerminal22LifecycleExecution } from './r3-terminal22-qualified-observe.mjs';
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
 const requested=process.env.YMCC_TERMINAL22_TEST_TEMP;assert(requested&&path.isAbsolute(requested),'Set YMCC_TERMINAL22_TEST_TEMP to the existing G-core task temp root');
 const temp=path.resolve(requested),core=path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace'),relative=path.relative(core,temp);
 assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));assert(!fs.lstatSync(temp).isSymbolicLink());assert.equal(fs.realpathSync(temp).toLowerCase(),temp.toLowerCase());
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
