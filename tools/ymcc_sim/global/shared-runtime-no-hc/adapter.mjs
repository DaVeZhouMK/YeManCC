import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
export const MODULE='tools/ymcc_sim/global/shared-runtime-no-hc';
export const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export const PYTHON='C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe';
export function sources(){
 const result=['tools/ymcc-shared-runtime/verify.py','tools/ymcc-shared-runtime/selftest.py','tools/ymcc-shared-runtime/DependencyPolicySelfTest.csproj','tools/ymcc-shared-runtime/DependencyPolicySelfTest.cs','tools/ymcc-shared-runtime/no-hc-references.targets','deps/ymcc-device-core-runtime/profile.json','deps/ymcc-device-core-runtime/upstream.runtime.json','native/ProcessEfficiency.cs',MODULE+'/run.mjs',MODULE+'/adapter.mjs','tools/ymcc_sim/global/runner.mjs','tools/ymcc_sim/global/host-process.ps1'];
 function walk(relative){for(const e of fs.readdirSync(path.join(REPO,relative),{withFileTypes:true})){const f=relative+'/'+e.name;if(e.isDirectory()){if(!['obj','bin'].includes(e.name))walk(f);}else if(/\.(cs|csproj|json|md|txt|nuspec|dll)$/.test(e.name))result.push(f);}}
 for(const dir of ['InputHost','native/BatteryHost','deps/ymcc-shared-runtime'])walk(dir);
 for(const file of ['capabilities/HcDeviceCapabilityCatalog.cs','capabilities/HcDeviceCapabilityCatalog.generated.cs','capabilities/HcBatteryCapabilityCatalog.cs','capabilities/HcBatteryCapabilityCatalog.generated.cs'])result.push(file);
 return [...new Set(result)].sort();
}
export function artifacts(){const profile=JSON.parse(fs.readFileSync(path.join(REPO,'deps/ymcc-device-core-runtime/profile.json'),'utf8'));const shared=JSON.parse(fs.readFileSync(path.join(REPO,'deps/ymcc-shared-runtime/manifest.json'),'utf8'));return [PYTHON,'C:/Program Files/dotnet/dotnet.exe','deps/hidmaestro/HIDMaestro.Core.dll',path.join(profile.sourceDirectory,'HandheldCompanion.runtime.json'),...shared.files.map(x=>path.join(profile.sourceDirectory,path.basename(x.originalPath)))].map(p=>path.resolve(REPO,p));}
export function getSharedRuntimeNoHcAdapters(){return [{id:'lifecycle.shared-runtime-no-hc',domain:'lifecycle',title:'Actual InputHost and BatteryHost isolated builds, strict shared binding and relocated provenance rejection',kind:'node',path:MODULE+'/run.mjs',args:['--evidence-dir','{outDir}/shared'],timeoutMs:240000,profiles:['desktop','full'],scope:'behavioral-mock',evidenceInputs:sources(),artifactInputs:artifacts(),resultFiles:['shared/result.json']}];}
