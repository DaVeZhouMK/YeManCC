import fs from 'node:fs';import path from 'node:path';import {fileURLToPath}from'node:url';import{installedTools}from'../controller-priority-ledger/adapter.mjs';
export const MODULE='tools/ymcc_sim/global/controller-start-protection';export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const INPUTS=['native/main.cpp','native/startup_readiness.h','native/game_input_override.h','native/input_shortcut_toggle.h','native/controller_persona.h','deps/json/json.hpp','tools/startup_readiness_native_probe.py',MODULE+'/startup-observer.mjs','native/virtual_start_protection.h','tools/fixtures/virtual_start_protection.cpp','tools/virtual_start_protection_selftest.mjs','tools/fixtures/hc_reviewed_calibration/toolchain.mjs',MODULE+'/adapter.mjs',MODULE+'/run.mjs',MODULE+'/tool-inputs.json',MODULE+'/README.md','tools/ymcc_sim/global/controller-priority-ledger/adapter.mjs','tools/ymcc_sim/global/controller-priority-ledger/tool-inputs.json','tools/ymcc_sim/global/controller-priority/adapter.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/execution-completion/index.mjs'];
const unusedAsanArtifacts=new Set(['asan_ignorelist.txt','clang_rt.asan_dbg_dynamic-x86_64.dll','clang_rt.asan_dbg_dynamic-x86_64.pdb','clang_rt.asan_dynamic-x86_64.dll','clang_rt.asan_dynamic-x86_64.pdb','llvm-symbolizer.exe']);
export const compilerEnvironment='set "VSLANG=1033"\r\nset "CL="\r\nset "_CL_="\r\nset "LINK="\r\nset "_LINK_="\r\n';
export function artifacts(){
 const inherited=installedTools();
 const compiler=inherited.find(file=>/[/\\]bin[/\\]Hostx64[/\\]x64[/\\]cl\.exe$/i.test(file));
 if(!compiler)throw Error('START_PROTECTION_X64_COMPILER_MISSING');
 // This non-ASan recipe selects English diagnostics, independently of the ledger fixture.
 const toolInputs=inherited.filter(file=>!(/[/\\]bin[/\\]Hostx64[/\\]x64[/\\]2052[/\\][^/\\]+\.dll$/i.test(file)||
  (/[/\\]bin[/\\]Hostx64[/\\]x64[/\\][^/\\]+$/i.test(file)&&unusedAsanArtifacts.has(path.basename(file).toLowerCase()))));
 const resources=['clui.dll','linkui.dll','cvtresui.dll'].map(name=>path.join(path.dirname(compiler),'1033',name));
 return [...new Set([...toolInputs,...resources,...JSON.parse(fs.readFileSync(path.join(REPO,MODULE,'tool-inputs.json'))).files])];
}
export function getControllerStartProtectionAdapters(){return [{id:'controller.start-protection',domain:'controller',title:'Versioned switch lifecycle: 30s maximum, 10s execution inspection, one bounded fallback and concurrent player/retry rejection',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/start-protection'],timeoutMs:180000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:INPUTS,artifactInputs:artifacts(),referenceInputs:['native/main.cpp','src/views/ButtonMappingView.vue'],referenceMeaning:'Actual complete coordinator IPC and Vue consumers are verified separately; class adapter does not execute these.',resultFiles:['start-protection/result.json'],boundary:'Current production class and extracted startup observer with memory IO and injected clock; no application/Host/device/driver/sleep.'}];}
