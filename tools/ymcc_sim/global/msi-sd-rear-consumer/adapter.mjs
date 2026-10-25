import {inputs,REPO} from './inputs.mjs';
import {ID,MODULE,COMPLETION,FAULTS,OUTER_TIMEOUT_MS} from './contracts.mjs';
export {ID,MODULE};
export function getMsiSdRearConsumerAdapters(repo=REPO){
 const declared=inputs(repo);
 return [{id:ID,domain:'controller',title:'Enabled MSI SD rear consumers: original 78/67093 positive and five compiled semantic source faults',
  kind:'powershell',path:MODULE+'/Run-MsiSdRear.ps1',
  args:['-EvidenceDir','{outDir}/msi-sd','-NodePath',declared.tools.nodeExecutable],
  timeoutMs:OUTER_TIMEOUT_MS,profiles:['desktop','full'],scope:'behavioral-mock',
  evidenceInputs:declared.sources,artifactInputs:declared.tools.files.map(row=>row.path),
  resultFiles:['msi-sd/result.json','msi-sd/wrapper-terminal.json','msi-sd/positive/result.json','msi-sd/faults/result.json',...FAULTS.map(row=>'msi-sd/faults/'+row.id+'/execution/result.json')],
  completionContract:{schemaVersion:1,rules:[{line:COMPLETION}]},
  boundary:'Original scripts unchanged and sequential; DefaultRelease only; memory IO only. Five compiled exact semantic rejections, native natural1 and original per-fault child natural0. Experiment-only fault explicitly excluded. Entire 582-file approved predeclared tool set and six manifests, sources and all runtime inputs compared before/after. Raw sourceDependencies checked, no independent full link or DLL load trace; no full Host, hardware, secondary display or automatic restore acceptance.'}];
}
