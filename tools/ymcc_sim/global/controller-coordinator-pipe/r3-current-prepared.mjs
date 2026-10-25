import {parseLifecycleLogData,readLifecycleLogData,LIFECYCLE_LOG_PRODUCER,LIFECYCLE_LOG_DATA_ROLE} from '../../../fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs';
// Explicit current-source prerequisite admission. This module never compiles,
// spawns, controls devices, or grants acceptance to compile-only results.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {createHash} from 'node:crypto';
import {extract} from '../../../fixtures/legion_motion_consumer/extract_main.mjs';
import {currentExpected,verifyCurrentSemantics,verifyCurrentChildren} from './r3-current105-oracle.mjs';
import{resolveFrozenExpectationToolPins,frozenExpectationArtifactPath}from'./r3-frozen-expectation-tool.mjs';
import {verifyCurrent105OriginalGroups as verifyCurrentOriginalGroups} from './r3-current105-original-groups.mjs';
const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const PROFILE_KIND='VC-CURRENT-105-6457-38835033-20261021';
const MAX_REGISTERED_INPUTS=640,MAX_COMPILER_INPUTS=3072;
const sharedInfrastructure=["tools/ymcc_sim/global/controller-coordinator-pipe/adapter.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/run.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/README.md","tools/ymcc_sim/global/controller-coordinator-pipe/r3-contract.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-contract.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-contract.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-scenarios.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-branch-evidence.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-reviewed-ownership-evidence.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-prerequisite-root.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-source-equivalence.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-frozen-authority.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-b51-frozen-authority.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-b49-compile-batch.mjs","tools/input_coordinator_private_terminals.mjs","tools/input_coordinator_b51_return_profile.mjs","tools/input_coordinator_private_terminal_prepare.mjs","tools/fixtures/input_coordinator_lifecycle/private_terminal_observer.h","tools/ymcc_sim/global/snapshot-node-tools.mjs","tools/ymcc_sim/global/source-snapshot.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/historical/source-snapshot-v6.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-managed-builder.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-cancellation-oracle.mjs","tools/ymcc_sim/global/controller-coordinator-pipe/r3-cancellation-oracle.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-cancellation-review.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-cancellation-cut47-independent-review.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-cancellation-b39-independent-review.json","tools/ymcc_sim/global/controller-coordinator-pipe/r3-scenarios.json","tools/ymcc_sim/global/controller-coordinator-pipe/bind-r3-fixtures.mjs","tools/input_coordinator_fixture_manifest.mjs","tools/ymcc_sim/global/controller-coordinator-lifecycle/adapter.mjs","tools/ymcc_sim/global/controller-lifecycle-pipe/adapter.mjs","tools/ymcc_sim/global/controller-lifecycle-pipe/managed-fixture.json","tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs","tools/ymcc_sim/global/controller-priority/adapter.mjs","tools/ymcc_sim/global/inputhost-protocol/adapter.mjs","tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json","tools/ymcc_sim/global/runner.mjs","tools/ymcc_sim/global/execution-completion/index.mjs"];
const sha=bytes=>createHash('sha256').update(bytes).digest('hex');
const fileHash=file=>sha(fs.readFileSync(file));
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const need=(ok,code)=>{if(!ok)throw Error('R3_CURRENT_PREPARED_'+code);};
const key=file=>path.resolve(file).toLowerCase();
const within=(root,file)=>{const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!path.isAbsolute(rel)&&rel!=='..'&&!rel.startsWith('..'+path.sep));};
const same=(a,b)=>typeof a==='string'&&typeof b==='string'&&key(a)===key(b);
const pinShape=row=>row&&typeof row.path==='string'&&path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256);
const live=new WeakMap(),registered=new Map();
const mutations=Object.freeze({positive:null,cancellation:'rollback-cancellation-guards',psOwnership:'rollback-ps-endpoint-ownership',psBinding:'rollback-ps-binding-revision',bypassAdmission:'bypass-admission',recoveryInheritance:'rollback-recovery-inheritance',switchReturn:'rollback-switch-return-rearm',switchOwner:'rollback-switch-fault-owner'});
const pipeRoles=new Set(['positive','cancellation','psOwnership','psBinding']);
const actualToolRows=new Set(['tools/virtual_controller_runtime_source.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/fixtures/input_coordinator_lifecycle/compiler_inputs.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs']);
function profileFile(repo){return path.join(repo,MODULE,'r3-current-prerequisites.json');}
export function validateCurrentProfileShape(value){
 need(value?.schemaVersion===1&&[PROFILE_KIND,'VC-CURRENT-101-6300-880D6734-20261021'].includes(value.kind),'EXPLICIT_PROFILE_KIND_REQUIRED');
 need(['UNBOUND','BOUND','EXTERNAL'].includes(value.status),'PROFILE_STATUS_REQUIRED');
 if(value.status==='UNBOUND')return {active:false};
 if(value.status==='EXTERNAL'){
  for(const name of ['workspaceRoot','authorizedBuildRoot','profilePath'])need(typeof value[name]==='string'&&path.isAbsolute(value[name]),'ABSOLUTE_'+name);
  need(within(path.join(value.workspaceRoot,'Build','Validation'),value.authorizedBuildRoot)&&!same(path.join(value.workspaceRoot,'Build','Validation'),value.authorizedBuildRoot),'OWNED_BUILD_SCOPE_REQUIRED');
  need(within(value.authorizedBuildRoot,value.profilePath)&&!same(value.authorizedBuildRoot,value.profilePath),'EXTERNAL_PROFILE_ESCAPED_BUILD_ROOT');
  return {active:true,external:true};
 }
 need(typeof value.sourceVersion==='string'&&value.sourceVersion.length>0,'SOURCE_VERSION_REQUIRED');
 for(const name of ['sourceRoot','workspaceRoot','authorizedBuildRoot'])need(typeof value[name]==='string'&&path.isAbsolute(value[name]),'ABSOLUTE_'+name);
 need(within(path.join(value.workspaceRoot,'Build','Validation'),value.authorizedBuildRoot)&&!same(path.join(value.workspaceRoot,'Build','Validation'),value.authorizedBuildRoot),'OWNED_BUILD_SCOPE_REQUIRED');
 need(/^[a-f0-9]{64}$/.test(value.sourceMainSha256),'EXACT_SOURCE_MAIN_REQUIRED');
 for(const name of ['sourceManifest','nodeToolsBinding','authorityBefore','authorityAfter','expectations'])need(pinShape(value[name]),'PIN_REQUIRED_'+name);
 need(Array.isArray(value.expectedManagedSources)&&value.expectedManagedSources.length===21&&new Set(value.expectedManagedSources.map(r=>r.relative)).size===21,'EXACT21_MANAGED_PROFILE_REQUIRED');
 for(const row of value.expectedManagedSources)need(typeof row.relative==='string'&&!path.isAbsolute(row.relative)&&!row.relative.includes('\\')&&!row.relative.includes(':')&&!row.relative.split('/').some(p=>p===''||p==='.'||p==='..')&&/^[a-f0-9]{64}$/.test(row.sha256),'STRICT_MANAGED_MEMBER_REQUIRED');
 need(value.roles&&Object.keys(value.roles).length===8&&Object.keys(mutations).every(role=>pinShape(value.roles[role])),'ALL_ORIGINAL_EIGHT_NATIVE_POLARITIES_REQUIRED');
 return {active:true,sourceVersion:value.sourceVersion};
}
export function currentPreparedProfileActive(repo){const file=profileFile(repo);if(!fs.existsSync(file))return false;return validateCurrentProfileShape(read(file)).active;}
function scopedPin(row,root){need(pinShape(row),'ABSOLUTE_FILE_PIN_REQUIRED');need(fs.lstatSync(row.path).isFile()&&!fs.lstatSync(row.path).isSymbolicLink(),'REGULAR_FILE_REQUIRED');if(root)need(within(root,row.path)&&within(fs.realpathSync(root),fs.realpathSync(row.path)),'PIN_ESCAPED_DECLARED_ROOT');need(fileHash(row.path)===row.sha256,'PIN_CHANGED:'+row.path);return row;}
function rowsUnique(rows){const result=new Map();for(const row of rows){need(pinShape(row),'COMPLETE_INPUT_PIN_REQUIRED');const k=key(row.path),old=result.get(k);need(!old||old.sha256===row.sha256,'CONFLICTING_INPUT_PIN');result.set(k,row);}return [...result.values()];}
function shapeCore(source,expectation){
 need(Array.isArray(source.core)&&source.core.length===75,'EXACT75_NATIVE_CORE_REQUIRED');
 const core=source.core.map(({symbol,source,sha256})=>({symbol,source:source??'expanded-native-main',sha256}));
 need(JSON.stringify(core)===JSON.stringify(expectation.nativeCore),'NATIVE_CORE_SOURCE_PROFILE_CHANGED');
 const structs=source.structCore?.map(({symbol,kind,source,sha256})=>({symbol,kind,source,sha256}));need(JSON.stringify(structs)===JSON.stringify(expectation.nativeStructCore),'COLD_ENVIRONMENT_SOURCE_PROFILE_CHANGED');
 const headers=source.compilerHeaderBindings?.map(({source,sha256})=>({source,sha256}));need(JSON.stringify(headers)===JSON.stringify(expectation.nativeCompilerHeaders),'REAL_COMPILED_HEADER_PROFILE_CHANGED');
 for(const wanted of expectation.originalSourceFixtureFiles){
  if(wanted.readMode===LIFECYCLE_LOG_DATA_ROLE){need(wanted.path===LIFECYCLE_LOG_PRODUCER&&wanted.fullSourceSha256ProvenanceOnly===true,'EXACT_LOG_DATA_MEMBER_REQUIRED');const row=source.records.find(r=>r.path===wanted.path);need(row?.inputRole===LIFECYCLE_LOG_DATA_ROLE,'ACTUAL_LOG_DATA_ROLE_REQUIRED');parseLifecycleLogData(fs.readFileSync(row.snapshot),wanted.dataPin);}
  else need(source.records.some(r=>r.path===wanted.path&&r.sha256===wanted.sha256),'ORIGINAL_PRODUCER_OR_FIXTURE_CHANGED:'+wanted.path);
 }
 const inert=source.compiledGenerated?.find(r=>r.path.replaceAll('\\','/').endsWith('/source/native/main.inert.cpp'));need(inert&&fileHash(inert.path)===inert.sha256,'ACTUAL_COMPILED_INERT_REQUIRED');
 const inertText=fs.readFileSync(inert.path,'utf8');
 for(const row of source.core){const record=row.source?source.records.find(r=>r.path===row.source):null;const actual=sha(extract(record?fs.readFileSync(record.snapshot,'utf8'):inertText,row.symbol).code);need(actual===row.executedSha256,'ACTUAL_COMPILED_CORE_BODY_CHANGED:'+row.symbol);}
 for(const row of source.structCore)need(sha(extract(inertText,row.symbol,row.kind).code)===row.executedSha256,'ACTUAL_COMPILED_STRUCT_CHANGED:'+row.symbol);
 for(const wanted of expectation.nativeCompilerHeaders){const record=source.records.find(r=>r.path===wanted.source);need(record&&record.sha256===wanted.sha256&&source.observedIncludePaths.some(file=>same(file,record.snapshot)),'ACTUAL_REQUIRED_HEADER_NOT_COMPILER_OBSERVED:'+wanted.source);}

}
function qualificationState(canonical,native,managed,buildRoot){
 const q=canonical.qualification;need(q&&pinShape(q.receipt)&&pinShape(q.raw),'ACTUAL_QUALIFICATION_PINS_REQUIRED');scopedPin(q.receipt,buildRoot);scopedPin(q.raw,buildRoot);const receipt=read(q.receipt.path),raw=read(q.raw.path);
 need(receipt.finishedAt&&receipt.postInputsStable===true,'QUALIFICATION_COMPLETED_AND_STABLE_REQUIRED');
 if(pipeRoles.has(canonical.role)){
  const expected=currentExpected(canonical.role),terminal=receipt.nativeProcess;
  need(terminal&&same(terminal.exe,canonical.nativeExe.path)&&terminal.args?.length===3&&terminal.args[1]==='full-pipe'&&same(terminal.args[2],managed.fixtureDll),'EXACT_QUALIFIED_NATIVE_AND_MANAGED_REQUIRED');
  need(same(receipt.preflight?.nativeExe?.path,canonical.nativeExe.path)&&receipt.preflight.nativeExe.sha256===canonical.nativeExe.sha256&&same(receipt.preflight?.managedDll?.path,managed.fixtureDll)&&receipt.preflight.managedDll.sha256===fileHash(managed.fixtureDll),'QUALIFIED_EXECUTABLE_PINS_REQUIRED');
  need(terminal.strictNativeBudgetMs===60000&&terminal.nativeWallDurationMs<=60000&&!terminal.timedOut&&!terminal.forcedTermination&&terminal.exitCode===expected.expectedExit,'ORIGINAL_SINGLE60_QUALIFICATION_REQUIRED');
  need(receipt.execution?.exitCode===expected.expectedExit&&receipt.execution.signal===null&&receipt.execution.outerTimedOut===false,'QUALIFICATION_NATURAL_OUTER_EXIT_REQUIRED');
  need(same(receipt.rawReport?.path,q.raw.path)&&receipt.rawReport.sha256===q.raw.sha256,'QUALIFIED_RAW_REPORT_REQUIRED');
  verifyCurrentSemantics(canonical.role,raw);
  verifyCurrentOriginalGroups(raw,{ownershipRollback:canonical.role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(canonical.role),cancellationRollback:canonical.role==='cancellation'});
  verifyCurrentChildren(canonical.role,raw,managed,terminal.pid);
 }else{
  const row=receipt.modeResults?.find(r=>r.label===q.stepLabel);need(row&&same(row.raw?.path,q.raw.path)&&row.raw.sha256===q.raw.sha256,'EXACT_LIFECYCLE_QUALIFICATION_STEP_REQUIRED');
  need(receipt.originalModeBudgetMs===45000&&receipt.originalGroupBudgetMs===90000&&receipt.execution?.groupWallMs<=90000&&receipt.execution.exitCode===0&&receipt.execution.signal===null&&receipt.execution.outerTimedOut===false,'ORIGINAL_LIFECYCLE_QUALIFICATION_BUDGETS_REQUIRED');
  need(row.mutation===mutations[canonical.role]&&raw.status==='FAIL'&&raw.forbiddenIo===0&&raw.fullTranslationUnit===true&&raw.normalProductMainExecuted===false&&raw.failures.length>0,'ACTUAL_LIFECYCLE_NEGATIVE_REQUIRED');
  const assertion=canonical.role==='bypassAdmission'?'actual durable request locks before consumer runs':canonical.role==='recoveryInheritance'?'single real return refresh restores requested operational target':canonical.role==='switchReturn'?'verified late cleanup queues only the continuation of the already charged recovery':'one current frame fault has exactly one serialized Stop owner';
  need(raw.failures.some(r=>r.assertion===assertion),'ORIGINAL_DESIGNATED_LIFECYCLE_FAULT_REQUIRED');
 }
 return q;
}
export function openCurrentPreparedContext(role,repo){
 need(Object.hasOwn(mutations,role),'KNOWN_NATIVE_ROLE_REQUIRED');repo=path.resolve(repo);const declarationFile=profileFile(repo),declarationBytes=fs.readFileSync(declarationFile),declarationSha=sha(declarationBytes),declaration=JSON.parse(declarationBytes.toString('utf8').replace(/^\uFEFF/,''));const selection=validateCurrentProfileShape(declaration);need(selection.active,'BOUND_CURRENT_PROFILE_REQUIRED');
 let profile=declaration,externalPin=null;
 if(selection.external){
  need(fs.existsSync(declaration.profilePath),'EXTERNAL_PROFILE_MISSING');
  const bytes=fs.readFileSync(declaration.profilePath);externalPin={path:declaration.profilePath,sha256:sha(bytes)};scopedPin(externalPin,declaration.authorizedBuildRoot);profile=JSON.parse(bytes.toString('utf8').replace(/^\uFEFF/,''));
  need(profile.status==='BOUND'&&validateCurrentProfileShape(profile).active&&same(profile.workspaceRoot,declaration.workspaceRoot)&&same(profile.authorizedBuildRoot,declaration.authorizedBuildRoot),'EXTERNAL_BOUND_PROFILE_SCOPE_REQUIRED');
 }
 const cacheKey=JSON.stringify([key(repo),declarationSha,externalPin?.sha256??null,role]);if(registered.has(cacheKey))return registered.get(cacheKey);
 need(same(fs.realpathSync(profile.authorizedBuildRoot),profile.authorizedBuildRoot),'BUILD_ROOT_REDIRECTION');
 need(fileHash(path.join(repo,'native/main.cpp'))===profile.sourceMainSha256,'CURRENT_PRODUCT_MAIN_CHANGED');
 const immutable=[{path:declarationFile,sha256:declarationSha},...(externalPin?[externalPin]:[]),profile.sourceManifest,profile.nodeToolsBinding,profile.authorityBefore,profile.authorityAfter,profile.expectations];for(const row of immutable)scopedPin(row);
 need(same(profile.expectations.path,path.join(repo,MODULE,'r3-current105-expectations.json')),'REPOSITORY_EXPECTATION_REQUIRED');const expectation=read(profile.expectations.path);need(JSON.stringify(profile.expectedManagedSources)===JSON.stringify(expectation.managedSources),'REVIEWED_EXACT21_MANAGED_PROFILE_CHANGED');
 const snapshot=read(profile.sourceManifest.path),before=read(profile.authorityBefore.path),after=read(profile.authorityAfter.path);
 need(snapshot.status==='PASS'&&same(snapshot.snapshotRoot,profile.sourceRoot)&&snapshot.files?.length>0,'COMPLETE_CAPTURE_REQUIRED');
 need(before.source?.snapshotIntegrityVerified===true&&before.source.creationStatus==='PASS'&&after.status==='AUTHORITY_STABLE'&&after.source?.snapshotIntegrityVerified===true&&after.fullSourceVerificationCalls===2,'FULL_PREPARATION_SOURCE_AUTHORITY_REQUIRED');
 need(before.source.fileCount===snapshot.files.length&&after.source.fileCount===snapshot.files.length,'COMPLETE_CAPTURE_MEMBERSHIP_REQUIRED');
 need(/^[a-f0-9]{64}$/.test(before.nodeTools?.graphSha256??'')&&after.nodeTools?.graphSha256===before.nodeTools.graphSha256&&Array.isArray(after.actualTools)&&after.actualTools.length>0&&JSON.stringify(after.actualTools)===JSON.stringify(before.actualTools),'PREPARATION_TOOLS_CHANGED');
 const rolePin=scopedPin(profile.roles[role],profile.authorizedBuildRoot),canonical=read(rolePin.path);need(canonical.schemaVersion===1&&canonical.kind==='CURRENT_SOURCE_COMPILED_AND_QUALIFIED_NATIVE'&&canonical.role===role&&canonical.sourceVersion===profile.sourceVersion&&canonical.sourceMainSha256===profile.sourceMainSha256&&canonical.mutation===mutations[role],'EXACT_CURRENT_NATIVE_POLARITY_REQUIRED');
 for(const name of ['buildReceipt','sourceManifest','nativeExe','managedManifest','compilerLedger'])scopedPin(canonical[name],profile.authorizedBuildRoot);
 const native=read(canonical.buildReceipt.path),source=read(canonical.sourceManifest.path),managed=read(canonical.managedManifest.path),compiler=read(canonical.compilerLedger.path);
 need(native.status==='COMPILED_NOT_EXECUTED'&&native.compilerExitCode===0&&native.runBranchReached===false&&native.results?.length===0&&(native.mutation??null)===mutations[role],'ORIGINAL_COMPILE_ONLY_PROVENANCE_REQUIRED');
 need(same(native.exe?.path,canonical.nativeExe.path)&&native.exe.sha256===canonical.nativeExe.sha256&&same(native.sourceManifest?.path,canonical.sourceManifest.path)&&native.sourceManifest.sha256===canonical.sourceManifest.sha256,'ACTUAL_COMPILED_OUTPUT_REQUIRED');
 need(managed.status==='BUILT'&&managed.realSdkLoaded===false&&managed.hardwareIo===false&&managed.semanticMutation==null&&managed.inputBindings?.semanticMutation==null&&same(managed.manifestPath,canonical.managedManifest.path),'SDK_FREE_UNMUTATED_MANAGED_REQUIRED');
 need(same(native.managedProof?.dll?.path,managed.fixtureDll)&&native.managedProof.dll.sha256===fileHash(managed.fixtureDll)&&same(native.managedProof.manifest,managed.manifestPath)&&native.managedProof.manifestSha256===canonical.managedManifest.sha256,'NATIVE_COMPILED_WITH_EXACT_RUNTIME_MANAGED_REQUIRED');
 shapeCore(source,expectation);need(source.records?.length>0&&new Set(source.records.map(r=>r.path)).size===source.records.length,'EXACT_NATIVE_SOURCE_MEMBERSHIP_REQUIRED');
 const mapped=[],dataInputs=[],all=[...immutable,rolePin,canonical.buildReceipt,canonical.sourceManifest,canonical.nativeExe,canonical.managedManifest,canonical.compilerLedger,...after.actualTools];
 for(const row of source.records){need(typeof row.path==='string'&&!path.isAbsolute(row.path)&&!row.path.includes('\\')&&!row.path.split('/').some(p=>p==='..'||p==='.'||p===''),'STRICT_NATIVE_SOURCE_MEMBER_REQUIRED');all.push({path:row.original,sha256:row.sha256},{path:row.snapshot,sha256:row.sha256});if(row.inputRole==='actual-current-compile-tool'){need(actualToolRows.has(row.path)&&after.actualTools.some(t=>same(t.path,row.original)&&t.sha256===row.sha256),'ACTUAL_COMPILE_TOOL_DECLARATION_REQUIRED');}else if(row.inputRole===LIFECYCLE_LOG_DATA_ROLE){need(row.path===LIFECYCLE_LOG_PRODUCER&&within(profile.sourceRoot,row.original),'ACTUAL_FROZEN_LOG_DATA_SOURCE_REQUIRED');const wanted=expectation.originalSourceFixtureFiles.find(x=>x.path===row.path&&x.readMode===LIFECYCLE_LOG_DATA_ROLE);need(wanted,'EXACT_LOG_DATA_SPEC_REQUIRED');readLifecycleLogData(repo,wanted.dataPin);dataInputs.push({relative:row.path,dataPin:structuredClone(wanted.dataPin)});}else{need(within(profile.sourceRoot,row.original),'ACTUAL_FROZEN_PRODUCT_SOURCE_REQUIRED');mapped.push({relative:row.path,path:path.join(repo,row.path),sha256:row.sha256});}need(within(profile.authorizedBuildRoot,row.snapshot),'FROZEN_COMPILE_INPUT_SCOPE');}
 need(dataInputs.length===1,'ONE_EXACT_LOG_DATA_INPUT_REQUIRED');
 const managedSources=managed.inputBindings.sources;need(managedSources?.length===21,'EXACT21_MANAGED_INPUTS_REQUIRED');for(const row of managedSources){const expected=profile.expectedManagedSources.find(r=>r.relative===row.relative);need(expected?.sha256===row.sha256&&within(profile.sourceRoot,row.path)&&within(profile.authorizedBuildRoot,row.snapshot),'CURRENT_MANAGED_SOURCE_PROFILE_REQUIRED');mapped.push({relative:row.relative,path:path.join(repo,row.relative),sha256:row.sha256});all.push({path:row.path,sha256:row.sha256},{path:row.snapshot,sha256:row.sha256});}
 need(new Set(managedSources.map(r=>r.relative)).size===21,'MANAGED_MEMBERSHIP_DUPLICATE');all.push(...source.compiledGenerated,...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts,...managed.inputBindings.toolchainAllowlist,...managed.evaluatedCompilerInputs);
 need(Array.isArray(compiler.inputs)&&compiler.inputs.length>20&&compiler.inputs.length<=MAX_COMPILER_INPUTS,'ACTUAL_COMPILER_CLOSURE_REQUIRED');const compilerSet=new Map(compiler.inputs.map(r=>[key(r.path),r.sha256]));need(source.observedIncludePaths.length>20&&source.observedIncludePaths.every(file=>compilerSet.has(key(file))),'EVERY_COMPILER_OBSERVED_INCLUDE_REQUIRED');all.push(...compiler.inputs);
 const q=qualificationState(canonical,native,managed,profile.authorizedBuildRoot);all.push(q.receipt,q.raw);const frozenTools=resolveFrozenExpectationToolPins({pins:rowsUnique(all),profile,repo,authorityTools:after.actualTools}),pinned=rowsUnique(frozenTools.pins);for(const row of pinned)scopedPin(row);for(const row of mapped)need(fileHash(row.path)===row.sha256,'CURRENT_SOURCE_CHANGED:'+row.relative);
 const infrastructure=['r3-frozen-expectation-tool.mjs','r3-current-managed-receipt.mjs','r3-current-routing.mjs','r3-current-prerequisites.json','r3-current-prepared.mjs','r3-current-oracle.mjs','r3-current-expectations.json','r3-current-ordered-observation.mjs','r3-current-source-order.json','r3-current-ownership.mjs','r3-current-original-groups.mjs','r3-current105-oracle.mjs','r3-current105-expectations.json','r3-current105-ordered-observation.mjs','r3-current105-source-order.json','r3-current105-original-groups.mjs','r3-current-run.mjs','r3-reviewed-contract.mjs','r3-reviewed-contract.json','r3-contract.mjs','run.mjs','adapter.mjs'];
 const sources=[...new Set([...mapped.map(r=>r.relative),...infrastructure.map(name=>MODULE+'/'+name),...sharedInfrastructure,'tools/fixtures/input_coordinator_lifecycle/lifecycle-log-data.mjs','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/ymcc_sim/global/controller-coordinator-lifecycle/adapter.mjs','tools/ymcc_sim/global/controller-coordinator-lifecycle/run.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'])];need(sources.length<=MAX_REGISTERED_INPUTS,'SOURCE_LIMIT640');
 const artifacts=[...new Set([process.execPath,declarationFile,...immutable.map(r=>r.path),rolePin.path,canonical.buildReceipt.path,canonical.sourceManifest.path,canonical.nativeExe.path,canonical.managedManifest.path,canonical.compilerLedger.path,...source.records.map(r=>r.snapshot),...source.compiledGenerated.map(r=>r.path),...managedSources.map(r=>r.snapshot),...managed.inputBindings.generatedInputs.map(r=>r.path),...managed.runtimeArtifacts.map(r=>r.path),...after.actualTools.map(r=>frozenExpectationArtifactPath(r,frozenTools.aliases)),q.receipt.path,q.raw.path])];need(artifacts.length<=MAX_REGISTERED_INPUTS,'ARTIFACT_LIMIT640');
 const context=Object.freeze({kind:'CURRENT_SOURCE_QUALIFIED_NATIVE_RUNTIME_CONTEXT',role});live.set(context,{repo,profile,canonical,native,source,managed,compiler,pinned,mapped,dataInputs,sources,artifacts,declarationSha,frozenExpectationToolAliases:frozenTools.aliases});registered.set(cacheKey,context);return context;
}
function liveState(context){const state=live.get(context);need(state,'LIVE_BRANDED_CONTEXT_REQUIRED');return state;}
export function requireCurrentPreparedContext(context){return structuredClone(liveState(context));}
export function currentPreparedSources(context){return [...liveState(context).sources];}
export function currentPreparedArtifacts(context){return [...liveState(context).artifacts];}
// Share actual byte reads only inside this synchronous verification pass.
// Every expected digest and each log-data contract is still checked. No digest
// survives a before/after boundary, a failed pass, or the next caller.
export function recheckCurrentPreparedContexts(contexts){
 const states=contexts.map(liveState),digests=new Map();
 const verify=(row,code)=>{const k=key(row.path);if(!digests.has(k))digests.set(k,fileHash(row.path));need(digests.get(k)===row.sha256,code);};
 return states.map(state=>{
  for(const row of state.pinned)verify(row,'BOUND_INPUT_CHANGED:'+row.path);
  for(const row of state.mapped)verify(row,'CURRENT_SOURCE_CHANGED:'+row.relative);
  for(const row of state.dataInputs)readLifecycleLogData(state.repo,row.dataPin);
  return {status:'ALL_CURRENT_COMPILED_INPUTS_REHASHED',files:state.pinned.length,currentSources:state.mapped.length};
 });
}
export function recheckCurrentPrepared(context){return recheckCurrentPreparedContexts([context])[0];}
export function verifyCurrentPreparedExecution(context,raw,terminal){const state=liveState(context),role=context.role;need(pipeRoles.has(role),'PIPE_EXECUTION_ROLE_REQUIRED');const expected=currentExpected(role);need(terminal&&terminal.exitCode===expected.expectedExit&&terminal.timedOut===false&&!terminal.launchError&&!terminal.signal&&!terminal.outputTruncated&&Number.isInteger(terminal.pid)&&terminal.pid>0&&terminal.durationMs<=60000,'ORIGINAL_NATURAL_SINGLE60_EXECUTION_REQUIRED');const exact=verifyCurrentSemantics(role,raw);verifyCurrentOriginalGroups(raw,{ownershipRollback:role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(role),cancellationRollback:role==='cancellation'});const children=verifyCurrentChildren(role,raw,state.managed,terminal.pid);recheckCurrentPrepared(context);return {exact,children,nativeExecutionProvenanceVerified:true,currentSourceAndArtifactsVerified:true,originalDetailedGroupsRetained:true,strictNativeBudgetMs:60000,wholeGoalComplete:false};}

