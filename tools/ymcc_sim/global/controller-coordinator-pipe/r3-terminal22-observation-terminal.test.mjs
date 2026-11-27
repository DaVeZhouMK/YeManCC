import test from 'node:test';
import assert from 'node:assert/strict';
import { ORIGINAL_NATIVE_BUDGET_MS, readCompletedNativeReport, createNativeObservationBudget } from './r3-terminal22-observation-terminal.mjs';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { parseTerminalManagedPreparationArguments,prepareTerminal22,resolveTerminal22PreparationLayout } from './r3-terminal22-prepare.mjs';
import { terminal22ExecutionFromProcess,parseTerminal22ObservationArguments,terminal22LifecyclePreparationPins,validateTerminal22LifecycleLayout,verifyTerminal22LifecycleCapture,terminal22LifecycleManagedDllPin,finalizeTerminal22LifecycleExecution,terminal26JointFixturePreflightBytes } from './r3-terminal22-qualified-observe.mjs';
import {native29HeldInterval} from './r3-terminal29-owned-pipe.mjs';
import {current29LifecycleRegistration,observeRegisteredCurrent29Lifecycle} from './r3-terminal22-qualified-observe.mjs';
import {createLifecycleInputPins,verifyLifecyclePinnedInputs} from './r3-terminal22-qualified-observe.mjs';
import {CURRENT29_LIFECYCLE_DECLARATION_ENV,getControllerCoordinatorLifecycleAdapters,REPO as coordinatorRepo} from '../controller-coordinator-lifecycle/adapter.mjs';

test('Terminal29 registered lifecycle refuses foreign or malformed declarations before any execution',async()=>{
 const foreign=path.join(coordinatorRepo,'foreign-preparation.json'),evidence=path.join(coordinatorRepo,'not-an-evidence-root');
 assert.throws(()=>current29LifecycleRegistration(coordinatorRepo,foreign),/escaped core Workspace/);
 assert.throws(()=>observeRegisteredCurrent29Lifecycle(foreign,evidence,'bad digest'),/declaration hash required/);
 await assert.rejects(observeRegisteredCurrent29Lifecycle(foreign,evidence,'0'.repeat(64)),/escaped core Workspace/);
});
test('Terminal29 registered lifecycle keeps the original ID and budget without fallback on invalid explicit configuration',()=>{
 const key=CURRENT29_LIFECYCLE_DECLARATION_ENV,old=process.env[key];
 try{
  process.env[key]=path.join(coordinatorRepo,'foreign-preparation.json');
  const [adapter]=getControllerCoordinatorLifecycleAdapters();
  assert.equal(adapter.id,'controller.coordinator-lifecycle');
  assert.equal(adapter.timeoutMs,90000);
  assert.match(adapter.blockedReason,/registered lifecycle input escaped core Workspace/);
  assert.deepEqual(adapter.artifactInputs,[]);
 }finally{if(old===undefined)delete process.env[key];else process.env[key]=old;}
});

function registrationReceipts(t,{productMainSha256}={}){
 const workspace=path.resolve(coordinatorRepo,'../../../Testing/YMCC/Workspace');
 assert.equal(fs.realpathSync.native(workspace).toLowerCase(),workspace.toLowerCase());
 const owner=fs.mkdtempSync(path.join(workspace,'t29-reg-'));
 t.after(()=>{
  assert.equal(path.dirname(owner),workspace);
  assert.match(path.basename(owner),/^t29-reg-/);
  assert.equal(fs.realpathSync.native(owner).toLowerCase(),owner.toLowerCase());
  assert(!fs.lstatSync(owner).isSymbolicLink());
  fs.rmSync(owner,{recursive:true});
 });
 const sourceRoot=path.join(owner,'Mainline','YeManCC-source','YeManCC');
 const build=path.join(owner,'Build','Validation','CurrentCoordinatorTerminal22');
 fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});
 fs.mkdirSync(build,{recursive:true});
 const contract='current105-terminal29-shared-input-power-v1';
 const put=(name,value)=>{
  const file=path.join(build,name),text=JSON.stringify(value);
  fs.writeFileSync(file,text);
  return {path:file,sha256:lifecycleRev2CreateHash('sha256').update(text).digest('hex')};
 };
 const mainFile=path.join(coordinatorRepo,'native','main.cpp');
 const mainSha=productMainSha256??lifecycleRev2CreateHash('sha256').update(fs.readFileSync(mainFile)).digest('hex');
 const layout=put('layout.json',{primaryRoot:coordinatorRepo,sourceRoot,workspaceRoot:owner,authorizedBuildRoot:build,productMainSha256:mainSha});
 const manifest=put('manifest.json',{});
 const managed=put('managed.json',{status:'MANAGED29_COMPILED_NOT_RUNTIME_ACCEPTED',layout,
  managed:{manifestPath:manifest.path,manifestSha256:manifest.sha256}});
 const roles=[...new Set(currentLifecycleModePlan().map(row=>row.role))];
 const artifacts=[layout.path,manifest.path,managed.path];
 let lastLedger;
 const preparations=roles.map((role,i)=>{
  const exe=put(`${i}-exe.fixture`,{role}),sourceManifest=put(`${i}-source.json`,{role});
  const compilerLedger=put(`${i}-ledger.json`,{role});
  const receipt=put(`${i}-native.json`,{role,status:'NATIVE29_COMPILED_NOT_RUNTIME_ACCEPTED',managedInputContract:contract,
   runtimeReusePolicy:'owned-lifecycle-child-v1',preparation:managed,native:{exe,sourceManifest},compilerLedger});
  lastLedger=compilerLedger.path;
  artifacts.push(receipt.path,exe.path,sourceManifest.path,lastLedger);
  return {role,...receipt};
 });
 const declare=(selection=preparations)=>put('declaration.json',{schemaVersion:1,kind:'TERMINAL29_LIFECYCLE_COMPILED_INPUTS',preparations:selection});
 return {owner,layout,managed,preparations,lastLedger,artifacts,declare};
}

test('Terminal29 registered lifecycle rejects stale main before reading remaining role artifacts', t=>{
 const mainFile=path.join(coordinatorRepo,'native','main.cpp');
 const main=lifecycleRev2CreateHash('sha256').update(fs.readFileSync(mainFile)).digest('hex');
 const stale=(main[0]==='0'?'1':'0')+main.slice(1);
 const f=registrationReceipts(t,{productMainSha256:stale}),declaration=f.declare(),realRead=fs.readFileSync,reads=[];
 t.mock.method(fs,'readFileSync',(file,...args)=>{
  if(typeof file==='string'&&path.dirname(file)===path.dirname(declaration.path))reads.push(path.basename(file));
  return realRead(file,...args);
 });
 let failure;
 try{current29LifecycleRegistration(coordinatorRepo,declaration.path,declaration.sha256);}catch(error){failure=error;}
 t.diagnostic(JSON.stringify({contentsRead:reads.length,files:reads}));
 assert.match(failure?.message??'REGISTERED_WITH_STALE_MAIN',/CURRENT_MAIN_DRIFT_BEFORE_REGISTRATION/);
 assert.deepEqual(reads,['declaration.json','0-native.json','managed.json','layout.json']);
 const key=CURRENT29_LIFECYCLE_DECLARATION_ENV,previous=process.env[key];
 try{
  process.env[key]=declaration.path;
  const [adapter]=getControllerCoordinatorLifecycleAdapters();
  assert.match(adapter.blockedReason,/CURRENT_MAIN_DRIFT_BEFORE_REGISTRATION/);
  assert.deepEqual(adapter.artifactInputs,[]);
 }finally{if(previous===undefined)delete process.env[key];else process.env[key]=previous;}
});

test('Terminal29 registered lifecycle reads each pinned JSON once with a closing shared Managed check', t=>{
 const f=registrationReceipts(t),declaration=f.declare(),realRead=fs.readFileSync,counts=new Map();
 t.mock.method(fs,'readFileSync',(file,...args)=>{
  if(typeof file==='string'&&path.dirname(file)===path.dirname(declaration.path)) counts.set(file,(counts.get(file)??0)+1);
  return realRead(file,...args);
 });
 const result=current29LifecycleRegistration(coordinatorRepo,declaration.path,declaration.sha256);
 assert.deepEqual(result.input,declaration);
 assert.deepEqual(new Set(result.artifactInputs),new Set([process.execPath,declaration.path,...f.artifacts]));
 t.diagnostic(JSON.stringify({totalReads:[...counts.values()].reduce((sum,n)=>sum+n,0),uniqueFiles:counts.size,
  declaration:counts.get(declaration.path),managed:counts.get(f.managed.path),layout:counts.get(f.layout.path),
  native:f.preparations.map(row=>counts.get(row.path))}));
 assert.equal(counts.size,24);
 for(const [file,count] of counts) assert.equal(count,file===f.managed.path?2:1,path.basename(file));
});

test('Terminal29 registered lifecycle detects shared Managed bytes changed during registration', t=>{
 const f=registrationReceipts(t),declaration=f.declare(),realRead=fs.readFileSync;
 t.mock.method(fs,'readFileSync',(file,...args)=>{
  const value=realRead(file,...args);
  if(file===f.lastLedger) fs.appendFileSync(f.managed.path,' ');
  return value;
 });
 assert.throws(()=>current29LifecycleRegistration(coordinatorRepo,declaration.path,declaration.sha256),/registered lifecycle input changed: .*managed/);
});

test('Terminal29 registered lifecycle rejects a different shared Managed pin and does not keep a digest across calls', t=>{
 const f=registrationReceipts(t),d=f.declare();
 current29LifecycleRegistration(coordinatorRepo,d.path,d.sha256);
 const r=f.preparations.at(-1),n=JSON.parse(fs.readFileSync(r.path,'utf8'));
 n.preparation.sha256='a'.repeat(64);
 const text=JSON.stringify(n);fs.writeFileSync(r.path,text);
 const changed=f.declare([...f.preparations.slice(0,-1),{...r,sha256:lifecycleRev2CreateHash('sha256').update(text).digest('hex')}]);
 assert.throws(()=>current29LifecycleRegistration(coordinatorRepo,changed.path,changed.sha256),/registered lifecycle (input changed|roles must share managed preparation)/);
 fs.appendFileSync(f.managed.path,' ');
 assert.throws(()=>current29LifecycleRegistration(coordinatorRepo,changed.path,changed.sha256),/registered lifecycle input changed: .*managed/);
});

test('Terminal29 owned observer preserves the original deadline at full FILETIME precision',()=>{
 const base=['--native-preparation','native.json','--out','result'];
 const pwsh=path.resolve('installed','pwsh.exe'),owned=[...base,'--terminal29-owned-pwsh',pwsh];
 assert.equal(parseTerminal22ObservationArguments(owned)['--terminal29-owned-pwsh'],pwsh);
 for(const args of [[...base,'--terminal29-owned-pwsh','pwsh.exe'],[...owned,'--native-budget-ms','60001'],
  [...owned,'--consumer-predecessors',path.resolve('predecessors.json')],['--lifecycle-preparations',path.resolve('prep.json'),'--out','result','--terminal29-owned-pwsh',pwsh]]){
  assert.throws(()=>parseTerminal22ObservationArguments(args),/explicit Terminal29 owned pipe/);
 }
 const start=134400000000000000n;
 const interval=ticks=>native29HeldInterval({business:{startFileTimeUtc:String(start)},businessExitFileTimeUtc:String(start+ticks)});
 assert.equal(interval(600000000n).withinOriginalBudget,true);
 assert.equal(interval(600000001n).withinOriginalBudget,false);
 assert.throws(()=>interval(-1n),/HELD_FILETIMES_REVERSED/);
 assert.throws(()=>native29HeldInterval({business:{startFileTimeUtc:String(start)}}),/HELD_FILETIMES_REQUIRED/);
});
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

function producerWiringFixture(t,{directCore=false}={}){
 const requested=directCore?undefined:process.env.YMCC_TERMINAL22_TEST_TEMP,core=path.resolve('G:/YeManCC-Work/Testing/YMCC/Workspace');
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
test('terminal22 managed resume requires an explicit capture digest and existing core owner',t=>{
 const f=producerWiringFixture(t),sha='a'.repeat(64);
 const parse=(...args)=>parseTerminalManagedPreparationArguments(args);
 for(const stage of ['resume-managed','resume-frozen-managed']){
  assert.equal(parse('--stage',stage,'--capture-sha256',sha)['--capture-sha256'],sha);
  for(const args of [['--stage',stage],['--stage',stage,'--capture-sha256','a']])assert.throws(()=>parse(...args),/RESUME_CAPTURE_SHA_REQUIRED_ONLY_FOR_RESUME/);
 }
 for(const args of [['--stage','capture','--capture-sha256',sha],['--capture-sha256',sha]])assert.throws(()=>parse(...args),/RESUME_CAPTURE_SHA_REQUIRED_ONLY_FOR_RESUME/);
 const resolve=workspaceRoot=>resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot,resume:true});
 assert.throws(()=>resolve(path.join(f.coreWorkspace,'missing')),/EXISTING_CORE_CAPTURE_OWNER/);
 const legacy=path.join(f.evidence,'old');fs.mkdirSync(legacy);
 assert.throws(()=>resolve(legacy),/EXISTING_CORE_CAPTURE_OWNER/);
 const owner=path.join(f.coreWorkspace,'owned');fs.mkdirSync(owner);
 assert.equal(resolve(owner).layoutMode,'G_CORE_WORKSPACE_OWNER');
});

test('Terminal29 preflight pin refuses source drift before tool binding without replacing whole-tree verification',async t=>{
 const contract='current105-terminal29-shared-input-power-v1',option='--expected-preflight-main-sha256';
 const sha=value=>lifecycleRev2CreateHash('sha256').update(value).digest('hex');
 const original='// original',changed='// modified',expected=sha(original),actual=sha(changed);
 const contractArgs=['--managed-input-contract',contract];
 const extraOption='--expected-preflight-source-pins',watched='native/wake_game_recovery.h';
 const pinArgs=[extraOption,JSON.stringify([[watched,expected]])];
 assert.deepEqual(parseTerminalManagedPreparationArguments([...contractArgs,option,expected,...pinArgs])[extraOption],[[watched,expected]]);
 assert.throws(()=>parseTerminalManagedPreparationArguments([...contractArgs,...pinArgs]),/EXTRA_PREFLIGHT_PINS_REQUIRE_MAIN_PIN/);
 for(const pins of [[],[['../outside.h',expected]],[['C:/outside.h',expected]],[['native/main.cpp',expected]],[[watched,'bad']],
  [[watched,expected],[watched.toUpperCase(),expected]],Array.from({length:9},(_,i)=>['native/source'+i+'.h',expected])]){
  assert.throws(()=>parseTerminalManagedPreparationArguments([...contractArgs,option,expected,extraOption,JSON.stringify(pins)]),/EXTRA_PREFLIGHT_PINS_INVALID/);
 }
 for(const extra of [[],['--stage','capture']])assert.equal(parseTerminalManagedPreparationArguments([...contractArgs,...extra,option,expected])[option],expected);
 for(const args of [[option,expected],[...contractArgs,option,'bad'],...['resume-managed','resume-frozen-managed'].map(stage=>[...contractArgs,'--stage',stage,'--capture-sha256',expected,option,expected])]){
  assert.throws(()=>parseTerminalManagedPreparationArguments(args),/TERMINAL29_PREFLIGHT_MAIN_SHA_REQUIRES_FRESH_CAPTURE/);
 }
 assert.throws(()=>parseTerminalManagedPreparationArguments([...contractArgs,option,expected,option,actual]),/STRICT_PREPARATION_ARGUMENTS/);
 for(const phase of ['before-capture','captured-main','before-source-after','other-file-drift'])await t.test(phase,async st=>{
  const f=producerWiringFixture(st),owner=path.join(f.coreWorkspace,'pin-owner');
  const {sourceRoot,authorizedBuildRoot}=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
  const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
  const main=write('native/main.cpp',phase==='before-capture'?changed:original);
  const another=write('a-first.txt','first source');
  const program=write('InputHost/Program.cs','HostInputPowerBinding _powerBindings;');
  const args=['--repo',f.primaryRoot,'--workspace',owner,...contractArgs,option,expected];
  if(phase==='before-capture'){
   await assert.rejects(prepareTerminal22(args),{code:'TERMINAL29_PREFLIGHT_MAIN_CHANGED',details:{phase,path:'native/main.cpp',expected,actual}});
   assert(!fs.existsSync(owner));return;
  }
  const copiedMain=path.join(sourceRoot,'native/main.cpp'),read=fs.readFileSync;
  let injected=0;
  st.mock.method(fs,'readFileSync',(...values)=>{
   const bytes=read(...values);
   if(values[0]===(phase==='captured-main'?program:copiedMain)&&injected===0){
    ++injected;fs.writeFileSync(phase==='other-file-drift'?another:main,changed);
   }
   return bytes;
  });
  const failure=phase==='other-file-drift'?'SNAPSHOT_SOURCE_OR_COPY_DRIFT':'TERMINAL29_PREFLIGHT_MAIN_CHANGED';
  await assert.rejects(prepareTerminal22(args),new RegExp('TERMINAL22_SOURCE_CAPTURE_FAILED:'+failure));
  assert.equal(injected,1);
  const prep=JSON.parse(fs.readFileSync(path.join(authorizedBuildRoot,'PREPARATION.json'),'utf8'));
  const manifest=JSON.parse(fs.readFileSync(prep.capture.manifestFile,'utf8'));
  assert.deepEqual([prep.status,prep.managedBuiltThisRun,prep.nodeBinding,manifest.status,manifest.failure.code],['INCOMPLETE',false,undefined,'FAIL',failure]);
  assert.deepEqual(fs.readdirSync(authorizedBuildRoot),['PREPARATION.json']);
  if(phase==='other-file-drift'){
   assert.equal(manifest.main.snapshot.sha256,expected);
   assert.deepEqual(manifest.sourceDiff.changed.map(row=>row.path),['a-first.txt']);
  }else assert.deepEqual(manifest.failure.details,{phase,path:'native/main.cpp',expected,actual});
 });
 for(const changedDuringBatch of [true,false])await t.test(changedDuringBatch?'mid-copy drift stops at the next checkpoint':'same bytes at checkpoint still reach the final source guard',async st=>{
  const f=producerWiringFixture(st),owner=path.join(f.coreWorkspace,'periodic-pin-owner');
  const {sourceRoot,authorizedBuildRoot}=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
  const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
  const first=write('0-first.txt','source');
  write('InputHost/Program.cs','HostInputPowerBinding _powerBindings;');
  const main=write('native/main.cpp',original),copiedMain=path.join(sourceRoot,'native/main.cpp');
  write('1-tail/a.txt','source');write('1-tail/b.txt','source');
  const open=fs.openSync,read=fs.readFileSync;
  let injected=false,lateInjected=false,checkpointReads=0;
  // Advance only the checkpoint clock; do not sleep or manufacture a large snapshot.
  st.mock.method(performance,'now',()=>injected?4100:100);
  st.mock.method(fs,'openSync',(file,...args)=>{
   if(file===first&&!injected){injected=true;fs.writeFileSync(main,changedDuringBatch?changed:original);}
   return open(file,...args);
  });
  st.mock.method(fs,'readFileSync',(file,...args)=>{
   const bytes=read(file,...args);
   if(file===main&&injected&&!lateInjected)++checkpointReads;
   if(!changedDuringBatch&&file===copiedMain&&!lateInjected){lateInjected=true;fs.writeFileSync(main,changed);}
   return bytes;
  });
  const phase=changedDuringBatch?'copy-checkpoint':'before-source-after';
  await assert.rejects(prepareTerminal22(['--repo',f.primaryRoot,'--workspace',owner,...contractArgs,option,expected]),/TERMINAL22_SOURCE_CAPTURE_FAILED:TERMINAL29_PREFLIGHT_MAIN_CHANGED/);
  const prep=JSON.parse(fs.readFileSync(path.join(authorizedBuildRoot,'PREPARATION.json'),'utf8'));
  const manifest=JSON.parse(fs.readFileSync(prep.capture.manifestFile,'utf8'));
  console.log(JSON.stringify({phaseExpected:phase,phaseActual:manifest.failure.details.phase,verifiedFiles:manifest.copyVerifiedFiles,checkpointReads}));
  assert.deepEqual([injected,lateInjected,prep.status,prep.managedBuiltThisRun,prep.nodeBinding,manifest.status],[true,!changedDuringBatch,'INCOMPLETE',false,undefined,'FAIL']);
  assert.deepEqual(manifest.failure.details,{phase,path:'native/main.cpp',expected,actual});
  assert.equal(manifest.copyVerifiedFiles,changedDuringBatch?1:5);
  assert.equal(checkpointReads,1);
  assert.equal(fs.existsSync(copiedMain),!changedDuringBatch);
  assert.deepEqual(fs.readdirSync(authorizedBuildRoot),['PREPARATION.json']);
 });
 for(const checkpoint of [true,false])await t.test(checkpoint?'managed Program drift stops at the copy checkpoint':'captured Program must match the bytes validated before capture',async st=>{
  const f=producerWiringFixture(st),owner=path.join(f.coreWorkspace,'program-pin-owner');
  const {authorizedBuildRoot}=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
  const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
  const oldProgram='HostInputPowerBinding _powerBindings;',newProgram=oldProgram+'\n// different valid revision';
  const first=write('0-first.txt','source'),program=write('InputHost/Program.cs',oldProgram);
  write('native/main.cpp',original);
  const open=fs.openSync;
  let injected=false;
  st.mock.method(performance,'now',()=>checkpoint&&injected?4100:100);
  st.mock.method(fs,'openSync',(file,...args)=>{
   if(file===first&&!injected){injected=true;fs.writeFileSync(program,newProgram);}
   return open(file,...args);
  });
  const phase=checkpoint?'copy-checkpoint':'captured-program';
  await assert.rejects(prepareTerminal22(['--repo',f.primaryRoot,'--workspace',owner,...contractArgs,option,expected]),/TERMINAL22_SOURCE_CAPTURE_FAILED:TERMINAL29_PREFLIGHT_PROGRAM_CHANGED/);
  const prep=JSON.parse(fs.readFileSync(path.join(authorizedBuildRoot,'PREPARATION.json'),'utf8'));
  const manifest=JSON.parse(fs.readFileSync(prep.capture.manifestFile,'utf8'));
  assert.deepEqual([injected,prep.status,prep.managedBuiltThisRun,prep.nodeBinding,manifest.status,manifest.copyVerifiedFiles],
   [true,'INCOMPLETE',false,undefined,'FAIL',checkpoint?1:2]);
  assert.deepEqual(manifest.failure.details,{phase,path:'InputHost/Program.cs',expected:sha(oldProgram),actual:sha(newProgram)});
  assert.deepEqual(fs.readdirSync(authorizedBuildRoot),['PREPARATION.json']);
  console.log(JSON.stringify({programPhase:phase,verifiedFiles:manifest.copyVerifiedFiles}));
 });
 for(const mode of ['unselected','selected','after-capture'])await t.test('additional source pin: '+mode,async st=>{
  const f=producerWiringFixture(st),owner=path.join(f.coreWorkspace,'extra-pin-owner');
  const {authorizedBuildRoot,sourceRoot}=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
  const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return file;};
  const source=write(watched,original),copied=path.join(sourceRoot,watched);
  write('z-tail.txt','source');
  write('InputHost/Program.cs','HostInputPowerBinding _powerBindings;');write('native/main.cpp',original);
  const manifestFile=path.join(sourceRoot,'.ymcc-source-snapshot','manifest.json');
  const open=fs.openSync,rename=fs.renameSync;
  let injected=false;
  st.mock.method(performance,'now',()=>injected?4100:100);
  if(mode==='after-capture')st.mock.method(fs,'renameSync',(from,to)=>{
   const complete=to===manifestFile&&JSON.parse(fs.readFileSync(from,'utf8')).status==='PASS';
   rename(from,to);
   if(complete){injected=true;fs.writeFileSync(source,changed);}
  });
  else st.mock.method(fs,'openSync',(file,flags,...args)=>{
   // Mutate only once the watched input descriptor has closed and the copy is read back.
   if(file===copied&&typeof flags==='number'&&(flags&fs.constants.O_WRONLY)===0&&!injected){injected=true;fs.writeFileSync(source,changed);}
   return open(file,flags,...args);
  });
  const args=['--repo',f.primaryRoot,'--workspace',owner,...contractArgs,option,expected,...(mode==='unselected'?[]:pinArgs)];
  const failure=mode==='unselected'?'SNAPSHOT_SOURCE_OR_COPY_DRIFT':'TERMINAL29_PREFLIGHT_SOURCE_CHANGED';
  await assert.rejects(prepareTerminal22(args),new RegExp(failure));
  const prep=JSON.parse(fs.readFileSync(path.join(authorizedBuildRoot,'PREPARATION.json'),'utf8'));
  const manifest=JSON.parse(fs.readFileSync(manifestFile,'utf8'));
  assert.deepEqual([injected,prep.status,prep.managedBuiltThisRun,prep.nodeBinding],[true,'INCOMPLETE',false,undefined]);
  assert.deepEqual(fs.readdirSync(authorizedBuildRoot),['PREPARATION.json']);
  assert.equal(manifest.copyVerifiedFiles,mode==='selected'?3:4);
  assert.equal(manifest.status,mode==='after-capture'?'PASS':'FAIL');
  if(mode==='unselected')assert.deepEqual(manifest.sourceDiff.changed.map(r=>r.path),[watched]);
  else assert.deepEqual((mode==='selected'?manifest.failure:prep.errors[0]).details,
   {phase:mode==='selected'?'copy-checkpoint':'before-node-binding',path:watched,expected,actual});
  console.log(JSON.stringify({watchedMode:mode,verifiedFiles:manifest.copyVerifiedFiles,manifestStatus:manifest.status}));
 });
});

test('terminal22 managed resume rejects changed or stale captures without touching the receipt',async t=>{
 const f=producerWiringFixture(t),owner=path.join(f.coreWorkspace,'resume-owner'),roots=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
 const sha=b=>lifecycleRev2CreateHash('sha256').update(b).digest('hex');
 const write=(file,value)=>{const bytes=JSON.stringify(value);fs.writeFileSync(file,bytes);return sha(bytes);};
 const args=(digest,stage='resume-managed')=>['--repo',f.primaryRoot,'--workspace',owner,'--stage',stage,'--capture-sha256',digest];
 const {authorizedBuildRoot,sourceRoot}=roots,metadata=path.join(sourceRoot,'.ymcc-source-snapshot');
 fs.mkdirSync(authorizedBuildRoot,{recursive:true});fs.mkdirSync(metadata,{recursive:true});fs.mkdirSync(path.join(f.primaryRoot,'native'));
 fs.writeFileSync(path.join(f.primaryRoot,'native','main.cpp'),'current');
 const manifestFile=path.join(metadata,'manifest.json'),bindingFile=path.join(metadata,'node-tools-binding.json');
 const receiptFile=path.join(authorizedBuildRoot,'PREPARATION.json'),layoutFile=path.join(authorizedBuildRoot,'LAYOUT-terminal22.json');
 const contract='current105-terminal22-v1',oldMain=sha('old'),currentMain=sha('current');
 const fixture=(productMainSha256=oldMain)=>{
  const manifestSha256=write(manifestFile,{status:'PASS',snapshotRoot:sourceRoot,sourceRoot:f.primaryRoot,copyVerifiedFiles:1,main:{snapshot:{sha256:productMainSha256}}});
  const nodeBindingSha256=write(bindingFile,{status:'PASS',snapshotRoot:sourceRoot,sourceRoot:f.primaryRoot,expectedSnapshotManifestSha256:manifestSha256});
  const layoutSha=write(layoutFile,{...roots,schemaVersion:1,managedInputContract:contract,sourceAuthority:{mode:'EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'},productMainSha256,manifestSha256,nodeBindingSha256});
  return write(receiptFile,{...roots,schemaVersion:1,status:'SOURCE_CAPTURED_NOT_COMPILED',preparationStage:'capture',managedInputContract:contract,sourceCaptureOnly:true,managedBuiltThisRun:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[],capture:{status:'PASS',manifestFile,copyVerifiedFiles:1},nodeBinding:{status:'PASS',path:bindingFile},layout:{path:layoutFile,sha256:layoutSha}});
 };
 const stale=fixture(),preFiles=fs.readdirSync(authorizedBuildRoot);
 await assert.rejects(prepareTerminal22(args('b'.repeat(64))),/RESUME_PIN_CHANGED/);
 await assert.rejects(prepareTerminal22(args('b'.repeat(64),'resume-frozen-managed')),/RESUME_PIN_CHANGED/);
 await assert.rejects(prepareTerminal22(args(stale)),/RESUME_CURRENT_MAIN_CHANGED/);
 assert.equal(sha(fs.readFileSync(receiptFile)),stale);assert.deepEqual(fs.readdirSync(authorizedBuildRoot),preFiles);
 const matching=fixture(currentMain);fs.mkdirSync(path.join(authorizedBuildRoot,'managed22'));
 await assert.rejects(prepareTerminal22(args(matching)),/RESUME_EXISTING_MANAGED_ATTEMPT/);
 await assert.rejects(prepareTerminal22(args(matching,'resume-frozen-managed')),/RESUME_EXISTING_MANAGED_ATTEMPT/);
 assert.equal(sha(fs.readFileSync(receiptFile)),matching);assert(!fs.existsSync(path.join(authorizedBuildRoot,'managed-resume')));
});
test('terminal22 frozen managed resume rejects a tampered captured member before compiling',async t=>{
 const f=producerWiringFixture(t),owner=path.join(f.coreWorkspace,'frozen-owner');
 const roots=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
 const {authorizedBuildRoot,sourceRoot}=roots;
 const sha=b=>lifecycleRev2CreateHash('sha256').update(b).digest('hex');
 const write=(file,value)=>{const bytes=JSON.stringify(value);fs.writeFileSync(file,bytes);return sha(bytes);};
 fs.mkdirSync(authorizedBuildRoot,{recursive:true});fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});
 fs.mkdirSync(path.join(f.primaryRoot,'native'));
 const main=path.join('native','main.cpp');fs.writeFileSync(path.join(f.primaryRoot,main),'original');
 const capture=createCapturedSourceSnapshot({repoRoot:f.primaryRoot,outDir:sourceRoot});assert.equal(capture.status,'PASS');
 const manifestFile=capture.manifestFile,manifestSha256=sha(fs.readFileSync(manifestFile));
 const bindingFile=path.join(path.dirname(manifestFile),'node-tools-binding.json');
 const nodeBindingSha256=write(bindingFile,{status:'PASS',snapshotRoot:sourceRoot,sourceRoot:f.primaryRoot,expectedSnapshotManifestSha256:manifestSha256});
 const contract='current105-terminal22-v1',layoutFile=path.join(authorizedBuildRoot,'LAYOUT-terminal22.json');
 const layoutSha=write(layoutFile,{...roots,schemaVersion:1,managedInputContract:contract,sourceAuthority:{mode:'EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'},productMainSha256:sha('original'),manifestSha256,nodeBindingSha256});
 const receipt=path.join(authorizedBuildRoot,'PREPARATION.json');
 const receiptSha=write(receipt,{...roots,schemaVersion:1,status:'SOURCE_CAPTURED_NOT_COMPILED',preparationStage:'capture',managedInputContract:contract,sourceCaptureOnly:true,managedBuiltThisRun:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[],capture:{status:'PASS',manifestFile,copyVerifiedFiles:capture.copyVerifiedFiles},nodeBinding:{status:'PASS',path:bindingFile},layout:{path:layoutFile,sha256:layoutSha}});
 fs.writeFileSync(path.join(f.primaryRoot,main),'new mainline');
 fs.writeFileSync(path.join(sourceRoot,main),'tampered');
 await assert.rejects(prepareTerminal22(['--repo',f.primaryRoot,'--workspace',owner,'--stage','resume-frozen-managed','--capture-sha256',receiptSha]),{message:'SNAPSHOT_CAPTURED_VERSION_CHANGED'});
 assert.equal(sha(fs.readFileSync(receipt)),receiptSha);
 assert.equal(sha(fs.readFileSync(manifestFile)),manifestSha256);
 const resume=path.join(authorizedBuildRoot,'managed-resume');
 assert.deepEqual(fs.readdirSync(resume),['PREPARATION.json']);
 const failure=JSON.parse(fs.readFileSync(path.join(resume,'PREPARATION.json'),'utf8'));
 assert.deepEqual([failure.status,failure.managedBuiltThisRun,failure.sourceApprovalGranted,failure.frozenSourceOnly],['INCOMPLETE',false,false,true]);
});
test('terminal29 managed resume waits for strict parallel authority without changing the frozen route',async t=>{
 for(const current of [true,false])await t.test(current?'strict current':'frozen',async st=>{
  const f=producerWiringFixture(st),owner=path.join(f.coreWorkspace,'managed29');
  const roots=resolveTerminal22PreparationLayout({primaryRoot:f.primaryRoot,workspaceRoot:owner});
  const {authorizedBuildRoot,sourceRoot}=roots;
  const sha=bytes=>lifecycleRev2CreateHash('sha256').update(bytes).digest('hex');
  const write=(file,bytes)=>{fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);return sha(bytes);};
  const json=(file,value)=>write(file,JSON.stringify(value));
  const writeSource=(relative,text)=>write(path.join(f.primaryRoot,relative),text);
  const pipe='tools/ymcc_sim/global/controller-coordinator-pipe/';
  const contract='current105-terminal29-shared-input-power-v1';
  const productMainSha256=writeSource('native/main.cpp','// managed route control');
  writeSource(pipe+'r3-managed-input-contract.mjs','export {requireTerminalManagedContract} from '+JSON.stringify(new URL('./r3-managed-input-contract.mjs',import.meta.url).href));
  writeSource('tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','export const toolchainFiles=()=>[];');
  writeSource(pipe+'r3-current-compile-batch.mjs',
   'export function beginCurrentCompileBatch({requireCurrentSource,parallelSource}){'+
   'if(requireCurrentSource!=='+current+'||Boolean(parallelSource)!=='+current+')throw Error("WRONG_MANAGED_AUTHORITY_ROUTE");'+
   'const token={routeControl:true};return parallelSource?new Promise(resolve=>setImmediate(()=>resolve(token))):token;}'+
   'export function finishCurrentCompileBatch(token){if(token?.routeControl!==true)throw Error("UNAWAITED_MANAGED_AUTHORITY");'+
   'const after={scope:"SYNTHETIC_ROUTING_CONTROL",source:{isCurrent:'+current+'}};'+
   'return '+current+'?new Promise(resolve=>setImmediate(()=>resolve(after))):after;}');
  writeSource(pipe+'r3-current-managed-builder.mjs',
   'export function buildManagedFixture(out,{batchAuthority}){if(batchAuthority?.routeControl!==true)throw Error("UNAWAITED_MANAGED_AUTHORITY");throw Error("CONTROL_STOP_BEFORE_COMPILER");}');
  fs.mkdirSync(path.dirname(sourceRoot),{recursive:true});
  const capture=createCapturedSourceSnapshot({repoRoot:f.primaryRoot,outDir:sourceRoot});assert.equal(capture.status,'PASS');
  const manifestFile=capture.manifestFile,manifestSha256=sha(fs.readFileSync(manifestFile));
  const bindingFile=path.join(path.dirname(manifestFile),'node-tools-binding.json');
  const nodeBindingSha256=json(bindingFile,{status:'PASS',snapshotRoot:sourceRoot,sourceRoot:f.primaryRoot,expectedSnapshotManifestSha256:manifestSha256});
  const layoutFile=path.join(authorizedBuildRoot,'LAYOUT-terminal29.json');
  const layoutSha=json(layoutFile,{...roots,schemaVersion:1,managedInputContract:contract,sourceAuthority:{mode:'EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'},productMainSha256,manifestSha256,nodeBindingSha256});
  const receipt=path.join(authorizedBuildRoot,'PREPARATION.json');
  const receiptSha=json(receipt,{...roots,schemaVersion:1,status:'SOURCE_CAPTURED_NOT_COMPILED',preparationStage:'capture',managedInputContract:contract,sourceCaptureOnly:true,managedBuiltThisRun:false,sourceApprovalGranted:false,publicRuntimeAccepted:false,normalHostMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[],capture:{status:'PASS',manifestFile,copyVerifiedFiles:capture.copyVerifiedFiles},nodeBinding:{status:'PASS',path:bindingFile},layout:{path:layoutFile,sha256:layoutSha}});
  if(!current)writeSource('native/main.cpp','// intentionally newer than the frozen capture');
  await assert.rejects(prepareTerminal22(['--repo',f.primaryRoot,'--workspace',owner,'--managed-input-contract',contract,'--stage',current?'resume-managed':'resume-frozen-managed','--capture-sha256',receiptSha]),{message:'CONTROL_STOP_BEFORE_COMPILER'});
  assert.equal(sha(fs.readFileSync(receipt)),receiptSha);
  const resume=path.join(authorizedBuildRoot,'managed-resume');assert.deepEqual(fs.readdirSync(resume),['PREPARATION.json']);
  const row=JSON.parse(fs.readFileSync(path.join(resume,'PREPARATION.json'),'utf8'));
  assert.deepEqual([row.status,row.managedBuiltThisRun,row.captureReadback.isCurrent,row.captureReadback.sourceVerificationMode],['INCOMPLETE',false,current,current?'parallel-primary-and-copy':undefined]);
  assert.deepEqual(row.batchAfter,{scope:'SYNTHETIC_ROUTING_CONTROL',source:{isCurrent:current}});
 });
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
async function runPreparedLifecycleWithClock({preflightMs=0,modeMs=0,compilerEvidenceMs=0}={}){
 const source=fs.readFileSync(new URL('./r3-current-run.mjs',import.meta.url),'utf8');
 const start=source.indexOf('export async function executeCurrentPreparedLifecycle(');
 assert(start>=0);
 let clock=1000;
 const timeouts=[],reports=new Map();
 const call=vm.runInNewContext(source.slice(start+'export '.length)+'\nexecuteCurrentPreparedLifecycle',{
  path,Date:{now:()=>clock},performance:{now:()=>clock},process:{env:{}},
  fs:{existsSync:file=>path.basename(file)==='fixture-result.json',mkdirSync(){},readFileSync:()=>'',writeFileSync(){}},
  need(ok,code){if(!ok)throw Error(code);},
  lifecycleRoles:[...new Set(currentLifecycleModePlan().map(row=>row.role))],
  currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,
  openCurrentPreparedContext:role=>({role}),requireCurrentPreparedContext:()=>({canonical:{nativeExe:{path:'inert-fixture.exe'}}}),
  recordInputs:()=>{clock+=preflightMs;return{sources:[],artifacts:[]};},
  recheckCurrentPreparedContexts(){},recheckInputs(){},verifyCurrentLifecycleMode(){},
  runProcess:async(_exe,args,{timeoutMs})=>{
   timeouts.push(timeoutMs);clock+=modeMs;
   return complete({durationMs:modeMs,exitCode:args[1].startsWith('mutation')||args[0].includes('negative-')?1:0,stdout:'',stderr:''});
  },
  read:()=>({cases:[],checks:1,failures:[],status:'PASS'}),hash:()=>'',files:()=>[],
  write:(file,value)=>reports.set(path.basename(file),value),LIFECYCLE_VERSION:1,
  compilerInputs:()=>{clock+=compilerEvidenceMs;return[];},
 });
 let error=null;
 try{await call(path.resolve('G:/unit-output/lifecycle'),'inert-repo');}catch(cause){error=cause;}
 return {timeouts,result:reports.get('result.json'),error};
}
test('prepared lifecycle launch inherits the remaining group time and rejects a late exit',async()=>{
 const {timeouts,result,error}=await runPreparedLifecycleWithClock({preflightMs:88000,modeMs:2100});
 assert.deepEqual(timeouts,[2000]);
 assert.match(error?.message??'',/ORIGINAL_NATURAL_MODE45_EXIT_REQUIRED/);
 assert.equal(result.status,'FAIL');assert.equal(result.steps.length,0);
});
test('prepared lifecycle counts compiler evidence in the final original90 verdict',async()=>{
 const late=await runPreparedLifecycleWithClock({preflightMs:89990,compilerEvidenceMs:11});
 assert.equal(late.timeouts.length,16);
 assert.match(late.error?.message??'',/ORIGINAL_GROUP90_BUDGET_EXCEEDED/);
 assert.equal(late.result.status,'FAIL');
 const exact=await runPreparedLifecycleWithClock({preflightMs:89990,compilerEvidenceMs:10});
 assert.equal(exact.error,null);assert.equal(exact.result.status,'PASS');assert.equal(exact.result.groupWallMs,90000);
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
test('Terminal29 lifecycle role snapshots share preflight verification and keep both bookends',t=>{
 const {coreWorkspace:owner}=producerWiringFixture(t);
 const file=path.join(owner,'role-copy.cpp'),bytes=Buffer.from('owned role copy');
 fs.writeFileSync(file,bytes);
 const expected={path:file,sha256:lifecycleRev2Sha(bytes)};
 let reads=0;
 const digest=source=>{reads++;return lifecycleRev2Sha(fs.readFileSync(source));};
 const ledger=createLifecycleInputPins(digest);
 ledger.add(expected,owner,{roleSnapshot:true});
 ledger.add(expected,undefined,{ledgerReference:true});
 assert.equal(reads,0);
 assert.equal(ledger.deferredCount(),1);
 assert.equal(ledger.inputs.size,1);
 assert.throws(()=>ledger.add({...expected,sha256:'a'.repeat(64)},undefined,{ledgerReference:true}),/conflicting lifecycle input pin/);
 assert.throws(()=>ledger.add({...expected,path:path.join(owner,'..','foreign.cpp')},owner,{roleSnapshot:true}),/escaped original authority/);
 verifyLifecyclePinnedInputs(ledger.inputs,digest,'pre');
 verifyLifecyclePinnedInputs(ledger.inputs,digest,'post');
 assert.equal(reads,2);
 fs.writeFileSync(file,'changed');
 assert.throws(()=>verifyLifecyclePinnedInputs(ledger.inputs,digest,'post'),/lifecycle post input changed/);
 assert.throws(()=>ledger.add(expected,owner),/actual lifecycle input changed/);
 const ordinary=createLifecycleInputPins(digest);
 assert.throws(()=>ordinary.add(expected,undefined,{ledgerReference:true}),/actual lifecycle input changed/);
});
test('Terminal29 lifecycle role snapshots refuse invalid copies before launch',t=>{
 const {coreWorkspace:owner}=producerWiringFixture(t);
 const real=path.join(owner,'actual'),alias=path.join(owner,'alias');
 fs.mkdirSync(real);fs.symlinkSync(real,alias,'junction');
 try{
  const bytes=Buffer.from('original'),sha256=lifecycleRev2Sha(bytes),file=path.join(real,'role-copy.cpp');
  const digest=f=>lifecycleRev2Sha(fs.readFileSync(f));
  for(const [pathname,reason]of [[file,/ENOENT/],[path.join(alias,'role-copy.cpp'),/REPARSE|redirected/i]]){
   const ledger=createLifecycleInputPins(digest);
   ledger.add({path:pathname,sha256},owner,{roleSnapshot:true});
   if(pathname!==file)fs.writeFileSync(file,bytes);
   assert.throws(()=>verifyLifecyclePinnedInputs(ledger.inputs,digest,'pre'),reason);
  }
  const ledger=createLifecycleInputPins(digest);
  ledger.add({path:file,sha256},owner,{roleSnapshot:true});
  fs.writeFileSync(file,'changed before launch');
  assert.throws(()=>verifyLifecyclePinnedInputs(ledger.inputs,digest,'pre'),/lifecycle pre input changed/);
 }finally{fs.unlinkSync(alias);}
});
test('terminal22 lifecycle qualification rev2: trusted verifier rejects repinned captured code before any import',async t=>{
 const f=producerWiringFixture(t),layout=lifecycleRev2Layout(f),verifierBytes=fs.readFileSync(new URL('../source-snapshot.mjs',import.meta.url)),runnerBytes=fs.readFileSync(new URL('../runner.mjs',import.meta.url));
 const write=(relative,bytes)=>{const file=path.join(f.primaryRoot,relative);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);};
 write('native/main.cpp','// synthetic owned source, never compiled\n');write('tools/ymcc_sim/global/source-snapshot.mjs',verifierBytes);write('tools/ymcc_sim/global/runner.mjs',runnerBytes);
 const capture=createCapturedSourceSnapshot({repoRoot:f.primaryRoot,outDir:layout.sourceRoot});assert.equal(capture.status,'PASS');layout.manifestSha256=lifecycleRev2Sha(fs.readFileSync(capture.manifestFile));layout.verifierCodeSha256=lifecycleRev2Sha(verifierBytes);
 const {verifiedAt:serialTime,...serial}=verifyTerminal22LifecycleCapture(layout,f.primaryRoot);
 const {verifiedAt:parallelTime,...parallel}=await verifyTerminal22LifecycleCapture(layout,f.primaryRoot,{parallelPrimary:true});
 assert.equal(serial.status,'FROZEN_SOURCE_VERIFIED');assert.deepEqual(parallel,serial);
 const marker=path.join(f.base,'untrusted-module-executed.txt'),payload='import fs from "node:fs";fs.writeFileSync('+JSON.stringify(marker)+',"must not execute");export const forged=true;\n',copiedVerifier=path.join(layout.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs');
 fs.writeFileSync(copiedVerifier,payload);layout.verifierCodeSha256=lifecycleRev2Sha(Buffer.from(payload));assert.throws(()=>verifyTerminal22LifecycleCapture(layout,f.primaryRoot),/differs from trusted local implementation/);assert.throws(()=>verifyTerminal22LifecycleCapture(layout,f.primaryRoot,{parallelPrimary:true}),/differs from trusted local implementation/);assert(!fs.existsSync(marker));
 fs.writeFileSync(copiedVerifier,verifierBytes);layout.verifierCodeSha256=lifecycleRev2Sha(verifierBytes);fs.writeFileSync(path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs'),payload);
 assert.throws(()=>verifyTerminal22LifecycleCapture(layout,f.primaryRoot),/SNAPSHOT_CAPTURED_VERSION_CHANGED/);await assert.rejects(verifyTerminal22LifecycleCapture(layout,f.primaryRoot,{parallelPrimary:true}),/SNAPSHOT_CAPTURED_VERSION_CHANGED/);assert(!fs.existsSync(marker));
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


import {executeTerminal22NativePreparation,terminalNativeImplementationRoot,terminal26LocalNativeToolFiles} from './r3-terminal22-native-prepare.mjs';
import {terminalManagedRuntimeEnvironment,terminalRuntimeOracleRoot} from './r3-terminal22-qualified-observe.mjs';
import {fileURLToPath as native26FileURLToPath} from 'node:url';
test('signal26 native routing: trusted fixture implementation keeps original frozen source root separate',()=>{
 const primary=path.resolve(path.dirname(native26FileURLToPath(import.meta.url)),'../../../..');
 const captured=path.resolve('G:/owned-frozen-source-sample');
 const layout={managedInputContract:'current105-terminal26-sdk-lifetime-compact-v1',primaryRoot:primary,sourceRoot:captured};
 assert.equal(terminalNativeImplementationRoot(layout),primary);assert.equal(terminalRuntimeOracleRoot(layout),path.join(primary,'tools/ymcc_sim/global/controller-coordinator-pipe'));
 const files=terminal26LocalNativeToolFiles(layout);
 assert.equal(new Set(files.map(file=>path.resolve(file).toLowerCase())).size,files.length);
 for(const file of files){
  const relative=path.relative(primary,file);
  assert(path.isAbsolute(file)&&relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),file);
  const stat=fs.lstatSync(file);
  assert(stat.isFile()&&!stat.isSymbolicLink()&&fs.realpathSync.native(file).toLowerCase()===path.resolve(file).toLowerCase(),file);
 }
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
test('shared input power29 routing pins the captured case data instead of the live worktree',()=>{
 const primary=path.resolve(path.dirname(native26FileURLToPath(import.meta.url)),'../../../..');
 const captured=path.resolve('G:/owned-frozen-source-sample');
 const layout={managedInputContract:'current105-terminal29-shared-input-power-v1',primaryRoot:primary,sourceRoot:captured};
 const pins=terminal26LocalNativeToolFiles(layout);
 assert.equal(terminalNativeImplementationRoot(layout),primary);
 for(const name of ['pipe_cases.inc','prepare_ack_cases.inc']){
  const input=path.join('tools','fixtures','input_coordinator_lifecycle',name);
  assert.deepEqual(pins.filter(file=>path.basename(file)===name),[path.join(captured,input)]);
  assert(!pins.includes(path.join(primary,input)));
 }
 const compiler=path.join('tools','input_coordinator_current_compile_only.mjs');
 assert(pins.includes(path.join(primary,compiler)));
 assert(!pins.includes(path.join(captured,compiler)));
});
test('strict Terminal29 reusable Native preparation rejects stale Managed receipts before toolchain or output',async t=>{
 const f=producerWiringFixture(t,{directCore:true}),contract='current105-terminal29-shared-input-power-v1';
 const sourceRoot=f.primaryRoot,authorizedBuildRoot=path.join(f.base,'Build','Validation','native29');
 fs.mkdirSync(authorizedBuildRoot,{recursive:true});
 const write=(name,value)=>{
  const file=path.join(authorizedBuildRoot,name),bytes=JSON.stringify(value);fs.writeFileSync(file,bytes,{flag:'wx'});
  return {path:file,sha256:lifecycleRev2Sha(bytes)};
 };
 const layout=write('layout.json',{sourceRoot,workspaceRoot:f.base,authorizedBuildRoot,managedInputContract:contract});
 const manifest=write('manifest.json',{});
 const batchAfter={status:'AUTHORITY_STABLE',source:{isCurrent:true},fullSourceVerificationCalls:2};
 const common={status:'MANAGED29_COMPILED_NOT_RUNTIME_ACCEPTED',managedInputContract:contract,publicRuntimeAccepted:false,sourceRoot,layout,managed:{manifestPath:manifest.path,manifestSha256:manifest.sha256},managedBuiltThisRun:true,managedMemberCount:29,batchAfter};
 const toolchain=path.join(authorizedBuildRoot,'not-present-toolchain.json');
 const priorExit=process.exitCode;
 try{
  for(const [name,changes] of Object.entries({old:{managedBuiltThisRun:undefined},stale:{batchAfter:{...batchAfter,source:{isCurrent:false}}},partial:{batchAfter:{...batchAfter,fullSourceVerificationCalls:1}},wrongCount:{managedMemberCount:28},badStatus:{batchAfter:{...batchAfter,status:'INCOMPLETE'}}})){
   const inFile=write(name+'.json',{...common,...changes}),out=path.join(authorizedBuildRoot,name);
   await assert.rejects(executeTerminal22NativePreparation(['--preparation',inFile.path,'--toolchain-manifest',toolchain,'--out',out,'--runtime-reuse-policy','owned-lifecycle-child-v1']),/current completed Managed29 receipt required/);
   assert(!fs.existsSync(out),'stale managed input must not start the Native preparation');
  }
  const invalidToolchain=write('invalid-toolchain.json',{schemaVersion:0}).path;
  for(const [name,changes] of Object.entries({current:{},frozen:{frozenSourceOnly:true,managedBuiltThisRun:undefined,batchAfter:{...batchAfter,source:{isCurrent:false}}}})){
   const inFile=write(name+'.json',{...common,...changes}),out=path.join(authorizedBuildRoot,name);
   const result=await executeTerminal22NativePreparation(['--preparation',inFile.path,'--toolchain-manifest',invalidToolchain,'--out',out,'--runtime-reuse-policy','owned-lifecycle-child-v1']);
   assert.equal(result.status,'INCOMPLETE');
   assert.match(result.errors[0].message,/0 !== 1/);
   assert.equal(result.phaseTiming.phases[0].phase,'verify-native-toolchain');
  }
 }finally{process.exitCode=priorExit;}
});
test('Terminal29 lifecycle batch verifies a shared toolchain once per wave and preserves a drift failure',async t=>{
 const f=producerWiringFixture(t,{directCore:true}),contract='current105-terminal29-shared-input-power-v1';
 const primaryRoot=path.resolve(path.dirname(native26FileURLToPath(import.meta.url)),'../../../..');
 const authorizedBuildRoot=path.join(f.base,'Build','Validation','native29');
 fs.mkdirSync(authorizedBuildRoot,{recursive:true});
 const write=(name,value)=>{
  const file=path.join(authorizedBuildRoot,name),bytes=JSON.stringify(value);
  fs.writeFileSync(file,bytes,{flag:'wx'});return {path:file,sha256:lifecycleRev2Sha(bytes)};
 };
 const layout=write('layout.json',{primaryRoot,sourceRoot:f.primaryRoot,workspaceRoot:f.base,authorizedBuildRoot,managedInputContract:contract});
 const manifest=write('managed.json',{}),canary=write('verify-once.json',{member:'canary'}),drift=write('drift.json',{member:'drift'});
 const preparation=write('preparation.json',{status:'MANAGED29_COMPILED_NOT_RUNTIME_ACCEPTED',managedInputContract:contract,publicRuntimeAccepted:false,
  layout,managed:{manifestPath:manifest.path,manifestSha256:manifest.sha256},managedBuiltThisRun:true,managedMemberCount:29,
  batchAfter:{status:'AUTHORITY_STABLE',source:{isCurrent:true},fullSourceVerificationCalls:2}});
 const original=row=>({original:row.path,sha256:row.sha256});
 const tools=write('native-toolchain.json',{schemaVersion:1,files:[original(canary)],tools:[{...original(drift),sha256:'0'.repeat(64)}]});
 const out=path.join(authorizedBuildRoot,'batch'),priorExit=process.exitCode;
 const read=fs.readFileSync;let reads=0;
 const readMock=t.mock.method(fs,'readFileSync',(file,...args)=>{if(file===canary.path)reads++;return read(file,...args);});
 let result;
 try{
  result=await executeTerminal22NativePreparation(['--preparation',preparation.path,'--toolchain-manifest',tools.path,'--out',out,
   '--role','lifecycle-batch','--runtime-reuse-policy','owned-lifecycle-child-v1']);
 }finally{readMock.mock.restore();process.exitCode=priorExit;}
 assert.equal(result.status,'INCOMPLETE');
 assert.deepEqual(result.roles,[]);
 assert.deepEqual(result.failures.map(r=>r.role),['positive','bypassAdmission','recoveryInheritance']);
 for(const row of result.failures){
  assert.match(row.errors[0].message,/native original member changed:/);
  assert(row.errors[0].message.includes(drift.path));
  const failed=JSON.parse(read(row.receipt.path,'utf8'));
  assert.equal(failed.phaseTiming.phases[0].phase,'verify-native-toolchain');
  assert(!failed.native && !failed.nativeExecuted && !failed.deviceIo);
 }
 assert(!fs.existsSync(path.join(out,'authority')));
 assert.equal(reads,1,'same-wave original tool canary reads');
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

test('MSI required return observations reject absent payloads but retain observed UNKNOWN',async()=>{
 const {inspectMsiReturnCases}=await import('../../../fixtures/input_coordinator_lifecycle/msi_return_observation.mjs');
 const observation={schemaVersion:1,observationIsReturnProof:false,coherentSnapshot:false,durableDischargeProven:null,
  modeReturnOwed:true,modeDebtReadStable:false,startSeamActive:false,durableDischargeUnavailableInStartSeam:false};
 const points=['msiReturnBeforeTakeover','msiReturnAfterVerifiedReturn','msiReturnAtFixtureEnd'];
 const rows=['pending-start','success-settle'].map(kind=>({name:'MSI/31000/same-intent-external-return-during-'+kind,
  ...Object.fromEntries(points.map(point=>[point,structuredClone(observation)])),msiReturnAfterCleanup:null}));
 const result=inspectMsiReturnCases(rows,{required:true});
 assert.equal(result.cases[0].observations.msiReturnAtFixtureEnd.status,'UNKNOWN');
 assert.equal(result.cases[0].observations.msiReturnAfterCleanup.status,'UNKNOWN');
 assert.equal(result.fullOwnershipReturnProven,false);assert.equal(result.durableDischargeProven,false);
 for(const point of points)for(const absent of [null,undefined]){
  const broken=structuredClone(rows);
  if(absent===undefined)delete broken[0][point];else broken[0][point]=absent;
  assert.throws(()=>inspectMsiReturnCases(broken,{required:true}),{message:'MSI_RETURN_DECLARED_OBSERVATION_MISSING:'+point});
 }
 assert.equal(inspectMsiReturnCases([{name:rows[0].name,msiReturnAtFixtureEnd:null}]).cases[0].observations.msiReturnAtFixtureEnd.status,'UNKNOWN');
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
 await t.test('all six real parameters reject null and missing required return observations',()=>{
  for(const entry of positive)for(const point of ['msiReturnBeforeTakeover','msiReturnAfterVerifiedReturn','msiReturnAtFixtureEnd']){
   check(entry,r=>{r.cases[0][point]=null;},/ORIGINAL_OBSERVATION_MISSING/);
   check(entry,r=>{delete r.cases[0][point];},/ORIGINAL_OBSERVATION_MISSING/);
  }
 });
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
