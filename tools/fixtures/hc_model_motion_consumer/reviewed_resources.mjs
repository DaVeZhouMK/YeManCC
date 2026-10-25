// Source selection policy for the 18 reviewed legacy deltas. Expected axes and
// source hashes are rebuilt from pinned HC JSON, not from the C++ catalog.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
import {requireHcMotionMatrix} from '../../lib/hc_motion_matrix_guard.mjs';
const sourcePolicy={
 AYANEO2:'AYANEO2',AYANEO2S:'AYANEO2',AYANEOAIR:'AYANEOAIR',AYANEOAIR1S:'AYANEOAIR1S',AYANEOAIRLite:'AYANEOAIR',AYANEOAIRPro:'AYANEOAIR',
 AYANEOFlip1SDS:'AYANEOFlip1SDS',AYANEOFlip1SKB:'AYANEOFlip1SKB',AYANEOFlipDS:'AYANEOFlipKB',AYANEOFlipKB:'AYANEOFlipKB',AYANEOSlide:'AYANEOSlide',
 MinisforumV3:'MinisforumV3',OneXPlayerG1AMD:'OneXPlayerG1',OneXPlayerG1Intel:'OneXPlayerG1',OneXPlayerMiniAMD:'OneXPlayerMiniAMD',OneXPlayerMiniIntel:'OneXPlayerMiniIntel',OneXPlayerMiniPro:'OneXPlayerMiniPro',OneXPlayerX1Mini:'OneXPlayerX1Mini',
};
const sha=b=>createHash('sha256').update(b).digest('hex');
export function reviewedResourceOracle(workspace,repo,legacy) {
 const root=path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream'),manifest=JSON.parse(fs.readFileSync(path.join(root,'source-manifest.json'))),axes=['X','Y','Z'];
 const evidenceRoot=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/CurrentCatalog/2026-10-17T10-53-19-524Z'),actual=JSON.parse(fs.readFileSync(path.join(evidenceRoot,'EFFECTIVE-HC-MATRICES.json'))),audit=JSON.parse(fs.readFileSync(path.join(evidenceRoot,'RESULT.json')));if(audit.status!=='pass'||audit.reference!=='a250b19a4b7e29dac3b383f2d1ac517d1af43e4e')throw Error('Reviewed loader evidence unavailable');
 return Object.entries(sourcePolicy).map(([name,resourceName])=>{
  const source='HandheldCompanion/Resources/Devices/'+resourceName+'.json',observed=actual.find(x=>x.name===name),rows=legacy.recipes.filter(r=>r.deviceClass==='HandheldCompanion.Devices.'+name);
  if(!observed||observed.resource!==source||rows.length!==1||rows[0].controllerPreferred)throw Error('Reviewed model scope '+name);
  const r=rows[0],p=path.join(root,'sources/hc',source),bytes=fs.readFileSync(p),receipt=manifest.find(r=>r.project==='hc'&&r.path===source),digest=sha(bytes),blob=createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
  if(!receipt||receipt.commit!==audit.reference||receipt.sha256.toLowerCase()!==digest||receipt.gitBlob!==blob)throw Error('Reviewed resource pin '+source);
  const config=JSON.parse(bytes),matrices=['gyro','accel'].map(sensor=>{
   const matrix=config[sensor==='gyro'?'GyroMatrix':'AcceleroMatrix'];const {axis,swap}=requireHcMotionMatrix(matrix,source+'/'+sensor);
   const transform=input=>{const output=[0,0,0];for(let i=0;i<3;i++)output[swap[i]]=input[i];return output.map((v,i)=>v*axis[i]);};
   const symbolic=transform([1,2,3]).map(v=>(v<0?'neg-':'')+axes[Math.abs(v)-1].toLowerCase()).join('-');
   const samples=(sensor==='gyro'?[[2,-3,5],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125],[1e-9,-1e-8,1e-7]]:[[11,13,-17],[-1,0,0],[0,-1,0],[0,0,-1],[19.5,-23.25,0.0625],[-1e-6,2e-7,3e-8]]).map(input=>({input,output:transform(input)}));
   return {sensor,id:`hc-a250b19a:reviewed-${name}:${sensor}:${symbolic}`,axis,swap,matrix,source,sourceSha256:digest,samples};
  });
  return {deviceClass:r.deviceClass,name,identityGyroMatrixId:r.gyroMatrixId,identityAccelMatrixId:r.accelMatrixId,gyroMatrixId:matrices[0].id,accelMatrixId:matrices[1].id,selectorId:'hc-a250b19a:reviewed:'+name,sourcePath:source,sourceSha256:digest,sourceScope:'hc-a250b19a-resource-json',matrices,physicalAcceptance:false};
 });
}
