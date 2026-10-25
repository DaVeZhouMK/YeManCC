import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {installedTools} from '../controller-priority-ledger/adapter.mjs';
import {toolchainFiles} from '../inputhost-protocol/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/controller-lifecycle-pipe';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const WORKSPACE=path.resolve(REPO,'../../..');
export const CASE_ID='controller.lifecycle-native-managed-pipe';
export const HEADERS=['input_host_pipe_transport.h','input_host_transaction_mutex.h','input_host_transport_state.h','input_host_selftest_rules.h','input_frame_submission.h','virtual_output_status.h','controller_persona.h','canonical_frame_fingerprint_memo.h','msi_input_host_transport_evidence.h','msi_steamdeck_release_fence_1726.h','input_chain_command_evidence.h','input_chain_ack_evidence.h','game_feedback_receipt.h'];
export const NATIVE_INPUTS=['native/main.cpp',...HEADERS.map(p=>'native/'+p),'capabilities/HcControllerFeedbackCapabilityCatalog.h','capabilities/HcControllerFeedbackCapabilityCatalog.generated.h','deps/json/json.hpp','tools/input_lifecycle_pipe_integration.mjs','tools/fixtures/input_lifecycle_integration/NativeEntry.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs'];
export const INFRASTRUCTURE=[MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/managed-fixture.json',MODULE+'/native-tool-inputs.json',MODULE+'/README.md','tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs','tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'];
export function inside(root,file){const rel=path.relative(path.resolve(root),path.resolve(file));return rel===''||(!rel.startsWith('..'+path.sep)&&rel!=='..'&&!path.isAbsolute(rel));}
export function managedContract(){
 const pin=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'managed-fixture.json'),'utf8'));
 if(pin.schemaVersion!==2||typeof pin.manifest!=='string'||path.isAbsolute(pin.manifest)||pin.manifest.split(/[\\/]/).includes('..'))throw Error('MANAGED_CONTRACT_PATH_INVALID');
 const manifest=path.resolve(WORKSPACE,pin.manifest);if(!inside(path.join(WORKSPACE,'Build'),manifest)&&!inside(path.join(WORKSPACE,'Testing/YMCC/Evidence'),manifest))throw Error('MANAGED_CONTRACT_OUTSIDE_OWNED_BUILD_OR_TESTING');
 const value=JSON.parse(fs.readFileSync(manifest,'utf8'));const buildRoot=path.dirname(value.fixtureProject);
 if(value.semanticMutation!=null||value.inputBindings?.semanticMutation!=null||value.status!=='BUILT'||value.hardwareIo!==false||value.realSdkLoaded!==false||!inside(buildRoot,manifest)||!inside(buildRoot,value.fixtureDll))throw Error('MANAGED_CONTRACT_SCOPE_INVALID');
 const bindings=value.inputBindings;if(!Array.isArray(bindings?.sources)||bindings.sources.length!==22||!Array.isArray(bindings.generatedInputs)||bindings.generatedInputs.length>40)throw Error('MANAGED_BINDINGS_INVALID');
 const reviewed=['InputHost/Program.cs','InputHost/PsEndpointOwnership.cs','InputHost/ParentProcessLifetime.cs','InputHost/HostTerminalCleanup.cs','InputHost/HostVirtualSignalEvidence.cs','InputHost/HostControllerOutputStop.cs','InputHost/HostControllerIdleCtsRelease.cs','InputHost/HostGameOutputObservation.cs','InputHost/MsiSteamDeckReleaseFence1726.cs','InputHost/MsiVirtualReportChain1410.cs','InputHost/ScreenTouchpadReport.cs','InputHost/ScreenButtonReport.cs','InputHost/CanonicalFrameFingerprintMemo.cs','InputHost/HostResponseFields.cs','tools/msi_steamdeck_release_fence_1726_selftest.mjs','tools/msi_steamdeck_release_fence_1726_selftest.cs','tools/inputhost_lifecycle_selftest.cs','tools/input_lifecycle_managed_fixture.mjs','tools/fixtures/input_lifecycle_integration/ManagedEntry.cs','tools/ymcc_sim/global/inputhost-protocol/adapter.mjs','tools/ymcc_sim/global/inputhost-protocol/toolchain-inputs.json','capabilities/HcControllerGameFeedbackCapabilityCatalog.generated.cs'].sort();
 if(JSON.stringify(bindings.sources.map(r=>path.relative(REPO,r.path).replaceAll('\\','/')).sort())!==JSON.stringify(reviewed))throw Error('MANAGED_SOURCE_MEMBERSHIP_UNREVIEWED');
 for(const row of bindings.sources)if(!inside(REPO,row.path)||!inside(buildRoot,row.snapshot))throw Error('MANAGED_SOURCE_OUTSIDE_REVIEWED_ROOTS');
 for(const row of bindings.generatedInputs)if(!inside(buildRoot,row.path))throw Error('MANAGED_GENERATED_OUTSIDE_BUILD');
 const artifacts=[manifest,...value.runtimeArtifacts.map(r=>r.path),...bindings.sources.map(r=>r.snapshot),...bindings.generatedInputs.map(r=>r.path),value.generatedIdentity.path];
 for(const file of artifacts)if(!inside(buildRoot,file))throw Error('MANAGED_ARTIFACT_OUTSIDE_BUILD');
 return {pin,manifest,value,buildRoot,artifacts};
}
export function runtimeTools(){
 const dotnet='C:/Program Files/dotnet';const runtime=toolchainFiles().filter(p=>/[\\/]shared[\\/]Microsoft.NETCore.App[\\/]/i.test(p)||path.resolve(p).toLowerCase()===path.resolve(dotnet,'dotnet.exe').toLowerCase());
 // The native dotnet host selects the latest installed stable hostfxr. Pin that
 // resolver, not every obsolete resolver directory. Current selection was
 // verified with the owned dotnet --info COREHOST_TRACE receipt.
 const fxr=path.join(dotnet,'host/fxr');
 const versions=fs.readdirSync(fxr,{withFileTypes:true}).filter(e=>e.isDirectory()&&/^\d+\.\d+\.\d+$/.test(e.name)).map(e=>e.name).sort((a,b)=>{const x=a.split('.').map(Number),y=b.split('.').map(Number);for(let i=0;i<3;i++)if(x[i]!==y[i])return y[i]-x[i];return 0;});
 if(!versions.length)throw Error('MANAGED_HOSTFXR_UNAVAILABLE');
 runtime.push(path.join(fxr,versions[0],'hostfxr.dll'));return runtime;
}
export function artifactFiles(){let managed=[];try{managed=managedContract().artifacts;}catch{managed=[path.resolve(WORKSPACE,'Build/Validation/VirtualController-Lifecycle-20261019/integration/managed/COMPILED-FIXTURE-REQUIRED')];}
 const native=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'native-tool-inputs.json'),'utf8')).files;
 // Borrow compiler binaries/bootstrap conservatively, but use this consumer's
 // own observed header/library closure. The ledger's unrelated header growth
 // must not enlarge this wire consumer's acceptance surface or 640-file bound.
 const nativeSet=new Set(native.map(p=>path.resolve(p).toLowerCase()));
 const unexecutedTools=new Set(['pwsh.exe','dumpbin.exe']);
 const compiler=installedTools().filter(p=>nativeSet.has(path.resolve(p).toLowerCase())||
   (!/[\\/]include[\\/]/i.test(p)&&!unexecutedTools.has(path.basename(p).toLowerCase())));
 return [...new Set([...compiler,...native,...runtimeTools(),...managed].map(p=>path.resolve(p)))];
}
export function sourceFiles(){let managed=[];try{managed=managedContract().value.inputBindings.sources.map(r=>path.relative(REPO,r.path).replaceAll('\\','/'));}catch{}
 return [...new Set([...NATIVE_INPUTS,...managed,...INFRASTRUCTURE])];}
export function getControllerLifecyclePipeAdapters(){return [{id:CASE_ID,domain:'controller',title:'Complete native request and actual managed Session over real private cross-process Windows pipes',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/lifecycle-pipe'],timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',blockedExitCodes:[7],evidenceInputs:sourceFiles(),artifactInputs:artifactFiles(),resultFiles:['lifecycle-pipe/result.json'],boundary:'Sixteen actual cross-process wire scenarios, including held-target parent exit/termination/retry across three current personas in Release/Checked, current native request + fast/DOM validation + managed real loop/Session, SDK and PnP fakes. Explicit source-bound managed prerequisite; no normal product Main, full Coordinator, live capture tickets, real SDK/devices, sleep or CPU acceptance.'}];}
