// Read-only intake of an original fixed-C++ public software run. This consumer
// grants no generic cache, generator provenance, current-mainline or Full proof.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {openTerminal26ArtifactContext,terminal26ArtifactInputs} from './r3-terminal26-artifact-inputs.mjs';
import {fixedArtifactConsumerSources,mergeFixedArtifactPin,verifyFixedArtifactOwnedInputs,fixedArtifactHeldProcessInterval,FIXED_CPP_SCOPE,FIXED_CPP_PIPE_IDS} from './r3-terminal26-artifact-run.mjs';
import {fixedArtifactOuterRegistration} from './r3-terminal26-artifact-adapter.mjs';
import {rawProgramLauncherDeclaration} from '../decky-persistent-lifecycle/raw-owned-producer.mjs';
import {terminalManagedRuntimeEnvironment} from './r3-terminal22-qualified-observe.mjs';
import {current26FixedExpected,verifyCurrent26FixedSemantics,verifyCurrent26FixedChildren} from './r3-terminal26-current-oracle.mjs';

const REPO=path.resolve(import.meta.dirname,'../../../..');
const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const DIRS=Object.freeze({positive:'coordinator-pipe',cancellation:'coordinator-pipe-cancellation-negative',psOwnership:'coordinator-pipe-ps-ownership-negative',psBinding:'coordinator-pipe-ps-binding-negative'});
const SHA=/^[a-f0-9]{64}$/;
const hash=b=>createHash('sha256').update(b).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const inside=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!path.isAbsolute(r)&&r!=='..'&&!r.startsWith('..'+path.sep));};
const need=(v,message)=>assert(v,'FIXED_CPP_REFERENCE_'+message);
const samePin=(a,b)=>{need(a&&b&&typeof a.path==='string'&&typeof b.path==='string'&&SHA.test(a.sha256??'')&&SHA.test(b.sha256??'')&&key(a.path)===key(b.path)&&a.sha256===b.sha256&&(a.bytes===undefined||b.bytes===undefined||a.bytes===b.bytes),'PIN_BINDING');};
const projection=r=>[key(r.path),r.bytes,r.sha256];
function exactRows(actual,expected,label,{bytes=true}={}){
 need(Array.isArray(actual)&&actual.length===expected.length,label+'_COUNT');
 const row=r=>{need(r&&typeof r.path==='string'&&SHA.test(r.sha256??'')&&(r.status===undefined||r.status==='OK'),label+'_ROW');return bytes?projection(r):[key(r.path),r.sha256];};
 const observed=actual.map(row),wanted=expected.map(row);
 need(new Set(observed.map(x=>x[0])).size===observed.length,label+'_DUPLICATE');
 assert.deepEqual(observed.sort((a,b)=>a[0].localeCompare(b[0])),wanted.sort((a,b)=>a[0].localeCompare(b[0])),label+'_EXACT');
}
function ordinaryPath(file){
 need(typeof file==='string'&&/^[A-Za-z]:[\\/]/.test(file)&&!/[\x00-\x1f]/.test(file)&&!file.slice(2).includes(':'),'ABSOLUTE_ORDINARY_PATH');
 need(file.split(/[\\/]/).slice(1).every(x=>x&&x!=='.'&&x!=='..'&&!/[. ]$/.test(x)),'PATH_ALIAS');
 return path.resolve(file);
}
function safeFile(file){
 file=ordinaryPath(file);
 for(let cursor=path.resolve(file);;cursor=path.dirname(cursor)){
  need(!fs.lstatSync(cursor).isSymbolicLink(),'REDIRECTED_PATH');if(path.dirname(cursor)===cursor)break;
 }
 return path.resolve(file);
}
function observation(){
 const pins=new Map();let bytesRead=0;
 function read(file,expected,json=false){
  file=safeFile(file);if(expected)need(SHA.test(expected.sha256??''),'EXPECTED_SHA');
  const fd=fs.openSync(file,'r');let data;
  try{
   const before=fs.fstatSync(fd,{bigint:true});need(before.isFile(),'REGULAR_FILE');
   need(before.size<=BigInt(Number.MAX_SAFE_INTEGER),'FILE_SIZE');
   if(json){need(before.size<=80n*1024n*1024n,'JSON_SIZE');data=fs.readFileSync(fd);}
   const digest=createHash('sha256');
   if(json)digest.update(data);else{const buffer=Buffer.allocUnsafe(1024*1024);let n;while((n=fs.readSync(fd,buffer,0,buffer.length,null))>0)digest.update(buffer.subarray(0,n));}
   const after=fs.fstatSync(fd,{bigint:true});
   for(const field of ['dev','ino','size','mtimeNs','ctimeNs'])need(before[field]===after[field],'CHANGED_DURING_READ');
   const pin={path:file,bytes:Number(before.size),sha256:digest.digest('hex')};bytesRead+=pin.bytes;
   if(expected)samePin(pin,expected);
   const old=pins.get(key(file));if(old)samePin(old,pin);else pins.set(key(file),pin);
   return json?{pin,value:JSON.parse(data.toString('utf8').replace(/^\uFEFF/,''))}:pin;
  }finally{fs.closeSync(fd);}
 }
 return {read,pins,get bytesRead(){return bytesRead;},json:(file,expected)=>read(file,expected,true),check:row=>read(row.path,row),
  close(){for(const pin of [...pins.values()])read(pin.path,pin);return {files:pins.size,bytesRead,allObservedBytesRehashed:true};}};
}

/** Pure binding checks used by the real consumer and bounded refusal controls.
 * A return here is not a qualified file/evidence reference. */
export function verifyFixedArtifactReferenceBindings({role,runDir,repo,view,expectedSources,expectedTools,outerSources,outerArtifacts,parent,budget,job,context,outer,leaf,nested,terminal,terminalPin,request,args}){
 need(Object.hasOwn(FIXED_CPP_PIPE_IDS,role),'ROLE');const id=FIXED_CPP_PIPE_IDS[role],t=leaf.value.actualProcess,x=t?.rawExecution;
 need(parent.runDir&&key(parent.runDir)===key(runDir)&&['PASS_SELECTED_SCOPE','COMPLETE_WITH_CACHED_REFERENCES'].includes(parent.status),'PARENT_SCOPE');
 need(parent.fullAcceptance===false&&parent.fullProductAcceptance===false,'PARENT_LIMITED_ACCEPTANCE');
 const rows=parent.rows?.filter(row=>row.id===id);need(rows?.length===1&&rows[0].status==='PASS'&&rows[0].executedThisRun===true&&rows[0].actualPassedThisRun===true,'ORIGINAL_ACTUAL_ROW');
 samePin(rows[0].reference,outer.pin);
 need(budget.status===parent.status&&budget.exitCode===0&&budget.processExitCode===0&&budget.processStarted===true&&budget.launchError===null&&budget.timedOut===false&&budget.ownedJobVerified===true&&budget.resourceZeroVerified===true,'PARENT_TERMINAL');
 need(Array.isArray(budget.sourceDrift)&&budget.sourceDrift.length===0&&Array.isArray(budget.sourceReadErrors)&&budget.sourceReadErrors.length===0,'PARENT_SOURCE_STABILITY');
 assert.deepEqual(budget.ownedJob,job,'FIXED_CPP_REFERENCE_PARENT_JOB_BINDING');
 need(job.status==='EXITED'&&job.exitCode===0&&job.rootExitCode===0&&job.deadlineExpired===false&&job.jobAssigned===true&&job.childExitedBeforeReturn===true&&job.jobDisposed===true&&job.zeroObservedBeforeDispose===true&&job.activeProcessesFinal===0&&job.terminationRequested===false&&job.cleanupError===null,'PARENT_NATURAL_ZERO');
 need(context.kind==='ymcc-fixture-wall-budget'&&key(context.runDir)===key(runDir)&&Number.isSafeInteger(context.budgetMs)&&context.budgetMs>0&&context.budgetMs<=1800000&&Number.isSafeInteger(context.deadlineEpochMs)&&context.budgetMs===budget.budgetMs,'PARENT_BUDGET_CONTEXT');
 need(job.deadlineEpochMs===context.deadlineEpochMs&&job.finishedEpochMs<=context.deadlineEpochMs&&budget.wallMs<=budget.budgetMs,'PARENT_DEADLINE');
 const o=outer.value;
 need(o.id===id&&o.kind==='node'&&o.scope==='behavioral-mock'&&o.source===MODULE+'/r3-terminal26-artifact-run.mjs'&&key(o.evidenceDir)===key(path.join(runDir,'cases',id)),'OUTER_ID_AND_SOURCE');
 need(o.status==='PASS'&&o.exitCode===0&&o.signal===null&&o.timedOut===false&&o.launchError===null&&o.outputTruncated===false&&o.sourceChanged===false&&o.candidateArtifactChanged===false&&o.requiredEvidenceMissing===false&&o.requiredEvidenceFailed===false,'OUTER_SUCCESS');
 exactRows(o.sourceInputs.map(row=>({...row,path:path.resolve(repo,row.path)})),outerSources,'OUTER_SOURCES',{bytes:false});exactRows(o.candidateArtifacts,outerArtifacts,'OUTER_ARTIFACTS',{bytes:false});
 need(o.structuredEvidence?.length===1&&o.structuredEvidence[0].failed===false&&o.structuredEvidence[0].sourceMismatches?.length===0,'EXACT_STRUCTURED_RESULT');samePin(o.structuredEvidence[0],leaf.pin);
 const l=leaf.value;
 need(l.id===id&&l.case===role&&l.status==='PASS'&&l.passed===true&&l.scope===FIXED_CPP_SCOPE&&l.lifecycleVersion==='VC-LIFECYCLE-20261020-R3'&&l.errors?.length===0,'LEAF_ROLE_SCOPE_STATUS');
 for(const field of ['cacheAcceptance','toolClosureComplete','currentPrimaryAccepted','fullSourceCurrent','fullCoordinatorRuntimeAccepted','fullSourceApproved','fullProductAcceptance'])need(l[field]===false,'NO_EXPANDED_CLAIM');
 need(l.claims?.fixedCompiledCppSoftwareExecuted===true&&l.claims.physicalHardwareValidated===false&&l.claims.operatingSystemSleepPerformed===false&&l.claims.installedApplicationExercised===false,'SOFTWARE_ONLY_CLAIMS');
 const n=nested.value;need(n.schemaVersion===1&&n.scope===FIXED_CPP_SCOPE&&n.role===role&&n.historicalJavaScriptGeneratorClosureComplete===false&&n.sourceAuthorityGranted===false&&n.cacheAcceptance===false,'NESTED_SCOPE');
 need(terminalPin&&Number.isSafeInteger(terminalPin.bytes)&&terminalPin.bytes>=0,'OBSERVED_TERMINAL_PIN');
 samePin(l.nestedInputProof.terminal,terminalPin);samePin(x.terminal,terminalPin);
 exactRows(n.sourcePins,expectedSources,'SOURCE_MEMBERSHIP');exactRows(n.artifactPins,expectedTools,'ARTIFACT_MEMBERSHIP');
 assert.deepEqual(n.qualification,view.metadata.qualification,'FIXED_CPP_REFERENCE_ORIGINAL_QUALIFICATION');samePin(l.nestedInputProof.manifest,nested.pin);samePin(l.nestedInputProof.terminal,x.terminal);
 const originalHashes=[{path:nested.pin.path,sha256:nested.pin.sha256},...expectedSources.filter(row=>inside(repo,row.path)).map(row=>({path:path.relative(repo,row.path).replaceAll('\\','/'),sha256:row.sha256}))];
 exactRows(l.sourceHashes.map(row=>({...row,path:path.resolve(repo,row.path)})),originalHashes.map(row=>({...row,path:path.resolve(repo,row.path)})),'LEAF_SOURCE_HASHES',{bytes:false});
 need(t.id===id&&Number.isSafeInteger(t.pid)&&t.pid>0&&t.signal===null&&t.error===null&&t.timedOut===false&&t.outputTruncated===false,'NATIVE_TERMINAL');
 const meta=view.metadata,expectedExit=role==='positive'?0:1;
 need(t.exitCode===expectedExit&&t.arguments?.length===3&&t.arguments[1]==='full-pipe'&&key(t.arguments[2])===key(meta.managed.fixtureDll)&&key(t.cwd)===key(t.arguments[0]),'NATIVE_ARGS_EXIT');
 need(inside(meta.native.runtimeLayout.publicRuntime.base,t.cwd)&&/^[a-f0-9]{16}$/.test(path.basename(t.cwd)),'NATIVE_OWNED_RUNTIME');samePin(t.program,meta.native.native.exe);
 need(key(l.nativeResult.report.path)===key(path.join(t.cwd,'fixture-result.json'))&&l.nativeResult.originalExitCode===expectedExit&&l.nativeResult.expectedSemanticFailure===(role!=='positive')&&l.nativeResult.mutation===meta.native.mutation,'NATIVE_REPORT_BINDING');
 need(x.collectionComplete===true&&x.cancelled===false&&x.ownerLost===false&&x.residualDescendants===false&&x.helperSignal===null&&x.helperExitCode===(expectedExit===0?0:2)&&x.completion.kind==='NATURAL'&&x.completion.forcedToolFinalization===false&&x.completion.productResourcesWaived===false,'RAW_COLLECTION');
 assert.deepEqual(x.actualBusiness,terminal.business,'FIXED_CPP_REFERENCE_RAW_BUSINESS');assert.deepEqual(x.job,terminal.job,'FIXED_CPP_REFERENCE_RAW_JOB');
 need(terminal.owner?.handleBound===true&&terminal.owner.pid===o.pid&&/^\d{17,20}$/.test(terminal.owner.expectedStartTicks??''),'ORIGINAL_OWNER_BINDING');
 samePin(terminal.request,x.request);samePin(terminal.argumentsPin,x.arguments);samePin(x.helperSource,x.launcher.helper);samePin(x.launcher.program,meta.profile.powershell);
 need(terminal.businessExitCode===expectedExit&&terminal.helperExitCode===x.helperExitCode&&terminal.signal===null&&terminal.error===null&&terminal.outputLimitExceeded===false&&terminal.status===(expectedExit===0?'EXITED':'FAILED')&&terminal.globalAcceptanceGranted===false&&terminal.undeclaredDependencyGraphProven===false,'RAW_TERMINAL');
 assert.deepEqual(args,t.arguments,'FIXED_CPP_REFERENCE_RAW_ARGUMENTS');assert.deepEqual(terminal.business.arguments,args,'FIXED_CPP_REFERENCE_BUSINESS_ARGUMENTS');assert.deepEqual(request.budget,x.budget,'FIXED_CPP_REFERENCE_REQUEST_BUDGET');samePin(request.arguments,x.arguments);
 exactRows(request.sources,expectedSources,'REQUEST_SOURCES');exactRows(request.tools,expectedTools,'REQUEST_TOOLS');
 assert.deepEqual(terminal.business.environment,request.environment,'FIXED_CPP_REFERENCE_EXACT_ENVIRONMENT');
 const environment=terminalManagedRuntimeEnvironment(meta.layout,{...request.environment});assert.deepEqual(environment,request.environment,'FIXED_CPP_REFERENCE_FIXED_MANAGED_ENVIRONMENT');
 need(request.environment.YEMAN_POWER_CONTROL_DIR===t.cwd&&request.environment.DOTNET_CLI_USE_MSBUILD_SERVER==='0'&&request.environment.MSBUILDDISABLENODEREUSE==='1','RUNTIME_ENVIRONMENT');
 const b=x.budget;need(b.originalBudgetMs===165000&&b.cleanupReserveMs===30000&&b.allocatedBudgetMs>30000&&b.allocatedBudgetMs<=165000&&b.deadlineEpochMs===b.allocatedAtEpochMs+b.allocatedBudgetMs,'NATIVE_BUDGET');
 need(terminal.business.originalDeadlineEpochMs===b.deadlineEpochMs&&terminal.business.runCutoffEpochMs===b.deadlineEpochMs-30000&&terminal.finishedEpochMs<=terminal.business.runCutoffEpochMs,'NATIVE_DEADLINE');
 assert.deepEqual(x.sourceBefore,terminal.declaredInputsBefore,'FIXED_CPP_REFERENCE_RAW_INPUTS_BEFORE');assert.deepEqual(x.sourceAfter,terminal.declaredInputsAfter,'FIXED_CPP_REFERENCE_RAW_INPUTS_AFTER');
 const inputProof=verifyFixedArtifactOwnedInputs(t,terminal,expectedSources,expectedTools),interval=fixedArtifactHeldProcessInterval(terminal,135000);
 const {manifest:originalManifest,terminal:originalTerminal,...originalInputProof}=l.nestedInputProof;
 samePin(originalManifest,nested.pin);samePin(originalTerminal,x.terminal);
 assert.deepEqual(originalInputProof,inputProof,'FIXED_CPP_REFERENCE_RECOMPUTED_INPUT_PROOF');
 assert.deepEqual(l.executionBudget.nativeInterval,interval,'FIXED_CPP_REFERENCE_HELD_CLOCK');need(l.executionBudget.nativeBudgetMs===135000&&l.executionBudget.nativeWallMs===interval.nativeWallMs,'NATIVE135');
 return {interval,inputProof,originalEnvironmentSha256:hash(JSON.stringify(request.environment))};
}

/** Require a pinned original intake, including its parent and resource pins.
 * No automatic run discovery, producer replay or cache-index migration. */
export function verifyTerminal26ArtifactReference({role,intakePin,repo=REPO}={}){
 const started=performance.now();need(Object.hasOwn(FIXED_CPP_PIPE_IDS,role),'ROLE');
 need(!process.env.NODE_OPTIONS&&!process.env.NODE_PATH&&process.execArgv.length===0,'NODE_PRELOAD_OR_ARGUMENTS');
 repo=path.resolve(repo);const core=path.resolve(repo,'../../../Testing/YMCC');
 need(intakePin&&SHA.test(intakePin.sha256??'')&&inside(path.join(core,'Evidence'),intakePin.path),'EXPLICIT_INTAKE_PIN');
 const io=observation(),anchor=io.json(intakePin.path,intakePin),a=anchor.value;
 const verifier=io.read(fileURLToPath(import.meta.url));
 need(a.schemaVersion===1&&a.status==='ORIGINAL_PROJECT_ENTRY_TWO_FIXED_CPP_ROLES_ACTUALLY_MATCHED'&&a.scope===FIXED_CPP_SCOPE&&a.toolClosureComplete===false&&a.cacheAcceptance===false&&a.currentPrimaryAccepted===false&&a.wholeGoalComplete===false,'ORIGINAL_INTAKE_SCOPE');
 const runDir=path.resolve(a.runDirectory);need(inside(path.join(core,'Evidence'),runDir),'ORIGINAL_CORE_EVIDENCE');
 need(key(a.workerResult.path)===key(path.join(runDir,'result.json'))&&key(a.executionBudget.path)===key(path.join(runDir,'execution-budget.json')),'ORIGINAL_PARENT_PIN_PATHS');
 const anchoredRoles=a.rows?.filter(row=>row.role===role&&row.id===FIXED_CPP_PIPE_IDS[role]);need(anchoredRoles?.length===1,'ROLE_PRESENT_IN_ORIGINAL_INTAKE');const anchoredRole=anchoredRoles[0];
 const parent=io.json(a.workerResult.path,a.workerResult),budget=io.json(a.executionBudget.path,a.executionBudget),job=io.json(path.join(runDir,'owned-job.json')),context=io.json(path.join(runDir,'budget-context.json'));
 assert.deepEqual(job.value,a.outerOwnedJob,'FIXED_CPP_REFERENCE_ANCHORED_OUTER_JOB');
 for(const pin of [...a.sourceInputs,...a.unchangedGlobalExecutorSources])io.check(pin);
 const workerArgs=io.json(path.join(runDir,'budget-arguments.json'));
 need(context.value.repoRoot&&key(context.value.repoRoot)===key(repo)&&/^[a-f0-9]{48}$/.test(context.value.nonce??''),'ORIGINAL_WORKER_CONTEXT');
 assert.deepEqual(workerArgs.value,[path.join(repo,'tools/ymcc_sim/optimization/cli.mjs'),'--budget-worker',context.pin.path,context.value.nonce],'FIXED_CPP_REFERENCE_WORKER_ARGUMENTS');
 for(const name of ['budget.stdout.log','budget.stderr.log','plan.json'])io.read(path.join(runDir,name));
 const originalIds=a.rows.map(row=>row.id);need(originalIds.length===2&&new Set(originalIds).size===2,'ORIGINAL_SELECTED_IDS');
 assert.deepEqual(parent.value.rows.map(row=>row.id).sort(),originalIds.sort(),'FIXED_CPP_REFERENCE_ORIGINAL_PARENT_SELECTION');
 assert.deepEqual(parent.value.summary,a.summary,'FIXED_CPP_REFERENCE_ANCHORED_PARENT_SUMMARY');
 need(a.summary.executedThisRun===2&&a.summary.actualPassedThisRun===2&&a.summary.cachedReferences===0&&budget.value.sourceClosureComplete===false&&budget.value.workerStatus===parent.value.status&&budget.value.workerEvidenceError===null&&budget.value.workerActualPassedThisRun===2,'ORIGINAL_WORKER_TERMINAL');
 const original=openTerminal26ArtifactContext(role,repo),view=terminal26ArtifactInputs(original),meta=view.metadata;
 need(key(process.execPath)===key(meta.profile.node.path),'PINNED_NODE');
 const registration=fixedArtifactOuterRegistration(view,repo,role),physical=new Map(),sources=new Map(),tools=new Map();
 for(const row of view.inputPins)mergeFixedArtifactPin(new Map(),physical,row);
 for(const row of view.sourcePins)mergeFixedArtifactPin(sources,physical,row);for(const row of view.artifactPins)mergeFixedArtifactPin(tools,physical,row);
 for(const relative of fixedArtifactConsumerSources(repo))mergeFixedArtifactPin(sources,physical,io.read(path.join(repo,relative)));
 for(const row of [meta.native.native.exe,meta.profile.powershell,meta.profile.node])mergeFixedArtifactPin(tools,physical,io.check(row));
 for(const file of rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:meta.profile.powershell.path}).files)mergeFixedArtifactPin(tools,physical,io.read(file));
 const expectedSources=[...sources.values()],expectedTools=[...tools.values()];
 for(const row of [...view.inputPins,...expectedSources,...expectedTools]){const old=io.pins.get(key(row.path));if(old)samePin(old,row);else io.check(row);}
 const outerSources=registration.evidenceInputs.map(relative=>io.read(path.join(repo,relative))),outerArtifacts=registration.artifactInputs.map(file=>io.read(file));
 for(const row of budget.value.sourcePins??[])io.check(row);
 const id=FIXED_CPP_PIPE_IDS[role],caseDir=path.join(runDir,'cases',id),leafDir=path.join(caseDir,DIRS[role]);
 const parentRows=parent.value.rows?.filter(row=>row.id===id);need(parentRows?.length===1,'ORIGINAL_ROW');
 const outer=io.json(path.join(caseDir,'result.json'),parentRows[0].reference);for(const name of ['stdout.log','stderr.log'])io.read(path.join(caseDir,name));
 samePin(outer.pin,anchoredRole.outer);
 const structured=outer.value.structuredEvidence;need(structured?.length===1,'STRUCTURED_RESULT_COUNT');
 const leaf=io.json(path.join(leafDir,'result.json'),structured[0]),l=leaf.value,t=l.actualProcess;need(t?.rawExecution,'RAW_EXECUTION_REQUIRED');
 const nested=io.json(path.join(leafDir,'nested-inputs.json'),l.nestedInputProof?.manifest),x=t.rawExecution;
 samePin(leaf.pin,anchoredRole.leaf);samePin(nested.pin,anchoredRole.nestedInputs);samePin(x.terminal,anchoredRole.jobProof);
 need(l.executionBudget.parentContext,'ORIGINAL_PARENT_CONTEXT_PIN');samePin(context.pin,l.executionBudget.parentContext);
 const terminal=io.json(x.terminal.path,x.terminal),request=io.json(x.request.path,x.request),args=io.json(x.arguments.path,x.arguments);
 const binding=verifyFixedArtifactReferenceBindings({role,runDir,repo,view,expectedSources,expectedTools,outerSources,outerArtifacts,parent:parent.value,budget:budget.value,job:job.value,context:context.value,outer,leaf,nested,terminal:terminal.value,terminalPin:terminal.pin,request:request.value,args:args.value});
 for(const row of [x.helperSource,x.launcher.program,x.launcher.helper,...Object.values(x.logs),...Object.values(terminal.value.logs),t.stdout,t.stderr])io.check(row);
 for(const stream of ['stdout','stderr']){samePin(x.logs[stream],terminal.value.logs[stream]);need(t[stream].sha256===x.logs[stream].sha256,'MIRRORED_RAW_LOG');}
 const report=io.json(l.nativeResult.report.path,l.nativeResult.report),options={managedInputContract:meta.profile.managedInputContract,nativeSourceProfile:meta.native.nativeSourceProfile,sourceCaptureManifestSha256:meta.profile.sourceCaptureManifestSha256,mutation:meta.native.mutation};
 samePin(report.pin,anchoredRole.nativeRaw);
 current26FixedExpected(role,options);const semantics=verifyCurrent26FixedSemantics(role,report.value,options),children=verifyCurrent26FixedChildren(role,report.value,meta.managed,t.pid,options);
 assert.deepEqual(l.semanticProof,semantics,'FIXED_CPP_REFERENCE_RECOMPUTED_SEMANTICS');assert.deepEqual(l.children,children,'FIXED_CPP_REFERENCE_RECOMPUTED_CHILDREN');need(l.checks===report.value.checks,'ORIGINAL_CHECK_COUNT');
 assert.deepEqual(l.cases,report.value.cases.map(row=>({name:row.name,sourceOrdinal:row.sourceOrdinal,nameOccurrence:row.nameOccurrence})),'FIXED_CPP_REFERENCE_ORIGINAL_CASES');
 need(l.nativeResult.status===report.value.status,'ORIGINAL_RAW_STATUS');assert.deepEqual(l.nativeResult.failures,report.value.failures,'FIXED_CPP_REFERENCE_ORIGINAL_FAILURES');
 const closing=io.close();
 return {schemaVersion:1,kind:'FIXED_CPP_PUBLIC_SOFTWARE_REFERENCE_V1',status:'CACHED_REFERENCE',scope:FIXED_CPP_SCOPE,role,id,originalIntake:anchor.pin,originalParent:parent.pin,originalCase:outer.pin,originalLeaf:leaf.pin,
  originalNativeReport:report.pin,originalSourceCaptureManifestSha256:meta.profile.sourceCaptureManifestSha256,originalNativePid:t.pid,heldNativeInterval:binding.interval,
  cases:semantics.cases,checks:semantics.checks,exactSemanticFailures:semantics.exactFailureCount,ownedChildren:children.common.ownedChildren,physicalInputs:physical.size,
  originalEnvironmentSha256:binding.originalEnvironmentSha256,verifier,evidencePins:[...io.pins.values()],closing,wallMs:performance.now()-started,
  executedThisRun:0,actualPassedThisRun:0,nativeProcessesStartedThisRun:0,compilerInvocationsThisRun:0,
  cacheIndexWritten:false,genericCacheQualified:false,historicalJavaScriptGeneratorClosureComplete:false,currentPrimaryAccepted:false,fullProductAcceptance:false,physicalHardwareValidated:false};
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  const argv=process.argv.slice(2),o={};need(argv.length===8,'EXACT_ARGUMENTS');
  for(let i=0;i<argv.length;i+=2){need(['--case','--intake','--intake-sha256','--out'].includes(argv[i])&&!Object.hasOwn(o,argv[i])&&argv[i+1]&&!argv[i+1].startsWith('--'),'ARGUMENTS');o[argv[i]]=argv[i+1];}
  need(Object.keys(o).length===4,'ARGUMENT_SET');
  const output=ordinaryPath(o['--out']),core=path.resolve(REPO,'../../../Testing/YMCC');need(inside(path.join(core,'Evidence'),output)&&!fs.existsSync(output),'FRESH_CORE_OUTPUT');safeFile(path.dirname(output));
  const result=verifyTerminal26ArtifactReference({role:o['--case'],intakePin:{path:o['--intake'],sha256:o['--intake-sha256']}});
  const fd=fs.openSync(output,'wx');try{fs.writeFileSync(fd,JSON.stringify(result,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}
  console.log(JSON.stringify({status:result.status,role:result.role,cases:result.cases,checks:result.checks,wallMs:result.wallMs,executedThisRun:0,actualPassedThisRun:0,output}));
 }catch(error){console.error('FIXED_CPP_REFERENCE_REJECTED '+error.message);process.exitCode=2;}
}
