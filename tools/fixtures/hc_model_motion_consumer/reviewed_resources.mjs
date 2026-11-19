// The original 18 JSON deltas retain their input-to-output interpretation.
// Win5 independently binds the pinned historical output-to-input callbacks.
import fs from 'node:fs';import path from 'node:path';import {createHash} from 'node:crypto';
import {requireHcMotionMatrix} from '../../lib/hc_motion_matrix_guard.mjs';
const sourcePolicy={
 AYANEO2:'AYANEO2',AYANEO2S:'AYANEO2',AYANEOAIR:'AYANEOAIR',AYANEOAIR1S:'AYANEOAIR1S',AYANEOAIRLite:'AYANEOAIR',AYANEOAIRPro:'AYANEOAIR',
 AYANEOFlip1SDS:'AYANEOFlip1SDS',AYANEOFlip1SKB:'AYANEOFlip1SKB',AYANEOFlipDS:'AYANEOFlipKB',AYANEOFlipKB:'AYANEOFlipKB',AYANEOSlide:'AYANEOSlide',
 MinisforumV3:'MinisforumV3',OneXPlayerG1AMD:'OneXPlayerG1',OneXPlayerG1Intel:'OneXPlayerG1',OneXPlayerMiniAMD:'OneXPlayerMiniAMD',OneXPlayerMiniIntel:'OneXPlayerMiniIntel',OneXPlayerMiniPro:'OneXPlayerMiniPro',OneXPlayerX1Mini:'OneXPlayerX1Mini',
};
const sha=b=>createHash('sha256').update(b).digest('hex');
// Reuse the original executed loader/callback evidence, not a second JSON-only
// transform agreeing with the importer. These are reviewed historical bytes,
// not a new execution or a physical-axis acceptance claim.
export function requireReviewedLoaderEvidence(auditBytes,actualBytes) {
 if(sha(auditBytes)!=='de1d0fe240ace71749216bcd8d180468ee3d29c493c9567bc3f4e3f0257d9ffe'||
    sha(actualBytes)!=='fb4d9a0283de4362e1bbf3142332f33d09e690eca10d25c4f25c41ea353f7a9d')throw Error('Reviewed callback evidence pin changed');
 const audit=JSON.parse(auditBytes),actual=JSON.parse(actualBytes);
 if(audit.status!=='pass'||audit.reference!=='a250b19a4b7e29dac3b383f2d1ac517d1af43e4e'||
    audit.actualSensorNumericCallbacks?.gyroSha256!=='2136da226d4eb603cd0fc6e8e637e1ee023207b6375a801ec69a663866559799'||
    audit.actualSensorNumericCallbacks?.accelSha256!=='af00802115fb44f5491adb312d66b5204f7e6e76139ac690d8ffb845a4702f02'||
    audit.sourcesUnchanged!==true||audit.inputsUnchanged!==true)throw Error('Reviewed callback source contract changed');
 return {audit,actual};
}
export function requireReviewedCallbackBasis(observed,sensor,axis,swap,source) {
 const callback=observed?.actual?.[sensor];
 if(!callback||JSON.stringify(callback.axis)!==JSON.stringify(axis)||JSON.stringify(callback.swap)!==JSON.stringify(swap))throw Error('Reviewed callback axes mismatch: '+source);
 // The original producer's first five inputs include all three basis vectors.
 // The tiny/zero threshold samples are outside this signed-permutation check.
 const inputs=[[1,2,3],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125]];
 for(const [n,input] of inputs.entries()) {
  const expected=[0,0,0];for(let i=0;i<3;i++)expected[swap[i]]=input[i];
  for(let i=0;i<3;i++)expected[i]*=axis[i];
  if(JSON.stringify(callback.vectors?.[n])!==JSON.stringify(expected))throw Error('Reviewed callback basis mismatch: '+source+'/'+n);
 }
}
export function reviewedResourceOracle(workspace,repo,legacy) {
 const root=path.join(workspace,'Docs/Research/PcCapabilities-HC-CTW-20261014/upstream'),manifest=JSON.parse(fs.readFileSync(path.join(root,'source-manifest.json'))),axes=['X','Y','Z'];
 const evidenceRoot=path.join(workspace,'Build/Validation/HC-ModelCapabilities-20261017/ControllerMotion/CurrentCatalog/2026-10-17T10-53-19-524Z'),{actual,audit}=requireReviewedLoaderEvidence(fs.readFileSync(path.join(evidenceRoot,'RESULT.json')),fs.readFileSync(path.join(evidenceRoot,'EFFECTIVE-HC-MATRICES.json')));
  const reviewed=Object.entries(sourcePolicy).map(([name,resourceName])=>{
  const source='HandheldCompanion/Resources/Devices/'+resourceName+'.json',observed=actual.find(x=>x.name===name),rows=legacy.recipes.filter(r=>r.deviceClass==='HandheldCompanion.Devices.'+name);
  if(!observed||observed.resource!==source||observed.fullName?.split('.').at(-1)!==name||observed.actual?.name!==observed.fullName||rows.length!==1||rows[0].controllerPreferred)throw Error('Reviewed model scope '+name);
  const r=rows[0],p=path.join(root,'sources/hc',source),bytes=fs.readFileSync(p),receipt=manifest.find(r=>r.project==='hc'&&r.path===source),digest=sha(bytes),blob=createHash('sha1').update(Buffer.concat([Buffer.from(`blob ${bytes.length}\0`),bytes])).digest('hex');
  if(!receipt||receipt.commit!==audit.reference||receipt.sha256.toLowerCase()!==digest||receipt.gitBlob!==blob)throw Error('Reviewed resource pin '+source);
  const config=JSON.parse(bytes),matrices=['gyro','accel'].map(sensor=>{
   const matrix=config[sensor==='gyro'?'GyroMatrix':'AcceleroMatrix'];const {axis,swap}=requireHcMotionMatrix(matrix,source+'/'+sensor);
   requireReviewedCallbackBasis(observed,sensor,axis,swap,source);
   const transform=input=>{const output=[0,0,0];for(let i=0;i<3;i++)output[swap[i]]=input[i];return output.map((v,i)=>v*axis[i]);};
   const symbolic=transform([1,2,3]).map(v=>(v<0?'neg-':'')+axes[Math.abs(v)-1].toLowerCase()).join('-');
   const samples=(sensor==='gyro'?[[2,-3,5],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125],[1e-9,-1e-8,1e-7]]:[[11,13,-17],[-1,0,0],[0,-1,0],[0,0,-1],[19.5,-23.25,0.0625],[-1e-6,2e-7,3e-8]]).map(input=>({input,output:transform(input)}));
   return {sensor,id:`hc-a250b19a:reviewed-${name}:${sensor}:${symbolic}`,axis,swap,matrix,source,sourceSha256:digest,callbackSemantics:'input-to-output',callbackBasisValidated:true,samples};
  });
  return {deviceClass:r.deviceClass,name,identityGyroMatrixId:r.gyroMatrixId,identityAccelMatrixId:r.accelMatrixId,gyroMatrixId:matrices[0].id,accelMatrixId:matrices[1].id,selectorId:'hc-a250b19a:reviewed:'+name,sourcePath:source,sourceSha256:digest,sourceScope:'hc-a250b19a-resource-json',matrices,physicalAcceptance:false};
  });
  return [...reviewed,win5HistoricalCallbackOracle(repo,legacy)];
}

export function win5HistoricalCallbackOracle(repo,legacy) {
 const commit='fdf0bf66a1e546d2321de9dd334a5b076828de65',root='capabilities/upstream/fdf0bf66/',source=root+'Win5MotionReview.json';
 const pins={
  'GPDWin5.cs':'46138518d31aecfdf0e8338b30139b2ab3c185f6f0b622afe88e9cb6b807bd5c',
  'IMUGyrometer.cs':'f29e7e019aa058cdf8523c65d73a1dd1a87b34fe494ad58459b09fc922960930',
  'IMUAccelerometer.cs':'324193c84b87481d908d6a5443c250ff52e6fda3bc7ae2de3bad6d6ab10acc53',
  'Win5MotionReview.json':'d0524d599683d07b67fb191b77a9f1efef6ee7068849d2dd618ba6cb650e109f',
 };
 const inputs=[],texts=new Map(),sizes=new Map();
 for(const [file,expected] of Object.entries(pins)) {
  const input=path.join(repo,root,file),bytes=fs.readFileSync(input),digest=sha(bytes);
  if(digest!==expected)throw Error('Win5 historical source pin changed: '+file);
  inputs.push({path:input,sha256:digest});texts.set(file,bytes.toString('utf8'));sizes.set(file,bytes.length);
 }
 const review=JSON.parse(texts.get('Win5MotionReview.json')),deviceClass='HandheldCompanion.Devices.GPDWin5',axes=['X','Y','Z'];
 if(review.schemaVersion!==1||review.kind!=='ymcc-reviewed-historical-callback-recipe'||review.deviceClass!==deviceClass||review.upstreamCommit!==commit||review.sourceScope!=='reviewed-historical-hc-callback-output-to-input'||review.sources?.length!==3)throw Error('Win5 historical review identity');
 for(const file of Object.keys(pins).slice(0,3)) {
  const rows=review.sources.filter(row=>row.file===file);
  if(rows.length!==1||rows[0].sha256!==pins[file]||rows[0].bytes!==sizes.get(file))throw Error('Win5 review source identity: '+file);
 }
 const rows=legacy.recipes.filter(row=>row.deviceClass===deviceClass);
 const identityGyroMatrixId='hc-06c0b954:gpd-win5:gyro:z-neg-x-neg-y',identityAccelMatrixId='hc-06c0b954:gpd-win5:accel:neg-x-neg-z-y';
 if(rows.length!==1||rows[0].controllerPreferred||rows[0].gyroMatrixId!==identityGyroMatrixId||rows[0].accelMatrixId!==identityAccelMatrixId)throw Error('Win5 canonical legacy tuple');
 const device=texts.get('GPDWin5.cs'),matrices=['gyro','accel'].map(sensor=>{
  const prefix=sensor==='gyro'?'Gyrometer':'Accelerometer',rawPrefix=sensor==='gyro'?'AngularVelocity':'Acceleration';
  const axisMatch=device.match(new RegExp('\\b'+prefix+'Axis\\s*=\\s*new\\s+Vector3\\s*\\(([^)]+)\\)'));
  const swapMatch=device.match(new RegExp('\\b'+prefix+'AxisSwap\\s*=\\s*new\\s+SortedDictionary<char,\\s*char>\\s*\\{([\\s\\S]*?)\\}\\s*;'));
  if(!axisMatch||!swapMatch)throw Error('Win5 historical device declaration: '+sensor);
  const signs=axisMatch[1].split(',').map(value=>Number(value.trim().replace(/[fF]$/,'')));
  const pairs=[...swapMatch[1].matchAll(/\{\s*'([XYZ])'\s*,\s*'([XYZ])'\s*\}/g)];
  if(signs.length!==3||pairs.length!==3||new Set(pairs.map(pair=>pair[1])).size!==3)throw Error('Win5 historical axes: '+sensor);
  const matrix={Axis:Object.fromEntries(axes.map((axis,i)=>[axis,signs[i]])),AxisSwap:Object.fromEntries(pairs.map(pair=>[pair[1],pair[2]]))};
  const {axis,swap}=requireHcMotionMatrix(matrix,source+'/'+sensor);
  const callback=texts.get(sensor==='gyro'?'IMUGyrometer.cs':'IMUAccelerometer.cs');
  const signature=sensor==='gyro'?'private void ReadingChanged(Gyrometer sender, GyrometerReadingChangedEventArgs args)':'private void ReadingChanged(Accelerometer sender, AccelerometerReadingChangedEventArgs args)';
  const start=callback.indexOf(signature),end=callback.indexOf('\n    }',start);
  if(start<0||end<start)throw Error('Win5 historical callback boundary: '+sensor);
  const body=callback.slice(start,end).replace(/\s+/g,'');
  if(!body.includes('foreach(charaxisinreading_axis.Keys)')||!body.includes('switch(IDevice.GetCurrent().'+prefix+'AxisSwap[axis])'))throw Error('Win5 callback must select input for each output axis: '+sensor);
  for(const output of axes) {
   const threshold=sensor==='gyro'?'if(!(Math.Abs(args.Reading.'+rawPrefix+output+')>=threshold))':'';
   if(!body.includes("case'"+output+"':"+threshold+'reading_axis[axis]=args.Reading.'+rawPrefix+output+';break;')||
      !body.includes('reading.reading.'+output+"=(float)reading_axis['"+output+"']*IDevice.GetCurrent()."+prefix+'Axis.'+output+';'))throw Error('Win5 callback raw/sign semantics: '+sensor+'/'+output);
  }
  // Unlike the old declaration oracle, swap[output] selects a raw input.
  const transform=input=>axis.map((sign,output)=>sign*input[swap[output]]);
  const expression=transform([1,2,3]).map(value=>(value<0?'neg-':'')+axes[Math.abs(value)-1].toLowerCase()).join('-');
  const expected=sensor==='gyro'?'y-neg-z-neg-x':'neg-x-neg-z-y',declaration=review[sensor];
  if(expression!==expected||JSON.stringify(declaration?.axis)!==JSON.stringify(axis)||JSON.stringify(declaration?.outputToInput)!==JSON.stringify(swap)||declaration?.expression!==expression||declaration?.canonicalIdentityMatrixId!==(sensor==='gyro'?identityGyroMatrixId:identityAccelMatrixId))throw Error('Win5 review disagrees with historical source: '+sensor);
  const samples=(sensor==='gyro'?[[2,-3,5],[1,0,0],[0,1,0],[0,0,1],[-7.25,2.5,-0.125],[1e-9,-1e-8,1e-7]]:[[11,13,-17],[-1,0,0],[0,-1,0],[0,0,-1],[19.5,-23.25,0.0625],[-1e-6,2e-7,3e-8]]).map(input=>({input,output:transform(input)}));
  return {sensor,id:sensor==='gyro'?'hc-fdf0bf66:gpd-win5:gyro:'+expression:identityAccelMatrixId,axis,swap,matrix,source,sourceSha256:pins['Win5MotionReview.json'],callbackSemantics:'output-to-input',samples};
 });
 return {deviceClass,name:'GPDWin5',identityGyroMatrixId,identityAccelMatrixId,gyroMatrixId:matrices[0].id,accelMatrixId:matrices[1].id,selectorId:'hc-fdf0bf66:reviewed:GPDWin5',sourcePath:source,sourceSha256:pins['Win5MotionReview.json'],sourceScope:review.sourceScope,upstreamCommit:commit,inputs,matrices,physicalAcceptance:false};
}
