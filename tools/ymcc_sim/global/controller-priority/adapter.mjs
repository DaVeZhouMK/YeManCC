import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/controller-priority';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const CASES=Object.freeze({
 'controller.external-priority-policy':{
  kind:'policy',title:'Actual external-controller arbitration: identity, timing, disconnect and negative admission',
  script:'tools/external_controller_priority_policy_selftest.ps1',
  sources:['tools/external_controller_priority_policy_selftest.ps1','tools/external_controller_priority_policy_selftest.cpp','native/external_controller_priority_policy.h'],
  minimum:19,fault:{path:'native/external_controller_priority_policy.h',before:'return c.physical && c.external && c.fresh && c.slot < 4 &&',after:'return true && c.external && c.fresh && c.slot < 4 &&'},
 },
 'controller.external-priority-runtime':{
  kind:'runtime',title:'Actual external-controller priority runtime: takeover, disconnect, owned return and sample-generation fences',
  script:'tools/external_controller_priority_runtime_selftest.ps1',
  sources:['tools/external_controller_priority_runtime_selftest.ps1','tools/external_controller_priority_runtime_selftest.cpp','native/external_controller_priority_runtime.h','native/external_controller_priority_policy.h','native/external_controller_priority_ledger.h','deps/json/json.hpp'],
  minimum:33,fault:{path:'native/external_controller_priority_runtime.h',before:'c.power!=currentPowerGeneration() || g_powerLifecycle.load()!=PowerLifecycle::Ready)return false;',after:'false || g_powerLifecycle.load()!=PowerLifecycle::Ready)return false;'},
 },
});
export function moduleSources(){return ['adapter.mjs','run.mjs','Run-Original.ps1','runner.test.mjs'].map(name=>MODULE+'/'+name);}
export function compilerArtifacts(){
 const root='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC';
 if(!fs.existsSync(root))return [];
 const version=fs.readdirSync(root).filter(name=>/^\d+\.\d+\.\d+$/.test(name)).sort((a,b)=>b.localeCompare(a,undefined,{numeric:true}))[0];
 return version?['cl.exe','link.exe','dumpbin.exe','c1xx.dll','c2.dll','mspdbcore.dll'].map(name=>path.resolve(root,version,'bin/Hostx64/x64',name)):[];
}
export function getControllerPriorityAdapters(){return Object.entries(CASES).map(([id,spec])=>({
 id,domain:'controller',title:spec.title,kind:'node',path:MODULE+'/run.mjs',
 args:['--test',id,'--evidence-dir','{outDir}/priority'],timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',
 resultFiles:['priority/result.json'],evidenceInputs:[...moduleSources(),...spec.sources,'tools/fixtures/hc_reviewed_calibration/toolchain.mjs','tools/ymcc_sim/global/host-process.ps1','tools/ymcc_sim/global/runner.mjs'],
 artifactInputs:compilerArtifacts(),referenceInputs:['native/main.cpp'],referenceMeaning:'Existing production header integration provenance only; the native app and real device effects do not execute.',
}));}
