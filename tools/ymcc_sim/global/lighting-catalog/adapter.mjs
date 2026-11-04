import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {getCapabilityCatalogAdapters} from '../capability-catalog/adapter.mjs';
import {lightingFixtureSourceInputs,lightingFixedSourceInputs} from '../lighting-signals/input-closure.mjs';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
export function getLightingCatalogAdapters(repoRoot=repo){
 const shared=getCapabilityCatalogAdapters(repoRoot)[0];
 const provenance=shared.evidenceInputs.filter(p=>p==='capabilities/upstream/f2e58468/performance-source-facts.json'||p==='capabilities/FAN-RETURN-CONSUMPTION.json'||p.startsWith('capabilities/Hc')||p.startsWith('capabilities/hc-')||p.startsWith('tools/PcCapabilityAudit/')||p.startsWith('tools/hc_capability_catalog')||p.startsWith('tools/lib/hc_controller_motion_catalog')||p.startsWith('tools/lib/hc_controller_feedback_catalog')||p.startsWith('tools/lib/hc_performance_metadata')||p.startsWith('tools/lib/hc_lighting_catalog')||p.startsWith('tools/lib/hc_battery_catalog')||p.startsWith('tools/lib/hc_motion_catalog')||p==='native/hc_oem_windows_motion_catalog.h'||p==='native/msi_motion_capabilities.h'||p==='tools/battery-charge-generate-routes.mjs'||p==='docs/battery-charge-capabilities.json'||p==='native/BatteryHost/msi-ap-route-review.json'||p==='native/BatteryHost/battery-routes.json'||p.startsWith('deps/handheldcompanion-runtime/')||p==='deps/ymcc-device-core-runtime/profile.json'||p.endsWith('/toolchain-inputs.json'));
 // These are the original two C# projects actually compiled by this leaf.
 // Read their explicit default compile inputs; never execute MSBuild while planning.
 const managedInputs=[];
 for(const project of ['tools/LightSetterSelfTest/LightSetterSelfTest.csproj','tools/HcLightingCapabilitySelfTest/HcLightingCapabilitySelfTest.csproj']){
  managedInputs.push(project);
  const text=fs.readFileSync(path.join(repoRoot,project),'utf8');
  for(const match of text.matchAll(/<Compile\s+Include="([^"]+)"/g)){
   const declared=match[1];
   if(declared.includes('$(')){
    if(!['$(LightingProgramSource)','$(AyaneoCeOracleSource)'].includes(declared))throw Error('Unreviewed lighting compile property: '+declared);
    continue; // Explicit Program.cs is below; the opt-in external oracle is not selected here.
   }
   if(declared.includes('*'))throw Error('Unreviewed lighting compile glob: '+declared);
   const relative=path.relative(repoRoot,path.resolve(repoRoot,path.dirname(project),declared));
   if(path.isAbsolute(relative)||relative==='..'||relative.startsWith('..'+path.sep))throw Error('Lighting compile input escapes repo');
   managedInputs.push(relative.replaceAll('\\','/'));
  }
 }
 const inputs=[...provenance,...managedInputs,...lightingFixtureSourceInputs(repoRoot),'tools/ymcc_sim/global/lighting-signals/input-closure.mjs','tools/lib/hc_lighting_source_adapters.mjs',...['OneXPlayer3','AYANEO3','AYANEONEXT2','AYANEODeviceCEc','AYANEOSuperJoyRgb'].map(name=>'capabilities/upstream/f2e58468/'+name+'.cs'),'native/oxp3_lighting_owner.h','native/superjoy_lighting_owner.h','tools/fixtures/hc_lighting_catalog/superjoy-native-ports.inc','tools/fixtures/hc_lighting_catalog/superjoy-native-cases.inc',
'capabilities/EXECUTION-CHAINS.json','tools/hc_capability_maintenance.mjs','tools/hc_capability_maintenance_selftest.mjs','tools/ymcc_sim/global/lighting-catalog/adapter.mjs','tools/HcLightingCapabilitySelfTest/Program.cs','tools/HcLightingCapabilitySelfTest/HcLightingCapabilitySelfTest.csproj','tools/hc_lighting_native_parity_selftest.mjs','tools/hc_lighting_native_consumer_selftest.mjs','tools/hc_lighting_native_feedback_selftest.mjs','tools/gamepad_feedback_evidence_selftest.mjs','tools/gamepad_manual_feedback_receipt_selftest.mjs','tools/fixtures/hc_lighting_catalog/native-before.inc','tools/fixtures/hc_lighting_catalog/operations-before.h','tools/fixtures/hc_lighting_catalog/binding-before.h','tools/fixtures/hc_reviewed_calibration/toolchain.mjs','FanLab/LightSetter/Program.cs','FanLab/LightSetter/DeviceCoreConsumer.cs','FanLab/LightSetter/OneXQueuedLighting.cs','FanLab/LightSetter/OneXLightingSession.cs','FanLab/LightSetter/LightingAssemblyPins.cs','FanLab/RuntimeDependencyContract.cs','FanLab/DeviceCoreSharedDependencies.cs','native/ProcessEfficiency.cs','native/main.cpp','native/oem_lighting_operations.h','native/lighting_device_core_binding.h','deps/json/json.hpp'];
 return [{id:'lighting.unified-capability-dispatch',domain:'lighting',title:'Unified model/action catalog through actual lighting consumers and full native gate parity',kind:'powershell',path:'tools/ymcc_sim/global/lighting-catalog/Run-LightingCatalog.ps1',args:['-EvidenceDir','{outDir}/lighting-catalog'],timeoutMs:300000,profiles:['desktop','full'],scope:'behavioral-mock',resultFiles:['lighting-catalog/result.json','lighting-catalog/native/result.json','lighting-catalog/native-consumers/result.json','lighting-catalog/native-feedback/result.json'],evidenceInputs:[...new Set(inputs)],artifactInputs:[...new Set([...shared.artifactInputs.filter(p=>/HandheldCompanion(?:\.runtime\.json|\.dll)$|Microsoft\.CodeAnalysis(?:\.CSharp)?\.dll$/i.test(p)),...lightingFixedSourceInputs(repoRoot).map(row=>row.path)])]}];
}
