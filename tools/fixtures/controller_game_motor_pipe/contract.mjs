import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import {fileURLToPath} from 'node:url';
import {installedTools} from '../../ymcc_sim/global/controller-priority-ledger/adapter.mjs';
import {toolchainFiles} from '../../ymcc_sim/global/inputhost-protocol/adapter.mjs';
import {staleRevision} from './faults.mjs';
import {COLD_RETURN_INPUTS} from './cold-return-context.mjs';
export const FIXTURE='tools/fixtures/controller_game_motor_pipe';
export const MODULE='tools/ymcc_sim/global/controller-game-motor-pipe-consumer';
export const CASE_ID='controller.game-motor-private-pipe-consumer';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../..');
export const WORKSPACE=path.resolve(REPO,'../../..');
export const STALE_SOURCE='fault-inputs/HostGameOutputObservation.stale-revision.cs';
export const TOOL_LIMIT=1024;
// These are independent inputs. Neither the old19 builder nor its pin is consumed.
export const MANAGED_SOURCES=Object.freeze([
 'InputHost/Program.cs','InputHost/ParentProcessLifetime.cs','InputHost/HostControllerOutputStop.cs',
 'InputHost/HostControllerIdleCtsRelease.cs','InputHost/HostGameOutputObservation.cs',
 'InputHost/MsiSteamDeckReleaseFence1726.cs','InputHost/MsiVirtualReportChain1410.cs',
 'InputHost/ScreenTouchpadReport.cs','InputHost/ScreenButtonReport.cs',
 'InputHost/CanonicalFrameFingerprintMemo.cs','InputHost/HostResponseFields.cs',
 'tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs',
 'tools/inputhost_lifecycle_selftest.cs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs',
 'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json',
 'capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs',
 FIXTURE+'/ManagedEntry.cs',FIXTURE+'/ManagedGameMotorFixture.cs',
 FIXTURE+'/build-managed.mjs',FIXTURE+'/contract.mjs',FIXTURE+'/faults.mjs',
 'InputHost/PsEndpointOwnership.cs','InputHost/HostRuntimeBinaryIdentity.cs'
]);
export const NATIVE_HEADERS=Object.freeze([
 'native/game_feedback_receipt.h','native/game_feedback_source_ticket.h','native/game_feedback_dispatch.h','native/touch_haptic_dispatch.h','capabilities/HcControllerFeedbackCapabilityCatalog.h',
 'capabilities/HcControllerFeedbackCapabilityCatalog.generated.h',
 'native/input_host_pipe_transport.h','native/input_host_transaction_mutex.h',
 'native/input_host_transport_state.h','native/input_host_selftest_rules.h','native/input_frame_submission.h',
 'native/virtual_output_status.h','native/controller_persona.h','native/canonical_frame_fingerprint_memo.h',
 'native/msi_input_host_transport_evidence.h','native/msi_steamdeck_release_fence_1726.h',
 'native/input_chain_command_evidence.h','native/input_chain_ack_evidence.h','native/input_host_recovery_health.h',
 'native/virtual_start_protection.h','native/input_startup_diagnostics.h','native/ps_endpoint_ownership.h',
 'native/virtual_target_return_runtime.h','native/virtual_target_return_session.h','native/virtual_target_return_store.h',
 'native/virtual_target_return_record.h','native/virtual_target_return_process.h','native/virtual_target_return_physical_path.h',
 'native/hidhide_cli_writer_runtime.h','native/hidhide_cli_writer_store.h','native/hidhide_cli_writer_record.h',...COLD_RETURN_INPUTS
]);
export const NATIVE_SOURCES=Object.freeze([
 'native/main.cpp',...NATIVE_HEADERS,'deps/json/json.hpp',
 FIXTURE+'/NativeEntry.cpp',FIXTURE+'/NativeMotorPipeTests.cpp',FIXTURE+'/run-native.mjs',
 FIXTURE+'/lifecycle-context.mjs',FIXTURE+'/target-return-context.mjs',FIXTURE+'/cold-return-context.mjs',FIXTURE+'/native-stage-evidence.mjs','native/virtual_switch_verification_runtime.h','native/virtual_tuple_recovery_runtime.h',
 'tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs',
 'tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs',
 'tools/fixtures/hc_reviewed_calibration/toolchain.mjs'
]);
const INFRASTRUCTURE=[MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/README.md',
 'tools/ymcc_sim/global/controller-game-motor-pipe-consumer/owned-build-bridge.mjs',FIXTURE+'/driver.mjs',
 'tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs',
 'tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json',
 'tools/ymcc_sim/global/controller-priority/adapter.mjs',
 'tools/ymcc_sim/global/controller-lifecycle-pipe/native-tool-inputs.json',
 'tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1',
 'tools/ymcc_sim/global/execution-completion/index.mjs'];
export const sourceFiles=()=>[...new Set([...MANAGED_SOURCES,...NATIVE_SOURCES,...INFRASTRUCTURE])];
export const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
export const digest=b=>crypto.createHash('sha256').update(b).digest('hex');
export const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
export const need=(v,m)=>{if(!v)throw Error(m);};
export const inside=(root,file)=>{const r=path.relative(path.resolve(root),path.resolve(file));return r===''||(!r.startsWith('..'+path.sep)&&r!=='..'&&!path.isAbsolute(r));};
export const row=p=>({path:path.resolve(p),sha256:hash(p)});
export function write(p,value){fs.writeFileSync(p,JSON.stringify(value,null,2)+'\n',{flag:'wx'});}
export function artifactFiles(){
 const nativeSpec=path.join(REPO,'tools/ymcc_sim/global/controller-lifecycle-pipe/native-tool-inputs.json');
 let native;try{native=read(nativeSpec).files;}catch{native=[path.join(WORKSPACE,'Build','GAME_MOTOR_NATIVE_TOOL_INPUTS_REQUIRED')];}
 const files=[...installedTools(),...native,...toolchainFiles(REPO)];
 // The source-exact serial revoke now uses the real standard condition variable.
 const serialMutexHeader=files.find(p=>path.basename(p)==='mutex');
 need(serialMutexHeader,'SERIAL_MUTEX_TOOL_INPUT_REQUIRED');
 files.push(path.join(path.dirname(serialMutexHeader),'condition_variable'));
 // Exact SDK includes observed while compiling the complete current target-duty header.
 const sdkAnchor=files.find(p=>path.basename(p).toLowerCase()==='winuser.h');
 need(sdkAnchor,'TARGET_RETURN_SDK_ANCHOR_REQUIRED');const sdkInclude=path.dirname(path.dirname(sdkAnchor));
 for(const relative of ['shared/hidusage.h','um/cfgmgr32.h','shared/cfg.h','shared/devpropdef.h'])files.push(path.join(sdkInclude,relative));

 const dotnet=read(path.join(REPO,'tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json')).dotnetRoot;
 const fxr=path.join(dotnet,'host/fxr');
 if(fs.existsSync(fxr))for(const version of fs.readdirSync(fxr,{withFileTypes:true}).filter(e=>e.isDirectory()))for(const entry of fs.readdirSync(path.join(fxr,version.name),{withFileTypes:true}))if(entry.isFile()&&entry.name.endsWith('.dll'))files.push(path.join(fxr,version.name,entry.name));
 return [...new Set(files.map(p=>path.resolve(p)))];
}
export function verifyRows(rows,label){for(const r of rows)need(fs.existsSync(r.path)&&hash(r.path)===r.sha256,label+':'+r.path);}
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
 for(const [name,file]of [['PsEndpointOwnership','PsEndpointOwnership.cs'],['HostRuntimeBinaryIdentity','HostRuntimeBinaryIdentity.cs']]){
  const owners=compiles.flatMap(r=>Array.from(fs.readFileSync(r.path,'utf8').matchAll(new RegExp('\\bclass\\s+'+name+'\\b','g')),()=>path.basename(r.path)));
  need(JSON.stringify(owners)===JSON.stringify([file]),'ACTUAL_HELPER_MUST_NOT_BE_SHADOWED:'+name);
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
