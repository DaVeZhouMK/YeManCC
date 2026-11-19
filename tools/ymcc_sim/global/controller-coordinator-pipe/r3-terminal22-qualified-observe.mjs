// Observe an actual fixed-frame native105 process. Never publishes a runtime profile.
import {terminal26ConsumerToolSuccessor} from './r3-terminal26-consumer-successor.mjs';
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import{createHash}from'node:crypto';import{fileURLToPath,pathToFileURL}from'node:url';
import {readCompletedNativeReport,createNativeObservationBudget,ORIGINAL_NATIVE_BUDGET_MS} from './r3-terminal22-observation-terminal.mjs';
import {captureFinalStages} from './r3-terminal22-stage-receipts.mjs';
import {terminalNativeImplementationRoot,terminal26LocalNativeToolFiles} from './r3-terminal22-native-prepare.mjs';
import {terminal26PreparedRuntimeLayout,terminal26PreparedRuntimeGenerationOptions} from './r3-terminal26-runtime-layout.mjs';import {isolateCurrentFixtureSettingsMutex} from '../../../fixtures/input_coordinator_lifecycle/settings_mutex_isolation.mjs';
export function terminalRuntimeOracleRoot(layout){return path.join(terminalNativeImplementationRoot(layout),'tools/ymcc_sim/global/controller-coordinator-pipe');}
import {currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode} from './r3-current-run.mjs';
import {verifyCapturedSourceSnapshot as verifyTrustedLifecycleSnapshot,validateSourceSnapshotRoots as validateTrustedLifecycleRoots} from '../source-snapshot.mjs';
import {runProcess as trustedLifecycleRunProcess} from '../runner.mjs';
import {validateManagedCompileRows as validateTrustedLifecycleManagedRows,requireTerminalManagedContract,terminalManagedStageStatus,terminalManagedContractFromDeclaration,verifyManagedDecoderCompileEvidence} from './r3-managed-input-contract.mjs';

// This is the native child's runProcess deadline/close receipt, not this CLI's exit.
// Missing observations remain missing; expected exits never fill measured fields.
export function terminal22ExecutionFromProcess(terminal){
 if(!terminal||typeof terminal!=='object')throw Error('TERMINAL22_ACTUAL_PROCESS_RESULT_REQUIRED');
 const naturalExit=terminal.timedOut===false&&terminal.launchError===null&&terminal.signal===null&&Number.isInteger(terminal.exitCode)&&terminal.durationMs>=0&&Number.isFinite(terminal.durationMs);
 return {exitCode:terminal.exitCode,signal:terminal.signal,outerTimedOut:terminal.timedOut,launchError:terminal.launchError,outputTruncated:terminal.outputTruncated,durationMs:terminal.durationMs,naturalExit,
  supervisionScope:'original-runProcess-native-child-deadline-and-close',observerCliExitObserved:false};
}

export function parseTerminal22ObservationArguments(args){
 assert(Array.isArray(args),'strict observer options required');const options={};
 for(let i=0;i<args.length;i++){
  const key=args[i];assert(['--native-preparation','--lifecycle-preparations','--out','--native-budget-ms','--consumer-predecessors'].includes(key)&&!Object.hasOwn(options,key),'strict observer options required');
  assert(typeof args[i+1]==='string'&&args[i+1]&&!args[i+1].startsWith('--'),'option value required');options[key]=args[++i];
 }
 assert(options['--out']&&!!options['--native-preparation']!==!!options['--lifecycle-preparations'],'exactly one explicit native or lifecycle preparation input required');
 if(options['--lifecycle-preparations'])assert(options['--native-budget-ms']===undefined,'native watchdog option is not a lifecycle mode budget');
 if(options['--native-budget-ms']!==undefined)assert(/^[1-9][0-9]{4,5}$/.test(options['--native-budget-ms'])&&Number(options['--native-budget-ms'])>=60000&&Number(options['--native-budget-ms'])<=180000,'bounded integer native fixture watchdog required');
 // TERMINAL26_CONSUMER_SUCCESSOR_BEGIN parser
 if(options['--consumer-predecessors']!==undefined){
  assert(options['--native-preparation']&&!options['--lifecycle-preparations'],'consumer predecessor proof requires one native preparation');
  assert(path.isAbsolute(options['--native-preparation'])&&!/[\0\r\n]/.test(options['--native-preparation']),'absolute native preparation required for consumer predecessor proof');
  assert(path.isAbsolute(options['--consumer-predecessors'])&&!/[\0\r\n]/.test(options['--consumer-predecessors']),'absolute consumer predecessor declaration required');
 }
 // TERMINAL26_CONSUMER_SUCCESSOR_END parser
 if(options['--lifecycle-preparations'])for(const value of Object.values(options))assert(path.isAbsolute(value)&&!/[\0\r\n]/.test(value),'absolute observer input paths required');
 return options;
}
export function terminal22LifecyclePreparationPins(value){
 const roles=[...new Set(currentLifecycleModePlan().map(row=>row.role))];
 assert(value?.schemaVersion===1&&terminalManagedContractFromDeclaration(value.kind)&&Object.keys(value).sort().join('|')==='kind|preparations|schemaVersion','explicit lifecycle compiled-input declaration required');
 assert(Array.isArray(value.preparations)&&value.preparations.length===roles.length&&new Set(value.preparations.map(row=>row?.role)).size===roles.length,'all original five lifecycle preparations required');
 assert.deepEqual(value.preparations.map(row=>row.role),roles,'original lifecycle preparation role order required');
 for(const row of value.preparations)assert(row&&Object.keys(row).sort().join('|')==='path|role|sha256'&&path.isAbsolute(row.path)&&!/[\0\r\n]/.test(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact lifecycle preparation file pin required');
 return structuredClone(value.preparations);
}
const lifecycleSame=(a,b)=>typeof a==='string'&&typeof b==='string'&&path.resolve(a).toLowerCase()===path.resolve(b).toLowerCase();
const lifecycleInside=(root,file)=>{const relative=path.relative(root,file);return relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep);};
function lifecyclePlainPath(file){
 assert(typeof file==='string'&&path.isAbsolute(file)&&!/[\0\r\n]/.test(file),'absolute lifecycle path required');
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  let st;try{st=fs.lstatSync(cursor);}catch(error){if(error.code!=='ENOENT')throw error;}
  if(st){assert(!st.isSymbolicLink(),'lifecycle reparse path rejected');assert(lifecycleSame(fs.realpathSync(cursor),cursor),'lifecycle path redirection rejected');}
  if(path.dirname(cursor)===cursor)break;
 }
}

const trustedLifecyclePrimaryRoot=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
const lifecycleFileSha=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
export function terminalManagedRuntimeEnvironment(layout,inherited=process.env){ const program=layout?.dotnet; assert(program&&typeof program.path==='string'&&path.isAbsolute(program.path)&&path.basename(program.path).toLowerCase()==='dotnet.exe'&&/^[a-f0-9]{64}$/.test(program.sha256??''),'explicit pinned managed runtime program required'); lifecyclePlainPath(program.path); assert(fs.lstatSync(program.path).isFile(),'managed runtime program must be a regular file'); assert.equal(lifecycleFileSha(program.path),program.sha256,'managed runtime program identity changed'); return {...inherited,YMCC_FIXTURE_DOTNET:path.resolve(program.path),DOTNET_ROOT:path.dirname(path.resolve(program.path)),DOTNET_MULTILEVEL_LOOKUP:'0',DOTNET_CLI_TELEMETRY_OPTOUT:'1'};}export function validateTerminal22LifecycleLayout(layout,trustedPrimaryRoot=trustedLifecyclePrimaryRoot){
 assert(layout&&lifecycleSame(layout.primaryRoot,trustedPrimaryRoot),'lifecycle primary differs from trusted observer repository');
 for(const key of ['primaryRoot','sourceRoot','workspaceRoot','authorizedBuildRoot'])lifecyclePlainPath(layout[key]);
 const project=path.resolve(trustedPrimaryRoot,'../../..'),core=path.join(project,'Testing','YMCC','Workspace'),evidence=path.join(project,'Testing','YMCC','Evidence');
 const owner=path.resolve(layout.workspaceRoot),primary=path.resolve(trustedPrimaryRoot),source=path.resolve(layout.sourceRoot);
 const coreOwner=lifecycleInside(core,owner),legacyOwner=lifecycleInside(evidence,owner);assert(coreOwner||legacyOwner,'lifecycle owner outside trusted project core or evidence');
 if(coreOwner){assert(lifecycleSame(primary,path.join(project,'Mainline','YeManCC-source','YeManCC')),'canonical trusted core primary required');assert(lifecycleSame(source,path.join(owner,'Mainline','YeManCC-source','YeManCC')),'lifecycle capture does not belong to exact core owner');}
 else{const prefix=path.basename(primary)+'-terminal22-';assert(lifecycleSame(path.dirname(source),path.dirname(primary))&&path.basename(source).toLowerCase().startsWith(prefix.toLowerCase())&&/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i.test(path.basename(source).slice(prefix.length)),'exact original legacy terminal22 sibling required');}
 assert(lifecycleSame(layout.authorizedBuildRoot,path.join(owner,'Build','Validation','CurrentCoordinatorTerminal22')),'exact original lifecycle authorized build root required');
 validateTrustedLifecycleRoots(primary,source);
 return {primaryRoot:primary,sourceRoot:source,workspaceRoot:owner,authorizedBuildRoot:path.resolve(layout.authorizedBuildRoot),runtimeAuthorityGranted:false};
}
export function verifyTerminal22LifecycleCapture(layout,trustedPrimaryRoot=trustedLifecyclePrimaryRoot){
 validateTerminal22LifecycleLayout(layout,trustedPrimaryRoot);
 const verifierFile=fileURLToPath(new URL('../source-snapshot.mjs',import.meta.url)),runnerFile=fileURLToPath(new URL('../runner.mjs',import.meta.url));
 assert.equal(layout.verifierCodeSha256,lifecycleFileSha(verifierFile),'lifecycle verifier pin differs from trusted local implementation');
 const manifestFile=path.join(layout.sourceRoot,'.ymcc-source-snapshot/manifest.json');lifecyclePlainPath(manifestFile);assert.equal(lifecycleFileSha(manifestFile),layout.manifestSha256,'lifecycle capture manifest changed');
 const proof=verifyTrustedLifecycleSnapshot({manifestFile,repoRoot:trustedPrimaryRoot,expectedManifestSha256:layout.manifestSha256});
 assert(proof.status==='FROZEN_SOURCE_VERIFIED'&&proof.snapshotIntegrityVerified===true&&proof.creationStatus==='PASS','trusted complete lifecycle source capture required');
 assert.equal(lifecycleFileSha(path.join(layout.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')),lifecycleFileSha(verifierFile),'captured lifecycle verifier differs from trusted code');
 assert.equal(lifecycleFileSha(path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs')),lifecycleFileSha(runnerFile),'captured lifecycle runner differs from trusted code');
 return proof;
}

// This is the pipe branch's pre-import source barrier; no path from an unverified
// native receipt selects executable modules. The production caller never overrides primary.
export function verifyTerminal23PipeSource(n,preparation,layout,trustedPrimaryRoot=trustedLifecyclePrimaryRoot){
 assert(lifecycleSame(n?.sourceRoot,layout?.sourceRoot)&&lifecycleSame(preparation?.sourceRoot,layout?.sourceRoot),'pipe native and managed preparation source roots must match verified layout');
 return verifyTerminal22LifecycleCapture(layout,trustedPrimaryRoot);
}
export function validateTerminalLifecycleNativeReceipt(n,contractId,mutation){
 const contract=requireTerminalManagedContract(contractId);
 assert(n.native?.status==='COMPILED_NOT_EXECUTED'&&n.native.compileOnly===true&&n.native.compilerExitCode===0&&n.native.compilerSignal===null&&n.native.compilerError===null&&n.native.runBranchReached===false&&n.native.fixtureExecuted===false&&n.native.nativeExecuted===false&&n.native.normalProductMainExecuted===false&&n.native.realDeviceIo===false&&n.native.mutation===mutation&&n.managedAdmissions===contract.count,'real successful original native compile-only receipt required');
 return {managedSources:contract.count,runtimeAuthorityGranted:false};
}

export function terminal22LifecycleManagedDllPin(managed,proof,authorizedBuildRoot){
 assert(managed&&proof&&lifecycleSame(managed.fixtureDll,proof.path)&&/^[a-f0-9]{64}$/.test(proof.sha256),'exact actual lifecycle managed DLL pin required');
 const file=path.resolve(managed.fixtureDll);assert(lifecycleInside(authorizedBuildRoot,file),'actual lifecycle managed DLL escaped authority');lifecyclePlainPath(file);
 const st=fs.lstatSync(file);assert(st.isFile()&&!st.isSymbolicLink(),'regular actual lifecycle managed DLL required');
 assert(Array.isArray(managed.runtimeArtifacts),'actual managed runtime artifact members required');const rows=managed.runtimeArtifacts.filter(row=>lifecycleSame(row.path,file));
 assert(rows.length===1&&rows[0].sha256===proof.sha256,'actual lifecycle managed DLL must be one exact runtime artifact');
 assert.equal(lifecycleFileSha(file),proof.sha256,'actual lifecycle managed DLL changed');return {path:file,sha256:proof.sha256};
}
export function finalizeTerminal22LifecycleExecution({started,ended=Date.now(),complete,failed=false,modeResults=[]}){
 const groupWallMs=ended-started;assert(Number.isFinite(groupWallMs)&&groupWallMs>=0,'actual final lifecycle clock required');
 const deadlineExceeded=groupWallMs>90000,plan=currentLifecycleModePlan(),planComplete=modeResults.length===plan.length&&modeResults.every((row,index)=>row.label===currentLifecycleModeLabel(plan[index])&&row.semanticAccepted===true);
 const accepted=complete===true&&!failed&&!deadlineExceeded&&planComplete,terminals=modeResults.map(row=>row.nativeProcess),facts=terminals.length>0&&terminals.every(row=>row&&typeof row.timedOut==='boolean'&&(row.signal===null||typeof row.signal==='string'));
 return {status:accepted?'BOUNDED_ORIGINAL16_LIFECYCLE_MATCHED_PUBLIC_RUNTIME_PENDING':'INCOMPLETE',deadlineExceeded,finishedAt:new Date(ended).toISOString(),
  execution:{groupWallMs,exitCode:accepted?0:1,signal:facts?(terminals.find(row=>row.signal!==null)?.signal??null):undefined,outerTimedOut:deadlineExceeded?true:facts?terminals.some(row=>row.timedOut):undefined,supervisionScope:'actual-lifecycle-observer-control-flow-and-native-mode-deadlines',observerCliExitObserved:false},qualificationAccepted:false};
}


// Finite current26 paths are proved against the actual generated translation unit.
// A serialized runtimeLayout field cannot grant paths the binary did not compile.
export function terminal26JointFixturePreflightBytes(n,manifest,compiledBytes){ assert(Buffer.isBuffer(compiledBytes),'actual joint compiled fixture bytes required'); assert.equal(n.native?.productPolicySuccessor?.id,'msi-real-policy-return-r8-r5-20261117'); assert.equal(manifest.productPolicySuccessor?.id,n.native.productPolicySuccessor.id); assert.equal(manifest.productPolicySuccessor.delivery.sha256,'0e463dd30290f2bb4d89aa9cd0165d2c29e4c0db720215e10a8eb1c6a9a1a234'); const io=manifest.policyIoProvenance,joint=io?.joint; assert.equal(io?.adapter?.sha256,'7054141b74b2421f8416260868a4abb3e84ca5b4190017b4921cb60a757e774e'); assert.equal(joint?.pairedStartupPreconditions,true,'paired joint source required'); const roles={  durable:['disjoint input helpers; no debt/sample/phase writes','retain-original-external-USB4-location-input-in-joint'],  visibility:['typed-visibility-prototypes','single-shared-visibility-model'],  os:['pre-main typed declarations','post-main OS definitions before original case','feedback scope encloses whole original policy scope','cycle-model-bridge'],  lifetime:['state-lifetime-before-original-cases','state-outlives-feedback-and-original-policy-scope','explicit-initial-shared-file-and-topology-inputs'] }; if(joint.cancellation){  const split=manifest.policyIoProvenance.joint.cancellation.edits.fixture.some(row=>row.role==='terminal-exit-case-uses-independent-original-process');  const declaredSplit=n.runtimeLayout?.parameters?.cases?.some(row=>row.boundary==='exit'&&Array.isArray(row.phases));  assert.equal(Boolean(split),Boolean(declaredSplit),'actual cancellation source and finite exit layout differ');  roles.cancellation=['boundary-input-declarations-before-original-TU',...(split?['terminal-exit-case-uses-independent-original-process']:[]),   'reuse-original-lambda-at-current-command-input','boundary-write-uses-current-ordinary-settings-baseline',   'observe-original-admitted-settings-receipt','explicit-cancellation-oracles-before-unchanged-normal-only-expectations']; } let text=compiledBytes.toString('utf8');assert(Buffer.from(text).equals(compiledBytes),'joint source must retain exact UTF8 bytes'); for(const [phase,expectedRoles]of Object.entries(roles).reverse()){  const proof=joint[phase];assert.equal(proof?.exactInverse,true,'joint fixture inverse required: '+phase);  assert(Array.isArray(proof.edits?.fixture),'joint fixture edits required: '+phase);  assert.deepEqual(proof.edits.fixture.map(row=>row.role),expectedRoles,'unknown or missing joint fixture edit role');  for(const edit of [...proof.edits.fixture].reverse()){   const before=Buffer.from(edit.beforeBase64,'base64'),after=Buffer.from(edit.afterBase64,'base64');   assert(before.length&&after.length&&before.toString('base64')===edit.beforeBase64&&after.toString('base64')===edit.afterBase64,    'exact nonempty joint fixture edit bytes required');   const from=after.toString('utf8'),to=before.toString('utf8');   assert.equal(text.split(from).length,2,'one actual joint edit span required: '+edit.role);   const inverse=text.replace(from,()=>to);   assert.equal(inverse.split(to).length,2,'one original joint edit span required: '+edit.role);   assert.equal(inverse.replace(to,()=>from),text,'whole joint fixture inverse differs');text=inverse;  } } const rows=io.insertionProof?.files?.filter(row=>row.file==='fixture');assert.equal(rows?.length,1,'one original fixture insertion proof required'); const row=rows[0];let inverse=Buffer.from(text); const digest=bytes=>createHash('sha256').update(bytes).digest('hex'); assert.equal(inverse.length,row.output.bytes);assert.equal(digest(inverse),row.output.sha256,'joint inverse must reach exact adapter output'); const expected={'include:prelude':'#include "msi_policy_return_io_prelude.inc"\n','include:support':'\n#include "msi_policy_return_io_support.inc"'}; assert.deepEqual(row.edits.map(edit=>edit.role),Object.keys(expected)); for(const edit of [...row.edits].reverse()){  const inserted=Buffer.from(expected[edit.role]);assert.equal(inserted.toString('base64'),edit.insertedBase64);assert.equal(inserted.length,edit.insertedBytes);  assert(inverse.subarray(edit.outputByte,edit.outputByte+inserted.length).equals(inserted),'exact original fixture include bytes required');  inverse=Buffer.concat([inverse.subarray(0,edit.outputByte),inverse.subarray(edit.outputByte+inserted.length)]); } assert.equal(inverse.length,row.input.bytes);assert.equal(digest(inverse),row.input.sha256); assert.equal(row.input.sha256,n.generationPreflight?.fixtureSha256,'joint inverse must reach actual preflight fixture'); return inverse;}export function terminalPreparedRuntimeDirectories(n,layout){ if(!n.runtimeLayout){assert(!n.native?.runtimeLayout,'compiled runtime layout metadata cannot be omitted');return null;} assert(requireTerminalManagedContract(layout.managedInputContract).signalEvidence,'runtime layout is explicit26 only');
 const declared=terminal26PreparedRuntimeLayout(n);assert.deepEqual(n.native.runtimeLayout,declared,'native runtime layout differs from preparation');
 assert(lifecycleSame(n.runtimeRoot,path.join(path.dirname(n.native.exe.path),'..','qualification-runtime'))&&lifecycleInside(layout.authorizedBuildRoot,n.runtimeRoot),'runtime layout escaped native preparation owner');
 lifecyclePlainPath(n.runtimeRoot);
 const file=path.join(path.dirname(n.native.exe.path),'fixture.cpp'),compileFile=path.join(path.dirname(n.native.exe.path),'COMPILE-ONLY.json');
 for(const value of [n.native.exe.path,n.native.sourceManifest.path,file,compileFile])lifecyclePlainPath(value);
 assert.deepEqual(JSON.parse(fs.readFileSync(compileFile,'utf8')),n.native,'actual native compile receipt differs from runtime preparation');
 const manifest=JSON.parse(fs.readFileSync(n.native.sourceManifest.path,'utf8'));
 let fixtureBoundarySource=fs.readFileSync(file,'utf8'); if(['msi-real-policy-return-r6-20261115','msi-real-policy-return-r7-20261115'].includes(n.native.productPolicySuccessor?.id)){  // This explicit generator adds only two I/O include lines after preflight.  // Prove their exact compiled bytes and inverse; retain both source epochs.  assert.equal(manifest.productPolicySuccessor.id,n.native.productPolicySuccessor.id);  assert.equal(manifest.productPolicySuccessor.delivery.sha256,n.native.productPolicySuccessor.id==='msi-real-policy-return-r7-20261115'?'9ace80b5875f069720346043c01aedd6f0f8761ef298de3c1d89317ab7f94827':'7705220e840f2e75fa3ce8a50a5cc6308b23561ada229fe2268b5d7004112e12');  assert.equal(manifest.policyIoProvenance.adapter.sha256,'7054141b74b2421f8416260868a4abb3e84ca5b4190017b4921cb60a757e774e');  const proof=manifest.policyIoProvenance.insertionProof.files.filter(row=>row.file==='fixture');assert.equal(proof.length,1);  const row=proof[0],expected={'include:prelude':'#include "msi_policy_return_io_prelude.inc"\n','include:support':'\n#include "msi_policy_return_io_support.inc"'};  assert.deepEqual(row.edits.map(edit=>edit.role),Object.keys(expected));  let inverse=fs.readFileSync(file);assert.equal(inverse.length,row.output.bytes);assert.equal(lifecycleFileSha(file),row.output.sha256);  for(const edit of [...row.edits].reverse()){   const bytes=Buffer.from(expected[edit.role]);assert.equal(bytes.toString('base64'),edit.insertedBase64);assert.equal(bytes.length,edit.insertedBytes);   assert(inverse.subarray(edit.outputByte,edit.outputByte+bytes.length).equals(bytes),'exact final I/O include bytes required');   inverse=Buffer.concat([inverse.subarray(0,edit.outputByte),inverse.subarray(edit.outputByte+bytes.length)]);  }  assert.equal(inverse.length,row.input.bytes);assert.equal(createHash('sha256').update(inverse).digest('hex'),row.input.sha256);  assert.equal(row.input.sha256,n.generationPreflight?.fixtureSha256,'original r6 preflight identity required');  fixtureBoundarySource=inverse.toString('utf8'); }else if(n.native.productPolicySuccessor?.id==='msi-real-policy-return-r8-r5-20261117'){  fixtureBoundarySource=terminal26JointFixturePreflightBytes(n,manifest,fs.readFileSync(file)).toString('utf8'); }else assert(n.generationPreflight?.fixtureSha256===lifecycleFileSha(file),'actual fixture differs from precompile generation observation'); assert.equal(lifecycleFileSha(n.native.sourceManifest.path),n.native.sourceManifest.sha256,'runtime source manifest drifted');
 const rows=manifest.compiledGenerated.filter(row=>lifecycleSame(row.path,file));assert.equal(rows.length,1,'one actual runtime fixture TU required');
 assert.equal(lifecycleFileSha(file),rows[0].sha256,'compiled runtime fixture bytes changed');
 const generated=isolateCurrentFixtureSettingsMutex('#include "main.inert.cpp"\n',{runtimeRoot:n.runtimeRoot,moduleMaintenance:true,managedInputContract:layout.managedInputContract,...terminal26PreparedRuntimeGenerationOptions(n)}).edits[0].after; assert(fixtureBoundarySource.split(generated).length===2,'compiled exact runtime boundary missing or duplicated'); // The original generator pins the complete TU before invoking the compiler;
 // a guard substring, a comment or caller-provided layout is not that proof.
 for(const value of [declared.pipe,...declared.lifecycle.map(row=>row.path),...(declared.parameters?[declared.parameters.positivePair.path,...declared.parameters.cases.flatMap(row=>[row.path,...(row.phases??[]).map(phase=>phase.path)])]:[])])lifecyclePlainPath(value); return declared;
}


export function terminal26CompiledSwitchSource(n,source){
 assert.equal(n?.managedInputContract,'current105-terminal26-sdk-lifetime-compact-v1','explicit26 switch source required');
 assert.equal(lifecycleFileSha(n.native.sourceManifest.path),n.native.sourceManifest.sha256,'actual switch source manifest changed');
 assert.deepEqual(source,JSON.parse(fs.readFileSync(n.native.sourceManifest.path,'utf8')),'actual switch source manifest required');
 const file=path.join(path.dirname(n.native.exe.path),'fixture.cpp');
 const rows=source.compiledGenerated.filter(row=>path.resolve(row.path).toLowerCase()===path.resolve(file).toLowerCase());
 assert.equal(rows.length,1,'exact compiled switch translation unit required');
 assert.equal(rows[0].sha256,n.generationPreflight?.fixtureSha256,'actual generation switch source pin required');
 const bytes=fs.readFileSync(file),digest=createHash('sha256').update(bytes).digest('hex');
 assert.equal(digest,rows[0].sha256,'actual compiled switch source changed');
 return {path:file,sha256:digest,bytes};
}

export function terminal26CompiledTupleSource(n,source){
 assert.equal(n?.managedInputContract,'current105-terminal26-sdk-lifetime-compact-v1','explicit26 tuple source required');
 const spec=JSON.parse(fs.readFileSync(new URL('./r3-terminal26-tuple-expectations.json',import.meta.url),'utf8'));
 assert.equal(lifecycleFileSha(n.native.sourceManifest.path),n.native.sourceManifest.sha256,'actual tuple source manifest changed');
 assert.deepEqual(source,JSON.parse(fs.readFileSync(n.native.sourceManifest.path,'utf8')),'actual tuple source manifest required');
 const root=path.dirname(n.native.exe.path),inertRelative=path.join('source','native','main.inert.cpp');
 const inertRows=source.compiledGenerated.filter(row=>lifecycleSame(row.path,path.join(root,inertRelative)));
 assert.equal(inertRows.length,1,'one exact compiled tuple inert input required');
 const inertDigest=inertRows[0].sha256,bindings=[
  ['tuple_recovery_cases.inc',spec.sourceBindings.compiledTupleSha256],
  ['fixture.cpp',n.generationPreflight?.fixtureSha256],
  ['pipe_cases.inc',spec.sourceBindings.pipeSha256]
 ];
 // A reviewed compiled successor admits its exact source, not old runtime success.
 if(inertDigest!==spec.sourceBindings.inertSha256){
  const matches=(spec.sourceBindings.inertSuccessors??[]).filter(row=>row.inertSha256===inertDigest);
  assert.equal(matches.length,1,'reviewed tuple inert successor required');
  const profile=matches[0],header=source.productHeaderSuccessor;
  assert(n.role==='positive'&&n.mutation===null,'positive tuple source role required');
  assert(source.productSourceSuccessor?.id===profile.productSourceSuccessorId&&source.productSourceSuccessor.inverseVerified===true&&
   n.generationPreflight?.productSourceSuccessor?.id===profile.productSourceSuccessorId,'tuple product source successor required');
  assert(header?.id===profile.productHeaderSuccessorId&&header.relative===profile.headerRelative&&header.inverseVerified===true&&header.sourceCaptureModified===false,'tuple reviewed product header required');
  const {generated,...headerPreflight}=header;
  assert.deepEqual(headerPreflight,n.generationPreflight?.productHeaderSuccessor,'actual tuple header generation provenance required');
  assert(header.after?.sha256===profile.compiledHeaderSha256&&header.after.bytes===profile.compiledHeaderBytes&&
   generated?.sha256===profile.compiledHeaderSha256&&generated.bytes===profile.compiledHeaderBytes&&
   lifecycleSame(generated.path,path.join(root,profile.compiledHeaderRelative)),'exact tuple compiled header binding required');
  bindings.push([profile.compiledHeaderRelative,profile.compiledHeaderSha256,profile.compiledHeaderBytes]);
 }
 bindings.push([inertRelative,inertDigest]);
 let tupleSource;
 for(const [relative,digest,expectedBytes]of bindings){
  assert(typeof digest==='string'&&/^[a-f0-9]{64}$/.test(digest),'actual generation source pin required');
  const file=path.join(root,relative);lifecyclePlainPath(file);
  const rows=source.compiledGenerated.filter(row=>lifecycleSame(row.path,file));
  assert.equal(rows.length,1,'one exact compiled tuple witness input required');
  assert.equal(rows[0].sha256,digest,'compiled tuple witness source pin mismatch');
  const bytes=fs.readFileSync(file);assert.equal(createHash('sha256').update(bytes).digest('hex'),digest,'compiled tuple witness bytes changed');
  if(expectedBytes!==undefined)assert.equal(bytes.length,expectedBytes,'compiled tuple header size changed');
  if(relative==='tuple_recovery_cases.inc'){assert.equal(bytes.length,spec.sourceBindings.compiledTupleBytes);tupleSource={path:file,sha256:digest,bytes};}
 }
 return tupleSource;
}
export function terminal26PostOnlyConsumerBindings(legacyFiles){
 const expected={
  'r3-current-run.mjs':'cc87e70a2f6fa895615ff7a3f130403039cb036b3227863bef7279334b9bf03a',
  'r3-terminal22-qualified-observe.mjs':'92f6b2c38f9ea1a3f40d122948307b9dfe3c8c9f7930679530c02f30d0cac80a',
  'r3-terminal22-observation-terminal.test.mjs':'98631f313dc26b168a2a1f0e84e999e2708b3cf382f54627d2c2f04bc10d593f'
 };
 const legacy=Object.entries(expected).map(([name,sha256])=>{
  const file=legacyFiles?.[name];assert(typeof file==='string'&&path.isAbsolute(file),'sealed original consumer required');
  lifecyclePlainPath(file);assert.equal(lifecycleFileSha(file),sha256,'sealed original consumer identity mismatch');return {name,path:file,sha256};
 });
 const current=Object.keys(expected).concat('r3-terminal26-tuple-expectations.json').map(name=>{
  const file=fileURLToPath(new URL('./'+name,import.meta.url));return {name,path:file,sha256:lifecycleFileSha(file)};
 });
 return {kind:'EXPLICIT26_TUPLE_POST_ONLY_CONSUMER_BINDINGS_V1',legacy,current,nativeExecuted:false,managedExecuted:false,oldOuterFailurePreserved:true,runtimeAuthorityGranted:false};
}

async function observeTerminal22Lifecycle(inputFile,out){
 const groupStarted=Date.now();
 const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
 const hash=file=>createHash('sha256').update(fs.readFileSync(file)).digest('hex');
 const pin=file=>({path:path.resolve(file),sha256:hash(file),bytes:fs.statSync(file).size});
 const inputs=new Map();
 function add(row,scope){
  assert(row&&path.isAbsolute(row.path)&&/^[a-f0-9]{64}$/.test(row.sha256),'exact actual lifecycle input pin required');lifecyclePlainPath(row.path);
  const file=path.resolve(row.path);assert(fs.lstatSync(file).isFile(),'regular lifecycle input required');if(scope)assert(lifecycleInside(scope,file),'lifecycle input escaped original authority');
  const key=file.toLowerCase(),previous=inputs.get(key);assert(!previous||previous.sha256===row.sha256,'conflicting lifecycle input pin');assert.equal(hash(file),row.sha256,'actual lifecycle input changed: '+file);inputs.set(key,{path:file,sha256:row.sha256});return file;
 }
 lifecyclePlainPath(inputFile);const inputPin=pin(inputFile);add(inputPin);const declaration=read(inputFile),declaredContract=terminalManagedContractFromDeclaration(declaration.kind),selected=terminal22LifecyclePreparationPins(declaration),plan=currentLifecycleModePlan(),prepared=new Map();
 let common,shared,sourceCheck;
 for(const selectedPin of selected){
  const nativeFile=add(selectedPin),n=read(nativeFile);assert(n.schemaVersion===1&&n.status===terminalManagedStageStatus('native',declaredContract.id)&&n.nativeExecuted===false&&n.publicRuntimeAccepted===false,'actual compiled lifecycle preparation required');
  const rolePlan=plan.find(row=>row.role===selectedPin.role);assert(n.role===rolePlan.role&&n.mutation===rolePlan.mutation,'exact original lifecycle native polarity required');
  const preparationFile=add(n.preparation),preparation=read(preparationFile);assert(preparation.status===terminalManagedStageStatus('managed',declaredContract.id)&&preparation.publicRuntimeAccepted===false,'actual managed22 preparation required');
  const layoutFile=add(preparation.layout),layout=read(layoutFile);assert(layout.schemaVersion===1&&layout.sourceAuthority?.mode==='EXPLICIT_CURRENT_CAPTURE_COMPILE_ONLY'&&layout.managedInputContract===declaredContract.id,'explicit original terminal22 compile layout required');
     if(n.managedInputContract!==undefined)assert.equal(n.managedInputContract,declaredContract.id);if(preparation.managedInputContract!==undefined)assert.equal(preparation.managedInputContract,declaredContract.id);
   if(declaredContract.steamDeckPadDecoder)assert(n.managedInputContract===declaredContract.id&&preparation.managedInputContract===declaredContract.id,'explicit decoder lifecycle preparation required');
validateTerminal22LifecycleLayout(layout);
  assert(lifecycleInside(layout.authorizedBuildRoot,nativeFile)&&lifecycleInside(layout.authorizedBuildRoot,preparationFile)&&lifecycleInside(layout.authorizedBuildRoot,layoutFile),'lifecycle preparations escaped authority');
  assert(lifecycleSame(n.sourceRoot,layout.sourceRoot)&&lifecycleSame(preparation.sourceRoot,layout.sourceRoot),'one captured lifecycle source required');
  const managedFile=add({path:preparation.managed.manifestPath,sha256:preparation.managed.manifestSha256},layout.authorizedBuildRoot),managed=read(managedFile);
  assert(managed.status==='BUILT'&&managed.managedInputContract===layout.managedInputContract&&managed.managedInputCount===declaredContract.count&&managed.terminalCleanupCompiled===true&&managed.realSdkLoaded===false&&managed.hardwareIo===false&&managed.semanticMutation==null,'actual unmutated SDK-free managed22 build required');
  const decoder=verifyManagedDecoderCompileEvidence(managed,declaredContract.id,layout.authorizedBuildRoot);if(decoder){add(decoder.generated,layout.authorizedBuildRoot);add(decoder.compiled,layout.authorizedBuildRoot);}
   add(terminal22LifecycleManagedDllPin(managed,n.native?.managedProof?.dll,layout.authorizedBuildRoot),layout.authorizedBuildRoot);
  const identity=JSON.stringify([layoutFile,preparation.layout.sha256,preparationFile,n.preparation.sha256,layout.sourceRoot,layout.primaryRoot,layout.workspaceRoot,layout.authorizedBuildRoot,layout.productMainSha256,layout.manifestSha256,managedFile,preparation.managed.manifestSha256]);
  if(!common){
   common={identity,layout,preparation,managed,managedContract:declaredContract};const moduleRoot=path.join(layout.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
   // Trust local original implementations before reading executable captured modules.
   sourceCheck=()=>verifyTerminal22LifecycleCapture(layout);sourceCheck();
   const sharedFile=path.join(moduleRoot,'r3-current-run.mjs');add({path:sharedFile,sha256:hash(fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url)))});
   shared={currentLifecycleModePlan,currentLifecycleModeLabel,currentLifecycleRemainingBudget,verifyCurrentLifecycleMode};
   const localContract=fileURLToPath(new URL('./r3-managed-input-contract.mjs',import.meta.url)),contractFile=path.join(moduleRoot,'r3-managed-input-contract.mjs');assert.equal(layout.managedContractCodeSha256,hash(localContract),'lifecycle managed contract differs from trusted code');add({path:contractFile,sha256:hash(localContract)});validateTrustedLifecycleManagedRows(layout.expectedManagedSources,layout.managedInputContract);validateTrustedLifecycleManagedRows(managed.inputBindings.sources,layout.managedInputContract);
   assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources,'actual managed22 rows differ from compile layout');
   add({path:path.join(layout.sourceRoot,'native/main.cpp'),sha256:layout.productMainSha256});add({path:path.join(layout.sourceRoot,'.ymcc-source-snapshot/manifest.json'),sha256:layout.manifestSha256});add({path:path.join(layout.sourceRoot,'.ymcc-source-snapshot/node-tools-binding.json'),sha256:layout.nodeBindingSha256});
   const verifierFile=path.join(layout.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs');add({path:verifierFile,sha256:layout.verifierCodeSha256});add(pin(fileURLToPath(new URL('../source-snapshot.mjs',import.meta.url))));add(pin(fileURLToPath(new URL('../runner.mjs',import.meta.url))));add(pin(fileURLToPath(new URL('./r3-current-run.mjs',import.meta.url))));add(pin(localContract));
   for(const row of managed.inputBindings.sources){assert(lifecycleSame(row.path,path.join(layout.sourceRoot,row.relative))&&lifecycleInside(layout.authorizedBuildRoot,row.snapshot),'managed22 source/snapshot escaped original lifecycle roots');add({path:row.path,sha256:row.sha256});add({path:row.snapshot,sha256:row.sha256},layout.authorizedBuildRoot);}
   for(const row of [...managed.inputBindings.generatedInputs,...managed.runtimeArtifacts])add(row,layout.authorizedBuildRoot);for(const row of managed.evaluatedCompilerInputs)add(row);
  }else assert.equal(identity,common.identity,'all lifecycle preparations must share exact source layout and managed build');
  assert(n.batchAfter?.status==='AUTHORITY_STABLE'&&n.batchAfter.source?.snapshotIntegrityVerified===true&&n.batchAfter.source.creationStatus==='PASS'&&n.batchAfter.fullSourceVerificationCalls===2&&lifecycleSame(n.batchAfter.source.snapshotRoot,layout.sourceRoot)&&n.batchAfter.source.manifestSha256===layout.manifestSha256,'actual stable compile authority required');
  for(const row of n.batchAfter.actualTools)add(row);if(n.runner)add(n.runner);if(n.toolchainManifest)add(n.toolchainManifest);
  validateTerminalLifecycleNativeReceipt(n,declaredContract.id,rolePlan.mutation);
  const compileFile=path.join(path.dirname(nativeFile),'native','COMPILE-ONLY.json');add(pin(compileFile),layout.authorizedBuildRoot);assert.deepEqual(read(compileFile),n.native,'actual native compile receipt differs from preparation');
  add(n.native.exe,layout.authorizedBuildRoot);add(n.native.buildScript,layout.authorizedBuildRoot);const sourceFile=add(n.native.sourceManifest,layout.authorizedBuildRoot),source=read(sourceFile);
  assert(lifecycleSame(n.native.managedProof?.dll?.path,managed.fixtureDll)&&n.native.managedProof.dll.sha256===hash(managed.fixtureDll)&&lifecycleSame(n.native.managedProof.manifest,managedFile)&&n.native.managedProof.manifestSha256===preparation.managed.manifestSha256,'native was not compiled with the exact lifecycle managed22');
  for(const row of source.records){const actualTool=n.batchAfter.actualTools.some(tool=>lifecycleSame(tool.path,row.original)&&tool.sha256===row.sha256);assert(lifecycleInside(layout.sourceRoot,row.original)||row.inputRole==='actual-current-compile-tool'&&actualTool,'native lifecycle source escaped capture or actual tool authority');add({path:row.original,sha256:row.sha256});add({path:row.snapshot,sha256:row.sha256},layout.authorizedBuildRoot);}for(const row of source.compiledGenerated)add(row,layout.authorizedBuildRoot);
  if(n.compilerLedger){const ledgerFile=add(n.compilerLedger,layout.authorizedBuildRoot),ledger=read(ledgerFile);for(const row of ledger.inputs)add(row);}
  assert(lifecycleSame(n.runtimeRoot,path.join(path.dirname(nativeFile),'qualification-runtime'))&&lifecycleInside(layout.authorizedBuildRoot,n.runtimeRoot),'original compiled runtime owner required');lifecyclePlainPath(n.runtimeRoot);
  const runtimeLayout=terminalPreparedRuntimeDirectories(n,layout);
  if(runtimeLayout){for(const test of plan.filter(row=>row.role===n.role)){const row=runtimeLayout.lifecycle.find(row=>row.label===currentLifecycleModeLabel(test));assert(row&&!fs.existsSync(row.path),'fresh exact lifecycle mode directory required');}}else assert(!fs.existsSync(n.runtimeRoot),'fresh original lifecycle runtime required');
  prepared.set(n.role,{n,nativeFile,managed,runtimeLayout,source});
 }
 const {layout}=common;assert(lifecycleInside(layout.authorizedBuildRoot,inputFile),'lifecycle declaration escaped original authority');lifecyclePlainPath(out);const root=path.resolve(out);assert(lifecycleInside(layout.authorizedBuildRoot,root)&&!fs.existsSync(root),'fresh observation root under original lifecycle authority required');
 add(pin(fileURLToPath(import.meta.url)));fs.mkdirSync(root,{recursive:true});
 const result={schemaVersion:1,status:'VERIFYING_LIFECYCLE_INPUTS',kind:'TERMINAL'+declaredContract.count+'_ORIGINAL_LIFECYCLE_OBSERVATION',managedInputContract:declaredContract.id,inputDeclaration:inputPin,sourceRoot:layout.sourceRoot,originalModeBudgetMs:45000,originalGroupBudgetMs:90000,modeResults:[],preflight:{managedDll:pin(common.managed.fixtureDll),managedSources:declaredContract.count,nativePreparations:selected},qualificationAccepted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,sourceApprovalGranted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,compilationFailureCountedAsNegative:false,errors:[]};
 const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');let started=groupStarted;
 try{
  result.sourceBefore=sourceCheck();for(const row of inputs.values()){lifecyclePlainPath(row.path);assert.equal(hash(row.path),row.sha256,'lifecycle pre-run input changed');}fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2)+'\n');
  const runnerFile=path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs');add({path:runnerFile,sha256:hash(fileURLToPath(new URL('../runner.mjs',import.meta.url)))});const runProcess=trustedLifecycleRunProcess;
  for(const {n,runtimeLayout}of prepared.values()){if(runtimeLayout)fs.mkdirSync(n.runtimeRoot,{recursive:true});else fs.mkdirSync(n.runtimeRoot);}result.startedAt=new Date(started).toISOString();result.status='RUNNING_ORIGINAL16_LIFECYCLE_MODES';save();
  for(const test of shared.currentLifecycleModePlan()){
   const budget=shared.currentLifecycleRemainingBudget(started);
   const {n,runtimeLayout,source}=prepared.get(test.role),label=shared.currentLifecycleModeLabel(test),dir=runtimeLayout?runtimeLayout.lifecycle.find(row=>row.label===label).path:path.join(n.runtimeRoot,label);assert(!fs.existsSync(dir),'fresh lifecycle mode evidence required');fs.mkdirSync(dir);
   const args=[dir,test.mode],processResult=await runProcess(n.native.exe.path,args,{cwd:dir,env:{...process.env,YEMAN_POWER_CONTROL_DIR:dir},timeoutMs:budget.timeoutMs});
   const {stdout,stderr,...terminal}=processResult;fs.writeFileSync(path.join(dir,'stdout.log'),stdout);fs.writeFileSync(path.join(dir,'stderr.log'),stderr);
   const row={label,role:test.role,mode:test.mode,mutation:test.mutation,expectedNegative:!!test.mutation||test.mode.startsWith('mutation'),expectedNativeExit:test.expectedExit,nativeProcess:{exe:n.native.exe.path,args,...terminal,originalModeBudgetMs:45000,effectiveTimeoutMs:budget.timeoutMs},execution:terminal22ExecutionFromProcess(terminal),semanticAccepted:false};result.modeResults.push(row);save();
   assert(terminal.timedOut===false&&terminal.launchError===null&&terminal.signal===null&&terminal.outputTruncated===false&&Number.isFinite(terminal.durationMs)&&terminal.durationMs>=0&&terminal.durationMs<=budget.timeoutMs&&terminal.exitCode===test.expectedExit,'original natural lifecycle45 exit required: '+label);
   const rawFile=path.join(dir,'fixture-result.json');assert(fs.existsSync(rawFile),'complete lifecycle native raw required');row.raw=pin(rawFile);const raw=read(rawFile),entryIndex=test.mode==='entry'?0:test.mode==='mutation1'?1:2;
   const entryText=!test.mutation&&(test.mode==='entry'||test.mode.startsWith('mutation'))?fs.readFileSync(path.join(dir,'msi-start-order-entry-selftest-m'+entryIndex+'.txt'),'utf8'):undefined;
   const tupleSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='tuple-protection'&&!test.mutation?terminal26CompiledTupleSource(n,source):undefined;
   const switchSource=declaredContract.signalEvidence&&test.role==='positive'&&test.mode==='switch-recovery'&&!test.mutation?terminal26CompiledSwitchSource(n,source):undefined;
   const semantic=shared.verifyCurrentLifecycleMode(test,raw,{entryText,managedInputContract:declaredContract.id,tupleSource,switchSource,requireMsiReturnObservation:test.mode==='recovery-protection'&&Boolean(source.policyFixture?.returnObservation)});row.msiReturnObservation=semantic.msiReturnObservation;row.semanticAccepted=true;row.exitCode=terminal.exitCode;row.checks=raw.checks;row.originalOrderChecks=['entry','mutation1','mutation2'].includes(test.mode)?56:undefined;row.caseNames=raw.cases.map(item=>item.name);row.rawSummary={status:raw.status,checks:raw.checks,failures:raw.failures};save();  }
  assert(Date.now()-started<=90000,'original lifecycle group90 budget exceeded');for(const row of inputs.values()){lifecyclePlainPath(row.path);assert.equal(hash(row.path),row.sha256,'lifecycle post-run input changed');}result.sourceAfter=sourceCheck();assert(Date.now()-started<=90000,'original lifecycle group90 budget exceeded after input verification');result.postInputsStable=true;result.originalPlanComplete=result.modeResults.length===plan.length;assert(result.originalPlanComplete,'all original16 modes required');
  result.qualificationSteps=Object.fromEntries(result.modeResults.filter(row=>row.mutation).map(row=>[row.role,{stepLabel:row.label,raw:row.raw}]));result.status='ORIGINAL16_VERIFIED_PENDING_FINAL_DEADLINE';
 }catch(error){result.status='INCOMPLETE';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
 // One final measured wall sample controls status, exit and deadline facts together.
 const final=finalizeTerminal22LifecycleExecution({started,complete:result.originalPlanComplete===true&&result.postInputsStable===true,failed:result.errors.length>0,modeResults:result.modeResults});
 result.status=final.status;result.execution=final.execution;result.finishedAt=final.finishedAt;
 if(final.deadlineExceeded)result.errors.push({message:'original lifecycle final group90 budget exceeded'});if(final.execution.exitCode!==0)process.exitCode=1;
 save();console.log(JSON.stringify({status:result.status,modeResults:result.modeResults.length,resultFile,errors:result.errors.map(row=>row.message)}));return result;
}

export async function observeTerminal22(args=process.argv.slice(2)){
const options=parseTerminal22ObservationArguments(args);if(options['--lifecycle-preparations'])return observeTerminal22Lifecycle(options['--lifecycle-preparations'],options['--out']);
assert(options['--native-preparation']&&options['--out'],'native preparation and fresh owned result directory required');
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,'')),hash=p=>createHash('sha256').update(fs.readFileSync(p)).digest('hex'),pin=p=>({path:path.resolve(p),sha256:hash(p),bytes:fs.statSync(p).size});
const nativeFile=path.resolve(options['--native-preparation']),n=read(nativeFile);
assert([terminalManagedStageStatus('native'),terminalManagedStageStatus('native','current105-terminal23-sd-decoder-v1'),terminalManagedStageStatus('native','current105-terminal26-sdk-lifetime-compact-v1')].includes(n.status),'explicit compiled native preparation required');assert.equal(n.nativeExecuted,false);assert.equal(n.publicRuntimeAccepted,false);
const preparation=read(n.preparation.path);assert.equal(hash(n.preparation.path),n.preparation.sha256);
const layout=read(preparation.layout.path);assert.equal(hash(preparation.layout.path),preparation.layout.sha256);
const observedContract=requireTerminalManagedContract(layout.managedInputContract);const nativeBudget=createNativeObservationBudget(observedContract.id,options['--native-budget-ms']===undefined?undefined:Number(options['--native-budget-ms']));assert.equal(n.status,terminalManagedStageStatus('native',observedContract.id));assert.equal(preparation.status,terminalManagedStageStatus('managed',observedContract.id));
if(observedContract.steamDeckPadDecoder){assert.equal(n.managedInputContract,observedContract.id);assert.equal(preparation.managedInputContract,observedContract.id);verifyTerminal23PipeSource(n,preparation,layout);}
const root=path.resolve(options['--out']);assert(!fs.existsSync(root),'fresh observer evidence required');
const relative=path.relative(layout.authorizedBuildRoot,root);assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'observer evidence outside original authority root');
const runtimeLayout=terminalPreparedRuntimeDirectories(n,layout),runtimeDirectory=runtimeLayout?runtimeLayout.pipe:n.runtimeRoot;assert(!fs.existsSync(runtimeDirectory),'the exact compiled runtime directory must be unused');
const moduleRoot=terminalRuntimeOracleRoot(layout);
const oracle=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-oracle.mjs')).href);
assert(['positive','cancellation','psOwnership','psBinding'].includes(n.role),'full-pipe observation role required');
const oracleOptions={managedInputContract:observedContract.id},expected=oracle.currentExpected(n.role,oracleOptions),{verifyCurrent105OriginalGroups}=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-original-groups.mjs')).href);if(observedContract.signalEvidence)assert.equal(expected.managedInputContract,observedContract.id,'TERMINAL26_EXACT_RUNTIME_ORACLE_NOT_PAIRED');
const {runProcess}=observedContract.steamDeckPadDecoder?{runProcess:trustedLifecycleRunProcess}:await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/runner.mjs')).href);
const {verifyCapturedSourceSnapshot}=await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')).href);
const manifest=read(n.native.sourceManifest.path),managed=read(preparation.managed.manifestPath);
// TERMINAL26_CONSUMER_SUCCESSOR_BEGIN binding
const postCompileConsumers=options['--consumer-predecessors']===undefined?null:terminal26ConsumerToolSuccessor(n,path.resolve(options['--consumer-predecessors']));
assert(!postCompileConsumers||observedContract.signalEvidence===true,'explicit26 consumer source successor required');
// TERMINAL26_CONSUMER_SUCCESSOR_END binding
if(observedContract.steamDeckPadDecoder){
 assert.equal(layout.managedContractCodeSha256,hash(fileURLToPath(new URL('./r3-managed-input-contract.mjs',import.meta.url))));
 validateTrustedLifecycleManagedRows(layout.expectedManagedSources,observedContract.id);validateTrustedLifecycleManagedRows(managed.inputBindings.sources,observedContract.id);
 assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources);
 for(const row of managed.inputBindings.sources){assert(lifecycleSame(row.path,path.join(layout.sourceRoot,row.relative))&&lifecycleInside(layout.authorizedBuildRoot,row.snapshot));lifecyclePlainPath(row.path);lifecyclePlainPath(row.snapshot);}
 verifyManagedDecoderCompileEvidence(managed,observedContract.id,layout.authorizedBuildRoot);terminal22LifecycleManagedDllPin(managed,n.native?.managedProof?.dll,layout.authorizedBuildRoot);
}
const inputs=new Map();function add(p,digest){p=path.resolve(p);lifecyclePlainPath(p);assert(fs.lstatSync(p).isFile()&&!fs.lstatSync(p).isSymbolicLink(),'regular bounded input required');const k=p.toLowerCase(),old=inputs.get(k);assert(!old||old.sha256===digest,'input digest conflict');assert.equal(hash(p),digest,'changed input '+p);inputs.set(k,{path:p,sha256:digest});}
function addPin(row){add(row.path,row.sha256);}
fs.mkdirSync(root,{recursive:true});
const result={schemaVersion:1,status:'VERIFYING_INPUTS',role:n.role,nativePreparation:pin(nativeFile),observer:pin(fileURLToPath(import.meta.url)),strictNativeBudgetMs:nativeBudget.ms,originalNativeBudgetMs:ORIGINAL_NATIVE_BUDGET_MS,nativeTerminalAdmission:'NOT_OBSERVED',qualificationAccepted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,physicalAcceptance:false,errors:[]};
// TERMINAL26_CONSUMER_SUCCESSOR_BEGIN result
if(postCompileConsumers)result.postCompileConsumerSuccessor={kind:postCompileConsumers.kind,legacyConsumers:postCompileConsumers.legacyConsumers,currentConsumers:postCompileConsumers.currentConsumers,sourceProofs:postCompileConsumers.sourceProofs,compilerReceiptsUnmodified:true};
// TERMINAL26_CONSUMER_SUCCESSOR_END result

const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');
const controlFiles=[fileURLToPath(import.meta.url),fileURLToPath(new URL('./r3-terminal26-consumer-successor.mjs',import.meta.url)),...['r3-terminal22-observation-terminal.mjs','r3-terminal22-stage-receipts.mjs'].map(name=>fileURLToPath(new URL('./'+name,import.meta.url))),...terminal26LocalNativeToolFiles(layout)];result.controlInputs=[...new Map(controlFiles.map(file=>[path.resolve(file).toLowerCase(),path.resolve(file)])).values()].map(pin);
try{
 for(const row of result.controlInputs)addPin(row);
 const compileFile=path.join(path.dirname(nativeFile),'native','COMPILE-ONLY.json');addPin(pin(compileFile));assert.deepEqual(read(compileFile),n.native,'actual native compile receipt differs from preparation');
 addPin(layout.dotnet);addPin(n.native.exe);addPin(n.native.sourceManifest);addPin(n.native.buildScript);addPin(n.preparation);addPin(preparation.layout);addPin({path:preparation.managed.manifestPath,sha256:preparation.managed.manifestSha256});
 for(const row of manifest.records){add(row.original,row.sha256);add(row.snapshot,row.sha256);}
 // TERMINAL26_CONSUMER_SUCCESSOR_BEGIN merge
const actualCompileTools=postCompileConsumers?postCompileConsumers.currentTools:n.batchAfter.actualTools;
for(const row of [...manifest.compiledGenerated,...actualCompileTools,...managed.evaluatedCompilerInputs,...managed.runtimeArtifacts])addPin(row);
if(postCompileConsumers)for(const row of postCompileConsumers.sourceInputs)addPin(row);
// TERMINAL26_CONSUMER_SUCCESSOR_END merge
 for(const row of managed.inputBindings.sources){add(row.path,row.sha256);add(row.snapshot,row.sha256);}
 for(const row of managed.inputBindings.generatedInputs)addPin(row);
 const sourceCheck=()=>observedContract.steamDeckPadDecoder?verifyTerminal23PipeSource(n,preparation,layout):verifyCapturedSourceSnapshot({manifestFile:path.join(layout.sourceRoot,'.ymcc-source-snapshot/manifest.json'),repoRoot:layout.primaryRoot,expectedManifestSha256:layout.manifestSha256});
 result.sourceBefore=sourceCheck();assert.equal(result.sourceBefore.status,'FROZEN_SOURCE_VERIFIED');assert.equal(result.sourceBefore.creationStatus,'PASS');
 result.inputCount=inputs.size;result.preflight={nativeExe:pin(n.native.exe.path),managedDll:pin(managed.fixtureDll),managedSources:managed.inputBindings.sources.length};assert.equal(result.preflight.managedSources,observedContract.count);
 fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2));
 fs.mkdirSync(runtimeDirectory,{recursive:true});result.runtimeDirectory=runtimeDirectory;result.status='RUNNING_ORIGINAL105';save();
 const processResult=await runProcess(n.native.exe.path,[runtimeDirectory,'full-pipe',managed.fixtureDll],{cwd:runtimeDirectory,env:{...terminalManagedRuntimeEnvironment(layout),YEMAN_POWER_CONTROL_DIR:runtimeDirectory},timeoutMs:nativeBudget.ms});
 const {stdout,stderr,...terminal}=processResult;result.nativeProcess={exe:n.native.exe.path,args:[runtimeDirectory,'full-pipe',managed.fixtureDll],...terminal,strictNativeBudgetMs:nativeBudget.ms,originalNativeBudgetMs:ORIGINAL_NATIVE_BUDGET_MS,forcedTermination:terminal.timedOut,nativeWallDurationMs:terminal.durationMs};
 result.execution=terminal22ExecutionFromProcess(terminal);result.nativeTerminalAdmission='REJECTED_UNTIL_ORIGINAL_TERMINAL_GATE';
 fs.writeFileSync(path.join(root,'stdout.log'),stdout);fs.writeFileSync(path.join(root,'stderr.log'),stderr);
 if(n.finalStages)result.finalStageObservation=captureFinalStages(runtimeDirectory,terminal.pid);
 save();
 for(const row of inputs.values())assert.equal(hash(row.path),row.sha256,'post-run input changed: '+row.path);
 result.postInputsStable=true;result.sourceAfter=sourceCheck();assert.equal(result.sourceAfter.status,'FROZEN_SOURCE_VERIFIED');
 const rawFile=path.join(runtimeDirectory,'fixture-result.json');
 result.rawReport=fs.existsSync(rawFile)?pin(rawFile):null;
 const raw=readCompletedNativeReport(terminal,expected.expectedExit,()=>{assert(fs.existsSync(rawFile),'complete native raw report missing');return read(rawFile);},nativeBudget);
 result.nativeTerminalAdmission='ADMITTED_FOR_ORIGINAL_SEMANTIC_CHECKS';
 result.rawSummary={status:raw.status,cases:raw.cases.length,checks:raw.checks,failures:raw.failures,forbiddenIo:raw.forbiddenIo};
 result.exactSemantics=oracle.verifyCurrentSemantics(n.role,raw,oracleOptions);
 result.originalGroups=verifyCurrent105OriginalGroups(raw,{ownershipRollback:n.role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(n.role),cancellationRollback:n.role==='cancellation',currentRogOffExecution:observedContract.signalEvidence===true,retainedWakeHideRetry:observedContract.signalEvidence===true,managedInputContract:observedContract.id});
 result.children=oracle.verifyCurrentChildren(n.role,raw,managed,terminal.pid,oracleOptions);
 result.status='BOUNDED_ORIGINAL105_MATCHED_PUBLIC_RUNTIME_PENDING';
}catch(error){result.status='INCOMPLETE';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
result.finishedAt=new Date().toISOString();save();console.log(JSON.stringify({status:result.status,role:n.role,rawSummary:result.rawSummary?{...result.rawSummary,failures:result.rawSummary.failures.length}:null,error:result.errors.map(e=>e.message),resultFile}));
return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await observeTerminal22();