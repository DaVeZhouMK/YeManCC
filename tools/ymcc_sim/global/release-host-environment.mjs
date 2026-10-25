// Release-host selection changes tool/root identity only; consumer assertions stay unchanged.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
const repo=path.resolve(import.meta.dirname,'../../..');
const hash=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read=file=>JSON.parse(fs.readFileSync(file,'utf8').replace(/^\uFEFF/,''));
const key=file=>path.resolve(file).toLowerCase();
const inside=(parent,child)=>key(child).startsWith(key(parent)+path.sep);
export const HOST_ENVIRONMENT_SOURCE_INPUTS=Object.freeze(['tools/ymcc_sim/global/release-host-environment.mjs','tools/ymcc_sim/global/release-host-environment.ps1','tools/ymcc_sim/global/hosted-release-tools.json','tools/initialize-release-check-host.ps1','tools/capture-release-host-tools.ps1','tools/release-check-host/package.json','tools/release-check-host/package-lock.json','tools/invoke-hosted-release-gate.ps1']);
export function hostedReleaseProfile(){
 if(process.env.YMCC_RELEASE_HOSTED_ENVIRONMENT!=='1')return null;
 if(process.env.GITHUB_ACTIONS!=='true')throw Error('HOSTED_RELEASE_REQUIRES_GITHUB_RUNNER');
 const file=path.join(repo,HOST_ENVIRONMENT_SOURCE_INPUTS[2]),p=read(file);
 if(p.schemaVersion!==1||p.kind!=='ymcc-hosted-release-tool-declaration-candidate'||p.status!=='REVIEWED_HOST_DECLARATION')throw Error('HOSTED_RELEASE_TOOL_REVIEW_REQUIRED');
 if(p.imageOS!==process.env.ImageOS||p.imageVersion!==process.env.ImageVersion)throw Error('HOSTED_RELEASE_RUNNER_IMAGE_CHANGED');
 if(p.node.version!==process.version||key(p.node.path)!==key(process.execPath)||p.node.sha256!==hash(process.execPath))throw Error('HOSTED_RELEASE_NODE_CHANGED');
 for(const row of p.originalDeclarations){const file=path.resolve(repo,row.path);if(!inside(repo,file)||hash(file)!==row.sha256)throw Error('HOSTED_RELEASE_ORIGINAL_DECLARATION_CHANGED:'+row.path);}
 if(p.originalDeclarations.length!==3||p.missing.length||p.nativeManifest.files.length>3072||p.ownedManifest.files.length>3072)throw Error('HOSTED_RELEASE_TOOL_DECLARATION_INCOMPLETE');
 return p;
}
export function selectedNativeTools(){return hostedReleaseProfile()?.nativeManifest??read(path.join(repo,'tools/ymcc_sim/global/decky-update-recovery/native-launch-noio/native-tools.json'));}
export function selectedOwnedTools(){return hostedReleaseProfile()?.ownedManifest??read(path.join(repo,'tools/ymcc_sim/global/decky-persistent-lifecycle/owned-tools.json'));}
export function selectedPackageFixtureTools(){return hostedReleaseProfile()?.packageFixtureManifest??null;}
export function simulationNodePath(){return hostedReleaseProfile()?.node.path??'G:/YeManCC-Work/Environment/Toolchains/nodejs/node-v22.14.0-win-x64/node.exe';}
export function simulationCorePath(relative=''){
 const core=path.resolve(process.env.YMCC_SIM_CORE_ROOT??'G:/YeManCC-Work/Testing/YMCC');
 if(key(core)===key(path.parse(core).root)||key(core)===key(repo)||inside(repo,core)||inside(core,repo))throw Error('SIMULATION_CORE_SCOPE_INVALID');
 if(process.env.YMCC_SIM_CORE_ROOT){
  const marker=read(path.join(core,'.ymcc-release-check-core.json'));
  if(marker.schemaVersion!==1||marker.owner!=='YMCC-release-check'||key(marker.repo)!==key(repo))throw Error('SIMULATION_CORE_OWNER_CHANGED');
 }
 if(path.isAbsolute(relative)||relative.split(/[\\/]/).includes('..'))throw Error('SIMULATION_CORE_CHILD_PATH_INVALID');
 return path.join(core,relative);
}