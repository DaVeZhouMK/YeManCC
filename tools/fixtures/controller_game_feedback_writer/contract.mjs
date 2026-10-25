import fs from 'node:fs';
import path from 'node:path';
import {artifactFiles as motorArtifacts,MANAGED_SOURCES as MOTOR_MANAGED_SOURCES,NATIVE_HEADERS,REPO,WORKSPACE,FIXTURE as MOTOR_FIXTURE,hash,digest,read,need,inside,verifyRows} from '../controller_game_motor_pipe/contract.mjs';
export {REPO,WORKSPACE,MOTOR_FIXTURE,hash,digest,read,need,inside,verifyRows};
export const FIXTURE='tools/fixtures/controller_game_feedback_writer';
export const MODULE='tools/ymcc_sim/global/controller-game-feedback-writer-consumer';
export const STALE_SOURCE='fault-inputs/HostGameOutputObservation.stale-revision.cs';
export function managedSourcePaths(base=MOTOR_MANAGED_SOURCES){return Object.freeze([...new Set([...base.map(r=>r===MOTOR_FIXTURE+'/build-managed.mjs'?FIXTURE+'/build-managed.mjs':r===MOTOR_FIXTURE+'/ManagedGameMotorFixture.cs'?FIXTURE+'/ManagedGameMotorFixture.cs':r===MOTOR_FIXTURE+'/ManagedEntry.cs'?FIXTURE+'/ManagedEntry.cs':r),'InputHost/PsEndpointOwnership.cs','InputHost/HostRuntimeBinaryIdentity.cs'])]);}
export const MANAGED_SOURCES=managedSourcePaths();
export const CASE_ID='controller.game-feedback-writer-consumer';
export const TOOL_LIMIT=1024;
export const PROTOTYPE_PATHS=Object.freeze(['native/main.cpp','native/game_feedback_source_ticket.h','native/game_feedback_dispatch.h']);
export const HEADERS=Object.freeze([...new Set([...NATIVE_HEADERS,
 'native/game_feedback_source_ticket.h','native/game_feedback_dispatch.h','native/game_feedback_diagnostics.h',
 'native/touch_haptic_dispatch.h','native/xinput_identity_refresh_policy.h','native/touch_feedback_lease.h',
 'native/msi_virtual_report_diagnostic.h','capabilities/HcControllerInputCapabilityCatalog.h',
 'capabilities/HcControllerInputCapabilityCatalog.generated.h','tools/fixtures/xinput_feedback_msi_platform.inc'
])]);
export const SCOPE_INPUTS=Object.freeze([
 'capabilities/hc-controller-feedback-policy.json','capabilities/CONTROLLER-CONSUMPTION.json',
 'capabilities/INTEGRATION-STATUS.json','capabilities/EXECUTION-CHAINS.json',
 'capabilities/CONTROLLER-GAME-MOTOR-RECEIPT.json','capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json','capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json','capabilities/hc-device-catalog.json',
 'tools/hc_capability_catalog_generate.mjs','tools/lib/hc_controller_feedback_catalog.mjs'
]);
const ORIGINAL_CONSUMERS=[
 'tools/xinput_identity_feedback_lifecycle_selftest.mjs','tools/fixtures/xinput_identity_feedback_lifecycle_fixture.cpp',
 'tools/fixtures/xinput_identity_feedback_context.mjs',
 MOTOR_FIXTURE+'/NativeEntry.cpp',MOTOR_FIXTURE+'/lifecycle-context.mjs',MOTOR_FIXTURE+'/target-return-context.mjs',MOTOR_FIXTURE+'/cold-return-context.mjs',MOTOR_FIXTURE+'/native-stage-evidence.mjs','native/virtual_switch_verification_runtime.h','native/virtual_tuple_recovery_runtime.h','tools/fixtures/legion_motion_consumer/extract_main.mjs',
 'tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs',
 'tools/lib/hc_motion_matrix_guard.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
];
const LOCAL=['ManagedEntry.cs','XboxCombinedMotorTests.cpp','XboxManagedProtocolTests.cs','XboxProtocolControls.cpp','xbox-faults.mjs','prepare-xbox-fixture.mjs','build-managed.mjs','ManagedGameMotorFixture.cs','SteamCombinedEntry.cpp','SteamProtocolControls.cpp','SteamManagedProtocolTests.cs','steam-faults.mjs','run-managed-protocol-tests.mjs','contract.mjs','faults.mjs','prepare-physical-fixture.mjs','prepare-combined-fixture.mjs',
 'refine-physical-bridge.mjs','run-combined-pipe.mjs','driver.mjs','PersonaAdmissionControls.cpp','FrameAdmissionControls.cpp','CombinedMotorTests.cpp','PhysicalTestSupport.inc','native-extra-tool-inputs.json'];
const INFRASTRUCTURE=[MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/README.md',
 'tools/ymcc_sim/global/controller-game-motor-pipe-consumer/owned-build-bridge.mjs',
 'tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs','tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json',
 'tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/controller-lifecycle-pipe/native-tool-inputs.json',
 'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'];
export const sourceFiles=()=>[...new Set([...MANAGED_SOURCES,'native/main.cpp','deps/json/json.hpp',...HEADERS,...SCOPE_INPUTS,...ORIGINAL_CONSUMERS,...LOCAL.map(p=>FIXTURE+'/'+p),...INFRASTRUCTURE])];
export function artifactFiles(){const extra=read(path.join(REPO,FIXTURE,'native-extra-tool-inputs.json')).files;const files=[...new Set([...motorArtifacts(),...extra.map(r=>path.resolve(r.path))])];need(files.length<=TOOL_LIMIT,'WRITER_TOOL_CLOSURE_EXCEEDS1024');return files;}
export function verifyExtraNativeTools(){const extra=read(path.join(REPO,FIXTURE,'native-extra-tool-inputs.json'));need(extra.schemaVersion===1&&extra.files.length===10,'REVIEWED_NATIVE_EXTRA_TOOL_MEMBERSHIP');for(const r of extra.files)need(hash(r.path)===r.sha256,'REVIEWED_NATIVE_EXTRA_TOOL_IDENTITY_CHANGED:'+r.path);}
export const row=p=>({path:path.resolve(p),sha256:hash(p)});
export function write(p,value){fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
export const once=(text,before,after,label)=>{need(text.split(before).length===2,'SOURCE_GENERATOR_ANCHOR_DRIFT:'+(label??before));return text.replace(before,after);};
export function loadSourceSet(file,caseRoot){
 const set=read(file);need(set.schemaVersion===1&&['formal','prototype'].includes(set.mode),'SOURCE_SET_SCHEMA');
 need(path.resolve(set.caseRoot)===path.resolve(caseRoot)&&inside(caseRoot,file),'SOURCE_SET_CASE_OWNERSHIP');
 need(set.sources.length===sourceFiles().length&&JSON.stringify(set.sources.map(r=>r.relative))===JSON.stringify(sourceFiles()),'WRITER_SOURCE_MEMBERSHIP_DRIFT');
 for(const r of set.sources)need(inside(caseRoot,r.snapshot)&&hash(r.snapshot)===r.sha256,'FROZEN_SOURCE_CHANGED:'+r.relative);
 return{...set,resolve(relative){const r=set.sources.find(r=>r.relative===relative);need(r,'UNDECLARED_SOURCE_DEPENDENCY:'+relative);return r.snapshot;}};
}
export function captureSources(caseRoot,prototypeManifest){
 const configured=prototypeManifest?read(prototypeManifest):null;
 if(configured){need(process.env.YMCC_SIM_GLOBAL_RUN!=='1','PROTOTYPE_FORBIDDEN_IN_GLOBAL_PRODUCER');need(configured.mode==='prototype'&&configured.schemaVersion===1,'EXPLICIT_PROTOTYPE_SCHEMA_REQUIRED');need(JSON.stringify(configured.inputs.map(r=>r.relative).sort())===JSON.stringify([...PROTOTYPE_PATHS].sort()),'EXACT_PROTOTYPE3_PATHS_REQUIRED');}
 const sources=[],repoBaselines=[],external=[];
 for(const relative of sourceFiles()){
  const actual=path.join(REPO,relative),override=configured?.inputs.find(r=>r.relative===relative),file=override?override.file:actual;
  if(!fs.existsSync(file)){const e=Error('FORMAL_PRODUCT_INPUT_MISSING:'+relative);e.exitCode=7;throw e;}
  const bytes=fs.readFileSync(file),sha256=digest(bytes),snapshot=path.join(caseRoot,'input-sources',relative);
  if(override){need(path.isAbsolute(file)&&sha256===override.sha256,'PROTOTYPE_INPUT_IDENTITY_MISMATCH:'+relative);external.push({relative,file:path.resolve(file),sha256});}
  fs.mkdirSync(path.dirname(snapshot),{recursive:true});fs.writeFileSync(snapshot,bytes,{flag:'wx'});
  sources.push({relative,path:path.resolve(file),snapshot,sha256,bytes:bytes.length,origin:override?'explicit-prototype-candidate':'current-repository'});
  if(fs.existsSync(actual))repoBaselines.push({relative,path:actual,sha256:hash(actual)});
 }
 const set={schemaVersion:1,mode:configured?'prototype':'formal',caseRoot,sources,repoBaselines,externalPrototypeInputs:external,prototypeManifest:prototypeManifest?{path:prototypeManifest,sha256:hash(prototypeManifest)}:null};
 write(path.join(caseRoot,'source-set.json'),set);return set;
}
export function verifySourceSet(set){
 for(const r of set.sources){need(hash(r.snapshot)===r.sha256,'FROZEN_SNAPSHOT_STALE:'+r.relative);need(hash(r.path)===r.sha256,'ORIGINAL_SOURCE_CHANGED:'+r.relative);}
 for(const r of set.repoBaselines)need(hash(r.path)===r.sha256,'REPOSITORY_BASELINE_CHANGED:'+r.relative);
 if(set.prototypeManifest)need(hash(set.prototypeManifest.path)===set.prototypeManifest.sha256,'PROTOTYPE_MANIFEST_CHANGED');
}
export function nativeSourceFile(sourceSet,relative,overrides={}){const candidate=overrides[relative];if(candidate){need(inside(sourceSet.caseRoot,candidate),'PRIVATE_FAULT_OUTSIDE_CASE');return candidate;}return sourceSet.resolve(relative);}

export function verifyCapabilityScope(sourceSet){
 const resolve=relative=>sourceSet.resolve(relative),policy=read(resolve('capabilities/hc-controller-feedback-policy.json'));
 for(const key of ['gameOutputCallbackBound','allControllerFeedbackConnected','physicalAcceptance','gameFeedbackForwardingProven'])need(policy[key]===false,'BROAD_CAPABILITY_FLAG_PROMOTED:'+key);
 const scope=policy.gameMotorSoftwareConsumption;
 need(scope?.sourceConnected===true&&scope.capabilityId==='controller.feedback.game.dualsense-edge-usb-classic'&&scope.physicalAcceptance===false&&scope.wholeProcessCpuReduction===null,'EXACT_SOFTWARE_SCOPE_MISSING');
 need(scope.supportedVirtualTargets.length===1&&scope.supportedVirtualTargets[0].profileIdentity==='dualsense-edge'&&scope.supportedVirtualTargets[0].protocol==='dualsense-usb-classic-v1'&&scope.supportedVirtualTargets[0].reportSource===0&&scope.supportedVirtualTargets[0].reportId===2&&scope.supportedVirtualTargets[0].bodyBytes===63,'EXACT_ORIGINAL_SONY_TARGET_SCOPE_REQUIRED');
 const steam=read(resolve('capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json')),additional=policy.additionalGameMotorSoftwareConsumption;
  need(steam.schemaVersion===1&&steam.capabilityId==='controller.feedback.game.steam-deck-usb-classic-rumble'&&steam.profileIdentity==='steam-deck-composite'&&steam.protocol==='steam-deck-usb-classic-rumble-v1'&&steam.protocolToken===2&&steam.compatibilityToken===3&&steam.physicalAcceptance===false&&steam.wholeProcessCpuReduction===null&&steam.softwareAcceptanceEvidenceRecordedSeparately===true,'EXACT_SEPARATE_STEAM_RECEIPT_CONTRACT_REQUIRED');
  need(steam.wire.reportSource===1&&steam.wire.reportId===235&&steam.wire.bodyBytes===63&&steam.wire.leftBodyOffset===4&&steam.wire.rightBodyOffset===6&&steam.wire.motorWordBytes===2&&steam.wire.byteOrder==='little-endian'&&steam.wire.zeroPairMeansStop===true,'STEAM_FROZEN_WIRE_SCOPE_CHANGED');
  need(Array.isArray(additional)&&additional.length===2&&JSON.stringify(additional.map(x=>x.capabilityId))===JSON.stringify([steam.capabilityId,'controller.feedback.game.xbox-elite-v2-xinput-classic'])&&additional[0].capabilityId===steam.capabilityId&&additional[0].profileIdentity===steam.profileIdentity&&additional[0].protocol===steam.protocol&&additional[0].sourceConnected===true&&additional[0].contract==='capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json'&&additional[0].physicalAcceptance===false&&additional[0].wholeProcessCpuReduction===null&&additional[0].softwareAcceptanceEvidenceRecordedSeparately===true,'SEPARATE_STEAM_SOFTWARE_SCOPE_MISSING');
  need(JSON.stringify(steam.supportedPhysicalSources)===JSON.stringify(['controller.feedback.xinput.physical','controller.feedback.msi-dinput.physical'])&&JSON.stringify(additional[0].supportedPhysicalSources)===JSON.stringify(steam.supportedPhysicalSources),'STEAM_PHYSICAL_SOURCE_SCOPE_WIDENED');
 const xbox=read(resolve('capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json')),xboxScope=additional[1];
 need(xbox.schemaVersion===1&&xbox.capabilityId==='controller.feedback.game.xbox-elite-v2-xinput-classic'&&xbox.profileIdentity==='xbox-elite-v2'&&xbox.protocol==='xbox-elite-v2-xinput-classic-v1'&&xbox.protocolToken===3&&xbox.compatibilityToken===4&&xbox.physicalAcceptance===false&&xbox.wholeProcessCpuReduction===null&&xbox.softwareAcceptanceEvidenceRecordedSeparately===true,'EXACT_XBOX_RECEIPT_CONTRACT_REQUIRED');
 need(xbox.wire.reportSource===2&&xbox.wire.reportId===0&&JSON.stringify(xbox.wire.acceptedBodyBytes)===JSON.stringify([4,5])&&xbox.wire.raw4.leftWordOffset===0&&xbox.wire.raw4.rightWordOffset===2&&xbox.wire.flagged5.virtualIndexOffset===0&&xbox.wire.flagged5.ledOffset===1&&xbox.wire.flagged5.leftByteOffset===2&&xbox.wire.flagged5.rightByteOffset===3&&xbox.wire.flagged5.flagsOffset===4&&xbox.wire.flagged5.vibrationMask===2&&xbox.wire.flagged5.ledMask===1&&xbox.wire.flagged5.allowedFlagsMask===3&&xbox.wire.flagged5.physicalSourceChosenByVirtualIndex===false&&xbox.wire.flagged5.withoutVibration==='NoMotorUpdate; do not replace pending motor or zero'&&xbox.wire.zeroPairMeansStop===true,'XBOX_EXACT_RAW4_FLAGGED5_NO_LED_MOTOR_AUTHORITY_REQUIRED');
 need(xboxScope.capabilityId===xbox.capabilityId&&xboxScope.profileIdentity===xbox.profileIdentity&&xboxScope.protocol===xbox.protocol&&xboxScope.sourceConnected===true&&xboxScope.contract==='capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json'&&xboxScope.physicalAcceptance===false&&xboxScope.wholeProcessCpuReduction===null&&xboxScope.softwareAcceptanceEvidenceRecordedSeparately===true&&JSON.stringify(xboxScope.supportedPhysicalSources)===JSON.stringify(steam.supportedPhysicalSources)&&JSON.stringify(xbox.supportedPhysicalSources)===JSON.stringify(steam.supportedPhysicalSources),'XBOX_SCOPE_OR_PHYSICAL_SOURCE_AUTHORITY_CHANGED');
 need(scope.supportedPhysicalSources.length===2&&JSON.stringify(scope.supportedPhysicalSources.map(x=>x.writerCapabilityId))===JSON.stringify(['controller.feedback.xinput.physical','controller.feedback.msi-dinput.physical']),'SOFTWARE_WRITER_SCOPE_WIDENED');
 need(scope.diagnostics?.availabilityAuthority===false&&scope.diagnostics.consumer==='nativeGameFeedbackDiagnosticsSnapshot','DIAGNOSTIC_NOT_AN_AVAILABILITY_AUTHORITY');
 const canonical=value=>JSON.stringify((function sort(v){return Array.isArray(v)?v.map(sort):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,sort(v[k])])):v;})(value));
 const locations=[];
 for(const relative of ['capabilities/CONTROLLER-CONSUMPTION.json','capabilities/INTEGRATION-STATUS.json','capabilities/EXECUTION-CHAINS.json','capabilities/hc-device-catalog.json']){
  const doc=read(resolve(relative)),found=[];
  function visit(value,pointer=''){if(Array.isArray(value))value.forEach((v,i)=>visit(v,pointer+'/'+i));else if(value&&typeof value==='object')for(const[k,v]of Object.entries(value)){if(k==='gameMotorSoftwareConsumption')found.push({pointer:pointer+'/'+k,value:v});else visit(v,pointer+'/'+k);}}
  visit(doc);need(found.length===1&&canonical(found[0].value)===canonical(scope),'CAPABILITY_SCOPE_RECORD_MISMATCH:'+relative);
   const steamFound=[];function visitSteam(value,pointer=''){if(Array.isArray(value))value.forEach((v,i)=>visitSteam(v,pointer+'/'+i));else if(value&&typeof value==='object')for(const[k,v]of Object.entries(value)){if(k==='additionalGameMotorSoftwareConsumption')steamFound.push({pointer:pointer+'/'+k,value:v});else visitSteam(v,pointer+'/'+k);}}
   visitSteam(doc);need(steamFound.length===1&&canonical(steamFound[0].value)===canonical(additional),'SEPARATE_STEAM_SCOPE_RECORD_MISMATCH:'+relative);locations.push({relative,pointer:found[0].pointer,sha256:hash(resolve(relative))});
 }
 const header=fs.readFileSync(resolve('capabilities/HcControllerFeedbackCapabilityCatalog.generated.h'),'utf8'),cs=fs.readFileSync(resolve('capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs'),'utf8');
 const h=/physicalFeedbackCatalogSha256\s*=\s*"([a-f0-9]{64})"/.exec(header)?.[1],c=/CatalogSha256\s*=\s*"([a-f0-9]{64})"/.exec(cs)?.[1];
 const metadata=read(resolve('capabilities/hc-device-catalog.json')).domains.controllerFeedback;
 need(h&&h===c&&c===metadata.catalogSha256,'GENERATED_HEADER_CS_METADATA_IDENTITY_MISMATCH');
 return{scope,additionalXboxScope:xboxScope,xboxContract:{relative:'capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json',sha256:hash(resolve('capabilities/CONTROLLER-XBOX-XINPUT-RUMBLE-RECEIPT.json'))},additionalSteamScope:additional[0],steamContract:{relative:'capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json',sha256:hash(resolve('capabilities/CONTROLLER-STEAMDECK-RUMBLE-RECEIPT.json'))},locations,generatedCatalogSha256:h,policySha256:hash(resolve('capabilities/hc-controller-feedback-policy.json')),generatorProvenance:SCOPE_INPUTS.filter(p=>p.startsWith('tools/')).map(relative=>({relative,sha256:hash(resolve(relative)),executedByThisLeaf:false})),runtimeJsonAuthority:false};
}

export function verifyManaged(manifestPath,caseRoot){
 const value=read(manifestPath),buildRoot=path.dirname(value.fixtureProject),variant=value.variant;
 need(['normal','stale-revision'].includes(variant),'MANAGED_VARIANT_INVALID');
 need(path.resolve(buildRoot)===path.resolve(caseRoot,'managed',variant)&&inside(buildRoot,manifestPath),'MANAGED_CASE_OWNERSHIP_INVALID');
 need(value.status==='BUILT'&&value.behaviorPass===null&&!value.fixtureExecuted&&value.sourceStable&&value.toolchainStable,'MANAGED_NOT_SOURCE_STABLE_BUILD');
 for(const name of ['normalHostMainExecuted','realSdkLoaded','hardwareIo','sourceFilesEdited'])need(value[name]===false,'MANAGED_BOUNDARY:'+name);
 const binding=value.inputBindings,expected=[...MANAGED_SOURCES,...(variant==='stale-revision'?[STALE_SOURCE]:[])];
 need(binding.variant===variant&&path.resolve(binding.caseRoot)===path.resolve(caseRoot),'MANAGED_BINDING_SCOPE');
 need(binding.sources.length===expected.length&&JSON.stringify(binding.sources.map(r=>r.relative))===JSON.stringify(expected),'MANAGED_EXACT24_OR25_MEMBERSHIP_REQUIRED');
 need(new Set(expected).size===expected.length&&binding.generatedInputs.length<=40,'MANAGED_BINDING_BOUNDS');
 verifyRows(binding.sources,'MANAGED_SOURCE_STALE');
 for(const r of binding.sources){
  need(inside(buildRoot,r.snapshot)&&hash(r.snapshot)===r.sha256,'MANAGED_SNAPSHOT_STALE:'+r.relative);
  if(r.relative!==STALE_SOURCE)need(path.resolve(r.path)===path.resolve(REPO,r.relative),'MANAGED_SOURCE_WRONG_ROOT:'+r.relative);
  else need(path.resolve(r.path)===path.resolve(buildRoot,STALE_SOURCE)&&r.privateSourceFault===true,'PRIVATE_HOST_FAULT_WRONG_ROOT');
 }
 if(variant==='stale-revision'){
  const original=binding.sources.find(r=>r.relative==='InputHost/HostGameOutputObservation.cs'),fault=binding.sources.at(-1);
  need(fault.originalSha256===original.sha256&&path.resolve(fault.derivedFrom)===path.resolve(original.path),'PRIVATE_HOST_FAULT_PROVENANCE');
  need(fs.readFileSync(fault.path,'utf8')===fs.readFileSync(original.snapshot,'utf8').replace(/^\uFEFF/,'').replace('                ++_motorRevision;','                if (_motorRevision == 0) ++_motorRevision; // private semantic source fault: replay revision'),'PRIVATE_HOST_FAULT_NOT_EXACT');
 }
 verifyRows(binding.generatedInputs,'MANAGED_GENERATED_STALE');
 verifyRows(binding.toolchainAllowlist,'MANAGED_COMPILER_TOOL_STALE');
 const actual=value.evaluatedCompilerInputs,declared=new Map(binding.toolchainAllowlist.map(r=>[path.resolve(r.path).toLowerCase(),r.sha256]));
 verifyRows(actual,'ACTUAL_COMPILER_INPUT_STALE');
 for(const r of actual){need(['Compile','Reference','Analyzer','Import'].includes(r.kind),'ACTUAL_COMPILER_KIND_INVALID');if(!inside(buildRoot,r.path))need(declared.get(path.resolve(r.path).toLowerCase())===r.sha256,'UNDECLARED_ACTUAL_COMPILER_INPUT:'+r.path);}
 const compiles=actual.filter(r=>r.kind==='Compile');need(compiles.length===19&&actual.some(r=>r.kind==='Reference'),'ACTUAL19_COMPILER_LEDGER_REQUIRED');
 const observation=binding.sources.find(r=>r.relative===(variant==='stale-revision'?STALE_SOURCE:'InputHost/HostGameOutputObservation.cs'));
 for(const [name,source]of [['HostGameOutputObservation.cs',observation],['HcControllerGameFeedbackCapabilityCatalog.generated.cs',binding.sources.find(r=>r.relative==='capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs')],['PsEndpointOwnership.cs',binding.sources.find(r=>r.relative==='InputHost/PsEndpointOwnership.cs')],['HostRuntimeBinaryIdentity.cs',binding.sources.find(r=>r.relative==='InputHost/HostRuntimeBinaryIdentity.cs')]]){
  const matches=compiles.filter(r=>path.resolve(r.path)===path.resolve(buildRoot,name));need(matches.length===1&&matches[0].sha256===source.sha256,'ACTUAL_UNIQUE_COMPILE_MISSING:'+name);
 }
 const declarations=compiles.flatMap(r=>Array.from(fs.readFileSync(r.path,'utf8').matchAll(/\bclass\s+HcControllerGameFeedbackCapabilityCatalog\b/g),()=>path.basename(r.path)));
 need(JSON.stringify(declarations)===JSON.stringify(['HcControllerGameFeedbackCapabilityCatalog.generated.cs']),'DUPLICATE_EMBEDDED_CATALOG');
 need(hash(value.generatedIdentity.path)===value.generatedIdentity.sha256&&compiles.some(r=>path.resolve(r.path)===path.resolve(value.generatedIdentity.path)&&r.sha256===value.generatedIdentity.sha256),'MANAGED_COMPILED_IDENTITY_STALE');
 const bytes=fs.readFileSync(manifestPath),marker=Buffer.from(',"inputBindings":'),index=bytes.lastIndexOf(marker);need(index>=0,'MANAGED_RAW_BINDINGS_MISSING');
 need(digest(bytes.subarray(index+marker.length,-2))===value.inputManifestSha256,'MANAGED_INPUT_IDENTITY_MISMATCH');
 verifyRows(value.runtimeArtifacts,'MANAGED_RUNTIME_STALE');
 need(fs.readFileSync(value.fixtureDll).includes(Buffer.from(value.inputManifestSha256,'utf16le')),'ACTUAL_DLL_INPUT_IDENTITY_MISSING');
 const artifacts=[manifestPath,...value.runtimeArtifacts.map(r=>r.path),...binding.sources.map(r=>r.snapshot),...binding.generatedInputs.map(r=>r.path),value.generatedIdentity.path];
 for(const p of artifacts)need(inside(buildRoot,p),'MANAGED_ARTIFACT_OUTSIDE_CASE');
 return{manifest:manifestPath,manifestSha256:hash(manifestPath),value,buildRoot,artifacts};
}
