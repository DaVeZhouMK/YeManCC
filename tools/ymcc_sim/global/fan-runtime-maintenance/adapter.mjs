import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const MODULE='tools/ymcc_sim/global/fan-runtime-maintenance/';
const REPO=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'../../../..');
function projectSources(repo,relative){const rows=[];const root=path.join(repo,relative);function visit(dir){for(const entry of fs.readdirSync(dir,{withFileTypes:true})){if(entry.isDirectory()){if(entry.name!=='bin'&&entry.name!=='obj')visit(path.join(dir,entry.name));}else if(entry.isFile()&&/\.(?:cs|csproj|props|targets|json)$/i.test(entry.name))rows.push(path.relative(repo,path.join(dir,entry.name)).replaceAll('\\','/'));}}visit(root);return rows;}
export function getFanRuntimeMaintenanceAdapters(repo=REPO){
 const inputs=[MODULE+'Run-Maintenance.ps1',MODULE+'adapter.mjs',MODULE+'buffer-shape-contract.mjs','tools/fan_runtime_maintenance_isolation_selftest.ps1','tools/fan_runtime_maintenance_isolation_selftest.cs',
  'tools/fan_maintenance_result_buffers_selftest.ps1','tools/fan_maintenance_result_buffers_selftest.cs',
  'FanLab/DeviceCoreSharedDependencies.cs','FanLab/RuntimeDependencyContract.cs','native/ProcessEfficiency.cs',
  ...projectSources(repo,'FanLab/real-host'),...projectSources(repo,'FanLab/fan-backend'),
  ...fs.readdirSync(path.join(repo,'capabilities')).filter(name=>name.endsWith('.cs')).map(name=>'capabilities/'+name),
  ...['global.json','Directory.Build.props','Directory.Build.targets','FanLab/Directory.Build.props','FanLab/Directory.Build.targets','NuGet.Config'].filter(name=>fs.existsSync(path.join(repo,name)))];
 return [{id:'fan.source-current-runtime-maintenance',domain:'fan',title:'Current production Host runtime maintenance and diagnostic isolation; no HC Main or hardware',
  kind:'powershell',path:MODULE+'Run-Maintenance.ps1',args:['-EvidenceDir','{outDir}/maintenance'],timeoutMs:180000,profiles:['desktop','full'],
  scope:'behavioral-mock',resultFiles:['maintenance/buffer-shape-contract.json','maintenance/result.json','maintenance/original-maintenance-result.json','maintenance/original-result-buffers-result.json','maintenance/result-buffers/result.json'],evidenceInputs:[...new Set(inputs)]}];
}
