// Observe an actual fixed-frame native105 process. Never publishes a runtime profile.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import{createHash}from'node:crypto';import{fileURLToPath,pathToFileURL}from'node:url';
import {readCompletedNativeReport} from './r3-terminal22-observation-terminal.mjs';
import {captureFinalStages} from './r3-terminal22-stage-receipts.mjs';
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
  const key=args[i];assert(['--native-preparation','--lifecycle-preparations','--out'].includes(key)&&!Object.hasOwn(options,key),'strict observer options required');
  assert(typeof args[i+1]==='string'&&args[i+1]&&!args[i+1].startsWith('--'),'option value required');options[key]=args[++i];
 }
 assert(options['--out']&&!!options['--native-preparation']!==!!options['--lifecycle-preparations'],'exactly one explicit native or lifecycle preparation input required');
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
export function validateTerminal22LifecycleLayout(layout,trustedPrimaryRoot=trustedLifecyclePrimaryRoot){
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
  assert(lifecycleSame(n.runtimeRoot,path.join(path.dirname(nativeFile),'qualification-runtime'))&&lifecycleInside(layout.authorizedBuildRoot,n.runtimeRoot),'original compiled runtime owner required');lifecyclePlainPath(n.runtimeRoot);assert(!fs.existsSync(n.runtimeRoot),'fresh original lifecycle runtime required');
  prepared.set(n.role,{n,nativeFile,managed});
 }
 const {layout}=common;assert(lifecycleInside(layout.authorizedBuildRoot,inputFile),'lifecycle declaration escaped original authority');lifecyclePlainPath(out);const root=path.resolve(out);assert(lifecycleInside(layout.authorizedBuildRoot,root)&&!fs.existsSync(root),'fresh observation root under original lifecycle authority required');
 add(pin(fileURLToPath(import.meta.url)));fs.mkdirSync(root,{recursive:true});
 const result={schemaVersion:1,status:'VERIFYING_LIFECYCLE_INPUTS',kind:'TERMINAL'+declaredContract.count+'_ORIGINAL_LIFECYCLE_OBSERVATION',managedInputContract:declaredContract.id,inputDeclaration:inputPin,sourceRoot:layout.sourceRoot,originalModeBudgetMs:45000,originalGroupBudgetMs:90000,modeResults:[],preflight:{managedDll:pin(common.managed.fixtureDll),managedSources:declaredContract.count,nativePreparations:selected},qualificationAccepted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,sourceApprovalGranted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,compilationFailureCountedAsNegative:false,errors:[]};
 const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');let started=groupStarted;
 try{
  result.sourceBefore=sourceCheck();for(const row of inputs.values()){lifecyclePlainPath(row.path);assert.equal(hash(row.path),row.sha256,'lifecycle pre-run input changed');}fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2)+'\n');
  const runnerFile=path.join(layout.sourceRoot,'tools/ymcc_sim/global/runner.mjs');add({path:runnerFile,sha256:hash(fileURLToPath(new URL('../runner.mjs',import.meta.url)))});const runProcess=trustedLifecycleRunProcess;
  for(const {n}of prepared.values())fs.mkdirSync(n.runtimeRoot);result.startedAt=new Date(started).toISOString();result.status='RUNNING_ORIGINAL16_LIFECYCLE_MODES';save();
  for(const test of shared.currentLifecycleModePlan()){
   const budget=shared.currentLifecycleRemainingBudget(started);
   const {n}=prepared.get(test.role),label=shared.currentLifecycleModeLabel(test),dir=path.join(n.runtimeRoot,label);assert(!fs.existsSync(dir),'fresh lifecycle mode evidence required');fs.mkdirSync(dir);
   const args=[dir,test.mode],processResult=await runProcess(n.native.exe.path,args,{cwd:dir,env:{...process.env,YEMAN_POWER_CONTROL_DIR:dir},timeoutMs:budget.timeoutMs});
   const {stdout,stderr,...terminal}=processResult;fs.writeFileSync(path.join(dir,'stdout.log'),stdout);fs.writeFileSync(path.join(dir,'stderr.log'),stderr);
   const row={label,role:test.role,mode:test.mode,mutation:test.mutation,expectedNegative:!!test.mutation||test.mode.startsWith('mutation'),expectedNativeExit:test.expectedExit,nativeProcess:{exe:n.native.exe.path,args,...terminal,originalModeBudgetMs:45000,effectiveTimeoutMs:budget.timeoutMs},execution:terminal22ExecutionFromProcess(terminal),semanticAccepted:false};result.modeResults.push(row);save();
   assert(terminal.timedOut===false&&terminal.launchError===null&&terminal.signal===null&&terminal.outputTruncated===false&&Number.isFinite(terminal.durationMs)&&terminal.durationMs>=0&&terminal.durationMs<=budget.timeoutMs&&terminal.exitCode===test.expectedExit,'original natural lifecycle45 exit required: '+label);
   const rawFile=path.join(dir,'fixture-result.json');assert(fs.existsSync(rawFile),'complete lifecycle native raw required');row.raw=pin(rawFile);const raw=read(rawFile),entryIndex=test.mode==='entry'?0:test.mode==='mutation1'?1:2;
   const entryText=!test.mutation&&(test.mode==='entry'||test.mode.startsWith('mutation'))?fs.readFileSync(path.join(dir,'msi-start-order-entry-selftest-m'+entryIndex+'.txt'),'utf8'):undefined;
   shared.verifyCurrentLifecycleMode(test,raw,{entryText});row.semanticAccepted=true;row.exitCode=terminal.exitCode;row.checks=raw.checks;row.originalOrderChecks=['entry','mutation1','mutation2'].includes(test.mode)?56:undefined;row.caseNames=raw.cases.map(item=>item.name);row.rawSummary={status:raw.status,checks:raw.checks,failures:raw.failures};save();
  }
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
assert([terminalManagedStageStatus('native'),terminalManagedStageStatus('native','current105-terminal23-sd-decoder-v1')].includes(n.status),'explicit compiled native preparation required');assert.equal(n.nativeExecuted,false);assert.equal(n.publicRuntimeAccepted,false);
const preparation=read(n.preparation.path);assert.equal(hash(n.preparation.path),n.preparation.sha256);
const layout=read(preparation.layout.path);assert.equal(hash(preparation.layout.path),preparation.layout.sha256);
const observedContract=requireTerminalManagedContract(layout.managedInputContract);assert.equal(n.status,terminalManagedStageStatus('native',observedContract.id));assert.equal(preparation.status,terminalManagedStageStatus('managed',observedContract.id));
if(observedContract.steamDeckPadDecoder){assert.equal(n.managedInputContract,observedContract.id);assert.equal(preparation.managedInputContract,observedContract.id);verifyTerminal23PipeSource(n,preparation,layout);}
const root=path.resolve(options['--out']);assert(!fs.existsSync(root),'fresh observer evidence required');
const relative=path.relative(layout.authorizedBuildRoot,root);assert(relative&&!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep),'observer evidence outside original authority root');
assert(!fs.existsSync(n.runtimeRoot),'the exact compiled runtime directory must be unused');
const moduleRoot=path.join(layout.sourceRoot,'tools/ymcc_sim/global/controller-coordinator-pipe');
const oracle=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-oracle.mjs')).href);
assert(['positive','cancellation','psOwnership','psBinding'].includes(n.role),'full-pipe observation role required');
const expected=oracle.currentExpected(n.role),{verifyCurrent105OriginalGroups}=await import(pathToFileURL(path.join(moduleRoot,'r3-current105-original-groups.mjs')).href);
const {runProcess}=observedContract.steamDeckPadDecoder?{runProcess:trustedLifecycleRunProcess}:await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/runner.mjs')).href);
const {verifyCapturedSourceSnapshot}=await import(pathToFileURL(path.join(n.sourceRoot,'tools/ymcc_sim/global/source-snapshot.mjs')).href);
const manifest=read(n.native.sourceManifest.path),managed=read(preparation.managed.manifestPath);
if(observedContract.steamDeckPadDecoder){
 assert.equal(layout.managedContractCodeSha256,hash(fileURLToPath(new URL('./r3-managed-input-contract.mjs',import.meta.url))));
 validateTrustedLifecycleManagedRows(layout.expectedManagedSources,observedContract.id);validateTrustedLifecycleManagedRows(managed.inputBindings.sources,observedContract.id);
 assert.deepEqual(managed.inputBindings.sources.map(({relative,sha256})=>({relative,sha256})),layout.expectedManagedSources);
 for(const row of managed.inputBindings.sources){assert(lifecycleSame(row.path,path.join(layout.sourceRoot,row.relative))&&lifecycleInside(layout.authorizedBuildRoot,row.snapshot));lifecyclePlainPath(row.path);lifecyclePlainPath(row.snapshot);}
 verifyManagedDecoderCompileEvidence(managed,observedContract.id,layout.authorizedBuildRoot);terminal22LifecycleManagedDllPin(managed,n.native?.managedProof?.dll,layout.authorizedBuildRoot);
}
const inputs=new Map();function add(p,digest){p=path.resolve(p);assert(fs.lstatSync(p).isFile()&&!fs.lstatSync(p).isSymbolicLink(),'regular bounded input required');const k=p.toLowerCase(),old=inputs.get(k);assert(!old||old.sha256===digest,'input digest conflict');assert.equal(hash(p),digest,'changed input '+p);inputs.set(k,{path:p,sha256:digest});}
function addPin(row){add(row.path,row.sha256);}
fs.mkdirSync(root,{recursive:true});
const result={schemaVersion:1,status:'VERIFYING_INPUTS',role:n.role,nativePreparation:pin(nativeFile),observer:pin(fileURLToPath(import.meta.url)),strictNativeBudgetMs:60000,nativeTerminalAdmission:'NOT_OBSERVED',qualificationAccepted:false,publicRuntimeAccepted:false,currentPrimaryAccepted:false,fullCoordinatorRuntimeAccepted:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,physicalAcceptance:false,errors:[]};
const resultFile=path.join(root,'OBSERVATION.json'),save=()=>fs.writeFileSync(resultFile,JSON.stringify(result,null,2)+'\n');
result.controlInputs=[result.observer,...['r3-terminal22-observation-terminal.mjs','r3-terminal22-stage-receipts.mjs'].map(name=>pin(fileURLToPath(new URL('./'+name,import.meta.url))))];
try{
 for(const row of result.controlInputs)addPin(row);
 addPin(n.native.exe);addPin(n.native.sourceManifest);addPin(n.native.buildScript);addPin(n.preparation);addPin(preparation.layout);addPin({path:preparation.managed.manifestPath,sha256:preparation.managed.manifestSha256});
 for(const row of manifest.records){add(row.original,row.sha256);add(row.snapshot,row.sha256);}
 for(const row of [...manifest.compiledGenerated,...n.batchAfter.actualTools,...managed.evaluatedCompilerInputs,...managed.runtimeArtifacts])addPin(row);
 for(const row of managed.inputBindings.sources){add(row.path,row.sha256);add(row.snapshot,row.sha256);}
 for(const row of managed.inputBindings.generatedInputs)addPin(row);
 const sourceCheck=()=>observedContract.steamDeckPadDecoder?verifyTerminal23PipeSource(n,preparation,layout):verifyCapturedSourceSnapshot({manifestFile:path.join(layout.sourceRoot,'.ymcc-source-snapshot/manifest.json'),repoRoot:layout.primaryRoot,expectedManifestSha256:layout.manifestSha256});
 result.sourceBefore=sourceCheck();assert.equal(result.sourceBefore.status,'FROZEN_SOURCE_VERIFIED');assert.equal(result.sourceBefore.creationStatus,'PASS');
 result.inputCount=inputs.size;result.preflight={nativeExe:pin(n.native.exe.path),managedDll:pin(managed.fixtureDll),managedSources:managed.inputBindings.sources.length};assert.equal(result.preflight.managedSources,observedContract.count);
 fs.writeFileSync(path.join(root,'INPUTS.json'),JSON.stringify([...inputs.values()],null,2));
 fs.mkdirSync(n.runtimeRoot,{recursive:true});result.status='RUNNING_ORIGINAL105';save();
 const processResult=await runProcess(n.native.exe.path,[n.runtimeRoot,'full-pipe',managed.fixtureDll],{cwd:n.runtimeRoot,env:{...process.env,YEMAN_POWER_CONTROL_DIR:n.runtimeRoot},timeoutMs:60000});
 const {stdout,stderr,...terminal}=processResult;result.nativeProcess={exe:n.native.exe.path,args:[n.runtimeRoot,'full-pipe',managed.fixtureDll],...terminal,strictNativeBudgetMs:60000,forcedTermination:terminal.timedOut,nativeWallDurationMs:terminal.durationMs};
 result.execution=terminal22ExecutionFromProcess(terminal);result.nativeTerminalAdmission='REJECTED_UNTIL_ORIGINAL_TERMINAL_GATE';
 fs.writeFileSync(path.join(root,'stdout.log'),stdout);fs.writeFileSync(path.join(root,'stderr.log'),stderr);
 if(n.finalStages)result.finalStageObservation=captureFinalStages(n.runtimeRoot,terminal.pid);
 save();
 for(const row of inputs.values())assert.equal(hash(row.path),row.sha256,'post-run input changed: '+row.path);
 result.postInputsStable=true;result.sourceAfter=sourceCheck();assert.equal(result.sourceAfter.status,'FROZEN_SOURCE_VERIFIED');
 const rawFile=path.join(n.runtimeRoot,'fixture-result.json');
 result.rawReport=fs.existsSync(rawFile)?pin(rawFile):null;
 const raw=readCompletedNativeReport(terminal,expected.expectedExit,()=>{assert(fs.existsSync(rawFile),'complete native raw report missing');return read(rawFile);});
 result.nativeTerminalAdmission='ADMITTED_FOR_ORIGINAL_SEMANTIC_CHECKS';
 result.rawSummary={status:raw.status,cases:raw.cases.length,checks:raw.checks,failures:raw.failures,forbiddenIo:raw.forbiddenIo};
 result.exactSemantics=oracle.verifyCurrentSemantics(n.role,raw);
 result.originalGroups=verifyCurrent105OriginalGroups(raw,{ownershipRollback:n.role==='psOwnership',bindingRollback:['psBinding','cancellation'].includes(n.role),cancellationRollback:n.role==='cancellation'});
 result.children=oracle.verifyCurrentChildren(n.role,raw,managed,terminal.pid);
 result.status='BOUNDED_ORIGINAL105_MATCHED_PUBLIC_RUNTIME_PENDING';
}catch(error){result.status='INCOMPLETE';result.errors.push({message:error.message,stack:error.stack});process.exitCode=1;}
result.finishedAt=new Date().toISOString();save();console.log(JSON.stringify({status:result.status,role:n.role,rawSummary:result.rawSummary?{...result.rawSummary,failures:result.rawSummary.failures.length}:null,error:result.errors.map(e=>e.message),resultFile}));
return result;
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url))await observeTerminal22();