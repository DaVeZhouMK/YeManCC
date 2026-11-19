// Extend one actual terminal22 capture into a full native compile-only fixture.
// The original eight polarities and all public runtime gates are preserved.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath,pathToFileURL} from 'node:url';
import {requireTerminalManagedContract,terminalManagedStageStatus,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';
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
 return contract.signalEvidence?[...([...terminal26LocalToolRelatives,...(productSourceSuccessor?['tools/ymcc_sim/global/controller-coordinator-pipe/r3-terminal26-product-source-successor-v1.json']:[]),...([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor)?['tools/fixtures/input_coordinator_lifecycle/msi_return_observation.inc']:[])].map(relative=>path.join(root,relative))),...terminal26MsiCleanupSuccessorFiles(productHeaderSuccessor),...terminal26MsiPolicySuccessorFiles(productPolicySuccessor),...terminal26MsiPolicyFixtureInputFiles(fixtureInputSuccessor),...terminal26NativeSourceProfileFiles(nativeSourceProfile)]:[];
}
const roles=Object.freeze({positive:null,bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});

// Pin only compiler-observed headers and the three explicit translation units.
// The verified declaration is an allowlist/preimage, never a claim every member ran.
export function collectObservedCompilerLedger({native,allowedTools=[]}){
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
 const inputs=new Map(),used=[];
 const observedInput=file=>{const original=allowed.get(key(file));assert(original,'unbound observed compiler input: '+file);const row=exact(original);inputs.set(key(row.path),row);used.push(row.path);};
 for(const file of observed)observedInput(file);
 const fixture=source.compiledGenerated.filter(row=>key(row.path)===key(path.join(buildRoot,'fixture.cpp')));assert.equal(fixture.length,1,'unique actual fixture translation unit required');observedInput(fixture[0].path);
 for(const relative of ['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp']){const rows=source.records.filter(row=>row.path===relative);assert.equal(rows.length,1,'actual separate translation unit required: '+relative);observedInput(rows[0].snapshot);}
 inputs.set(key(native.buildScript.path),exact(native.buildScript));
 assert.equal(digest(native.sourceManifest.path),native.sourceManifest.sha256,'compiler source manifest changed during ledger');
 assert.equal(digest(logFile),buildLogSha256,'compiler log changed during ledger');
 return {schemaVersion:1,status:'ACTUAL_COMPILER_INPUTS_PINNED_NOT_RUNTIME_QUALIFIED',method:'ORIGINAL_MSVC_SHOWINCLUDES_AND_EXPLICIT_THREE_TU_RECIPE',inputs:[...inputs.values()],observedIncludePaths:reported,explicitTranslationUnits:[fixture[0].path,...['native/dsu_server.cpp','native/steam_original_recovery_engine.cpp'].map(relative=>source.records.find(row=>row.path===relative).snapshot)],sourceManifest:exact(native.sourceManifest),buildScript:exact(native.buildScript),buildLog:{path:logFile,sha256:buildLogSha256,bytes:fs.statSync(logFile).size},declaredAllowlistMembers:allowed.size,unobservedAllowlistClaimedUsed:false,sourceApprovalGranted:false,publicRuntimeAccepted:false};
}

export async function executeTerminal22NativePreparation(argv=process.argv.slice(2)){
const options={};for(let i=0;i<argv.length;i++){
  const key=argv[i];assert(['--preparation','--toolchain-manifest','--out','--role','--progress','--final-stages','--native-optimization','--product-source-successor','--product-header-successor','--product-policy-successor','--fixture-input-successor','--runtime-parameters','--native-source-profile','--runtime-reuse-policy'].includes(key)&&!Object.hasOwn(options,key),'unknown/duplicate option');
 assert(argv[i+1]&&!argv[i+1].startsWith('--'),'option requires value');options[key]=argv[++i];
}
for(const key of ['--preparation','--toolchain-manifest','--out'])assert(options[key],key+' required');
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
assert([terminalManagedStageStatus('managed'),terminalManagedStageStatus('managed','current105-terminal23-sd-decoder-v1'),terminalManagedStageStatus('managed','current105-terminal26-sdk-lifetime-compact-v1')].includes(preparation.status),'explicit managed terminal preparation required');assert.equal(preparation.publicRuntimeAccepted,false);
assert.equal(hash(preparation.layout.path),preparation.layout.sha256);assert.equal(hash(preparation.managed.manifestPath),preparation.managed.manifestSha256);
const layout=read(preparation.layout.path),contract=requireTerminalManagedContract(layout.managedInputContract),out=path.resolve(options['--out']);
const productSourceSuccessor=options['--product-source-successor'];
const productHeaderSuccessor=options['--product-header-successor'];
const productPolicySuccessor=options['--product-policy-successor'];
const fixtureInputSuccessor=options['--fixture-input-successor'];
const runtimeParameterLayout=options['--runtime-parameters'];
 const runtimeReusePolicy=options['--runtime-reuse-policy'];
 if(runtimeReusePolicy!==undefined)assert(runtimeReusePolicy==='owned-direct-child-v1'&&nativeSourceProfile==='current105-terminal26-current-capture-20261117a'&&contract.signalEvidence&&runtimeParameterLayout===undefined,'exact current26 public runtime policy required');
if(nativeSourceProfile!==undefined){assert(contract.signalEvidence,'terminal26 native profile contract required');assert([productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout].every(value=>value===undefined),'current native profile cannot mix private successors or parameter layouts');}
if(runtimeParameterLayout!==undefined){assert(['msi-policy-boundaries-r2','msi-policy-boundaries-r3-exit-process'].includes(runtimeParameterLayout),'unknown runtime parameters');assert([TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'boundary parameters require reviewed MSI policy successor');if(runtimeParameterLayout==='msi-policy-boundaries-r3-exit-process')assert.equal(productPolicySuccessor,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5,'split exit requires R8 policy');assert.equal(fixtureInputSuccessor,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'boundary parameters require original input-r2');}
if(fixtureInputSuccessor!==undefined){assert.equal(fixtureInputSuccessor,TERMINAL26_MSI_POLICY_FIXTURE_INPUT,'unknown fixture input successor');assert([TERMINAL26_MSI_POLICY_SUCCESSOR,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'fixture successor requires policy candidate');}
if(productPolicySuccessor!==undefined){assert(contract.signalEvidence,'terminal26 policy contract required');assert([TERMINAL26_MSI_POLICY_SUCCESSOR,TERMINAL26_MSI_POLICY_SUCCESSOR_R7,TERMINAL26_MSI_POLICY_SUCCESSOR_R8_R5].includes(productPolicySuccessor),'unknown terminal26 policy successor');}
if(productHeaderSuccessor!==undefined){assert(contract.signalEvidence,'terminal26 product header contract required');assert.equal(productHeaderSuccessor,TERMINAL26_MSI_CLEANUP_SUCCESSOR,'unknown terminal26 product header successor');}
if(productSourceSuccessor!==undefined){assert(contract.signalEvidence,'terminal26 successor contract required');assert.equal(productSourceSuccessor,TERMINAL26_PRODUCT_SUCCESSOR,'unknown terminal26 product successor');}
const nativeOptimization=options['--native-optimization']??'O2';
assert(['O2','Od'].includes(nativeOptimization),'explicit reviewed native optimization required');
assert(nativeOptimization==='O2'||contract.signalEvidence,'nondefault optimization is explicit26 fixture only');
assert.equal(preparation.status,terminalManagedStageStatus('managed',contract.id),'preparation status differs from selected managed contract');
if(contract.steamDeckPadDecoder)assert.equal(preparation.managedInputContract,contract.id,'explicit decoder contract preparation required');
assert(within(layout.authorizedBuildRoot,out)&&!fs.existsSync(out),'fresh owned native preparation required');
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
 const declared=read(toolchainFile);assert.equal(declared.schemaVersion,1);assert(Array.isArray(declared.files)&&declared.files.length>0);assert(Array.isArray(declared.tools)&&declared.tools.length>0);
 const nativeTools=new Map();
 say('Verify every original native toolchain member; archived frozen paths are not substituted');
 for(const row of [...declared.files,...declared.tools]){
  assert(path.isAbsolute(row.original)&&/^[a-f0-9]{64}$/.test(row.sha256),'native tool pin required');
  assert(fs.lstatSync(row.original).isFile()&&!fs.lstatSync(row.original).isSymbolicLink(),'regular original native tool required');
  assert.equal(hash(row.original),row.sha256,'native original member changed: '+row.original);
  const key=path.resolve(row.original).toLowerCase(),old=nativeTools.get(key);assert(!old||old.sha256===row.sha256,'native tool pin conflict');nativeTools.set(key,{path:row.original,sha256:row.sha256});
 }
 state.nativeToolchain={files:declared.files.length,tools:declared.tools.length,allOriginalMembersVerified:true};
  phaseTiming.enter('load-fixture-tools');
 const implementationRoot=terminalNativeImplementationRoot(layout);
 const moduleRoot=path.join(implementationRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
 authority=await import(pathToFileURL(path.join(moduleRoot,'r3-current-compile-batch.mjs')).href);
 const {buildCurrentCompileOnly,preflightCurrentCompileSources}=await import(pathToFileURL(path.join(implementationRoot,'tools/input_coordinator_current_compile_only.mjs')).href);
 const actual=new Map(preparation.batchAfter.actualTools.map(row=>[path.resolve(row.path).toLowerCase(),row.path]));
 const localToolFiles=terminal26LocalNativeToolFiles(layout,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,nativeSourceProfile);
 for(const file of localToolFiles)actual.set(path.resolve(file).toLowerCase(),file);
 state.fixtureToolImplementation={root:implementationRoot,sourceCaptureModified:false,localReviewedToolFiles:localToolFiles.map(pin),scope:'Reviewed fixture implementation plus separately declared optional exact product source successor; original capture and managed artifacts stay immutable'};
 for(const row of nativeTools.values())actual.set(path.resolve(row.path).toLowerCase(),row.path);
 actual.set(path.resolve(fileURLToPath(import.meta.url)).toLowerCase(),fileURLToPath(import.meta.url));
  if(contract.signalEvidence){phaseTiming.enter('static-preflight');state.generationPreflight=preflightCurrentCompileSources({sourceRoot:layout.sourceRoot,managedInputContract:contract.id,runtimeRoot:path.join(out,'qualification-runtime'),checkProgress,finalStages,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile});state.productPolicySuccessor=state.generationPreflight.productPolicySuccessor;state.fixtureInputSuccessor=state.generationPreflight.policyFixture?.inputSuccessor;state.productSourceSuccessor=state.generationPreflight.productSourceSuccessor;state.productHeaderSuccessor=state.generationPreflight.productHeaderSuccessor;say('Static core/fixture/logger/progress/stage and selected source successor preflight completed before full batch');}
  phaseTiming.enter('open-compile-authority');
 state.status='VERIFYING_NATIVE_BATCH';say('Open full captured source and original native toolchain authority');
 token=authority.beginCurrentCompileBatch({layout,actualToolFiles:[...actual.values()],receiptDirectory:path.join(out,'authority')});
  phaseTiming.enter('verify-managed-artifacts');
 const managed=read(preparation.managed.manifestPath);
 state.managedAdmissions=authority.verifyCurrentManagedSourceRows(managed,layout.sourceRoot,{batchAuthority:token}).length;
 assert.equal(state.managedAdmissions,contract.count);verifyManagedDecoderCompileEvidence(managed,contract.id,layout.authorizedBuildRoot);
 const nativeOut=path.join(out,'native'),runtimeRoot=path.join(out,'qualification-runtime');
 const argv=['--out',nativeOut,'--managed-dll',managed.fixtureDll,...(roles[role]?['--mutation',roles[role]]:[]),'--compile-only'];
 state.status='COMPILING_NATIVE';say('Compile the actual full native coordinator; no fixture or product is executed');
  phaseTiming.enter('generate-and-compile-native');
  state.checkProgress=checkProgress;state.finalStages=finalStages;state.runtimeParameterLayout=runtimeParameterLayout;state.runtimeReusePolicy=runtimeReusePolicy;state.nativeOptimization=nativeOptimization;state.native=buildCurrentCompileOnly({argv,batchAuthority:token,runtimeRoot,checkProgress,finalStages,nativeOptimization,productSourceSuccessor,productHeaderSuccessor,productPolicySuccessor,fixtureInputSuccessor,runtimeParameterLayout,runtimeReusePolicy,nativeSourceProfile});
 assert.equal(state.native.status,'COMPILED_NOT_EXECUTED');assert.equal(state.native.compileOnly,true);assert.equal(state.native.runBranchReached,false);
  phaseTiming.enter('collect-compiler-inputs');
  const compilerLedger=collectObservedCompilerLedger({native:state.native,allowedTools:[...nativeTools.values()]});
 const compilerLedgerFile=path.join(out,'COMPILER-INPUTS.json');fs.writeFileSync(compilerLedgerFile,JSON.stringify(compilerLedger,null,2)+'\n',{flag:'wx'});
 state.compilerLedger=pin(compilerLedgerFile);state.actualObservedCompilerInputs=compilerLedger.inputs.length;
 state.runtimeRoot=runtimeRoot;if(contract.signalEvidence){state.runtimeLayout=terminal26RuntimeLayout(runtimeRoot,{runtimeParameterLayout,runtimeReusePolicy});assert.deepEqual(state.native.runtimeLayout,state.runtimeLayout,'actual compiled runtime layout differs from preparation');}state.status=terminalManagedStageStatus('native',contract.id);
 }catch(error){phaseTiming.fail(error.message);state.status='INCOMPLETE';state.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
finally{
   if(token){phaseTiming.enter('finish-compile-authority');try{state.batchAfter=authority.finishCurrentCompileBatch(token);}catch(error){phaseTiming.fail(error.message);state.status='INCOMPLETE';state.errors.push({phase:'finish-authority',message:error.message,stack:error.stack});process.exitCode=1;}}
   phaseTiming.finish();
 save();console.log(JSON.stringify({status:state.status,role,resultFile,errors:state.errors.map(row=>row.message)}));
}
 return state;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await executeTerminal22NativePreparation();
