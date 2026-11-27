// Extend one actual terminal22 capture into a full native compile-only fixture.
// The original eight polarities and all public runtime gates are preserved.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {requireTerminalManagedContract,terminalManagedStageStatus,TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT} from './r3-managed-input-contract.mjs';
import {sharedNativePowerEvidenceTools,verifyCompiledSharedNativePower} from '../../../fixtures/input_coordinator_lifecycle/native_power_peer.mjs';
import {originalLifecycleRuntimeLabels,TERMINAL29_LIFECYCLE_RUNTIME_POLICY,terminal29LifecycleRuntimeLayout} from '../../../fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
import {terminal29CompileProfileRelative,terminal29SettledProfileRelative} from '../../../input_coordinator_terminal29_source_profile.mjs';
import {terminal26RuntimeLayout} from './r3-terminal26-runtime-layout.mjs';
import {terminal26NativeSourceProfileFiles,TERMINAL26_PRODUCT_SUCCESSOR,TERMINAL26_MSI_CLEANUP_SUCCESSOR,terminal26MsiCleanupSuccessorFiles,TERMINAL26_MSI_POLICY_SUCCESSOR,terminal26MsiPolicySuccessorFiles,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,terminal26MsiPolicyFixtureInputFiles,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5} from '../../../input_coordinator_terminal26_source_profile.mjs';

// Diagnostic wall-clock segments only. They do not set or extend any deadline,
// skip a hash check, grant source approval, or describe launcher/tool cleanup.
export function createNativePreparationPhaseTiming(now=()=>performance.now()){
 assert.equal(typeof now,'function','phase timing clock required');
 const started=now();assert(Number.isFinite(started),'finite phase start required');
 const names=new Set(['verify-native-toolchain','load-fixture-tools','static-preflight','open-compile-authority','verify-managed-artifacts','generate-and-compile-native','collect-compiler-inputs','finish-compile-authority']);
 const rows=[];let active=null,finished=false,last=started;
 const tick=()=>{const current=now();assert(Number.isFinite(current)&&current>=last,'monotonic phase clock required');last=current;return current;};
 const close=(ended,status,reason)=>{if(active){rows.push({phase:active.phase,startedOffsetMs:active.started-started,endedOffsetMs:ended-started,elapsedMs:ended-active.started,status,...(reason?{reason}:{})});active=null;}};
 return {
  enter(phase){assert(!finished&&names.has(phase)&&!rows.some(row=>row.phase===phase)&&active?.phase!==phase,'unique declared phase required');const current=tick();close(current,'COMPLETED');active={phase,started:current};},
  fail(reason){assert(!finished&&active,'active failed phase required');close(tick(),'FAILED',String(reason));},
  finish(){assert(!finished,'phase timing already finished');close(tick(),'COMPLETED');finished=true;},
  snapshot(){const current=finished?last:tick(),activeMs=active?current-active.started:0,attributedMs=rows.reduce((sum,row)=>sum+row.elapsedMs,0)+activeMs;return {schemaVersion:1,scope:'native preparation after output admission through authority close; final receipt write and outer launcher/tool cleanup excluded',elapsedMs:current-started,attributedMs,unattributedMs:current-started-attributedMs,finished,phases:rows.map(row=>({...row})),activePhase:active?{phase:active.phase,startedOffsetMs:active.started-started,elapsedMs:activeMs}:null,parentAndChildDurationsMustNotBeAdded:true,compilerSubprocessElapsedRecordedSeparately:true,executionOrAcceptanceGranted:false};}
 };
}
// Explicit26 can use reviewed local fixture tools over an immutable source
// capture. Both authority and compiler come from this same module graph;
// no caller supplied implementation or serialized token is accepted.
export function terminalNativeImplementationRoot(layout){
 const contract=requireTerminalManagedContract(layout?.managedInputContract);
 const trustedRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
 if(!contract.signalEvidence)return path.resolve(layout.sourceRoot);
 assert.equal(path.resolve(layout.primaryRoot).toLowerCase(),trustedRoot.toLowerCase(),'terminal26 local implementation must belong to original trusted primary');
 return trustedRoot;
}
const terminal26LocalToolRelatives=Object.freeze([
 'tools/fixtures/input_coordinator_lifecycle/denied_touchpad_input.mjs',
 'tools/input_coordinator_current_compile_only.mjs','tools/input_coordinator_current105_source_adapters.mjs','tools/input_coordinator_msi_policy_source.mjs',
 'tools/input_coordinator_terminal22_source_adapters.mjs','tools/input_coordinator_terminal26_source_profile.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-compile-source-profile.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-compile-source-profile-v2.json',
 'tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-native-source-profile.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal23-compile-source-profile.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-adapter-profile.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current-compile-batch.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-managed-input-contract.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-expectations.json',
 'tools/ymcc_sim/global/source-snapshot.mjs','tools/ymcc_sim/global/snapshot-node-tools.mjs',
 'tools/virtual_controller_runtime_source.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs',
 'tools/fixtures/input_coordinator_lifecycle/managed_manifest_binding.mjs',
 'tools/fixtures/input_coordinator_lifecycle/check_progress.mjs','tools/fixtures/input_coordinator_lifecycle/final_stages.mjs',
 'tools/fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs','tools/fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs','tools/fixtures/input_coordinator_lifecycle/msi_return_observation.mjs',
 // Exact source/data closure of the paired current26 runtime consumer.
 'tools/fixtures/battery_ui_native_recovery/snapshot-node-binding.mjs',
 'tools/ymcc_sim/global/bundled-node-tool-authority.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/historical/source-snapshot-v6.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-compile-batch.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-frozen-authority.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-b51-frozen-authority.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-contract.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current-ownership.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current-prepared.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current-run.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-oracle.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-ordered-observation.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-original-groups.mjs',
 'tools/fixtures/input_coordinator_lifecycle/public_power_ownership.mjs',
 'tools/fixtures/input_coordinator_lifecycle/public_power_ownership.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal29-runtime-expectations.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal29-ps-pause-expectations.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-current105-source-order.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-frozen-expectation-tool.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-contract.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-contract.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-scenarios.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-scenarios.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-source-equivalence.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-native-prepare.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-observation-terminal.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-qualified-observe.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal22-stage-receipts.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-runtime-expectations.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-tuple-expectations.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-switch-contract.mjs',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-switch-expectations.json',
 'tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-runtime-layout.mjs',
 'tools/ymcc_sim/global/execution-completion/index.mjs',
 'tools/ymcc_sim/global/runner.mjs',
]);
export function terminal26LocalNativeToolFiles(layout,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,nativeSourceProfile){
 const root=terminalNativeImplementationRoot(layout),contract=requireTerminalManagedContract(layout.managedInputContract);
 if(nativeSourceProfile!==undefined){assert(contract.signalEvidence,'terminal26 native profile contract required');terminal26NativeSourceProfileFiles(nativeSourceProfile);}
 if(productSourceSuccessor!==undefined){assert(contract.signalEvidence,'terminal26 successor contract required');assert.equal(productSourceSuccessor,TERMINAL26_PRODUCT_SUCCESSOR,'unknown terminal26 product successor');}
 if(productHeaderSuccessor!==undefined)assert(contract.signalEvidence,'terminal26 product header contract required');
 if(!contract.signalEvidence)return [];
 const local=new Set(terminal26LocalToolRelatives);
 if(contract.sharedInputPower)for(const relative of [...sharedNativePowerEvidenceTools,
  'tools/input_coordinator_terminal29_source_profile.mjs',terminal29CompileProfileRelative,terminal29SettledProfileRelative])local.add(relative);
 if(productSourceSuccessor)local.add('tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-product-source-successor-v1.json');
 if([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor))
  local.add('tools/fixtures/input_coordinator_lifecycle/msi_return_observation.inc');
 // Terminal29 adapts these two inputs from the frozen capture. Pin the bytes
 // actually consumed while keeping the implementation and older routes pinned.
 const capturedCases=contract.sharedInputPower?new Set(['tools/fixtures/input_coordinator_lifecycle/pipe_cases.inc','tools/fixtures/input_coordinator_lifecycle/prepare_ack_cases.inc']):null;
 return [...Array.from(local,relative=>path.join(capturedCases?.has(relative)?layout.sourceRoot:root,relative)),...terminal26MsiCleanupSuccessorFiles(productHeaderSuccessor),...terminal26MsiPolicySuccessorFiles(productPolicySuccessor),...terminal26MsiPolicyFixtureInputFiles(fixtureInputSuccessor),...terminal26NativeSourceProfileFiles(nativeSourceProfile)];
}
const roles=Object.freeze({positive:null,bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});

// Pin only compiler-observed headers and the three explicit translation units.
// The verified declaration is an allowlist/preimage, never a claim every member ran.
export function collectObservedCompilerLedger({native,allowedTools=[],sourceRoot,managedInputContract}={}){
 assert(native?.status==='COMPILED_NOT_EXECUTED'&&native.compileOnly===true&&native.compilerExitCode===0&&native.runBranchReached===false,'actual successful compile-only result required');
 const digest=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const key=file=>path.resolve(file).toLowerCase();
 const regular=file=>{assert(path.isAbsolute(file),'absolute compiler input required');const st=fs.lstatSync(file);assert(st.isFile()&&!st.isSymbolicLink(),'regular compiler input required');};
 const exact=row=>{assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact compiler input pin required');regular(row.path);assert.equal(digest(row.path),row.sha256,'compiler input changed: '+row.path);return {path:path.resolve(row.path),sha256:row.sha256};};
 exact(native.sourceManifest);exact(native.buildScript);exact(native.exe);
 const source=JSON.parse(fs.readFileSync(native.sourceManifest.path,'utf8').replace(/^\uFEFF/,''));
 assert(Array.isArray(source.records)&&source.records.length>0&&Array.isArray(source.compiledGenerated)&&Array.isArray(source.observedIncludePaths)&&source.observedIncludePaths.length>0,'actual compiler source manifest required');
 const allowed=new Map();
 const admit=row=>{assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'precompiled exact input pin required');const k=key(row.path),old=allowed.get(k);assert(!old||old.sha256===row.sha256,'conflicting precompiled input');allowed.set(k,{path:path.resolve(row.path),sha256:row.sha256});};
 for(const row of allowedTools)admit(row);
 for(const row of source.records)admit({path:row.snapshot,sha256:row.sha256});
 for(const row of source.compiledGenerated)admit(row);
 const buildRoot=path.dirname(native.buildScript.path),logFile=path.join(buildRoot,'build.log');regular(logFile);
 const buildLogSha256=digest(logFile),log=fs.readFileSync(logFile,'utf8');
 const observed=[...new Set(log.split(/\r?\n/).filter(line=>/(?:including file:|包含文件:)/i.test(line)).map(line=>line.match(/[A-Z]:\\.*$/i)?.[0]?.trim()).filter(Boolean).map(key))];
 assert(observed.length>0,'actual compiler include log required');
 const reported=[...new Set(source.observedIncludePaths.map(key))];
 assert.deepEqual([...observed].sort(),[...reported].sort(),'compiler include log and source manifest differ');
 const powerGeneration=verifyCompiledSharedNativePower({manifest:source,buildRoot,sourceRoot,managedInputContract});
 const inputs=new Map(),used=[];
 const observedInput=file=>{const original=allowed.get(key(file));assert(original,'unbound observed compiler input: '+file);const row=exact(original);inputs.set(key(row.path),row);used.push(row.path);};
 for(const file of observed)observedInput(file);
 const fixture=source.compiledGenerated.filter(row=>key(row.path)===key(path.join(buildRoot,'fixture.cpp')));assert.equal(fixture.length,1,'unique actual fixture translation unit required');observedInput(fixture[0].path);
 for(const relative of ['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp']){const rows=source.records.filter(row=>row.path===relative);assert.equal(rows.length,1,'actual separate translation unit required: '+relative);observedInput(rows[0].snapshot);}
 inputs.set(key(native.buildScript.path),exact(native.buildScript));
 assert.equal(digest(native.sourceManifest.path),native.sourceManifest.sha256,'compiler source manifest changed during ledger');
 assert.equal(digest(logFile),buildLogSha256,'compiler log changed during ledger');
 return {schemaVersion:1,status:'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED',method:'ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE',inputs:[...inputs.values()],observedIncludePaths:reported,explicitTranslationUnits:[fixture[0].path,...['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp'].map(relative=>source.records.find(row=>row.path===relative).snapshot)],sourceManifest:exact(native.sourceManifest),buildScript:exact(native.buildScript),buildLog:{path:logFile,sha256:buildLogSha256,bytes:fs.statSync(logFile).size},...(powerGeneration?{sharedNativePowerGeneration:powerGeneration}:{}),declaredAllowlistMembers:allowed.size,unobservedAllowlistClaimedUsed:false,sourceApprovalGranted:false,publicRuntimeAccepted:false};
}

const lifecycleBatchWaves=Object.freeze([Object.freeze(['positive','bypassAdmission','recoveryInheritance']),Object.freeze(['switchReturn','switchOwner'])]);
const lifecycleBatchRoleStaged='TERMINAL29_NATIVE_ROLE_STAGED';

export async function executeTerminal22NativePreparation(argv=process.argv.slice(2)){
const options={};for(let i=0;i<argv.length;i++){
  const key=argv[i];assert(['--preparation','--toolchain-manifest','--out','--role','--progress','--final-stages','--native-optimization','--product-source-successor','--product-header-successor','--product-policy-successor','--fixture-input-successor','--runtime-parameters','--native-source-profile','--runtime-reuse-policy'].includes(key)&&!Object.hasOwn(options,key),'unknown/duplicate option');
 assert(argv[i+1]&&!argv[i+1].startsWith('--'),'option requires value');options[key]=argv[++i];
}
for(const key of ['--preparation','--toolchain-manifest','--out'])assert(options[key],key+' required');
 if(options['--role']==='lifecycle-batch')return executeTerminal29LifecycleBatch(options);
 return executeNativeRole(options);
}

async function executeTerminal29LifecycleBatch(options){
 const accepted=new Set(['--preparation','--toolchain-manifest','--out','--role','--progress','--final-stages','--native-optimization','--runtime-reuse-policy']);
 assert(Object.keys(options).every(key=>accepted.has(key))&&options['--runtime-reuse-policy']===TERMINAL29_LIFECYCLE_RUNTIME_POLICY&&
  (options['--native-optimization']??'O2')==='O2','exact Terminal29 lifecycle batch options required');
 const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
 const key=file=>path.resolve(file).toLowerCase();
 const pin=(file,expected)=>{
  assert.equal(key(fs.realpathSync(file)),key(file),'batch file redirection');
  assert(fs.lstatSync(file).isFile(),'batch file must be regular');
  const raw=fs.readFileSync(file),sha256=createHash('sha256').update(raw).digest('hex');
  if(expected!==undefined)assert.equal(sha256,expected,'batch file changed: '+file);
  return {path:file,bytes:raw.byteLength,sha256};
 };
 const inputs={preparation:pin(path.resolve(options['--preparation'])),toolchainManifest:pin(path.resolve(options['--toolchain-manifest']))};
 const preparation=read(inputs.preparation.path);
 inputs.layout=pin(path.resolve(preparation.layout.path),preparation.layout.sha256);
 const layout=read(inputs.layout.path),contract=requireTerminalManagedContract(layout.managedInputContract);
 assert(contract.sharedInputPower&&preparation.frozenSourceOnly!==true,'strict current Managed29 batch preparation required');
 terminalNativeImplementationRoot(layout);
 const out=path.resolve(options['--out']),rel=path.relative(layout.authorizedBuildRoot,out);
 assert(rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep)&&path.dirname(rel)==='.', 'fresh direct batch child of authorized build root required');
 assert.equal(key(fs.realpathSync(layout.authorizedBuildRoot)),key(layout.authorizedBuildRoot),'batch build root redirection');
 fs.mkdirSync(out);
 const verify=reference=>assert.deepEqual(pin(reference.path,reference.sha256),reference);
 for(const reference of Object.values(inputs))verify(reference);
 const authorityDirectory=path.join(out,'authority'),rolesStaged=[],failures=[];
 let opening,token,verifier,toolPaths,after;
 const batch={admit({authority,layout:memberLayout,actual,member}){
  assert.deepEqual(memberLayout,layout,'batch role layout differs');
  for(const name of ['preparation','toolchainManifest'])assert.deepEqual(member[name],inputs[name],'batch role input differs: '+name);
  const paths=[...actual.values()],keys=paths.map(key).sort();
  if(opening){
   assert.equal(authority,verifier,'batch role authority differs');
   assert.deepEqual(keys,toolPaths,'batch role tool members differ');
  }else{
   verifier=authority;toolPaths=keys;
   opening=Promise.resolve().then(()=>verifier.beginCurrentCompileBatch({layout,actualToolFiles:paths,receiptDirectory:authorityDirectory,requireCurrentSource:true,parallelSource:true})).then(value=>(token=value));
  }
  return opening;
 }};
 try{
  for(const wave of lifecycleBatchWaves){
   // Share only the static declaration read within this wave, including a refusal.
   // The next wave, compile authority and actual compiler ledger recheck their inputs.
   let toolchainOutcome;
   const waveBatch={...batch,verifyNativeToolchain(member,verifyOriginal){
    assert.deepEqual(member,inputs.toolchainManifest,'batch role toolchain manifest changed');
    if(!toolchainOutcome){
     try{toolchainOutcome={value:verifyOriginal()};}catch(error){toolchainOutcome={error};}
    }
    if(Object.hasOwn(toolchainOutcome,'error'))throw toolchainOutcome.error;
    return toolchainOutcome.value;
   }};
   const settled=await Promise.allSettled(wave.map(async role=>{
    const roleRoot=path.join(out,role),state=await executeNativeRole({...options,'--role':role,'--out':roleRoot},waveBatch);
    const receipt=pin(path.join(roleRoot,'NATIVE-PREPARATION.json'));
    if(state.status!==lifecycleBatchRoleStaged)return {role,receipt,failed:true,errors:state.errors};
    assert.equal(state.role,role);assert.equal(state.mutation,roles[role]);
    const nativeRoot=path.join(roleRoot,'native');
    const compileReceipt=pin(path.join(nativeRoot,'COMPILE-ONLY.json'));
    assert.deepEqual(read(compileReceipt.path),JSON.parse(JSON.stringify(state.native)),'batch role compile receipt differs');
    const artifacts=[['compilerLedger',state.compilerLedger,path.join(roleRoot,'COMPILER-INPUTS.json')],
     ['compilerProcess',state.native.compilerProcess,path.join(nativeRoot,'build-process.json')],
     ['compileReceipt',compileReceipt,compileReceipt.path],
     ['buildScript',state.native.buildScript,path.join(roleRoot,'native','build.cmd')],
     ['sourceManifest',state.native.sourceManifest,path.join(roleRoot,'native','source-manifest.json')],
     ['exe',state.native.exe,path.join(roleRoot,'native','CoordinatorFixture.exe')]];
    const pins=Object.fromEntries(artifacts.map(([name,expected,file])=>{
     assert.equal(key(expected.path),key(file),'batch role output differs: '+name);
     return [name,pin(file,expected.sha256)];
    }));
    return {role,receipt,artifacts:pins};
   }));
   settled.forEach((result,index)=>{
    if(result.status==='rejected')failures.push({role:wave[index],stage:'role',error:String(result.reason)});
    else if(result.value.failed)failures.push({role:wave[index],stage:'role',receipt:result.value.receipt,errors:result.value.errors});
    else rolesStaged.push(result.value);
   });
   if(failures.length)break;
  }
 }finally{
  if(token)try{after=await verifier.finishCurrentCompileBatch(token);}catch(error){failures.push({stage:'finish-authority',error:String(error)});}
 }
 if(!failures.length){
  try{
   assert(after?.status==='AUTHORITY_STABLE'&&after.source?.isCurrent===true&&after.fullSourceVerificationCalls===2,'completed current batch authority required');
   assert.deepEqual(rolesStaged.map(row=>row.role),lifecycleBatchWaves.flat(),'all original batch roles required');
   for(const reference of [...Object.values(inputs),...rolesStaged.flatMap(row=>[row.receipt,...Object.values(row.artifacts)])])verify(reference);
  }catch(error){failures.push({stage:'closure',error:String(error)});}
 }
 const authorityPins=Object.fromEntries(['before','after'].flatMap(phase=>{
  const file=path.join(authorityDirectory,`authority-${phase}.json`);
  return fs.existsSync(file)?[[phase,pin(file)]]:[];
 }));
 const state={schemaVersion:1,status:failures.length?'INCOMPLETE':'TERMINAL29_NATIVE_BATCH_COMPILED_ONLY',producerPid:process.pid,inputs,roles:rolesStaged,failures,
  authority:authorityPins,fullSourceVerificationCalls:after?.fullSourceVerificationCalls??null,publicRuntimeAccepted:false,nativeExecuted:false};
 const resultFile=path.join(out,'NATIVE-BATCH.json');
 fs.writeFileSync(resultFile,JSON.stringify(state,null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({status:state.status,resultFile,resultSha256:pin(resultFile).sha256,producerPid:state.producerPid,roles:rolesStaged.map(row=>row.role),failures:failures.length}));
 if(failures.length)process.exitCode=1;
 return state;
}

async function executeNativeRole(options,batch){
const nativeSourceProfile=options['--native-source-profile'];terminal26NativeSourceProfileFiles(nativeSourceProfile);
const role=options['--role']??'positive';assert(Object.hasOwn(roles,role),'unknown original native polarity');
const checkProgress=options['--progress']==='completed-assertions';assert(options['--progress']===undefined||checkProgress,'unknown read-only progress mode');
const finalStages=options['--final-stages']==='observed';assert(options['--final-stages']===undefined||finalStages,'unknown final-stage observation mode');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const pin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
const within=(root,file)=>{const rel=path.relative(root,file);return rel&&!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep);};
const preparationFile=path.resolve(options['--preparation']),toolchainFile=path.resolve(options['--toolchain-manifest']);
const preparation=read(preparationFile);
assert([terminalManagedStageStatus('managed'),terminalManagedStageStatus('managed','current105-terminal23-sd-decoder-v1'),terminalManagedStageStatus('managed','current105-terminal26-sdk-lifetime-compact-v1'),terminalManagedStageStatus('managed',TERMINAL_SHARED_INPUT_POWER_MANAGED_CONTRACT)].includes(preparation.status),'explicit managed terminal preparation required');assert.equal(preparation.publicRuntimeAccepted,false);
assert.equal(hash(preparation.layout.path),preparation.layout.sha256);assert.equal(hash(preparation.managed.manifestPath),preparation.managed.manifestSha256);
const layout=read(preparation.layout.path),contract=requireTerminalManagedContract(layout.managedInputContract),out=path.resolve(options['--out']);
const productSourceSuccessor=options['--product-source-successor'];
const productHeaderSuccessor=options['--product-header-successor'];
const productPolicySuccessor=options['--product-policy-successor'];
const fixtureInputSuccessor=options['--fixture-input-successor'];
const runtimeParameterLayout=options['--runtime-parameters'];
 const runtimeReusePolicy=options['--runtime-reuse-policy'];
 if(runtimeReusePolicy!==undefined&&!contract.sharedInputPower)assert(runtimeReusePolicy==='owned-direct-child-v1'&&nativeSourceProfile!==undefined&&contract.signalEvidence&&runtimeParameterLayout===undefined,'exact current26 public runtime policy required');
if(nativeSourceProfile!==undefined){assert(contract.signalEvidence,'terminal26 native profile contract required');assert([productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout].every(value=>value===undefined),'current native profile cannot mix private successors or parameter layouts');}
if(runtimeParameterLayout!==undefined){assert(['msi-policy-boundaries-r2','msi-policy-boundaries-r3-exit-process'].includes(runtimeParameterLayout),'unknown runtime parameters');assert([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'boundary parameters require reviewed MSI policy successor');if(runtimeParameterLayout==='msi-policy-boundaries-r3-exit-process')assert.equal(productPolicySuccessor,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5,'split exit requires R8 policy');assert.equal(fixtureInputSuccessor,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'boundary parameters require original input-r2');}
if(fixtureInputSuccessor!==undefined){assert.equal(fixtureInputSuccessor,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'unknown fixture input successor');assert([TERMINAL26_MSI_POLICY_SUCCESSOR,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'fixture successor requires policy candidate');}
if(productPolicySuccessor!==undefined){assert(contract.signalEvidence,'terminal26 policy contract required');assert([TERMINAL26_MSI_POLICY_SUCCESSOR,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'unknown terminal26 policy successor');}
if(productHeaderSuccessor!==undefined){assert(contract.signalEvidence,'terminal26 product header contract required');assert.equal(productHeaderSuccessor,TERMINAL26_MSI_CLEANUP_SUCCESSOR,'unknown terminal26 product header successor');}
if(productSourceSuccessor!==undefined){assert(contract.signalEvidence,'terminal26 successor contract required');assert.equal(productSourceSuccessor,TERMINAL26_PRODUCT_SUCCESSOR,'unknown terminal26 product successor');}
const nativeOptimization=options['--native-optimization']??'O2';
if(contract.sharedInputPower)assert([productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,nativeSourceProfile].every(value=>value===undefined)&&
 (runtimeReusePolicy===undefined||runtimeReusePolicy===TERMINAL29_LIFECYCLE_RUNTIME_POLICY)&&nativeOptimization==='O2','exact Terminal29 compile route required');
assert(['O2','Od'].includes(nativeOptimization),'explicit reviewed native optimization required');
assert(nativeOptimization==='O2'||contract.signalEvidence,'nondefault optimization is explicit26 fixture only');
assert.equal(preparation.status,terminalManagedStageStatus('managed',contract.id),'preparation status differs from selected managed contract');
if(contract.steamDeckPadDecoder)assert.equal(preparation.managedInputContract,contract.id,'explicit decoder contract preparation required');
assert(within(layout.authorizedBuildRoot,out)&&!fs.existsSync(out),'fresh owned native preparation required');
const runtimeRoot=path.join(out,'qualification-runtime');
const lifecycleReuseLayout=runtimeReusePolicy===TERMINAL29_LIFECYCLE_RUNTIME_POLICY?terminal29LifecycleRuntimeLayout(runtimeRoot,roles[role]):undefined;
const requireCurrentSource=lifecycleReuseLayout!==undefined&&preparation.frozenSourceOnly!==true;
 if(batch)assert(requireCurrentSource&&contract.sharedInputPower,'strict Terminal29 required for shared authority');
if(requireCurrentSource)assert(preparation.managedBuiltThisRun===true&&preparation.managedMemberCount===contract.count&&
 preparation.batchAfter?.status==='AUTHORITY_STABLE'&&preparation.batchAfter.source?.isCurrent===true&&
 preparation.batchAfter.fullSourceVerificationCalls===2,'current completed Managed29 receipt required');
for(let parent=path.dirname(out);within(layout.authorizedBuildRoot,parent);parent=path.dirname(parent))if(fs.existsSync(parent))assert(!fs.lstatSync(parent).isSymbolicLink(),'redirected output parent');
fs.mkdirSync(out,{recursive:true});
const state={schemaVersion:1,status:'PREPARING',...(nativeSourceProfile===undefined?{}:{nativeSourceProfile}),managedInputContract:contract.id,role,mutation:roles[role],sourceRoot:layout.sourceRoot,preparation:pin(preparationFile),
 toolchainManifest:pin(toolchainFile),runner:pin(fileURLToPath(import.meta.url)),sourceApprovalGranted:false,
 publicRuntimeAccepted:false,nativeExecuted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,errors:[]};
 const phaseTiming=createNativePreparationPhaseTiming();
 const resultFile=path.join(out,'NATIVE-PREPARATION.json'),save=()=>{state.phaseTiming=phaseTiming.snapshot();fs.writeFileSync(resultFile,JSON.stringify(state,null,2)+'\n');};
const say=message=>{console.log(message);save();};
let authority,token;
try{
  phaseTiming.enter('verify-native-toolchain');
 const verifyOriginal=()=>{
  const bytes=fs.readFileSync(toolchainFile);
  assert.equal(createHash('sha256').update(bytes).digest('hex'),state.toolchainManifest.sha256,'native toolchain manifest changed before verification');
  const declared=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));assert.equal(declared.schemaVersion,1);assert(Array.isArray(declared.files)&&declared.files.length>0);assert(Array.isArray(declared.tools)&&declared.tools.length>0);
  const nativeTools=new Map();
  say('Verify every original native toolchain member; archived frozen paths are not substituted');
  for(const row of [...declared.files,...declared.tools]){
   assert(path.isAbsolute(row.original)&&/^[a-f0-9]{64}$/.test(row.sha256),'native tool pin required');
   assert(fs.lstatSync(row.original).isFile()&&!fs.lstatSync(row.original).isSymbolicLink(),'regular original native tool required');
   assert.equal(hash(row.original),row.sha256,'native original member changed: '+row.original);
   const key=path.resolve(row.original).toLowerCase(),old=nativeTools.get(key);assert(!old||old.sha256===row.sha256,'native tool pin conflict');nativeTools.set(key,{path:row.original,sha256:row.sha256});
  }
  return {nativeTools,files:declared.files.length,tools:declared.tools.length};
 };
 const {nativeTools,files,tools}=batch?batch.verifyNativeToolchain(state.toolchainManifest,verifyOriginal):verifyOriginal();
 state.nativeToolchain={files,tools,allOriginalMembersVerified:true,...(batch?{verificationScope:'shared-batch-wave'}:{})};
  phaseTiming.enter('load-fixture-tools');
 const implementationRoot=terminalNativeImplementationRoot(layout);
 const moduleRoot=path.join(implementationRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
 authority=await import(pathToFileURL(path.join(moduleRoot,'r3-current-compile-batch.mjs')).href);
 const {buildCurrentCompileOnly,buildCurrentCompileOnlyInWorker,preflightCurrentCompileSources}=await import(pathToFileURL(path.join(implementationRoot,'tools/input_coordinator_current_compile_only.mjs')).href);
 const actual=new Map(preparation.batchAfter.actualTools.map(row=>[path.resolve(row.path).toLowerCase(),row.path]));
 const localToolFiles=terminal26LocalNativeToolFiles(layout,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,nativeSourceProfile);
 for(const file of localToolFiles)actual.set(path.resolve(file).toLowerCase(),file);
 state.fixtureToolImplementation={root:implementationRoot,sourceCaptureModified:false,localReviewedToolFiles:localToolFiles.map(pin),scope:contract.sharedInputPower?'Reviewed fixture implementation and captured case data; original capture and managed artifacts stay immutable':'Reviewed fixture implementation plus separately declared optional exact product source successor; original capture and managed artifacts stay immutable'};
 for(const row of nativeTools.values())actual.set(path.resolve(row.path).toLowerCase(),row.path);
 actual.set(path.resolve(fileURLToPath(import.meta.url)).toLowerCase(),fileURLToPath(import.meta.url));
  if(contract.signalEvidence){phaseTiming.enter('static-preflight');state.generationPreflight=preflightCurrentCompileSources({sourceRoot:layout.sourceRoot,managedInputContract:contract.id,runtimeRoot:path.join(out,'qualification-runtime'),checkProgress,finalStages,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile,...(contract.sharedInputPower?{mutation:roles[role]}:{})});state.productPolicySuccessor=state.generationPreflight.productPolicySuccessor;state.fixtureInputSuccessor=state.generationPreflight.policyFixture?.inputSuccessor;state.productSourceSuccessor=state.generationPreflight.productSourceSuccessor;state.productHeaderSuccessor=state.generationPreflight.productHeaderSuccessor;say('Static core/fixture/logger/progress/stage and selected source successor preflight completed before full batch');}
  phaseTiming.enter('open-compile-authority');
 state.status='VERIFYING_NATIVE_BATCH';say('Open full captured source and original native toolchain authority');
 token=batch?await batch.admit({authority,layout,actual,member:state}):await authority.beginCurrentCompileBatch({layout,actualToolFiles:[...actual.values()],receiptDirectory:path.join(out,'authority'),requireCurrentSource,parallelSource:requireCurrentSource&&contract.sharedInputPower===true});
  phaseTiming.enter('verify-managed-artifacts');
 const managed=read(preparation.managed.manifestPath);
 state.managedAdmissions=authority.verifyCurrentManagedSourceRows(managed,layout.sourceRoot,{batchAuthority:token}).length;
 assert.equal(state.managedAdmissions,contract.count);
 const nativeOut=path.join(out,'native');
 const argv=['--out',nativeOut,'--managed-dll',managed.fixtureDll,...(roles[role]?['--mutation',roles[role]]:[]),'--compile-only'];
 state.status='COMPILING_NATIVE';say('Compile the actual full native coordinator; no fixture or product is executed');
  phaseTiming.enter('generate-and-compile-native');
   state.checkProgress=checkProgress;state.finalStages=finalStages;state.runtimeParameterLayout=runtimeParameterLayout;state.runtimeReusePolicy=runtimeReusePolicy;state.nativeOptimization=nativeOptimization;
   const compileOptions={argv,batchAuthority:token,runtimeRoot,checkProgress,finalStages,nativeOptimization,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile};
   state.native=batch?await buildCurrentCompileOnlyInWorker(compileOptions):buildCurrentCompileOnly(compileOptions);
 assert.equal(state.native.status,'COMPILED_NOT_EXECUTED');assert.equal(state.native.compileOnly,true);assert.equal(state.native.runBranchReached,false);
  phaseTiming.enter('collect-compiler-inputs');
  const compilerLedger=collectObservedCompilerLedger({native:state.native,allowedTools:[...nativeTools.values()],sourceRoot:layout.sourceRoot,managedInputContract:contract.id});
 const compilerLedgerFile=path.join(out,'COMPILER-INPUTS.json');fs.writeFileSync(compilerLedgerFile,JSON.stringify(compilerLedger,null,2)+'\n',{flag:'wx'});
 state.compilerLedger=pin(compilerLedgerFile);state.actualObservedCompilerInputs=compilerLedger.inputs.length;
 state.runtimeRoot=runtimeRoot;
 if(contract.sharedInputPower){
  const mutation=roles[role],labels=originalLifecycleRuntimeLabels(mutation),manifest=read(state.native.sourceManifest.path);
  assert.deepEqual(manifest.settingsMutexIsolation.originalLifecycleLabels,labels,'exact original lifecycle mode root binding required');
  assert.equal(manifest.settingsMutexIsolation.lifecycleMutation,mutation);
  assert.equal(manifest.settingsMutexIsolation.exactOriginalModeRootsOnly,lifecycleReuseLayout===undefined);
  state.originalLifecycleRootBinding={mutation,labels,runtimeRoot,finiteOriginalModesOnly:true};
  if(lifecycleReuseLayout){
   for(const bound of [state.generationPreflight,state.native,manifest.settingsMutexIsolation])assert.deepEqual(bound.lifecycleRuntimeLayout,lifecycleReuseLayout,'actual compiled lifecycle runtime layout differs from preparation');
   assert.equal(manifest.settingsMutexIsolation.runtimeEntryAdmissionEnforced,true);
   assert.equal(manifest.settingsMutexIsolation.runtimeBoundaryOnly,false);
   state.lifecycleRuntimeLayout=lifecycleReuseLayout;
  }
 }else if(contract.signalEvidence){state.runtimeLayout=terminal26RuntimeLayout(runtimeRoot,{runtimeParameterLayout,runtimeReusePolicy});assert.deepEqual(state.native.runtimeLayout,state.runtimeLayout,'actual compiled runtime layout differs from preparation');}
 state.status=batch?lifecycleBatchRoleStaged:terminalManagedStageStatus('native',contract.id);
 }catch(error){phaseTiming.fail(error.message);state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
finally{
    if(token&&!batch){phaseTiming.enter('finish-compile-authority');try{state.batchAfter=await authority.finishCurrentCompileBatch(token);}catch(error){phaseTiming.fail(error.message);state.status='INCOMPLETE';state.errors.push({phase:'finish-authority',message:error.message,stack:error.stack});process.exitCode=1;}}
   phaseTiming.finish();
 save();console.log(JSON.stringify({status:state.status,role,resultFile,errors:state.errors.map(row=>row.message)}));
}
 return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await executeTerminal22NativePreparation();
