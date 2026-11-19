import fs from 'node:fs';
import crypto from 'node:crypto';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {selectedNativeTools,HOST_ENVIRONMENT_SOURCE_INPUTS} from '../release-host-environment.mjs';
export const MODULE='tools/ymcc_sim/global/controller-lifecycle-completion';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASE_ID='controller.lifecycle-completion';
export const INPUTS=['native/main.cpp','native/input_host_lifecycle_claim.h','tools/input_host_lifecycle_completion_selftest.mjs','tools/fixtures/input_host_lifecycle_completion/fixture.cpp','tools/fixtures/legion_motion_consumer/extract_main.mjs','tools/lib/hc_controller_motion_fixture.mjs','tools/fixtures/hc_remaining_model_capabilities/expectations.mjs','tools/lib/hc_motion_matrix_guard.mjs','deps/json/json.hpp','native/virtual_controller_ui_output_runtime.h',"native/virtual_controller_queued_start_runtime.h","native/msi_power_rearm_work_runtime.h","native/virtual_controller_start_runtime.h"];
export const INFRASTRUCTURE=[MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/tool-inputs.json',...HOST_ENVIRONMENT_SOURCE_INPUTS,'tools/ymcc_sim/global/release-host-environment.mjs','tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/execution-completion/index.mjs'];
// One declaration supplies both identity inputs and the producer's exact compiler recipe.
export function lifecycleCompletionTools({capturePins=false}={}){
 const declaration=selectedNativeTools(),native=declaration.native;
 const spec=JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'tool-inputs.json'),'utf8'));
 const need=(ok,code)=>{if(!ok)throw Error(code);};
 const absolute=p=>typeof p==='string'&&path.isAbsolute(p);
 need(native&&[native.compiler,native.linker,native.commandProcessor].every(absolute),'LIFECYCLE_NATIVE_DECLARATION_INVALID');
 for(const roots of [native.include,native.lib])need(Array.isArray(roots)&&roots.length>0&&roots.every(absolute),'LIFECYCLE_NATIVE_ROOTS_INVALID');
 need(path.dirname(native.compiler).toLowerCase()===path.dirname(native.linker).toLowerCase(),'LIFECYCLE_LINKER_ROOT_MISMATCH');
 need(Array.isArray(declaration.files)&&declaration.files.length>0&&Array.isArray(declaration.additionalActualDependencies??[])&&Array.isArray(spec.files),'LIFECYCLE_TOOL_FILES_INVALID');
 const declared=[...declaration.files,...(declaration.additionalActualDependencies??[])].map(row=>{need(row&&absolute(row.path)&&/^[a-f0-9]{64}$/i.test(row.sha256),'LIFECYCLE_TOOL_PIN_INVALID');return row.path;});
 need(spec.files.every(absolute),'LIFECYCLE_EXTRA_TOOL_PATH_INVALID');
 const files=[...new Map([...declared,...spec.files,path.resolve(process.execPath)].map(p=>[path.resolve(p).toLowerCase(),path.resolve(p)])).values()];
 for(const p of files)need(fs.statSync(p).isFile(),'LIFECYCLE_TOOL_NOT_FILE:'+p);
 for(const p of [native.compiler,native.linker])need(files.some(f=>f.toLowerCase()===path.resolve(p).toLowerCase()),'LIFECYCLE_COMPILER_NOT_DECLARED:'+p);
 for(const roots of [native.include,native.lib])for(const p of roots)need(fs.statSync(p).isDirectory(),'LIFECYCLE_TOOL_ROOT_NOT_DIRECTORY:'+p);
 for(const row of spec.configurationLibraries??[])need(native.lib.some(root=>path.resolve(root,row.name).toLowerCase()===path.resolve(row.path).toLowerCase())&&spec.files.includes(row.path),'LIFECYCLE_CONFIGURATION_LIBRARY_OUTSIDE_SELECTED_ROOT:'+row.path);
 const digests=new Map();
 const digest=file=>{
  const key=path.resolve(file).toLowerCase();
  if(!digests.has(key))digests.set(key,crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex'));
  return digests.get(key);
 };
 for(const row of [...declaration.files,...(declaration.additionalActualDependencies??[]),...(spec.configurationLibraries??[])])need(digest(row.path)===row.sha256.toLowerCase(),'LIFECYCLE_FIXED_TOOL_CHANGED:'+row.path);
 return {native,files,...(capturePins?{pins:files.map(path=>({path,sha256:digest(path)}))}:{})};
}
export function installedTools(){return lifecycleCompletionTools().files;}
export function getControllerLifecycleCompletionAdapters(){return [{
 id:CASE_ID,domain:'controller',title:'Actual native lifecycle ownership, queued cleanup and exception completion with semantic fault controls',
 kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/lifecycle'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',
 evidenceInputs:[...INPUTS,...INFRASTRUCTURE],artifactInputs:installedTools(),resultFiles:['lifecycle/result.json'],
 boundary:'Exact queue/snapshot/scope code. Device dispatch and OS endpoints are inert; no product/Host/driver start or physical return claim.'
}];}
