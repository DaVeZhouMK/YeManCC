import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const here=path.dirname(fileURLToPath(import.meta.url));
const modulePath='tools/ymcc_sim/global/lighting-component-continuity/v4/';
const repo=path.resolve(here,'../../../../..');
export function getLightingBuildDeliveryAdapters(repoRoot=repo){
 const root=path.resolve(repoRoot),contractPath=path.join(root,modulePath,'fixture.contract.json');
 const contract=JSON.parse(fs.readFileSync(contractPath,'utf8').replace(/^\uFEFF/,''));
 if(contract.kind!=='ymcc-lighting-build-delivery-global-contract-v1'||contract.zip.name!=='fixture.zip'||!/^[a-f0-9]{64}$/.test(contract.zip.sha256))throw Error('LIGHTING_V4_FIXTURE_CONTRACT');
 return [{id:'lifecycle.lighting-build-delivery',domain:'lifecycle',title:'Actual v4 lighting plan generation, exact native tuple, original two-engine connector and locked-file rollback',kind:'powershell',path:modulePath+'Run-BuildDelivery.ps1',args:['-EvidenceDir','{outDir}/p','-FixtureContractPath',contractPath],timeoutMs:780000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['p/result.json'],artifactInputs:[path.join(root,modulePath,contract.zip.name)],evidenceInputs:[...['adapter.mjs','Run-BuildDelivery.ps1','Archive-IO.ps1','fixture.contract.json','README.md'].map(file=>modulePath+file),...contract.frozenTools.map(row=>row.path),'tools/hc-device-runtime/verify-lighting-portable-evidence.ps1','tools/ymcc_sim/global/lighting-component-continuity/New-ReviewedPlan.ps1'],referenceInputs:['FanLab/LightSetter/Program.cs','native/main.cpp','capabilities/hc-device-catalog.json'],referenceMeaning:'Live sources are references only. This case regenerates plans and executes the selected frozen tools against the exact source/PE/PDB/profile/native tuple, under owned temporary installation roots. It never launches the native application or writes C.'}];
}
