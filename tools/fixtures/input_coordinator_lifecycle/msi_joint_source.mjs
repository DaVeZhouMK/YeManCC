// Source pairing for the existing two MSI policy cases. No producer is run here.
import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {extract} from '../legion_motion_consumer/extract_main.mjs';

const root='G:/YeManCC-Work/Testing/YMCC/Evidence/VC-ROG-CommonSelfHeal-20261108-194834/msi-common-cleanup-review-20261115/msi-policy-return-private-r8/revision-r5/fixture-joint-followup';
const handoffPin={path:path.join(root,'HANDOFF.json'),bytes:11811,sha256:'40266d2293f4ab84a31074fa203dc210fe36e20792b94d2a04445faef16d053f'};
const digest=bytes=>createHash('sha256').update(bytes).digest('hex');
const pin=bytes=>({bytes:bytes.length,sha256:digest(bytes)});
export function readMsiJointPin(row){
 assert(row&&path.isAbsolute(row.path),'MSI_JOINT_ABSOLUTE_SOURCE_REQUIRED');
 const stat=fs.lstatSync(row.path);assert(stat.isFile()&&!stat.isSymbolicLink(),'MSI_JOINT_ORDINARY_SOURCE_REQUIRED');
 const bytes=fs.readFileSync(row.path);
 assert.equal(bytes.length,row.bytes,'MSI_JOINT_SOURCE_SIZE:'+row.path);
 assert.equal(digest(bytes),row.sha256,'MSI_JOINT_SOURCE_SHA:'+row.path);
 return bytes;
}
function replace(text,before,after,role,edits){
 // Only the documented newline representation is adaptable. No fuzzy matching.
 if(!text.includes(before)&&before.includes('\n')){
  const crlf=before.replaceAll('\r\n','\n').replaceAll('\n','\r\n');
  if(text.includes(crlf)){before=crlf;after=after.replaceAll('\r\n','\n').replaceAll('\n','\r\n');}
 }
 assert.equal(text.split(before).length,2,'MSI_JOINT_EXACT_UNIQUE_HUNK:'+role);
 const result=text.replace(before,()=>after);
 assert.equal(result.split(after).length,2,'MSI_JOINT_UNIQUE_INVERSE:'+role);
 assert.equal(result.replace(after,()=>before),text,'MSI_JOINT_WHOLE_INVERSE:'+role);
 edits.push({role,beforeBase64:Buffer.from(before).toString('base64'),afterBase64:Buffer.from(after).toString('base64')});
 return result;
}
export function reverseMsiJointText(text,edits){
 for(const row of [...edits].reverse()){
  const after=Buffer.from(row.afterBase64,'base64').toString('utf8');
  assert.equal(text.split(after).length,2,'MSI_JOINT_REVERSE_HUNK:'+row.role);
  text=text.replace(after,()=>Buffer.from(row.beforeBase64,'base64').toString('utf8'));
 }
 return text;
}
export function loadMsiJointSource(){
 const handoff=JSON.parse(readMsiJointPin(handoffPin));
 assert.equal(handoff.sourceReady,false);assert.equal(handoff.readyForExecution,false);
 const references=[handoffPin,...handoff.references,...handoff.sourceFiles.flatMap(row=>[row.before,row.after])];
 for(const row of references)readMsiJointPin(row);
 const get=name=>{
  const row=handoff.references.find(row=>path.basename(row.path)===name);
  assert(row,'MSI_JOINT_REFERENCE_MISSING:'+name);return JSON.parse(readMsiJointPin(row));
 };
 const patch=get('SOURCE-PATCH.json'),durable=get('DURABLE-EXACT-READBACK.json'),returned=get('RETURNED-OS-LEAF-PATCH.json');
 assert.equal(patch.hunks.length,11);assert.equal(durable.edits.length,5);assert.equal(returned.edits.length,3);
 for(const row of patch.files){
  const before=readMsiJointPin(row.before).toString('utf8'),edits=[];
  let result=before;
  for(const hunk of patch.hunks.filter(hunk=>hunk.target===row.target))
   result=replace(result,hunk.before,hunk.after,hunk.symbol,edits);
  assert(Buffer.from(result).equals(readMsiJointPin(row.after)),'MSI_JOINT_OWNER_WHOLE_AFTER:'+row.target);
  assert.equal(reverseMsiJointText(result,edits),before,'MSI_JOINT_OWNER_WHOLE_BEFORE:'+row.target);
 }
 const beforeReturn=readMsiJointPin(returned.before).toString('utf8'),returnEdits=[];
 let ioSupport=beforeReturn;
 for(const hunk of returned.edits)ioSupport=replace(ioSupport,hunk.before,hunk.after,hunk.symbol,returnEdits);
 assert(Buffer.from(ioSupport).equals(readMsiJointPin(returned.after)),'MSI_JOINT_RETURNED_WHOLE_AFTER');
 assert.equal(reverseMsiJointText(ioSupport,returnEdits),beforeReturn);
 for(const row of durable.inputs)references.push(row.before);
 references.push(returned.before,returned.after);
 for(const row of references)readMsiJointPin(row);
 return {handoff,patch,durable,returned,references,ioSupport:Buffer.from(ioSupport),
  companion:readMsiJointPin(patch.newCompanion),
  authority:handoffPin,sourceReady:false,runtimeAccepted:false};
}
export function msiJointSourceFiles(){
 return [...new Set(loadMsiJointSource().references.map(row=>row.path))];
}
export function pairMsiJointFixture(text){
 const input=text,loaded=loadMsiJointSource(),edits=[];
 for(const hunk of loaded.patch.hunks.filter(row=>row.target==='original-r5/fixture.cpp'))
  text=replace(text,hunk.before,hunk.after,hunk.symbol,edits);
 const originalAssertions=value=>[...extract(value,'recoveryProtectionCases').code.matchAll(/\bcheck\([^;]*?,\s*"([^"\r\n]*)"\s*\)/g)].map(row=>row[1]);
 const current=originalAssertions(text);let at=0;
 for(const name of originalAssertions(input)){
  // The API-only success setup is explicitly replaced by the real SUBMIT oracle.
  if(name==='original MSI terminal case establishes real API success without another admission or fallback')continue;
  const found=current.indexOf(name,at);assert(found>=at,'MSI_JOINT_ORIGINAL_ASSERTION_REMOVED:'+name);at=found+1;
 }
 assert.equal(reverseMsiJointText(text,edits),input);
 return {text,companion:loaded.companion,provenance:{authority:loaded.authority,edits,
  before:pin(Buffer.from(input)),after:pin(Buffer.from(text)),exactInverse:true,
  replacesApiOnlySuccess:true,originalCasesPreserved:true,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointDurable({main,fixture,locations}){
 const loaded=loadMsiJointSource(),original={main,fixture,locations};
 const results={...original},edits={main:[],fixture:[],locations:[]};
 for(const hunk of loaded.durable.edits){
  const file=hunk.target==='before/generated/main.inert.cpp'?'main':
   hunk.target==='before/generated/fixture.cpp'?'fixture':
   hunk.target==='ORIGINAL_NATIVE/source/native/msi_foreground_isolation_runtime.h'?'locations':null;
  assert(file,'MSI_JOINT_UNKNOWN_DURABLE_ROLE');
  results[file]=replace(results[file],hunk.before,hunk.after,hunk.symbol,edits[file]);
 }
 const locationsEnd='  return {L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3)"};\n return {};\n}';
 const declaredExternal='  return {L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(3)"};\n'+
  ' if(root==R"(USB\\VID_045E&PID_02EA\\MSI_POLICY_RETURN_EXTERNAL)")\n'+
  '  return {L"PCIROOT(0)#PCI(1400)#USBROOT(0)#USB(4)"};\n return {};\n}';
 results.fixture=replace(results.fixture,locationsEnd,declaredExternal,
  'retain-original-external-USB4-location-input-in-joint',edits.fixture);
 for(const file of Object.keys(results))assert.equal(reverseMsiJointText(results[file],edits[file]),original[file]);
 return {...results,ioSupport:loaded.ioSupport,provenance:{authority:loaded.authority,edits,
  exactInverse:true,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointVisibilityGenerated({main,fixture}){
 const original={main,fixture},edits={main:[],fixture:[]};
 const change=(file,before,after,role)=>{
  if(file==='main')main=replace(main,before,after,role,edits.main);
  else fixture=replace(fixture,before,after,role,edits.fixture);
 };
 const prefix=(name,body)=>{
  const before=extract(main,name).code,brace=before.indexOf('{');assert(brace>=0);
  change('main',before,before.slice(0,brace+1)+'\n'+body+'\n'+before.slice(brace+1),name);
 };
 change('fixture','static bool fixtureMsiJointEnabled=false;',
  'static bool fixtureMsiJointEnabled=false;\nnamespace msiJointVisibility {\nstatic bool run(const std::wstring&,std::string*,DWORD*,ULONGLONG);\nstatic bool cycle(const std::wstring&,const char*);\nstatic bool supplyCycleSignal(const std::string&,std::uint64_t,std::uint64_t,const char*);\n}',
  'typed-visibility-prototypes');
 change('fixture','#include "msi_policy_return_io_support.inc"',
  '#include "msi_joint_visibility.inc"\n#include "msi_policy_return_io_support.inc"',
  'single-shared-visibility-model');
 prefix('hidHideRun','if(fixtureMsiJointEnabled && !(fixtureHealthInventoryRead && arguments==L"--dev-all"))return msiJointVisibility::run(arguments,output,exitCodeOut,absoluteDeadlineTick);');
 // Keep the actual pipe-first query. Its SDK-free Host backend must consume
 // the same state file; never hide a stale/fixed-empty reply by forcing CLI.
 change('main','if(msiPolicyReturnIo::scopeEnabled)return msiPolicyReturnIo::visibilitySnapshot(snapshot);',
  'if(msiPolicyReturnIo::scopeEnabled && !fixtureMsiJointEnabled)return msiPolicyReturnIo::visibilitySnapshot(snapshot);',
  'remove-fixed-empty-shortcut-only-in-joint');
 prefix('msiDinputOpen','if(fixtureMsiJointEnabled)return fixtureMsiJointOpenDinput();');
 prefix('ymStartExecutablePresent','if(fixtureMsiJointEnabled)return fixtureFullPipe && !fixtureManagedDll.empty() && fspath::is_regular_file(fixtureManagedDll);');
 prefix('ymccXInputSetState14',
  'if(fixtureMsiJointEnabled && msiPolicyReturnIo::scopeEnabled){\n'+
  ' if(!vibration || userIndex>=4 || vibration->wLeftMotorSpeed || vibration->wRightMotorSpeed){\n'+
  '  ++forbiddenIo;throw std::logic_error("MSI-joint-only-explicit-native-zero-rumble-input-is-admitted");\n'+
  ' }\n XINPUT_STATE observed{};return msiPolicyReturnIo::getState(userIndex,&observed);\n}');
 for(const file of ['main','fixture'])assert.equal(reverseMsiJointText(file==='main'?main:fixture,edits[file]),original[file]);
 const modelPath=fileURLToPath(new URL('./msi_joint_visibility.inc',import.meta.url));
 const model=fs.readFileSync(modelPath);
 return {main,fixture,model,modelReference:{path:modelPath,...pin(model)},
  provenance:{edits,exactInverse:true,pipeFirstQueryPreserved:true,
   oneSharedStateRequired:true,sourceReady:false,runtimeAccepted:false}};
}
const osRoot='G:/YeManCC-Work/Testing/YMCC/Evidence/BacklogAssist-20261102/Runtime105CurrentMembership-20261114/native26-routing/lifecycle-runtime-layout/real-store-original-consumer-20261116/r4-intake-20261117/visibility-joint/cycle-feedback-pnp';
const osHandoffPin={path:path.join(osRoot,'HANDOFF.json'),bytes:7754,sha256:'c8cefe9fa727943ee110dd76d7eefa88a134833d880d7338960e41dc1ce8ade2'};
export function loadMsiJointOsSource(){
 const handoff=JSON.parse(readMsiJointPin(osHandoffPin));
 assert.equal(handoff.sourceReady,false);assert.equal(handoff.readyForStaticComposition,true);
 const refs=[osHandoffPin,...handoff.outputs];for(const row of refs)readMsiJointPin(row);
 const read=name=>{
  const row=handoff.outputs.find(row=>row.name===name);assert(row,'MSI_JOINT_OS_REFERENCE:'+name);
  return JSON.parse(readMsiJointPin(row));
 };
 const patch=read('source-patch.json'),bridge=read('central-model-bridge-patch.json');
 assert.equal(patch.hunks.length,14);assert.equal(bridge.hunks.length,1);
 for(const row of patch.inputs)readMsiJointPin(row);
 const support=new Map([...patch.includes,bridge.include].map(row=>[row.integrateAs,readMsiJointPin(row)]));
 return {handoff,patch,bridge,support,references:[...refs,...patch.inputs],authority:osHandoffPin};
}
const asyncCycleCommands={physicalReleaseWindowDutyBeginRefresh:'HIDHIDE_CYCLE_BEGIN',
 physicalReleaseWindowDutyPoll:'HIDHIDE_CYCLE_POLL'};
function pairMsiJointAsyncPort(main,hunk,undo,edits){
 const command=asyncCycleCommands[hunk.symbol];assert(command);
 const pipe=`inputHostRequestLocked("${command}",`;
 const guarded=`fixtureMsiJointEnabled ? fixtureMsiJointUnsupportedAsyncCycle("${command}") : ${pipe}`;
 const role=(undo?'preserve-original-async-request:':'controlled-async-port:')+hunk.symbol;
 // Keep the archived full-source proof, but apply only its exact transport substitution
 // to the current function. BEGIN/POLL ownership and settlement remain production code.
 assert.equal(replace(hunk.before,pipe,guarded,role,[]),hunk.after);
 const current=extract(main,hunk.symbol).code;
 const updated=replace(current,undo?guarded:pipe,undo?pipe:guarded,role,[]);
 return replace(main,current,updated,role,edits);
}
export function pairMsiJointOsGenerated({main,fixture}){
 const original={main,fixture},loaded=loadMsiJointOsSource(),edits={main:[],fixture:[]};
 const apply=(file,hunk)=>{
  if(file==='main')main=replace(main,hunk.before,hunk.after,hunk.symbol??'cycle-model-bridge',edits.main);
  else fixture=replace(fixture,hunk.before,hunk.after,hunk.symbol??'cycle-model-bridge',edits.fixture);
 };
 for(const hunk of loaded.patch.hunks){
  const file=hunk.target==='before/generated/main.inert.cpp'?'main':hunk.target==='before/generated/fixture.cpp'?'fixture':null;
  assert(file,'MSI_JOINT_UNKNOWN_OS_ROLE');
  if(file==='main'&&Object.hasOwn(asyncCycleCommands,hunk.symbol))main=pairMsiJointAsyncPort(main,hunk,false,edits.main);
  else apply(file,hunk);
 }
 for(const hunk of loaded.bridge.hunks)apply('fixture',hunk);
 for(const file of ['main','fixture'])assert.equal(reverseMsiJointText(file==='main'?main:fixture,edits[file]),original[file]);
 return {main,fixture,support:loaded.support,provenance:{authority:loaded.authority,edits,
  exactInverse:true,asyncRefusalPreserved:true,originalPhysicalPathAlgorithmPreserved:true,
  feedbackScopeCoversOriginalPolicyScope:true,sourceReady:false,runtimeAccepted:false}};
}
export function observeMsiJointReturn(text){
 const original=text,edits=[];
 const apply=(before,after,role)=>{text=replace(text,before,after,role,edits);};
 apply('#include "msi_policy_return_driver.inc"',
  '#include "msi_return_observation.inc"\n#include "msi_policy_return_driver.inc"','original-observer-before-driver');
 const takeover='  if(actualMsiPolicy){\n   msiPolicyReturnIo::inventoryStage=msiPolicyReturnIo::InventoryStage::ObserveTakeover;';
 apply(takeover,'  if(actualMsiPolicy)fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnBeforeTakeover");\n'+takeover,'before-real-takeover');
 const returned='  check(g_virtualStartProtection.view(startProtectionClock).locked==beforeReturn.locked,';
 apply(returned,'  if(actualMsiPolicy){\n   fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnAfterVerifiedReturn");\n   try {cases.back()["realPolicyProducer"]=msiPolicyReturnIo::driverReceipts;cases.back()["startEvents"]=fixtureEvents;cases.back()["startTrace"]=g_startEntrySeam.trace;}catch(...){}\n  }\n'+returned,'after-original-return');
 const cleanup='if(actualMsiPolicy){fixtureMsiJointCloseRestoredHost();';
 apply(cleanup,'if(actualMsiPolicy)fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnBeforeCleanup");'+
  cleanup+'fixtureCaptureMsiReturnObservation(cases.back(),"msiReturnAfterCleanup");','around-original-OFF-cleanup');
 assert.equal(reverseMsiJointText(text,edits),original);
 return {text,provenance:{edits,exactInverse:true,originalSamplerReused:true,
  observationsAreReturnProof:false,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointDinputBinding(companion,productMain){
 const before=companion,edits=[];
 const actualOpen=extract(productMain,'msiDinputOpen').code;
 const begin=actualOpen.indexOf('    g_msiDinput.readableLogged = false;');
 const end=actualOpen.lastIndexOf('    return true;');
 assert(begin>=0&&end>begin,'MSI_JOINT_ORIGINAL_BINDING_TAIL_REQUIRED');
 const tail=actualOpen.slice(begin,end);
 assert.equal(tail.split('msiSampleReopen("pad-bound");').length,2,'MSI_JOINT_REAL_REOPEN_REQUIRED');
 const originalBinding=extract(companion,'fixtureMsiJointOpenDinput').code;
 assert(!originalBinding.includes('msiSampleReopen'),'MSI_JOINT_BINDING_ALREADY_REOPENED');
 const changedBinding=replace(originalBinding,'    return true;',tail+'    return true;',
  'original-binding-tail-and-sample-reopen',[]);
 companion=replace(companion,originalBinding,changedBinding,'complete-control-binding-tail',edits);
 assert.equal(reverseMsiJointText(companion,edits),before);
 return {companion,provenance:{edits,exactInverse:true,
  completeOriginalOpen:{...pin(Buffer.from(actualOpen))},verbatimTail:pin(Buffer.from(tail)),
  sourceReady:false,runtimeAccepted:false,
  limits:'Controlled object and declared axis range remain boundary INPUT; real enumeration and SetProperty are not tested.'}};
}
export function pairMsiJointStateLifetime(fixture,companion){
 const original={fixture,companion},edits={fixture:[],companion:[]};
 const apply=(file,before,after,role)=>{
  if(file==='fixture')fixture=replace(fixture,before,after,role,edits.fixture);
  else companion=replace(companion,before,after,role,edits.companion);
 };
 apply('fixture','#include "msi_joint_visibility_cycle_bridge.inc"',
  '#include "msi_joint_visibility_cycle_bridge.inc"\n#include "msi_joint_state_scope.inc"','state-lifetime-before-original-cases');
 apply('fixture','  FixtureMsiJointFeedbackScope jointFeedback(actualMsiPolicy);',
  '  FixtureMsiJointStateScope jointState(actualMsiPolicy);\n  FixtureMsiJointFeedbackScope jointFeedback(actualMsiPolicy);',
  'state-outlives-feedback-and-original-policy-scope');
 apply('fixture','if(actualMsiPolicy)fixtureMsiJointBegin();',
  'if(actualMsiPolicy){fixtureMsiJointPrepareState();fixtureMsiJointBegin();}',
  'explicit-initial-shared-file-and-topology-inputs');
 apply('companion','!g_msiDinputReturnOwed.load() && !g_msiCrashLoaded && !g_msiCrashStoreInvalid,',
  '!g_msiDinputReturnOwed.load() && !g_msiCrashStoreInvalid &&\n'+
  '        (!g_msiCrashLoaded || !msiCrashHasDuty(g_msiCrashRecord)),',
  'loaded-clean-cache-is-not-retained-owned-duty');
 // Keep the unconditional startup refusal until the final managed/backend
 // source admission has succeeded. These controls alone cannot grant Ready.
 apply('companion','static unsigned fixtureMsiJointBindings=0;',
  'static unsigned fixtureMsiJointBindings=0;\nstatic bool fixtureMsiJointChildrenReturned(){return fullPipeChildObservers.empty();}\n'+
  'static void fixtureMsiJointObserveState(bool returned){\n if(cases.empty())return;\n'+
  ' if(returned)cases.back()["jointVisibilityFinal"]=msiJointVisibility::document(msiJointVisibility::state);\n'+
  ' else cases.back()["jointStateCleanupUnconfirmed"]=true;\n}',
  'actual-private-child-and-model-observation-only');
 for(const file of ['fixture','companion'])assert.equal(reverseMsiJointText(file==='fixture'?fixture:companion,edits[file]),original[file]);
 const statePath=fileURLToPath(new URL('./msi_joint_state_scope.inc',import.meta.url)),state=fs.readFileSync(statePath);
 return {fixture,companion,state,stateReference:{path:statePath,...pin(state)},
  provenance:{edits,exactInverse:true,startupRefusalPreserved:true,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointAsyncSignal(main,companion){
 const original={main,companion},edits={main:[],companion:[]},os=loadMsiJointOsSource();
 for(const name of Object.keys(asyncCycleCommands)){
  const hunk=os.patch.hunks.find(row=>row.symbol===name);assert(hunk);
  main=pairMsiJointAsyncPort(main,hunk,true,edits.main);
 }
 const poll=extract(main,'physicalReleaseWindowDutyPoll').code;
 const power=poll.includes('PhysicalReleaseWindowRefreshBinding binding;')?'binding.powerGeneration':'currentPowerGeneration()';
 const before='        json receipt;\r\n        pipeOk = inputHostRequestLocked("HIDHIDE_CYCLE_POLL",';
 const after=`        json receipt;\r\n        if(fixtureMsiJointEnabled)msiJointVisibility::supplyCycleSignal(g_inputHostRunId,g_inputHostEpoch,${power},"done");\r\n        pipeOk = inputHostRequestLocked("HIDHIDE_CYCLE_POLL",`;
 main=replace(main,poll,replace(poll,before,after,'explicit-controlled-cycle-completion-at-original-poll',[]),
  'explicit-controlled-cycle-completion-at-original-poll',edits.main);
 const refusal='    throw std::logic_error("MSI-nonseam-joint-NOT_READY:original-visibility-CLI-and-PnP-cycle-leaves-missing");';
 companion=replace(companion,refusal,
  '    if(!msiJointVisibility::state.configured || !fixtureMsiJointState::active ||\n'+
  '       !fixtureMsiJointOsPortsReady() || fixtureManagedDll.empty())\n'+
  '        throw std::logic_error("MSI-nonseam-joint-NOT_READY:paired-visibility-state-or-OS-ports-missing");',
  'replace-startup-refusal-with-actual-paired-source-preconditions',edits.companion);
 for(const file of ['main','companion'])assert.equal(reverseMsiJointText(file==='main'?main:companion,edits[file]),original[file]);
 return {main,companion,provenance:{edits,exactInverse:true,
  originalBeginAndPollAndOwnerChecksPreserved:true,signal:'explicit done at original current-token poll; no device IO',
  physicalOrBusinessProofSupplied:false,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointPsWake(bytes){
 assert(Buffer.isBuffer(bytes),'MSI_JOINT_PS_WAKE_BYTES_REQUIRED');
 const loaded=loadMsiJointSource(),hunk=loaded.patch.hunks.find(row=>row.target==='tools/fixtures/input_coordinator_lifecycle/ps_wake_cases.inc');
 assert(hunk,'MSI_JOINT_PS_WAKE_HUNK_REQUIRED');
 const original=bytes.toString('utf8'),edits=[];
 const text=replace(original,hunk.before,hunk.after,hunk.symbol,edits);
 assert.equal(reverseMsiJointText(text,edits),original);
 return {bytes:Buffer.from(text),provenance:{authority:loaded.authority,edits,exactInverse:true,
  originalControlledPathLeafOnly:true,actualXinputProviderUsed:true,sourceReady:false,runtimeAccepted:false}};
}
export function pairMsiJointCurrentModeInventory(bytes){
 assert(Buffer.isBuffer(bytes),'MSI_JOINT_INVENTORY_BYTES_REQUIRED');
 const original=bytes.toString('utf8'),edits=[];let text=original;
 const before='    d.physicalIdentity = d.baseContainerDeviceInstanceId;';
 const after=String.raw`    if(fixtureMsiJointEnabled && builtin){
        const auto mode=fixtureMsiJointCommandMode();
        if(mode==2){
            d.productId=0x1902;
            d.baseContainerDeviceInstanceId=R"(USB\VID_0DB0&PID_1902\MSI_POLICY_RETURN_BUILTIN)";
            d.deviceInstanceId=R"(HID\VID_0DB0&PID_1902&MI_00&COL01\MSI_POLICY_RETURN_BUILTIN)";
            d.sourcePath=R"(\\?\HID#VID_0DB0&PID_1902&MI_00&COL01#MSI_POLICY_RETURN_BUILTIN#{4D1E55B2-F16F-11CF-88CB-001111000030})";
            d.baseContainerDevicePath=R"(\\?\USB#VID_0DB0&PID_1902#MSI_POLICY_RETURN_BUILTIN#{EC87F1E3-C13B-4100-B5F7-8B84D54260CB})";
        }else if(mode!=1)d.resolved=false;
    }
    d.physicalIdentity = d.baseContainerDeviceInstanceId;`;
 text=replace(text,before,after,'declared-enumeration-input-follows-exact-original-mode-report',edits);
 const location='    if (key == builtinRoot) return {builtinLocation};';
 text=replace(text,location,location+'\n    if(fixtureMsiJointEnabled && key==R"(USB\\VID_0DB0&PID_1902\\MSI_POLICY_RETURN_BUILTIN)")return {builtinLocation};','declared-mode2-location-input',edits);
 const port='    const int kind = !supported ? -1 : key == builtinRoot ? 0 : key == externalRoot ? 1 : -1;';
 const changed='    const bool builtinMode2=fixtureMsiJointEnabled && key==R"(USB\\VID_0DB0&PID_1902\\MSI_POLICY_RETURN_BUILTIN)";\n    const int kind = !supported ? -1 : (key == builtinRoot||builtinMode2) ? 0 : key == externalRoot ? 1 : -1;';
 text=replace(text,port,changed,'declared-mode2-internal-port-input',edits);
 assert.equal(reverseMsiJointText(text,edits),original);
 return {bytes:Buffer.from(text),provenance:{edits,exactInverse:true,
  inputBasis:'exact64B ordinary command file; MODE2 exposes declared PID1902, MODE1 exposes declared PID1901; unknown unresolved',
  mainSourceMatcherChanged:false,acceptanceOrProofAssigned:false,sourceReady:false,runtimeAccepted:false}};
}

export function pairMsiJointLifecycleDispatch(companion){
 const original=companion,edits=[];
 const signature='static void fixtureMsiJointCloseRestoredHost() {';
 const dispatch=String.raw`static void fixtureMsiJointDispatchCleanup(const char* stage,bool mustDispatch) {
    json before;
    {std::lock_guard<std::mutex> queue(g_inputHostLifecycleWorkMx);before=g_inputHostLifecycleWorkQueue;}
    const auto spawns=fullPipeSpawnCount;
    unsigned calls=0;
    for(;calls<32 && g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire);++calls)
        inputHostRunQueuedLifecycleWork();
    json after;
    {std::lock_guard<std::mutex> queue(g_inputHostLifecycleWorkMx);after=g_inputHostLifecycleWorkQueue;}
    cases.back()["jointOffLifecycleDispatches"].push_back({{"stage",stage},{"before",before},{"after",after},
        {"actualWorkerCalls",calls},{"newHostSpawns",fullPipeSpawnCount-spawns},
        {"workRequested",g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire)},
        {"workInFlight",g_inputHostLifecycleWorkInFlight.load(std::memory_order_acquire)}});
    fixtureMsiJointRequire((!mustDispatch || calls>0) && after.empty() &&
        !g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire) &&
        !g_inputHostLifecycleWorkInFlight.load(std::memory_order_acquire) &&
        fullPipeSpawnCount==spawns && !g_realStickTest.virtualTargetEnabled &&
        inputHostConfiguredPersona()=="disabled",
        "MSI-joint-OFF-runs-original-queued-owner-without-new-Host-or-stranded-work");
}`;
 companion=replace(companion,signature,dispatch+'\n\n'+signature,
  'bounded-original-queue-dispatch-without-clock-or-flag-assignment',edits);
 const before='    inputCaptureRefreshRealStickSettings(true);\n    inputHostStop("virtual-target-disabled");';
 const after=String.raw`    inputCaptureRefreshRealStickSettings(true);
    // The original settings consumer queues OFF. Schedule its ORIGINAL serial
    // worker instead of directly stopping and leaving that queued owner behind.
    // Supply only the already scheduled settle observation point, once. The
    // product's settle tick and every protection/recovery deadline remain intact.
    const auto cleanupNow=fixtureMonotonicTick();
    const auto cleanupDue=g_inputHostBuildSettleUntilTick;
    const auto cleanupWait=cleanupDue>cleanupNow?cleanupDue-cleanupNow:0;
    cases.back()["jointOffScheduledSettle"]={{"now",cleanupNow},{"due",cleanupDue},
        {"advanceMs",cleanupWait},{"protectionClockBefore",startProtectionClock}};
    startProtectionClock+=cleanupWait;fixturePowerClockOffset+=cleanupWait;
    fixtureMsiJointDispatchCleanup("admitted-off-at-original-settle",true);`;
 companion=replace(companion,before,after,'admitted-off-consumed-by-original-queued-owner',edits);
 const closing='    if(!fullPipeChildObservers.empty())\n        cases.back()["jointFinalChild"]=fullPipeChildOutcome(';
 companion=replace(companion,closing,
  '    fixtureMsiJointDispatchCleanup("after-physical-return-and-durable-discharge",false);\n'+closing,
  'observe-and-dispatch-late-original-source-return-work-before-next-case',edits);
 assert.equal(reverseMsiJointText(companion,edits),original);
 return {companion,provenance:{edits,exactInverse:true,originalQueuedOwnerDispatched:true,
  directStopReplaced:true,queueOrBusyFlagsAssigned:false,productClocksOrDeadlinesChanged:false,
  simulationClockInput:'advance once to the existing settle tick; no looping until acceptance',
  addedCaseOrDefaultGate:false,sourceReady:false,runtimeAccepted:false}};
}

export function pairMsiJointVerifierObservation(companion){
 const original=companion,edits=[];
 const observer=String.raw`// Read-only endpoints around the ORIGINAL two verification calls. No receipt,
// busy flag, clock, ticket, source or assertion is assigned by this observer.
static void fixtureMsiJointObserveVerification(const char* stage,const json& ack) {
    std::lock_guard<InputHostMutex> host(g_inputHostMx);
    namespace verification=ymcc::inputHost::switchVerificationRuntime;
    const auto view=g_virtualStartProtection.view(startProtectionClock);
    const auto latest=inputHostRecoveryHealthLatestSourceLocked();
    const auto& accepted=verification::acceptedSource;
    const auto sourceJson=[](const ymcc::input::recoveryHealth::Source& source){
        return json{{"sequence",source.sequence},{"started",source.started},{"completed",source.completed},
            {"power",source.power},{"topology",source.topology},{"identity",source.identity},
            {"live",source.live},{"stable",source.stable},{"continuity",source.continuity},
            {"route",static_cast<unsigned>(source.route)},{"incarnation",source.incarnation}};
    };
    const char* missing="unobserved";
    const bool output=verification::currentOutputProvenLocked(view,&missing);
    const bool busy=verification::busyLocked();
    json queued=json::array();
    {std::lock_guard<std::mutex> queue(g_inputHostLifecycleWorkMx);
     for(const auto& reason:g_inputHostLifecycleWorkQueue)queued.push_back(reason);}
    const auto& observation=verification::observation;
    json row={{"stage",stage},{"ack",ack},{"startProtectionClock",startProtectionClock},
        {"monotonicTick",fixtureMonotonicTick()},{"powerClockOffset",fixturePowerClockOffset},
        {"view",{{"requestId",view.requestId},{"persona",view.persona},{"phase",view.phase},
            {"locked",view.locked},{"remainingMs",view.remainingMs},{"elapsedMs",view.elapsedMs},
            {"verificationPending",view.verificationPending},{"verificationRemainingMs",view.verificationRemainingMs},
            {"candidateOutcome",view.candidateOutcome},{"fallbackAttempts",view.fallbackAttempts},
            {"inspectionStartedElapsedMs",view.inspectionStartedElapsedMs},
            {"inspectionDeadlineElapsedMs",view.inspectionDeadlineElapsedMs}}},
        {"boundTicket",g_inputHostStartProtectionBoundTicket.load(std::memory_order_acquire)},
        {"switchVerificationPending",g_inputHostSwitchVerificationPending.load(std::memory_order_acquire)},
        {"currentOutputProven",output},{"currentOutputReason",missing},{"busy",busy},
        {"busyInputs",{{"configuredPersona",inputHostConfiguredPersona()},
            {"nativeForegroundCyclePending",g_msiNativeForegroundCyclePending.load(std::memory_order_acquire)},
            {"wakeExecutionPending",inputHostWakeExecutionPending(view.requestId)},
            {"psWaitingForAdmission",g_inputHostPsEndpointFault.waitingForAdmission},
            {"msiOwnedReturnWorking",g_msiOwnedReturnWorking.load(std::memory_order_acquire)},
            {"msiTerminalReturnPending",g_msiTerminalReturnPending.load(std::memory_order_acquire)},
            {"switchExecutionReturnPending",g_inputHostSwitchExecutionReturnPending.load(std::memory_order_acquire)},
            {"lifecycleWorkRequested",g_inputHostLifecycleWorkRequested.load(std::memory_order_acquire)},
            {"lifecycleWorkInFlight",g_inputHostLifecycleWorkInFlight.load(std::memory_order_acquire)},
            {"switchRecoveryPending",g_inputHostSwitchRecoveryPending.load(std::memory_order_acquire)},
            {"recoveryPending",g_inputHostRecoveryPending.load(std::memory_order_acquire)},
            {"tupleHealPending",g_inputHostTupleHealPending.load(std::memory_order_acquire)},
            {"slotHandoffPlanActive",g_ymccSlotHandoffPlanActive.load(std::memory_order_acquire)},
            {"physicalReleaseWindowDutyActive",g_physicalReleaseWindowDutyActive.load(std::memory_order_acquire)},
            {"queuedReasons",queued}}},
        {"observation",{{"requestId",observation.requestId},{"checks",observation.checks},
            {"healthyChecks",observation.healthyChecks},{"excludedChecks",observation.excludedChecks},
            {"knownFaultChecks",observation.knownFaultChecks},{"pauseReasonMask",observation.pauseReasonMask},
            {"pauseReasonMaskSeen",observation.pauseReasonMaskSeen},
            {"pauseReasonObservedMask",observation.pauseReasonObservedMask},
            {"lastState",observation.lastState},{"lastEvidence",observation.lastEvidence}}},
        {"latestSource",sourceJson(latest)},
        {"acceptedSource",{{"requestId",accepted.requestId},{"targetRevision",accepted.targetRevision},
            {"msiModeGeneration",accepted.msiModeGeneration},
            {"context",{{"host",accepted.context.host},{"power",accepted.context.power},
                {"intent",accepted.context.intent},{"topology",accepted.context.topology},
                {"identity",accepted.context.identity},{"allowed",accepted.context.allowed}}},
            {"source",sourceJson(accepted.source)}}}};
    cases.back()["jointVerificationEndpoints"].push_back(std::move(row));
}`;
 const signature='static void fixtureMsiJointAcceptInitial(bool terminal,std::uint64_t request) {';
 companion=replace(companion,signature,observer+'\n\n'+signature,'read-only-verifier-endpoint-observer',edits);
 for(const [clock,point] of [
  ['    startProtectionClock+=9999;fixturePowerClockOffset+=9999;','9999'],
  ['    ++startProtectionClock;++fixturePowerClockOffset;','10000']]){
  const before=clock+'\n    (void)fixtureMsiJointSubmitCurrent();fullPipeVerificationStep();';
  const after=clock+'\n    ack=fixtureMsiJointSubmitCurrent();\n'+
   '    fixtureMsiJointObserveVerification("before-'+point+'-verifier",ack);\n'+
   '    fullPipeVerificationStep();\n'+
   '    fixtureMsiJointObserveVerification("after-'+point+'-verifier",ack);';
  companion=replace(companion,before,after,'observe-original-'+point+'-verifier',edits);
 }
 assert.equal(reverseMsiJointText(companion,edits),original);
 return {companion,provenance:{edits,exactInverse:true,readOnlyEndpointObservation:true,
  originalClocksCallsAndAssertionsPreserved:true,productSemanticsChanged:false,
  sourceReady:false,runtimeAccepted:false}};
}

export function pairMsiJointCancellationInputs(fixture,companion){
 const original={fixture,companion},edits={fixture:[],companion:[]};
 const apply=(file,before,after,role)=>{
  if(file==='fixture')fixture=replace(fixture,before,after,role,edits.fixture);
  else companion=replace(companion,before,after,role,edits.companion);
 };
 const declaration='static void fixtureMsiJointCloseRestoredHost();';
 apply('fixture',declaration,declaration+'\n'+
  'static std::function<void()> fixtureMsiJointBoundaryHook;\n'+
  'static std::function<bool()> fixtureMsiJointLateForwardGuard;\n'+
  'static unsigned fixtureMsiJointBoundarySpawns=0;\n'+
  'static bool fixtureMsiJointSelectCancellationCase(bool);\n'+
  'static bool fixtureMsiJointBoundaryReady(const char*);\n'+
  'static void fixtureMsiJointApplyCancellation();\n'+
  'static void fixtureMsiJointFinishCancellation(const std::string&,bool,bool,std::uint64_t);',
  'boundary-input-declarations-before-original-TU');
 const selected='  if(selectedMsi31&&!(family==0&&delay==31000ULL))continue;';
 apply('fixture',selected,selected+'\n  if(selectedMsi31&&!fixtureMsiJointSelectCancellationCase(terminal))continue;',
  'terminal-exit-case-uses-independent-original-process');
 apply('fixture','   g_startEntrySeam.modeDiscoveryHook=[&](){',
  '   fixtureMsiJointBoundaryHook=[&](){','reuse-original-lambda-at-current-command-input');
 apply('fixture','     auto next=doc;next["input"]["revision"]=1;',
  '     const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));\n'+
  '     auto next=current;next["input"]["revision"]=current["input"]["revision"].get<int>()+1;',
  'boundary-write-uses-current-ordinary-settings-baseline');
 apply('fixture','     const auto outcome=writeTarget(doc,next);',
  '     const auto outcome=writeTarget(current,next);\n'+
  '     cases.back()["boundarySettingsReceipt"]=outcome;',
  'observe-original-admitted-settings-receipt');
 const normal='  check(g_virtualStartProtection.view(startProtectionClock).locked==beforeReturn.locked,"policy return never resets or prematurely releases user protection");';
 apply('fixture',normal,'  if(actualMsiPolicy&&!selectedBoundary.empty()){\n'+
  '   fixtureMsiJointFinishCancellation(selectedBoundary,boundaryFired,actualPolicyStarted,before.requestId);\n'+
  '   fixtureMsiJointEnabled=false;fixtureMsiDurableJoint=false;fixtureFullPipe=false;\n'+
  '   continue;\n  }\n'+normal,
  'explicit-cancellation-oracles-before-unchanged-normal-only-expectations');
 const opened='    return handle; // The ORIGINAL Slot/caller owns completion/cancellation/close.';
 apply('companion',opened,
  '    if(fixtureMsiJointBoundaryHook && g_inputHostSwitchRecoveryStartRequestId &&\n'+
  '       !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire)){\n'+
  '        try{fixtureMsiJointApplyCancellation();}\n'+
  '        catch(...){if(!CloseHandle(handle))throw std::logic_error("MSI-joint-boundary-input-handle-close-failed");throw;}\n'+
  '    }\n'+opened,'actual-declared-command-discovery-input-before-original-durable-write');
 const support=String.raw`
// Five existing parameters, one current OS discovery INPUT, original product
// settings/fence/clock consumers. No acceptedSource, mode receipt or duty is set.
static bool fixtureMsiJointSelectCancellationCase(bool terminal) {
    const auto* value=std::getenv("YMCC_FIXTURE_MSI_POLICY_PHASE");
    const std::string phase=value?value:"";
    const auto boundary=msiPolicyReturnIo::selectedBoundary();
    if(boundary!="exit"){
        if(!phase.empty())throw std::invalid_argument("MSI-joint-phase-only-belongs-to-terminal-exit");
        return true;
    }
    if(phase!="pending"&&phase!="success")
        throw std::invalid_argument("MSI-joint-real-exit-requires-one-original-case-per-process");
    return terminal==(phase=="success");
}
static bool fixtureMsiJointBoundaryReady(const char* value) {
    if(!value)return false;
    const std::string boundary=value;
    return boundary=="clock"||boundary=="off"||boundary=="new-request"||
        boundary=="intent"||boundary=="exit";
}
static void fixtureMsiJointApplyCancellation() {
    fixtureMsiJointRequire(fixtureMsiJointEnabled && fixtureMsiJointBoundaryHook &&
        !ymStartSeamActive() && g_inputHostSwitchRecoveryStartRequestId &&
        !g_externalPriorityVirtualSuspended.load(std::memory_order_acquire),
        "MSI-joint-cancellation-input-reaches-actual-nonseam-policy-return");
    fixtureMsiJointLateForwardGuard=inputHostMsiExternalReturnGuard();
    fixtureMsiJointRequire(fixtureMsiJointLateForwardGuard && fixtureMsiJointLateForwardGuard(),
        "MSI-joint-cancellation-input-has-live-original-forward-permission-before-change");
    fixtureMsiJointBoundarySpawns=fullPipeSpawnCount;
    const auto before=g_virtualStartProtection.view(startProtectionClock);
    const auto intent=g_inputHostVirtualIntentRevision.load(std::memory_order_acquire);
    cases.back()["jointCancellationInput"]={{"stage","actual-command-open-before-original-durable-mode-write"},
        {"startSeamActive",ymStartSeamActive()},{"requestBefore",before.requestId},
        {"phaseBefore",before.phase},{"clockBefore",startProtectionClock},{"intentBefore",intent},
        {"scopeRequest",g_inputHostSwitchRecoveryStartRequestId},
        {"scopePower",g_inputHostSwitchRecoveryStartPower},{"scopeDeadline",g_inputHostSwitchRecoveryStartDeadline},
        {"spawnsBefore",fixtureMsiJointBoundarySpawns}};
    auto callback=std::move(fixtureMsiJointBoundaryHook);fixtureMsiJointBoundaryHook={};
    callback(); // the original selected parameter action, exactly once
    const auto after=g_virtualStartProtection.view(startProtectionClock);
    cases.back()["jointCancellationInput"].update({{"requestAfter",after.requestId},
        {"phaseAfter",after.phase},{"clockAfter",startProtectionClock},
        {"intentAfter",g_inputHostVirtualIntentRevision.load(std::memory_order_acquire)},
        {"forwardPermissionAfter",fixtureMsiJointLateForwardGuard()}});
    fixtureMsiJointRequire(!fixtureMsiJointLateForwardGuard(),
        "MSI-joint-selected-cancellation-revokes-original-forward-permission-at-input");
}
static void fixtureMsiJointFinishCancellation(const std::string& boundary,bool fired,bool started,
                                             std::uint64_t originalRequest) {
    fixtureMsiJointRequire(fired && !fixtureMsiJointBoundaryHook &&
        cases.back().contains("policyBoundary") && cases.back().contains("jointCancellationInput") &&
        cases.back()["policyBoundary"].value("kind",std::string{})==boundary &&
        !cases.back()["jointCancellationInput"].value("startSeamActive",true),
        "MSI-joint-selected-cancellation-must-actually-fire-once-on-current-path");
    unsigned results=0;json result;
    for(const auto& event:fixtureEvents){
        if(event.value("event",std::string{})!="input-host-msi-policy-return")continue;
        const auto detail=event.value("detail",json::object());
        if(detail.value("stage",std::string{})=="result"){++results;result=detail;}
    }
    fixtureMsiJointRequire(!started && results==1 &&
        result.value("requestId",std::uint64_t{})==originalRequest && !result.value("startReturned",true),
        "MSI-joint-cancelled-original-return-has-one-real-negative-Start-result");
    fixtureMsiJointRequire(fixtureMsiJointLateForwardGuard && !fixtureMsiJointLateForwardGuard() &&
        fullPipeSpawnCount==fixtureMsiJointBoundarySpawns && !g_inputHostProcess &&
        fullPipeChildObservers.empty() && fixtureMsiJointCommandMode()!=2 &&
        !g_msiDinputReturnOwed.load(std::memory_order_acquire) && !g_msiCrashRecord.modeReturnPending,
        "MSI-joint-late-old-permission-cannot-create-Host-or-new-mode2-duty");
    const auto view=g_virtualStartProtection.view(startProtectionClock);
    const auto& input=cases.back()["jointCancellationInput"];
    if(boundary=="off"){
        fixtureMsiJointRequire(view.requestId!=originalRequest && view.persona=="disabled" &&
            inputHostConfiguredPersona()=="disabled" && !g_realStickTest.virtualTargetEnabled,
            "MSI-joint-admitted-OFF-owns-new-request-and-old-return-cannot-resurrect-output");
    }else if(boundary=="new-request"){
        fixtureMsiJointRequire(view.requestId!=originalRequest && view.persona=="elite" &&
            inputHostConfiguredPersona()=="elite" && g_realStickTest.virtualTargetEnabled,
            "MSI-joint-new-request-retains-own-persona-and-is-not-executed-by-old-return");
    }else if(boundary=="exit"){
        fixtureMsiJointRequire(inputHostExitFenceRaised() && view.requestId==originalRequest,
            "MSI-joint-exit-fence-remains-raised-without-new-player-admission");
    }else{
        fixtureMsiJointRequire(view.requestId==originalRequest && view.fallbackAttempts==0 &&
            inputHostConfiguredPersona()=="steamdeck",
            "MSI-joint-clock-or-publication-revocation-keeps-original-player-and-zero-fallback");
        if(boundary=="clock")fixtureMsiJointRequire(
            input["clockAfter"].get<std::uint64_t>()-input["clockBefore"].get<std::uint64_t>()==3500 &&
            input["clockAfter"].get<std::uint64_t>()>=input["scopeDeadline"].get<std::uint64_t>(),
            "MSI-joint-clock-input-exceeds-only-original-policy-window-without-renewal");
        else fixtureMsiJointRequire(input["intentAfter"].get<std::uint64_t>()>
            input["intentBefore"].get<std::uint64_t>(),
            "MSI-joint-publication-refresh-revokes-old-forward-scope");
    }
    cases.back()["jointCancellationResult"]={{"boundary",boundary},{"actualReturnResult",result},
        {"lateForwardPermission",fixtureMsiJointLateForwardGuard()},{"newHostSpawns",fullPipeSpawnCount-fixtureMsiJointBoundarySpawns},
        {"requestBeforeCleanup",view.requestId},{"phaseBeforeCleanup",view.phase}};
    if(boundary!="exit"){
        if(inputHostConfiguredPersona()!="disabled"){
            // A newer admitted request retains its original30s protection.
            // Cleanup reaches that existing endpoint once; no early OFF bypass.
            const auto pending=g_virtualStartProtection.view(startProtectionClock);
            const auto wait=pending.locked?pending.remainingMs:0;
            cases.back()["jointCancellationResult"]["cleanupAdmissionAdvanceMs"]=wait;
            startProtectionClock+=wait;fixturePowerClockOffset+=wait;
            const auto current=json::parse(ymSettingsReadRaw(YM_SETTINGS_FILE));
            const auto off=targetDocument("disabled",current["input"]["revision"].get<int>()+1);
            fixtureMsiJointRequire(writeTarget(current,off).value("ok",false),
                "MSI-joint-cancellation-cleanup-uses-real-admitted-OFF");
            inputCaptureRefreshRealStickSettings(true);
        }
        const auto now=fixtureMonotonicTick(),due=g_inputHostBuildSettleUntilTick;
        const auto wait=due>now?due-now:0;
        startProtectionClock+=wait;fixturePowerClockOffset+=wait;
        fixtureMsiJointDispatchCleanup("cancelled-return-admitted-OFF",true);
    }else{
        // Actual exit INPUT and the original producer/Host owners. Their real
        // receipts publish retirement; the fixture never assigns returned bits.
        g_exitRequested.store(true,std::memory_order_release);
        inputHostQuiesceForCaptureExit();
        fixtureMsiJointRequire(inputCaptureAwaitStopForExit() &&
            inputCaptureExitProducerOwnershipReleased() && inputCaptureExitOwnershipReleased(),
            "MSI-joint-exit-runs-original-capture-retirement-and-proves-producer-return");
        fixtureMsiJointRequire(inputHostTryOrdinaryStopForExit(),
            "MSI-joint-exit-original-owner-attempts-app-exit-stop");
        fixtureMsiJointRequire(inputHostRetireLocalResourcesForExit() && inputHostExitLocalOwnershipReleased(),
            "MSI-joint-exit-original-owner-returns-process-primary-and-pipe");
        cases.back()["jointCancellationResult"]["actualExitOwner"]={
            {"exitRequested",g_exitRequested.load()},{"fenceRaised",inputHostExitFenceRaised()},
            {"captureProducerReturned",inputCaptureExitProducerOwnershipReleased()},
            {"captureOwnershipReturned",inputCaptureExitOwnershipReleased()},
            {"HostLocalOwnershipReturned",inputHostExitLocalOwnershipReleased()},
            {"phase",std::string(std::getenv("YMCC_FIXTURE_MSI_POLICY_PHASE"))},
            {"scope","controlled capture and owned Host retirement;not complete application exit"}};
        for(unsigned i=0;i<32&&g_inputHostLifecycleWorkRequested.load();++i)inputHostRunQueuedLifecycleWork();
    }
    for(unsigned i=0;i<ymcc::msi::ownedReturn::Recovery::maxObservations && g_msiDinputReturnOwed.load();++i){
        startProtectionClock+=ymcc::msi::ownedReturn::Recovery::observationIntervalMs;
        fixturePowerClockOffset+=ymcc::msi::ownedReturn::Recovery::observationIntervalMs;
        fixtureMsiJointReturnedRead();
    }
    if(boundary!="exit"){
        fixtureMsiJointDispatchCleanup("cancelled-return-after-physical-discharge",false);
        const auto closing=g_virtualStartProtection.view(startProtectionClock);
        if(closing.verificationPending){startProtectionClock+=closing.verificationRemainingMs;
            fixturePowerClockOffset+=closing.verificationRemainingMs;fullPipeVerificationStep();}
        fixtureMsiJointRequire(g_virtualStartProtection.view(startProtectionClock).phase=="success" &&
            !g_virtualStartProtection.view(startProtectionClock).locked,
            "MSI-joint-cancellation-OFF-completes-original-verifier-before-next-case");
    }
    json queue;
    {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);queue=g_inputHostLifecycleWorkQueue;}
    fixtureMsiJointRequire(queue.empty() && !g_inputHostLifecycleWorkRequested.load() &&
        !g_inputHostLifecycleWorkInFlight.load() && !g_inputHostProcess &&
        !g_inputHostPrepared && !g_inputHostReleaseQuarantined && fullPipeChildObservers.empty() &&
        !g_hidHideVisibilityJournal.active && !g_msiDinputReturnOwed.load() &&
        !g_msiDinputRouteDinputArmed.load() && !g_msiCrashRecord.modeReturnPending &&
        !fixtureMsiJointFeedbackDebt.load() && fullPipeSpawnCount==fixtureMsiJointBoundarySpawns,
        "MSI-joint-cancellation-returns-all-owned-work-before-independent-next-case");
    cases.back()["jointCancellationResult"].update({{"cleanupQueue",queue},{"cleanupComplete",true},
        {"lateForwardPermissionAfterCleanup",fixtureMsiJointLateForwardGuard()},
        {"finalPhase",g_virtualStartProtection.view(startProtectionClock).phase}});
    fixtureMsiJointLateForwardGuard={};
}
`;
 companion+=support;
 edits.companion.push({role:'explicit-original-cancellation-oracles-and-cleanup',beforeBase64:'',afterBase64:Buffer.from(support).toString('base64')});
 for(const file of ['fixture','companion'])assert.equal(reverseMsiJointText(file==='fixture'?fixture:companion,edits[file]),original[file]);
 return {fixture,companion,provenance:{edits,exactInverse:true,
  parameters:['clock','off','new-request','intent','exit'],originalNormalAssertionsPreserved:true,
  originalModeDiscoveryInputAtCurrentCommandOpen:true,originalProductGuardsPreserved:true,
  callbackMustFire:true,originalForwardPermissionMustBeRevoked:true,newDefaultIds:0,
  sourceReady:false,runtimeAccepted:false}};
}
