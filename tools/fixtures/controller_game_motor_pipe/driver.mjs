import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {pathToFileURL} from 'node:url';
import {runProcess} from '../../ymcc_sim/global/runner.mjs';
import {requireOwnedStep} from './native-stage-evidence.mjs';
import {CASE_ID,FIXTURE,REPO,WORKSPACE,TOOL_LIMIT,sourceFiles,artifactFiles,row,read,write,hash,inside,need,verifyRows,verifyManaged} from './contract.mjs';
import {NATIVE_REVISION_FAULT} from './faults.mjs';
const EXPECTED_CASES=['nonzero-fast','nonzero-dom','stale-ticket','stale-host-tuple','revision-replay','unsupported-profile','zero-then-unbind','copied-callback-after-unbind','neutral-capture','no-capture'];
function walk(root){return fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'CASE_OUTPUT_LINK');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});}
function sourceRows(root){return sourceFiles().map(relative=>({...row(path.join(REPO,relative)),relative}));}
function loadedChild(item,contract,tools,root){
 const c=item.managedReceipt;
 need(item.passed&&c.status==='OBSERVED'&&c.failure===null,'MANAGED_CHILD_NOT_OBSERVED');
 for(const name of ['actualProductionPipeLoop','actualProductionMessageCodec','actualProductionParentProcessLifetime','actualProductionHostSessionConsumer','finalCleanupAttempted','finalCleanupOk'])need(c[name]===true,'MANAGED_CHILD_BOUNDARY:'+name);
 for(const name of ['productMainExecuted','hardwareIo','realSdkLoaded','hasController','hasContext'])need(c[name]===false,'MANAGED_CHILD_BOUNDARY:'+name);
 need(c.buildReceiptSha256===contract.manifestSha256&&path.resolve(c.buildReceiptPath)===path.resolve(contract.manifest),'WRONG_CHILD_BUILD');
 need(c.inputManifestSha256===contract.value.inputManifestSha256,'WRONG_CHILD_INPUT_IDENTITY');
 const ownSha=hash(contract.value.fixtureDll);let own=0;
 for(const stage of ['startupBindings','finalBindings']){
  for(const group of ['sourceOriginals','sourceSnapshots','generated'])for(const r of c[stage][group])need(r.matched===true&&hash(r.path)===r.sha256,'CHILD_ACTUAL_INPUT_STALE:'+r.path);
  for(const r of c[stage].loadedAssemblies){
   need(hash(r.path)===r.sha256,'ACTUAL_LOADED_ASSEMBLY_STALE:'+r.path);
   if(r.ownAssembly){need(path.resolve(r.path)===path.resolve(contract.value.fixtureDll)&&r.sha256===ownSha,'WRONG_ACTUAL_LOADED_FIXTURE');++own;}
   else need(tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(r.path).toLowerCase()&&t.sha256===r.sha256),'UNDECLARED_ACTUAL_RUNTIME_ASSEMBLY:'+r.path);
  }
 }
 need(own===2&&c.processId!==c.parentPid,'OWN_CHILD_IDENTITY_MISSING');
 const events=c.gameMotorEvents;need(events.length>0&&events[0].left===0x12&&events[0].right===0xA5&&events[0].bodyBytes===63,'NONZERO_SDK_FIXTURE_EVENT_MISSING');
 return{id:item.id,managedPid:c.processId,parentPid:c.parentPid,actualLoadedFixtureSha256:ownSha,buildManifestSha256:contract.manifestSha256,finalCleanupOk:true};
}
function ownedExitProof(result,expectedCount){
 const exits=result.logs.filter(e=>e.event==='fixture-owned-child-exit');
 need(exits.length===expectedCount,'OWNED_CHILD_EXIT_COUNT_MISSING');
 for(const e of exits){need(e.natural===true&&e.forcedTermination===false&&e.exitCodeKnown===true,'OWNED_CHILD_NOT_NATURALLY_EXITED');need(e.exitCode===0||e.exitCode===5,'OWNED_CHILD_UNEXPECTED_CLEANUP_EXIT');}
 return exits.map(e=>({pid:e.pid,natural:e.natural,forcedTermination:e.forcedTermination,exitCode:e.exitCode,finishObserved:e.finishObserved,receipt:e.receipt}));
}
function revisionCausality(item,stale){
 const frames=item.commands.filter(c=>c.command==='SUBMIT_FRAME'),events=item.managedReceipt.gameMotorEvents;
 need(frames.length===2&&events.length===2,'TWO_ACTUAL_EVENT_FRAMES_REQUIRED');
 for(const f of frames)need(f.ok===true&&!f.captureRejected&&f.captureReason==='current-frame'&&f.sequenceAfter===f.sequenceBefore+1&&f.transportFault===false,'REVISION_CONTROL_FRAME_MUST_BE_NEW_CURRENT_AND_ACCEPTED');
 need(frames[1].sequenceBefore===frames[0].sequenceAfter&&frames[1].fastUsed===true&&frames[1].motorFast.present===true,'REVISION_CONTROL_MUST_REACH_REAL_FAST_REPLY');
 need(events[1].sequence===events[0].sequence+1&&events[0].sdkSequence===events[1].sdkSequence,'SDK_SEQUENCE_REPEAT_MUST_NOT_BE_REQUEST_REPLAY');
 need(stale?frames[1].gameReceipt.received===frames[0].gameReceipt.received&&frames[1].gameReceipt.rejected===frames[0].gameReceipt.rejected+1:frames[1].gameReceipt.received===frames[0].gameReceipt.received+1,'REVISION_CAUSALITY_NOT_OBSERVED');
 return{newAcceptedRequestSequences:frames.map(f=>f.sequenceAfter),sdkSequence:events[0].sdkSequence,secondFastReceiptPresent:true,received:frames.map(f=>f.gameReceipt.received),rejected:frames.map(f=>f.gameReceipt.rejected),actualInboxRevisionRejection:stale};
}
export async function execute(root){
 root=path.resolve(root);need(inside(path.join(WORKSPACE,'Build'),root)&&!fs.existsSync(root),'FRESH_CASE_OWNED_BUILD_ROOT_REQUIRED');fs.mkdirSync(root,{recursive:true});
 const steps=[];let sources=[],tools=[];
 async function processStep(name,exe,args,cwd,timeoutMs=240000){
  const p=await runProcess(exe,args,{cwd,env:{...process.env,YMCC_SIM_GAME_MOTOR_CASE:root},timeoutMs,outputLimit:4*1024*1024});
  fs.writeFileSync(path.join(root,name+'.stdout.log'),p.stdout??'');fs.writeFileSync(path.join(root,name+'.stderr.log'),p.stderr??'');
  const {stdout,stderr,...terminal}=p;steps.push({name,exe,args,...terminal});fs.writeFileSync(path.join(root,'process-ledger.json'),JSON.stringify({steps},null,2)+'\n');
  requireOwnedStep(root,name,p,args);return terminal;
 }
 try{
  sources=sourceRows(root);const paths=artifactFiles();need(paths.length<=TOOL_LIMIT,'DECLARED_TOOL_BOUND_EXCEEDED');tools=paths.map(row);
  write(path.join(root,'inputs-before.json'),{sources,tools,declaredToolLimit:TOOL_LIMIT,managedSourcesNormal:22,managedSourcesStaleRevision:23});
  const contracts={};
  for(const variant of ['normal','stale-revision']){
   const out=path.join(root,'managed',variant);
   await processStep('managed-'+variant,process.execPath,[path.join(REPO,FIXTURE,'build-managed.mjs'),'--case-root',root,'--variant',variant,'--out',out],REPO,180000);
   const b=read(path.join(out,'build-result.json'));need(b.status==='BUILT','MANAGED_BUILD_INCOMPLETE');contracts[variant]=verifyManaged(b.manifestPath,root);need(hash(b.manifestPath)===b.manifestSha256,'BUILDER_MANIFEST_IDENTITY_MISMATCH');
  }
  const normalOut=path.join(root,'normal');
  await processStep('normal-native',process.execPath,[path.join(REPO,FIXTURE,'run-native.mjs'),'--case-root',root,'--out',normalOut,'--managed-dll',contracts.normal.value.fixtureDll,'--managed-manifest',contracts.normal.manifest],REPO,300000);
  const normal=read(path.join(normalOut,'RESULT.json'));need(normal.status==='PASS'&&normal.passed&&normal.sourceUnchanged&&normal.configurations.length===2,'NORMAL_MOTOR_PIPE_NOT_PASS');
  const observations=[],causality=[],ownedChildExits=[];
  for(const c of normal.configurations){
   need(['Release','Checked'].includes(c.mode)&&c.status==='PASS'&&c.exitCode===0&&c.result.checks===281,'NORMAL_EXPECTATION_CHANGED');
   need(JSON.stringify(c.result.cases.map(item=>item.id))===JSON.stringify(EXPECTED_CASES),'ORIGINAL10_CASE_MEMBERSHIP_CHANGED');
   for(const p of [...c.includes,...c.linkerLibraries])need(inside(root,p)||tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(p).toLowerCase()),'UNDECLARED_NATIVE_COMPILER_INPUT:'+p);
   ownedChildExits.push({mode:c.mode,variant:'normal',children:ownedExitProof(c.result,10)});
   for(const item of c.result.cases)observations.push({mode:c.mode,...loadedChild(item,contracts.normal,tools,root)});
   causality.push({mode:c.mode,variant:'normal',...revisionCausality(c.result.cases.find(item=>item.id==='revision-replay'),false)});
  }
  const dotnetRoot=read(path.join(REPO,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')).dotnetRoot;
  const controlRoot=path.join(root,'stale-revision-control');fs.mkdirSync(controlRoot);
  const controlProfiles=[];
  for(const c of normal.configurations){
   const exe=path.join(normalOut,c.mode,'fixture.exe');need(hash(exe)===c.executableSha256,'NORMAL_EXECUTABLE_DRIFT');
   const dir=path.join(controlRoot,c.mode);fs.mkdirSync(dir);const run=path.join(dir,'run');fs.mkdirSync(run);
   await processStep('stale-control-'+c.mode,exe,[path.join(dotnetRoot,'dotnet.exe'),contracts['stale-revision'].value.fixtureDll,run,'stale-motor-revision'],dir,120000);
   const value=read(path.join(run,'result.json'));need(value.status==='PASS'&&value.passed&&value.checks===33&&value.cases.length===1&&value.cases[0].id==='revision-replay','STALE_CONTROL_EXPECTATION_CHANGED');
   ownedChildExits.push({mode:c.mode,variant:'stale-revision-control',children:ownedExitProof(value,1)});
   observations.push({mode:c.mode,variant:'stale-revision',...loadedChild(value.cases[0],contracts['stale-revision'],tools,root)});
   causality.push({mode:c.mode,variant:'stale-revision',...revisionCausality(value.cases[0],true)});
   need(hash(exe)===c.executableSha256,'UNCHANGED_NATIVE_CONTROL_BINARY_REQUIRED');
   controlProfiles.push({mode:c.mode,status:'PASS',checks:value.checks,nativeExecutable:exe,nativeExecutableSha256:c.executableSha256,normalNativeBinaryReused:true,resultPath:path.join(run,'result.json'),resultSha256:hash(path.join(run,'result.json'))});
  }
  write(path.join(controlRoot,'result.json'),{status:'PASS',profiles:controlProfiles,causality:causality.filter(c=>c.variant==='stale-revision'),sourceFilesEdited:false,gameFeedbackForwardingProven:false,physicalAcceptance:false});
  const faultRoot=path.join(root,'native-revision-guard-removed');
  await processStep('native-revision-guard-removed',process.execPath,[path.join(REPO,FIXTURE,'run-native.mjs'),'--case-root',root,'--out',faultRoot,'--managed-dll',contracts['stale-revision'].value.fixtureDll,'--managed-manifest',contracts['stale-revision'].manifest,'--revision-guard-fault','--expect-rejection',NATIVE_REVISION_FAULT.expectedAssertion],REPO,240000);
  const fault=read(path.join(faultRoot,'RESULT.json'));need(fault.status==='PASS'&&fault.expectedSemanticRejection===NATIVE_REVISION_FAULT.expectedAssertion&&fault.configurations.length===2,'NATIVE_FAULT_EXPECTATION_CHANGED');
  for(const c of fault.configurations){ownedChildExits.push({mode:c.mode,variant:'native-guard-removed',children:ownedExitProof(c.result,1)});need(c.status==='EXPECTED_SEMANTIC_REJECTION'&&c.exitCode===1&&c.result.error.includes(NATIVE_REVISION_FAULT.expectedAssertion)&&c.executableSha256,'NATIVE_FAULT_NOT_COMPILED_SEMANTIC_REJECTION');for(const p of [...c.includes,...c.linkerLibraries])need(inside(root,p)||tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(p).toLowerCase()),'UNDECLARED_FAULT_COMPILER_INPUT:'+p);}
  for(const c of Object.values(contracts))verifyManaged(c.manifest,root);
  verifyRows(sources,'SOURCE_CHANGED_DURING_RUN');verifyRows(tools,'TOOL_CHANGED_DURING_RUN');
  const hashes=[...sources.map(r=>({path:r.relative,sha256:r.sha256})),...walk(root).map(row)];
  const result={schemaVersion:1,id:CASE_ID,status:'PASS',passed:true,normal:{path:path.join(normalOut,'RESULT.json'),sha256:hash(path.join(normalOut,'RESULT.json')),profiles:normal.configurations.map(c=>({mode:c.mode,checks:c.result.checks,cases:c.result.cases.map(item=>({id:item.id,checks:item.checks,passed:item.passed}))}))},staleRevisionControl:{path:path.join(controlRoot,'result.json'),sha256:hash(path.join(controlRoot,'result.json')),profiles:controlProfiles},nativeGuardFault:{path:path.join(faultRoot,'RESULT.json'),sha256:hash(path.join(faultRoot,'RESULT.json')),expectedAssertion:NATIVE_REVISION_FAULT.expectedAssertion,profiles:fault.configurations.map(c=>({mode:c.mode,status:c.status,exitCode:c.exitCode,error:c.result.error,executableSha256:c.executableSha256}))},managedFixtures:Object.entries(contracts).map(([variant,c])=>({variant,manifest:c.manifest,manifestSha256:c.manifestSha256,dll:c.value.fixtureDll,dllSha256:hash(c.value.fixtureDll),sourceCount:c.value.inputBindings.sources.length,evaluatedCompileCount:c.value.evaluatedCompilerInputs.filter(r=>r.kind==='Compile').length})),actualManagedChildren:observations,ownedChildExitProof:ownedChildExits,revisionCausality:causality,sourceHashes:hashes,toolchainArtifacts:tools,sourceStable:true,toolchainStable:true,actualAssemblyIdentitiesVerified:true,compilationFailureCountedAsNegative:false,old19BuilderOrPinUsed:false,boundary:{controlledSdkEventToActualDecoderMailboxSerializer:true,completeNativeRequestFastDomInboxExecuted:true,actualFrameCurrentRequestCurrentMayIssueExecuted:true,privateOwnedCrossProcessPipes:true,ownedChildNaturalExitAsserted:true,normalProductMainExecuted:false,realSdkLoaded:false,hardwareIo:false,gameFeedbackForwardingProven:false,physicalAcceptance:false,fullCoordinatorLifecycleExecuted:false,wholeProcessCpuReduction:null}};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){
  write(path.join(root,'failure.json'),{status:'FAIL',error:error.stack??error.message,stageFailure:error.stageFailure??null,steps,sourceBindings:sources,toolBindings:tools,compilationFailureCountedAsNegative:false,normalProductMainExecuted:false,realSdkLoaded:false,hardwareIo:false});throw error;
 }
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{need(process.argv.length===4&&process.argv[2]==='--evidence-dir','Use --evidence-dir ABS_FRESH_CASE_CHILD');const r=await execute(process.argv[3]);console.log(JSON.stringify({id:r.id,status:r.status,normalCases:10,normalChecksPerConfiguration:281,staleControlChecksPerConfiguration:33,guardFaultConfigurations:2,physicalAcceptance:false}));}
 catch(error){console.error(error.stack??error.message);process.exitCode=2;}
}
