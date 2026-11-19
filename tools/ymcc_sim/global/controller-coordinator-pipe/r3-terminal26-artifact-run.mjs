// Explicit fixed compiled-artifact software execution. No current-mainline or
// historical whole-JavaScript generator qualification is granted here.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash,randomBytes} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
import {openTerminal26ArtifactContext,terminal26ArtifactInputs} from './r3-terminal26-artifact-inputs.mjs';
import {terminal26PublicRuntimeDirectory} from './r3-terminal26-runtime-layout.mjs';
import {terminalManagedRuntimeEnvironment} from './r3-terminal22-qualified-observe.mjs';
import {current26FixedExpected,verifyCurrent26FixedSemantics,verifyCurrent26FixedChildren} from './r3-terminal26-current-oracle.mjs';
import {startOwnedRawProducer,rawProgramLauncherDeclaration} from '../decky-persistent-lifecycle/raw-owned-producer.mjs';

const MODULE='tools/ymcc_sim/global/controller-coordinator-pipe';
const REPO=path.resolve(import.meta.dirname,'../../../..');
export const FIXED_CPP_SCOPE='FIXED_CPP_ARTIFACT_SOFTWARE_ONLY';
export const FIXED_CPP_PIPE_IDS=Object.freeze({positive:'controller.coordinator-native-managed-pipe',cancellation:'controller.coordinator-native-managed-pipe.cancellation-negative',psOwnership:'controller.coordinator-native-managed-pipe.ps-ownership-negative',psBinding:'controller.coordinator-native-managed-pipe.ps-binding-negative'});
const digest=b=>createHash('sha256').update(b).digest('hex');
const key=p=>path.resolve(p).toLowerCase();
const inside=(root,p)=>{const relative=path.relative(path.resolve(root),path.resolve(p));return relative===''||(!path.isAbsolute(relative)&&relative!=='..'&&!relative.startsWith('..'+path.sep));};
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function plain(p){
 assert(typeof p==='string'&&path.isAbsolute(p)&&!/[\0\r\n]/.test(p),'ABSOLUTE_PLAIN_PATH_REQUIRED');
 for(let cursor=path.resolve(p);;cursor=path.dirname(cursor)){
  const stat=fs.lstatSync(cursor);assert(!stat.isSymbolicLink(),'FIXED_CPP_REPARSE_REFUSED');
  if(path.dirname(cursor)===cursor)break;
 }
 return path.resolve(p);
}
const pin=p=>{p=plain(p);assert(fs.statSync(p).isFile(),'FIXED_CPP_REGULAR_FILE_REQUIRED');const b=fs.readFileSync(p);return {path:p,bytes:b.length,sha256:digest(b)};};
const checked=row=>{assert(row&&typeof row.path==='string'&&/^[a-f0-9]{64}$/.test(row.sha256??''),'FIXED_CPP_PIN_REQUIRED');const actual=pin(row.path);assert.equal(actual.sha256,row.sha256,'FIXED_CPP_INPUT_CHANGED:'+row.path);if(row.bytes!==undefined)assert.equal(actual.bytes,row.bytes);return actual;};
const fresh=(p,value)=>{const fd=fs.openSync(p,'wx');try{fs.writeFileSync(fd,JSON.stringify(value,null,2)+'\n');fs.fsyncSync(fd);}finally{fs.closeSync(fd);}return pin(p);};

export function mergeFixedArtifactPin(target,physicalPins,row){
 assert(target instanceof Map&&physicalPins instanceof Map&&row&&typeof row.path==='string'&&Number.isSafeInteger(row.bytes)&&row.bytes>=0&&/^[a-f0-9]{64}$/.test(row.sha256??''),'FIXED_CPP_COMPLETE_MERGE_PIN_REQUIRED');
 const id=key(row.path),existing=physicalPins.get(id),declared=target.get(id);
 for(const previous of [existing,declared])if(previous)assert(previous.bytes===row.bytes&&previous.sha256===row.sha256,'FIXED_CPP_CONFLICTING_AUTHORITY_PIN:'+row.path);
 if(!existing)physicalPins.set(id,{...row});if(!declared)target.set(id,{...row});
}

export function fixedArtifactHeldProcessInterval(raw,nativeBudgetMs){
 assert(Number.isSafeInteger(nativeBudgetMs)&&nativeBudgetMs>0&&/^\d+$/.test(raw.businessExitFileTimeUtc??'')&&/^\d+$/.test(raw.business?.startFileTimeUtc??''),'FIXED_CPP_HELD_NATIVE_CLOCK_REQUIRED');
 const ticks=BigInt(raw.businessExitFileTimeUtc)-BigInt(raw.business.startFileTimeUtc);
 assert(ticks>=0n&&ticks<=BigInt(nativeBudgetMs)*10000n,'FIXED_CPP_NATIVE135_WINDOW_EXCEEDED');
 return {nativeWallMs:Number(ticks)/10000,elapsed100ns:ticks.toString(),basis:'GetProcessTimes held process creation to exit, including suspended admission; exact100ns comparison'};
}

// Relative imports and JSON reads of these consumers are declared in source by
// the adapter. The owned native process receives their exact bytes as well.
export function fixedArtifactConsumerSources(repo=REPO){
 const seeds=[
 MODULE+'/r3-terminal26-artifact-run.mjs',MODULE+'/r3-terminal26-artifact-inputs.mjs',MODULE+'/r3-terminal26-artifact-prerequisites.json',
 MODULE+'/r3-terminal26-current-oracle.mjs',MODULE+'/r3-terminal26-current-negative-expectations.json',
 MODULE+'/r3-terminal26-runtime-layout.mjs',MODULE+'/r3-terminal22-qualified-observe.mjs',
 'tools/ymcc_sim/global/decky-persistent-lifecycle/raw-owned-producer.mjs','tools/ymcc_sim/global/host-process.ps1',
 ];
 const seen=new Set();
 const visit=relative=>{
  const file=path.resolve(repo,relative);assert(inside(repo,file),'FIXED_CPP_CONSUMER_IMPORT_OUTSIDE_REPOSITORY');
  const logical=path.relative(repo,file).replaceAll('\\','/');if(seen.has(logical))return;seen.add(logical);plain(file);
  if(!/\.[cm]?js$/.test(file))return;
  const text=fs.readFileSync(file,'utf8');
  // These reviewed consumers use literal static ESM imports. Other actual
  // source/data members are supplied by the independent descriptor reader.
  const statements=/^\s*(?:import\s+(?:[^;]*?\sfrom\s*)?|export\s+(?:\*|\{[^}]*\})\s+from\s*)['"]([^'"\r\n]+)['"]/gm;
  for(const match of text.matchAll(statements)){
   const specifier=match[1];if(specifier.startsWith('node:'))continue;
   assert(specifier.startsWith('.'),'FIXED_CPP_UNDECLARED_CONSUMER_PACKAGE_IMPORT');
   visit(path.relative(repo,path.resolve(path.dirname(file),specifier)));
  }
 };
 for(const relative of seeds)visit(relative);
 return [...seen].sort();
}

// A real optimized parent may provide a smaller remaining deadline. Reading
// this exact ancestor only reduces our own finite budget; it grants no source
// or execution authority, and no other arbitrary budget file is discovered.
export function fixedArtifactLeafBudget({root,id,started,leafBudgetMs,cleanupReserveMs}){
 assert(Number.isSafeInteger(started)&&Number.isSafeInteger(leafBudgetMs)&&leafBudgetMs>0&&leafBudgetMs<=1800000);
 assert(Number.isSafeInteger(cleanupReserveMs)&&cleanupReserveMs>0&&cleanupReserveMs<leafBudgetMs);
 const caseDirectory=path.dirname(root),casesDirectory=path.dirname(caseDirectory),runDirectory=path.dirname(casesDirectory);
 let deadline=started+leafBudgetMs,parentContext=null;
 if(path.basename(caseDirectory)===id&&path.basename(casesDirectory)==='cases'){
  const contextFile=path.join(runDirectory,'budget-context.json');
  if(fs.existsSync(contextFile)){
   const bytes=pin(contextFile),value=read(contextFile);
   assert(value.kind==='ymcc-fixture-wall-budget'&&value.schemaVersion===1&&key(value.runDir)===key(runDirectory),'FIXED_CPP_PARENT_CONTEXT_INVALID');
   assert(Number.isSafeInteger(value.deadlineEpochMs)&&Number.isSafeInteger(value.cleanupReserveMs)&&value.cleanupReserveMs>0,'FIXED_CPP_PARENT_DEADLINE_REQUIRED');
   deadline=Math.min(deadline,value.deadlineEpochMs-value.cleanupReserveMs);parentContext=bytes;
  }
 }
 assert(deadline>started,'FIXED_CPP_PARENT_BUDGET_EXHAUSTED');
 return {parentStartedAtEpochMs:started,parentBudgetMs:deadline-started,deadlineEpochMs:deadline,cleanupReserveMs,parentContext};
}

export function verifyFixedArtifactOwnedInputs(terminal,raw,sourcePins,toolPins){
 assert(raw.collectionComplete===true&&raw.declaredPinsUnchanged===true&&raw.logsClosedAndFlushed===true,'FIXED_CPP_COMPLETE_OWNED_COLLECTION_REQUIRED');
 assert(raw.job?.activeProcessesFinal===0&&raw.job.zeroObservedBeforeDispose===true&&raw.job.cleanupError===null,'FIXED_CPP_OWNED_JOB_NOT_ZERO');
 assert(raw.business?.atomicJobAdmission===true&&raw.business.pid===terminal.pid,'FIXED_CPP_ATOMIC_PROCESS_OWNERSHIP_REQUIRED');
 assert(raw.timedOut===false&&raw.cancelled===false&&raw.ownerLost===false&&raw.residualDescendants===false&&raw.job.terminateRequested===false,'FIXED_CPP_NATURAL_OWNED_EXIT_REQUIRED');
 const expected=[...sourcePins.map(row=>({role:'sources',...row})),...toolPins.map(row=>({role:'tools',...row}))];
 const identity=r=>JSON.stringify([key(r.path),r.bytes,r.sha256]);
 assert.deepEqual(raw.declaredInputsBefore.map(r=>[r.role,identity(r)]),expected.map(r=>[r.role,identity(r)]),'FIXED_CPP_EXACT_BEFORE_INPUTS_REQUIRED');
 assert.deepEqual(raw.declaredInputsAfter.map(identity),expected.map(identity),'FIXED_CPP_EXACT_AFTER_INPUTS_REQUIRED');
 return {kind:'FIXED_CPP_EXACT_NESTED_INPUTS_OWNED_BEFORE_AND_AFTER_V1',sourceRows:sourcePins.length,toolRows:toolPins.length,physicalInputs:new Set(expected.map(r=>key(r.path))).size,
  actualProcessPid:terminal.pid,allDeclaredBytesCheckedBeforeAndAfter:true,scope:FIXED_CPP_SCOPE,toolClosureComplete:false,fullSourceApproved:false,cacheAcceptance:false};
}

export async function executeTerminal26ArtifactPipe(root,role='positive',repo=REPO){
 const started=Date.now();assert(Object.hasOwn(FIXED_CPP_PIPE_IDS,role),'FIXED_CPP_EXACT_PIPE_ROLE_REQUIRED');
 root=path.resolve(root);assert(!fs.existsSync(root),'FIXED_CPP_FRESH_EVIDENCE_REQUIRED');plain(path.dirname(root));fs.mkdirSync(root);
 const id=FIXED_CPP_PIPE_IDS[role];let phase='source-context',terminal=null,runtime=null,context=null,inputView=null;
 const result={schemaVersion:1,id,case:role,status:'FAIL',passed:false,scope:FIXED_CPP_SCOPE,startedAt:new Date(started).toISOString(),
  lifecycleVersion:'VC-LIFECYCLE-20261020-R3',cacheAcceptance:false,toolClosureComplete:false,currentPrimaryAccepted:false,fullSourceCurrent:false,fullCoordinatorRuntimeAccepted:false,fullSourceApproved:false,fullProductAcceptance:false,
  claims:{fixedCompiledCppSoftwareExecuted:false,physicalHardwareValidated:false,operatingSystemSleepPerformed:false,installedApplicationExercised:false},errors:[]};
 try{
  context=openTerminal26ArtifactContext(role,repo);inputView=terminal26ArtifactInputs(context);
  const meta=inputView.metadata;assert(meta&&meta.profile&&meta.layout&&meta.native&&meta.managed,'FIXED_CPP_VERIFIED_CONTEXT_METADATA_REQUIRED');
  const {profile,layout,managed}=meta,n=meta.native;
  assert(profile.scope===FIXED_CPP_SCOPE&&profile.cacheAcceptance===false&&profile.toolClosureComplete===false&&profile.currentPrimaryAccepted===false,'FIXED_CPP_LIMITED_SCOPE_REQUIRED');
  const options={managedInputContract:profile.managedInputContract,nativeSourceProfile:n.nativeSourceProfile,sourceCaptureManifestSha256:profile.sourceCaptureManifestSha256,mutation:n.mutation};
  const expected=current26FixedExpected(role,options),parent=fixedArtifactLeafBudget({root,id,started,leafBudgetMs:profile.leafBudgetMs,cleanupReserveMs:profile.cleanupReserveMs});
  const nonce=randomBytes(8).toString('hex');runtime=terminal26PublicRuntimeDirectory(n,nonce);const owner=path.dirname(runtime);
  assert(inside(profile.authorizedBuildRoot,runtime),'FIXED_CPP_RUNTIME_OUTSIDE_OWNER');
  if(!fs.existsSync(owner)){plain(path.dirname(owner));fs.mkdirSync(owner);}plain(owner);fs.mkdirSync(runtime);plain(runtime);
  const rawDirectory=path.join(path.dirname(n.native.exe.path),'j'+nonce);
  // The compiled entry requires an empty root, so TEMP is its own sibling.
  const privateTemp=path.join(path.dirname(n.native.exe.path),'t'+nonce);fs.mkdirSync(privateTemp);
  assert(path.join(rawDirectory,'stdout.raw').length<260&&path.join(rawDirectory,'stderr.raw').length<260,'FIXED_CPP_WIN32_RAW_OUTPUT_PATH_LIMIT');
  const physicalPins=new Map(),sources=new Map(),tools=new Map();
  for(const row of inputView.inputPins??[])mergeFixedArtifactPin(new Map(),physicalPins,row);
  for(const row of inputView.sourcePins??[])mergeFixedArtifactPin(sources,physicalPins,row);
  for(const row of inputView.artifactPins??[])mergeFixedArtifactPin(tools,physicalPins,row);
  for(const row of inputView.inputPins??[])assert(sources.has(key(row.path))||tools.has(key(row.path)),'FIXED_CPP_UNCLASSIFIED_NESTED_INPUT');
  const host=path.join(repo,'tools/ymcc_sim/global/host-process.ps1');mergeFixedArtifactPin(sources,physicalPins,pin(host));
  for(const relative of fixedArtifactConsumerSources(repo)){const p=path.join(repo,relative);mergeFixedArtifactPin(sources,physicalPins,pin(p));}
  const powershell=checked(profile.powershell),node=checked(profile.node);assert.equal(key(process.execPath),key(node.path),'FIXED_CPP_PINNED_NODE_REQUIRED');
  mergeFixedArtifactPin(tools,physicalPins,checked(n.native.exe));mergeFixedArtifactPin(tools,physicalPins,powershell);mergeFixedArtifactPin(tools,physicalPins,node);
  for(const file of rawProgramLauncherDeclaration({YMCC_SIM_RAW_PROGRAM_PWSH:powershell.path}).files)mergeFixedArtifactPin(tools,physicalPins,pin(file));
  const sourcePins=[...sources.values()],toolPins=[...tools.values()];
  assert(sourcePins.length<=3072&&toolPins.length<=3072,'FIXED_CPP_ORIGINAL_OWNED_INPUT_ROLE_LIMIT');
  const nested=fresh(path.join(root,'nested-inputs.json'),{schemaVersion:1,scope:FIXED_CPP_SCOPE,role,sourcePins,artifactPins:toolPins,qualification:meta.qualification,
   historicalJavaScriptGeneratorClosureComplete:false,sourceAuthorityGranted:false,cacheAcceptance:false});
  result.sourceHashes=[{path:nested.path,sha256:nested.sha256},...sourcePins.filter(row=>inside(repo,row.path)).map(row=>({path:path.relative(repo,row.path).replaceAll('\\','/'),sha256:row.sha256}))];
  const own=spawnSync(powershell.path,['-NoLogo','-NoProfile','-NonInteractive','-Command','([Diagnostics.Process]::GetProcessById('+process.pid+')).StartTime.ToUniversalTime().Ticks.ToString()'],{encoding:'utf8',windowsHide:true,timeout:5000});
  assert.equal(own.status,0,'FIXED_CPP_OWNER_PROCESS_IDENTITY_QUERY_FAILED');assert(/^\d{17,20}$/.test(own.stdout.trim()),'FIXED_CPP_OWNER_BIRTH_REQUIRED');
  phase='native-execution';const allocated=Date.now(),remaining=parent.parentBudgetMs-(allocated-started),allocation=profile.nativeBudgetMs+profile.cleanupReserveMs;
  assert(remaining>profile.cleanupReserveMs+1000,'FIXED_CPP_PARENT_REMAINDER_EXHAUSTED');
  const deadline=allocated+Math.min(allocation,remaining),environment=terminalManagedRuntimeEnvironment(layout,{...process.env,YEMAN_POWER_CONTROL_DIR:runtime,TEMP:privateTemp,TMP:privateTemp,DOTNET_CLI_HOME:privateTemp,DOTNET_CLI_USE_MSBUILD_SERVER:'0',MSBUILDDISABLENODEREUSE:'1'});
  terminal=await startOwnedRawProducer({id,program:n.native.exe.path,args:[runtime,'full-pipe',managed.fixtureDll],cwd:runtime,env:environment,host,powershell:powershell.path,ownerStartTicks:own.stdout.trim(),directory:rawDirectory,
   stdoutPath:path.join(root,'native.stdout.raw'),stderrPath:path.join(root,'native.stderr.raw'),allocatedAtEpochMs:allocated,deadlineEpochMs:deadline,parentStartedAtEpochMs:started,parentBudgetMs:parent.parentBudgetMs,
   originalBudgetMs:allocation,parentRemainingBeforeMs:remaining,cleanupReserveMs:profile.cleanupReserveMs,sourceInputs:sourcePins,toolArtifacts:toolPins}).wait();
  result.actualProcess=terminal;assert.equal(terminal.error,null,'FIXED_CPP_NATIVE_COLLECTION_INCOMPLETE');assert.equal(terminal.timedOut,false);assert.equal(terminal.signal,null);assert.equal(terminal.exitCode,expected.expectedExit,'FIXED_CPP_ORIGINAL_NATIVE_EXIT_MISMATCH');
  const terminalPin=checked(terminal.rawExecution.terminal),rawTerminal=read(terminalPin.path);phase='nested-input-and-resource-consumer';
  result.nestedInputProof={...verifyFixedArtifactOwnedInputs(terminal,rawTerminal,sourcePins,toolPins),manifest:nested,terminal:terminalPin};
  const nativeInterval=fixedArtifactHeldProcessInterval(rawTerminal,profile.nativeBudgetMs),nativeWallMs=nativeInterval.nativeWallMs;
  const rawPin=pin(path.join(runtime,'fixture-result.json')),raw=read(rawPin.path);phase='original-semantics';
  result.semanticProof=verifyCurrent26FixedSemantics(role,raw,options);result.children=verifyCurrent26FixedChildren(role,raw,managed,terminal.pid,options);
  result.checks=raw.checks;result.cases=raw.cases.map(row=>({name:row.name,sourceOrdinal:row.sourceOrdinal,nameOccurrence:row.nameOccurrence}));
  result.nativeResult={status:raw.status,originalExitCode:terminal.exitCode,expectedSemanticFailure:role!=='positive',mutation:n.mutation,failures:raw.failures,report:rawPin};
  result.executionBudget={leafBudgetMs:parent.parentBudgetMs,parentContext:parent.parentContext,nativeBudgetMs:profile.nativeBudgetMs,nativeWallMs,nativeInterval,outerNativeAllocationMs:allocation,actualOuterNativeAllocationMs:deadline-allocated,cleanupReserveMs:profile.cleanupReserveMs};
  assert(Date.now()<=parent.deadlineEpochMs,'FIXED_CPP_LEAF_PARENT_DEADLINE_EXCEEDED');
  result.boundary={fullNativeCoordinatorExecuted:true,actualManagedHostPipeExecuted:true,exactOwnedChildCount:result.children.common.ownedChildren,fullOriginalAssertionAndNegativeSetPreserved:true,sourceScope:FIXED_CPP_SCOPE,
   historicalWholeJavaScriptClosureVerified:false,normalProductMainExecuted:false,realSdkLoaded:false,deviceIo:false,physicalReturnAccepted:false,osSleep:false,compiledThisRun:false,sourceCurrentnessAgainstLiveMainline:false};
  result.claims.fixedCompiledCppSoftwareExecuted=true;result.status='PASS';result.passed=true;
 }catch(error){result.failureStage=phase;result.errors.push({message:error.message,stack:error.stack});result.actualProcess=terminal;result.runtimeDirectory=runtime;}
 result.finishedAt=new Date().toISOString();result.wallMs=Date.now()-started;
 fresh(path.join(root,'result.json'),result);
 if(!result.passed)throw Error('FIXED_CPP_ARTIFACT_PIPE_FAILED:'+result.errors.map(row=>row.message).join('|'));
 return result;
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{
  const args=process.argv.slice(2);assert(args.length===4,'FIXED_CPP_EXACT_CASE_AND_EVIDENCE_ARGUMENTS_REQUIRED');const options={};
  for(let i=0;i<args.length;i+=2){assert(['--case','--evidence-dir'].includes(args[i])&&!Object.hasOwn(options,args[i])&&args[i+1]&&!args[i+1].startsWith('--'),'FIXED_CPP_ARGUMENTS_INVALID');options[args[i]]=args[i+1];}
  assert(options['--case']&&options['--evidence-dir'],'FIXED_CPP_CASE_AND_EVIDENCE_REQUIRED');
  const result=await executeTerminal26ArtifactPipe(options['--evidence-dir'],options['--case']);
  console.log(JSON.stringify({id:result.id,status:result.status,scope:result.scope,cases:result.cases.length,checks:result.checks,expectedFailures:result.nativeResult.failures.length,children:result.children.common.ownedChildren,cacheAcceptance:false}));
 }catch(error){console.error(error.stack??error.message);process.exitCode=1;}
}
