import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {runProcess} from '../../ymcc_sim/global/runner.mjs';
import {requireOwnedStep} from '../controller_game_motor_pipe/native-stage-evidence.mjs';
import {CASE_ID,FIXTURE,MOTOR_FIXTURE,MANAGED_SOURCES,REPO,WORKSPACE,TOOL_LIMIT,sourceFiles,artifactFiles,row,read,write,hash,digest,inside,need,verifyRows,verifyManaged,captureSources,verifySourceSet,loadSourceSet,once,verifyExtraNativeTools,verifyCapabilityScope} from './contract.mjs';
import {generateCombined} from './run-combined-pipe.mjs';
import {SOURCE_FAULTS} from './faults.mjs';
import {STEAM_MANAGED_FAULTS} from './steam-faults.mjs';
import {XBOX_MANAGED_FAULTS,XBOX_NATIVE_FAULTS,steamNativeFaultsForCurrent,inspectXboxIntegration} from './xbox-faults.mjs';
const CASES=['xinput-dom','xinput-fast','MSI-fast','zero-after-nonzero','quiesce-after-nonzero','source-replaced-before-worker','manual-then-current-resume','zero-during-manual','stale-host-tuple-stops-game','MSI-failed-zero-retry','unsupported-profile','revoke-during-MSI-open','source-read-expiry'];
const STEAM_CASES=CASES.filter(id=>id!=='unsupported-profile');
const XBOX_CASES=[...STEAM_CASES,'elite-raw4-XInput','elite-raw4-MSI','elite-combined-LED-and-vibration','elite-led-only-preserves-game'];
const walk=root=>fs.readdirSync(root,{withFileTypes:true}).flatMap(e=>{need(!e.isSymbolicLink(),'CASE_SYMLINK_NOT_ALLOWED');const p=path.join(root,e.name);return e.isDirectory()?walk(p):[p];});
function naturalExits(value,expectedCount){
 const exits=value.logs.filter(r=>r.event==='fixture-owned-child-exit');
 need(exits.length===expectedCount,'OWNED_CHILD_EXIT_RECORDS_MISSING');
 for(const e of exits)need(e.natural===true&&e.forcedTermination===false&&e.exitCodeKnown===true&&(e.exitCode===0||e.exitCode===5),'OWNED_CHILD_DID_NOT_NATURALLY_FINISH');
 return exits;
}
function childReceipt(child,managed,tools,{profile='sony',raw=false}={}){
 need(child.status==='OBSERVED'&&child.failure===null&&child.finalCleanupOk===true,'SOURCE_BOUND_CHILD_RECEIPT_NOT_COMPLETE');
 for(const name of ['actualProductionHostSessionConsumer','actualProductionMessageCodec','actualProductionParentProcessLifetime','actualProductionPipeLoop'])need(child[name]===true,'CHILD_NOT_ACTUAL_CONSUMER:'+name);
 for(const name of ['hasController','hasContext','hardwareIo','realSdkLoaded','productMainExecuted'])need(child[name]===false,'CHILD_BOUNDARY:'+name);
 need(child.buildReceiptSha256===managed.manifestSha256&&child.inputManifestSha256===managed.value.inputManifestSha256,'WRONG_ACTUAL_MANAGED_BUILD');
 let own=0;const ownHash=hash(managed.value.fixtureDll);
 for(const stage of ['startupBindings','finalBindings']){
  for(const group of ['sourceOriginals','sourceSnapshots','generated'])for(const r of child[stage][group])need(r.matched===true&&hash(r.path)===r.sha256,'ACTUAL_CHILD_INPUT_STALE:'+r.path);
  for(const r of child[stage].loadedAssemblies){need(hash(r.path)===r.sha256,'ACTUAL_LOADED_ASSEMBLY_STALE');if(r.ownAssembly){need(path.resolve(r.path)===path.resolve(managed.value.fixtureDll)&&r.sha256===ownHash,'ACTUAL_FIXTURE_DLL_MISMATCH');++own;}else need(tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(r.path).toLowerCase()&&t.sha256===r.sha256),'UNDECLARED_ACTUAL_RUNTIME_ASSEMBLY:'+r.path);}
 }
 const event=child.gameMotorEvents[0],expectedBytes=profile==='xbox'?(raw?4:5):63;
 need(own===2&&event.left===0x12&&event.right===0xA5&&event.bodyBytes===expectedBytes,'ACTUAL_NONZERO_SDK_FIXTURE_EVENT_MISSING');
 need(event.reportSource===(profile==='xbox'?2:profile==='steam'?1:0)&&event.reportId===(profile==='xbox'?0:profile==='steam'?235:2),'ACTUAL_PROFILE_EVENT_SOURCE_REPORT_MISMATCH');
 if(profile==='xbox'){const bytes=Buffer.from(event.payloadBase64,'base64');need(bytes.length===expectedBytes&&(raw?bytes[1]===0x12&&bytes[3]===0xA5:bytes[0]===123&&bytes[1]===6&&bytes[2]===0x12&&bytes[3]===0xA5&&(bytes[4]===2||bytes[4]===3)),'EXACT_RAW4_OR_FLAGGED5_FIXTURE_PAYLOAD_REQUIRED');}
 return{pid:child.processId,parentPid:child.parentPid,ownAssemblySha256:ownHash,entryAndExitAssembliesVerified:true,realSdkLoaded:false,hardwareIo:false};
}
function compilerClosure(value,root,tools){
 for(const c of value.configurations){need(c.buildExit===0&&c.includes.length>20&&c.linkerLibraries.length>0,'ACTUAL_NATIVE_COMPILER_OBSERVATION_MISSING');for(const p of [...c.includes,...c.linkerLibraries])need(inside(root,p)||tools.some(t=>path.resolve(t.path).toLowerCase()===path.resolve(p).toLowerCase()),'UNDECLARED_NATIVE_COMPILER_INPUT:'+p);}
}
function faultOverrides(set,dir,fault){
 fs.mkdirSync(dir,{recursive:true});const relative=fault.kind==='source'?'native/main.cpp':fault.kind==='receipt'?'native/game_feedback_receipt.h':'native/game_feedback_dispatch.h';
 const original=loadSourceSet(path.join(set.caseRoot,'source-set.json'),set.caseRoot).resolve(relative),bytes=fs.readFileSync(original,'utf8');
 const candidate=path.join(dir,path.basename(relative));fs.writeFileSync(candidate,once(bytes,fault.before,fault.after,fault.id),{flag:'wx'});
 const overrides={[relative]:candidate};write(path.join(dir,'overrides.json'),overrides);
 write(path.join(dir,'fault-provenance.json'),{...fault,original,originalSha256:hash(original),candidate,candidateSha256:hash(candidate),productionModified:false});
 return{overrides,overrideFile:path.join(dir,'overrides.json'),candidate};
}
export async function execute(root,{mode='run',prototypeManifest=null,compileContract=false}={}){
 root=path.resolve(root);need(inside(path.join(WORKSPACE,'Build'),root)&&!fs.existsSync(root),'FRESH_OWNED_BUILD_CASE_REQUIRED');
 need(['run','contract'].includes(mode),'UNKNOWN_MAINTENANCE_MODE');fs.mkdirSync(root,{recursive:true});
 const steps=[];let set=null,tools=[];
 async function step(name,exe,args,timeoutMs){
  const p=await runProcess(exe,args,{cwd:REPO,env:{...process.env,YMCC_SIM_FEEDBACK_WRITER_CASE:root},timeoutMs,outputLimit:4*1024*1024});
  fs.writeFileSync(path.join(root,name+'.stdout.log'),p.stdout??'');fs.writeFileSync(path.join(root,name+'.stderr.log'),p.stderr??'');
  const{stdout,stderr,...terminal}=p;steps.push({name,exe,args,...terminal});fs.writeFileSync(path.join(root,'process-ledger.json'),JSON.stringify({steps},null,2)+'\n');
  requireOwnedStep(root,name,p,args);return terminal;
 }
 try{
  const integration=inspectXboxIntegration(REPO);write(path.join(root,'d12-integration-state.json'),integration);
  if(!integration.ready){
   verifyExtraNativeTools();tools=artifactFiles().map(row);verifyRows(tools,'PENDING_D12_TOOL_IDENTITY');
   const observations=sourceFiles().map(relative=>{const original=path.join(REPO,relative),exists=fs.existsSync(original);if(!exists)return{relative,exists:false};const snapshot=path.join(root,'pending-input-sources',relative);fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.copyFileSync(original,snapshot);need(hash(original)===hash(snapshot),'PENDING_SOURCE_CHANGED:'+relative);return{relative,path:original,snapshot,exists:true,sha256:hash(snapshot)};});
   const pending={schemaVersion:1,id:CASE_ID,status:integration.status,passed:false,sourceMode:'current-production-source',wholeMainSha256:hash(path.join(root,'pending-input-sources/native/main.cpp')),sourceCount:observations.length,toolCount:tools.length,toolLimit:TOOL_LIMIT,observations,integration,softwareBehaviorAcceptance:false,formalGlobalAcceptance:false,actualManagedChildren:0,physicalAcceptance:false};write(path.join(root,'pending-source-observation.json'),pending);
   if(mode==='contract'){write(path.join(root,'contract-result.json'),pending);return pending;}const error=Error('CURRENT_G_D12_SOURCE_NOT_READY');error.exitCode=7;throw error;
  }
  set=captureSources(root,prototypeManifest);verifyExtraNativeTools();tools=artifactFiles().map(row);need(tools.length<=TOOL_LIMIT,'WRITER_TOOL_BOUND_EXCEEDED');
  write(path.join(root,'inputs-before.json'),{sources:set.sources,repoBaselines:set.repoBaselines,tools,mode:set.mode,sourceCount:set.sources.length,toolLimit:TOOL_LIMIT});
  const sourceSetFile=path.join(root,'source-set.json');const frozenInputs=loadSourceSet(sourceSetFile,root),steamNativeFaults=steamNativeFaultsForCurrent(fs.readFileSync(frozenInputs.resolve('native/main.cpp'),'utf8'),fs.readFileSync(frozenInputs.resolve('native/game_feedback_receipt.h'),'utf8'));const capabilityScope=verifyCapabilityScope(loadSourceSet(sourceSetFile,root));write(path.join(root,'capability-scope.json'),capabilityScope);
  if(mode==='contract'){
   const first=generateCombined({caseRoot:root,out:path.join(root,'contract-a'),sourceSetFile});
   const second=generateCombined({caseRoot:root,out:path.join(root,'contract-b'),sourceSetFile});
   const comparisons=[];
   for(const name of ['physicalMemory','template']){need(hash(first.generated[name])===hash(second.generated[name]),'PERSISTENT_GENERATOR_NOT_REPRODUCIBLE:'+name);comparisons.push({name,sha256:hash(first.generated[name]),equal:true});}
   need(hash(first.fixture)===hash(second.fixture),'FULL_COMBINED_TRANSLATION_NOT_REPRODUCIBLE');
   const main=fs.readFileSync(first.sourceFile,'utf8'),physical=fs.readFileSync(first.generated.physicalMemory,'utf8');
   for(const fault of SOURCE_FAULTS){const text=fault.kind==='source'?main:fs.readFileSync(loadSourceSet(sourceSetFile,root).resolve('native/game_feedback_dispatch.h'),'utf8');need(text.split(fault.before).length===2,'ORIGINAL_SOURCE_FAULT_ANCHOR_DRIFT:'+fault.id);if(fault.kind==='source')need(physical.split(fault.before).length===2,'PHYSICAL_FUNCTION_NOT_SAME_SOURCE_FOR_FAULT:'+fault.id);}
   for(const fault of steamNativeFaults){const relative=fault.kind==='source'?'native/main.cpp':'native/game_feedback_receipt.h',text=fs.readFileSync(loadSourceSet(sourceSetFile,root).resolve(relative),'utf8');need(text.split(fault.before).length===2,'STEAM_NATIVE_SOURCE_FAULT_ANCHOR_DRIFT:'+fault.id);if(fault.kind==='source')need(fs.readFileSync(first.fixture,'utf8').split(fault.before).length===2,'STEAM_NATIVE_BINDING_NOT_ACTUAL_SOURCE:'+fault.id);}
    const helper=fs.readFileSync(loadSourceSet(sourceSetFile,root).resolve('InputHost/HostGameOutputObservation.cs'),'utf8');for(const fault of [...STEAM_MANAGED_FAULTS,...XBOX_MANAGED_FAULTS])need(helper.split(fault.before).length===2,'STEAM_MANAGED_SOURCE_FAULT_ANCHOR_DRIFT:'+fault.id);
    for(const fault of XBOX_NATIVE_FAULTS){const relative=fault.kind==='source'?'native/main.cpp':'native/game_feedback_receipt.h',text=fs.readFileSync(frozenInputs.resolve(relative),'utf8');need(text.split(fault.before).length===2,'XBOX_NATIVE_SOURCE_FAULT_ANCHOR_DRIFT:'+fault.id);if(fault.kind==='source')need(fs.readFileSync(first.fixture,'utf8').split(fault.before).length===2,'XBOX_BINDING_NOT_ACTUAL_CURRENT_SOURCE:'+fault.id);}
    const entry=fs.readFileSync(loadSourceSet(sourceSetFile,root).resolve(FIXTURE+'/SteamCombinedEntry.cpp'),'utf8');need(entry.includes('#include "CombinedMotorTests.cpp"')&&entry.includes('root/"steam"'),'ORIGINAL_SONY_COMBINED_DEFINITIONS_AND_SEPARATE_STEAM_CASE_ROOT_REQUIRED');
    const template=fs.readFileSync(first.generated.template,'utf8');
   need(template.includes('using PhysicalMemory::InputFrameSubmission;')&&template.includes('using PhysicalMemory::t_inputFrameSubmission;')&&template.includes('using PhysicalMemory::g_inputHostGameMotorInbox;'),'COMBINED_DOES_NOT_SHARE_FRAME_OR_INBOX');
   let compilation=null;
   if(compileContract){const out=path.join(root,'compile-only');await step('compile-contract-only',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--out',out,'--source-set',sourceSetFile,'--modes','Release','--compile-only','true'],180000);compilation=read(path.join(out,'result.json'));need(compilation.status==='COMPILE_ONLY_NOT_ACCEPTANCE','CONTRACT_COMPILE_MISCLASSIFIED');compilerClosure(compilation,root,tools);}
   verifySourceSet(set);verifyRows(tools,'TOOL_DRIFT_DURING_CONTRACT_CHECK');
   const value={schemaVersion:1,id:CASE_ID,status:'CONTRACT_CHECKED_NOT_EXECUTED',passed:false,softwareBehaviorAcceptance:false,sourceMode:set.mode==='formal'?'current-production-source':'explicit-prototype-source',formalGlobalAcceptance:false,sourceSetFile,wholeMainSha256:hash(first.sourceFile),sourceCount:set.sources.length,toolCount:tools.length,generatorComparisons:comparisons,combinedTranslationSha256:hash(first.fixture),sourceFaultAnchorCount:5,steamNativeSourceFaultAnchorCount:4,steamManagedSourceFaultAnchorCount:4,xboxNativeSourceFaultAnchorCount:4,xboxManagedSourceFaultAnchorCount:8,capabilityScope,caseMembership:CASES,steamCaseMembership:STEAM_CASES,xboxCaseMembership:XBOX_CASES,originalExpectedChecks:354,steamExpectedChecks:327,xboxExpectedChecks:448,commonChecks:{originalEntryAndTransport:3,integratedPersonaAdmission:1},combinedExpectedCases:41,combinedExpectedChecks:1130,originalCombinedTestsSha256:hash(loadSourceSet(sourceSetFile,root).resolve(FIXTURE+'/CombinedMotorTests.cpp')),actualManagedChildren:0,managedFixtureBuilt:false,sourceExactSerialFunctionsGenerated:true,sameFrameAndInboxVerified:true,compileOnly:compilation?{path:path.join(root,'compile-only/result.json'),sha256:hash(path.join(root,'compile-only/result.json'))}:null,normalProductMainExecuted:false,realSdkLoaded:false,realHardwareIo:false,physicalAcceptance:false};
   write(path.join(root,'contract-result.json'),value);return value;
  }
  await step('managed-normal',process.execPath,[path.join(REPO,FIXTURE,'build-managed.mjs'),'--case-root',root,'--variant','normal','--out',path.join(root,'managed/normal')],180000);
  const built=read(path.join(root,'managed/normal/build-result.json')),managed=verifyManaged(built.manifestPath,root);need(hash(built.manifestPath)===built.manifestSha256,'MANAGED_BUILDER_MANIFEST_IDENTITY_MISMATCH');
  const baseArgs=['--case-root',root,'--source-set',sourceSetFile,'--managed-manifest',managed.manifest];
  const positiveRoot=path.join(root,'positive');await step('positive',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),...baseArgs,'--out',positiveRoot],360000);
  const positive=read(path.join(positiveRoot,'result.json'));need(positive.status==='PASS'&&positive.configurations.length===2,'COMBINED_POSITIVE_NOT_COMPLETE');compilerClosure(positive,root,tools);
  const children=[],owned=[];
  for(const c of positive.configurations){
   need(c.exit===0&&c.result.checks===1130&&JSON.stringify(c.result.cases.map(x=>x.id))===JSON.stringify([...CASES,...STEAM_CASES,...XBOX_CASES]),'SONY13_STEAM12_XBOX16_COMBINED41_1130_CONTRACT_CHANGED');
   need(c.result.cases.slice(0,13).reduce((n,x)=>n+x.checks,0)+3===354&&c.result.cases.slice(13,25).reduce((n,x)=>n+x.checks,0)===327&&c.result.cases.slice(25).reduce((n,x)=>n+x.checks,0)===448&&c.result.separatePersonaAdmissionControls===true,'ORIGINAL_SONY_STEAM_XBOX_AND_SEPARATE_PERSONA_GROUPS_CHANGED');
   const diagnostics=c.result.logs.filter(r=>r.event==='input-host-submit-stats');need(diagnostics.length>0,'ACTUAL_LOW_FREQUENCY_DIAGNOSTIC_CONSUMER_NOT_EXECUTED');for(const row of diagnostics)need(row.gameMotorDispatch?.schemaVersion===1&&row.gameMotorDispatch.physicalAcceptance===false&&row.gameMotorDispatch.hardwareWriteIssuedByQuery===false&&row.gameMotorDispatch.availabilityAuthority===false,'ACTUAL_DIAGNOSTIC_SCOPE_INVALID');
   owned.push({scope:'positive',mode:c.mode,exits:naturalExits(c.result,41)});
   for(const[index,item]of c.result.cases.entries()){need(item.passed===true,'ORIGINAL_COMBINED_CASE_FAILED');children.push({profile:index<13?'sony':index<25?'steam':'xbox',case:item.id,mode:c.mode,...childReceipt(item.managedReceipt,managed,tools,{profile:index<13?'sony':index<25?'steam':'xbox',raw:item.id.startsWith('elite-raw4-')})});}
  }
  const controlsRoot=path.join(root,'frame-admission-controls');
  await step('additional-frame-admission-controls',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',controlsRoot,'--modes','Release,Checked','--frame-controls','true'],240000);
  const controls=read(path.join(controlsRoot,'result.json'));need(controls.status==='PASS'&&controls.additionalFrameAdmissionControls===true&&controls.configurations.length===2,'ADDITIONAL_ADMISSION_CONTROL_RECEIPT_MISSING');compilerClosure(controls,root,tools);
  for(const c of controls.configurations)need(c.exit===0&&c.result.additionalControlsOnly===true&&c.result.cases.length===6&&c.result.actualCurrentMayIssueRequestCurrent===true&&c.result.managedHostStarts===0,'ADDITIONAL_ADMISSION_CONTROL_CONTRACT_FAILED');
  const personaRoot=path.join(root,'persona-admission-controls');
  await step('additional-persona-admission-controls',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',personaRoot,'--modes','Release,Checked','--persona-controls','true'],240000);
  const personaControls=read(path.join(personaRoot,'result.json'));need(personaControls.status==='PASS'&&personaControls.additionalPersonaAdmissionControls===true&&personaControls.configurations.length===2,'ADDITIONAL_PERSONA_CONTROL_RECEIPT_MISSING');compilerClosure(personaControls,root,tools);
  for(const c of personaControls.configurations)need(c.exit===0&&c.result.cases.length===7&&c.result.additionalPersonaControlsOnly===true&&c.result.managedHostStarts===0,'ADDITIONAL_PERSONA_CONTROL_CONTRACT_FAILED');
  const steamControlsRoot=path.join(root,'steam-protocol-controls');
  await step('steam-protocol-controls',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',steamControlsRoot,'--modes','Release,Checked','--steam-protocol-controls','true'],240000);
  const steamControls=read(path.join(steamControlsRoot,'result.json'));need(steamControls.status==='PASS'&&steamControls.configurations.length===2,'STEAM_PROTOCOL_CONTROL_RECEIPT_MISSING');compilerClosure(steamControls,root,tools);
  for(const c of steamControls.configurations)need(c.exit===0&&c.result.cases.length===4&&c.result.additionalSteamProtocolControlsOnly===true&&c.result.managedHostStarts===0,'STEAM_PROTOCOL_CONTROL_CONTRACT_FAILED');
  const negative=[];
  for(const fault of SOURCE_FAULTS){
   const dir=path.join(root,'faults',fault.id),prepared=faultOverrides(set,dir,fault),evidence=path.join(dir,'evidence');
   await step('negative-'+fault.id,process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),...baseArgs,'--out',evidence,'--modes','Release','--overrides',prepared.overrideFile,'--expect-rejection',fault.expected],240000);
   const value=read(path.join(evidence,'result.json'));need(value.status==='PASS'&&value.expectedSemanticRejection===fault.expected&&value.configurations.length===1,'COMPILED_SOURCE_FAULT_CONTRACT_CHANGED');compilerClosure(value,root,tools);
   const c=value.configurations[0];need(c.buildExit===0&&c.exit===1&&c.status==='EXPECTED_SEMANTIC_REJECTION'&&String(c.result.error).includes(fault.expected),'COMPILER_OR_EXTRACTION_ERROR_IS_NOT_SEMANTIC_REJECTION');
   const exits=naturalExits(c.result,c.result.completedCases.length+1);owned.push({scope:fault.id,mode:'Release',exits});
   for(const e of exits){need(inside(root,e.receipt),'FAULT_CHILD_RECEIPT_OUTSIDE_CASE');children.push({scope:fault.id,...childReceipt(read(e.receipt),managed,tools)});}
   negative.push({id:fault.id,expectedAssertion:fault.expected,kind:fault.kind,resultPath:path.join(evidence,'result.json'),resultSha256:hash(path.join(evidence,'result.json')),buildExit:0,executionExit:1,naturalChildren:exits.length});
  }
  const steamNativeNegative=[];
  for(const fault of steamNativeFaults){
   const dir=path.join(root,'steam-native-faults',fault.id),prepared=faultOverrides(set,dir,fault),evidence=path.join(dir,'evidence');
   await step('steam-native-negative-'+fault.id,process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',evidence,'--modes','Release','--steam-protocol-controls','true','--overrides',prepared.overrideFile,'--expect-rejection',fault.expected],180000);
   const value=read(path.join(evidence,'result.json'));need(value.status==='PASS'&&value.expectedSemanticRejection===fault.expected&&value.configurations.length===1,'STEAM_NATIVE_SOURCE_FAULT_CONTRACT_CHANGED');compilerClosure(value,root,tools);
   const c=value.configurations[0];need(c.buildExit===0&&c.exit===1&&c.status==='EXPECTED_SEMANTIC_REJECTION'&&String(c.result.error).includes(fault.expected),'STEAM_NATIVE_COMPILER_ERROR_IS_NOT_REJECTION');
   steamNativeNegative.push({id:fault.id,expectedAssertion:fault.expected,kind:fault.kind,resultPath:path.join(evidence,'result.json'),resultSha256:hash(path.join(evidence,'result.json')),buildExit:0,executionExit:1,managedHostStarts:0});
  }
  const xboxControlsRoot=path.join(root,'xbox-protocol-controls');
  await step('xbox-protocol-controls',process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',xboxControlsRoot,'--modes','Release,Checked','--xbox-protocol-controls','true'],240000);
  const xboxControls=read(path.join(xboxControlsRoot,'result.json'));need(xboxControls.status==='PASS'&&xboxControls.configurations.length===2,'XBOX_PROTOCOL_CONTROL_RECEIPT_MISSING');compilerClosure(xboxControls,root,tools);
  for(const c of xboxControls.configurations)need(c.exit===0&&c.result.cases.length===4&&c.result.additionalXboxProtocolControlsOnly===true&&c.result.managedHostStarts===0,'XBOX_PROTOCOL_CONTROL_CONTRACT_FAILED');
  const xboxNativeNegative=[];
  for(const fault of XBOX_NATIVE_FAULTS){
   const dir=path.join(root,'xbox-native-faults',fault.id),prepared=faultOverrides(set,dir,fault),evidence=path.join(dir,'evidence');
   await step('xbox-native-negative-'+fault.id,process.execPath,[path.join(REPO,FIXTURE,'run-combined-pipe.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',evidence,'--modes','Release','--xbox-protocol-controls','true','--overrides',prepared.overrideFile,'--expect-rejection',fault.expected],180000);
   const value=read(path.join(evidence,'result.json'));need(value.status==='PASS'&&value.expectedSemanticRejection===fault.expected&&value.configurations.length===1,'XBOX_NATIVE_SOURCE_FAULT_CONTRACT_CHANGED');compilerClosure(value,root,tools);
   const c=value.configurations[0];need(c.buildExit===0&&c.exit===1&&c.status==='EXPECTED_SEMANTIC_REJECTION'&&String(c.result.error).includes(fault.expected),'XBOX_NATIVE_COMPILER_ERROR_IS_NOT_REJECTION');
   xboxNativeNegative.push({id:fault.id,expectedAssertion:fault.expected,kind:fault.kind,resultPath:path.join(evidence,'result.json'),resultSha256:hash(path.join(evidence,'result.json')),buildExit:0,executionExit:1,managedHostStarts:0});
  }
  const managedProtocolRoot=path.join(root,'steam-managed-protocol');
  await step('steam-managed-protocol-and-source-faults',process.execPath,[path.join(REPO,FIXTURE,'run-managed-protocol-tests.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',managedProtocolRoot],540000);
  const managedProtocol=read(path.join(managedProtocolRoot,'result.json'));need(managedProtocol.status==='PASS'&&managedProtocol.managedSteamFaultCount===4&&managedProtocol.positiveAndSourceFaults.length===5,'STEAM_MANAGED_PROTOCOL_FAULT_CONTRACT_CHANGED');
  const xboxManagedRoot=path.join(root,'xbox-managed-protocol');
  await step('xbox-managed-protocol-and-source-faults',process.execPath,[path.join(REPO,FIXTURE,'run-managed-protocol-tests.mjs'),'--case-root',root,'--source-set',sourceSetFile,'--out',xboxManagedRoot,'--protocol','xbox'],810000);
  const xboxManaged=read(path.join(xboxManagedRoot,'result.json'));need(xboxManaged.status==='PASS'&&xboxManaged.managedXboxFaultCount===8&&xboxManaged.positiveAndSourceFaults.length===9,'XBOX_MANAGED_PROTOCOL_FAULT_CONTRACT_CHANGED');
  verifyManaged(managed.manifest,root);verifySourceSet(set);verifyRows(tools,'TOOLS_CHANGED_DURING_EXECUTION');
  const sourceHashes=[...set.repoBaselines.map(r=>({path:r.relative,sha256:r.sha256})),...walk(root).map(row)];
  const formal=set.mode==='formal',result={schemaVersion:1,id:CASE_ID,status:formal?'PASS':'PROTOTYPE_VERIFIED_NOT_FORMAL',passed:formal,softwareChecksPassed:true,sourceMode:formal?'current-production-source':'explicit-prototype-source',formalGlobalAcceptance:false,capabilityScope,positive:{path:path.join(positiveRoot,'result.json'),sha256:hash(path.join(positiveRoot,'result.json')),profiles:positive.configurations.map(c=>({mode:c.mode,cases:41,checks:1130,exit:0,sony:{cases:13,checks:354,caseLocalChecks:351,originalCommonChecks:3},steam:{cases:12,checks:327},xbox:{cases:16,checks:448},integratedPersonaAdmissionAssertion:1}))},additionalFrameAdmissionControls:{path:path.join(controlsRoot,'result.json'),sha256:hash(path.join(controlsRoot,'result.json')),profiles:controls.configurations.map(c=>({mode:c.mode,cases:c.result.cases.length,checks:c.result.checks,original13Case354Executed:false,managedHostStarts:0}))},additionalPersonaAdmissionControls:{path:path.join(personaRoot,'result.json'),sha256:hash(path.join(personaRoot,'result.json')),profiles:personaControls.configurations.map(c=>({mode:c.mode,cases:c.result.cases.length,checks:c.result.checks,original13Case354Executed:false,old6Case46Executed:false,managedHostStarts:0}))},additionalSteamProtocolControls:{path:path.join(steamControlsRoot,'result.json'),sha256:hash(path.join(steamControlsRoot,'result.json')),profiles:steamControls.configurations.map(c=>({mode:c.mode,cases:4,checks:c.result.checks,managedHostStarts:0}))},additionalXboxProtocolControls:{path:path.join(xboxControlsRoot,'result.json'),sha256:hash(path.join(xboxControlsRoot,'result.json')),profiles:xboxControls.configurations.map(c=>({mode:c.mode,cases:4,checks:c.result.checks,managedHostStarts:0}))},xboxCompiledNativeSourceFaults:xboxNativeNegative,xboxManagedProtocol:{path:path.join(xboxManagedRoot,'result.json'),sha256:hash(path.join(xboxManagedRoot,'result.json')),normalAndFaults:xboxManaged.positiveAndSourceFaults.map(r=>({id:r.id,status:r.status,expectedAssertion:r.expectedAssertion,buildExit:r.buildExit,executionExit:r.executionExit})),eightManagedSourceFaults:8,managedHostSessionOrPipeExecuted:false},compiledSourceFaults:negative,steamCompiledNativeSourceFaults:steamNativeNegative,steamManagedProtocol:{path:path.join(managedProtocolRoot,'result.json'),sha256:hash(path.join(managedProtocolRoot,'result.json')),normalAndFaults:managedProtocol.positiveAndSourceFaults.map(r=>({id:r.id,status:r.status,expectedAssertion:r.expectedAssertion,buildExit:r.buildExit,executionExit:r.executionExit})),fourManagedSourceFaults:4,managedHostSessionOrPipeExecuted:false},managedFixture:{manifest:managed.manifest,manifestSha256:managed.manifestSha256,dll:managed.value.fixtureDll,dllSha256:hash(managed.value.fixtureDll),sourceCount:MANAGED_SOURCES.length,actualCompileCount:managed.value.evaluatedCompilerInputs.filter(r=>r.kind==='Compile').length},ownedManagedChildExits:owned,actualLoadedChildren:children,sourceHashes,toolchainArtifacts:tools,sourceSetFile,wholeMainSha256:hash(loadSourceSet(sourceSetFile,root).resolve('native/main.cpp')),allOriginalSourceAndToolBindingsStable:true,actualWriteStopRaiiAndLowFrequencyDiagnosticsConsumer:true,full1024ToolLimitRetained:true,compilationFailureCountedAsNegative:false,normalProductMainExecuted:false,realSdkLoaded:false,realHardwareIo:false,physicalAcceptance:false,fullCoordinatorLifecycleExecuted:false,wholeProcessCpuReduction:null,boundary:'Actual Host event/decoder/mailbox/SerializeResponse/RunPipeClientAsync -> same admitted frame source token -> actual native fast/DOM/Inbox -> existing serial worker and original XInput/MSI writer against controlled memory hardware terminals. No second JSON model or silent positive candidate injection.'};
  write(path.join(root,'result.json'),result);return result;
 }catch(error){write(path.join(root,'failure.json'),{status:error.exitCode===7?'BLOCKED':'FAIL',mode:set?.mode??'formal',error:error.stack??error.message,stageFailure:error.stageFailure??null,steps,compilationFailureCountedAsNegative:false,sourceOrProductFilesEdited:false,realSdkLoaded:false,realHardwareIo:false});throw error;}
}
if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)){
 try{const args=process.argv.slice(2),opts={};for(let i=0;i<args.length;i+=2){need(['--evidence-dir','--mode','--prototype-manifest','--compile-contract'].includes(args[i])&&!Object.hasOwn(opts,args[i])&&args[i+1]!==undefined,'INVALID_MAINTENANCE_ARGUMENT');opts[args[i]]=args[i+1];}const r=await execute(opts['--evidence-dir'],{mode:opts['--mode']??'run',prototypeManifest:opts['--prototype-manifest']??null,compileContract:opts['--compile-contract']==='true'});console.log(JSON.stringify({id:r.id,status:r.status,sourceMode:r.sourceMode,formalGlobalAcceptance:r.formalGlobalAcceptance,physicalAcceptance:false}));}catch(error){console.error(error.stack??error.message);process.exitCode=error.exitCode??2;}
}
