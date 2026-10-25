import fs from'node:fs';import path from'node:path';import assert from'node:assert/strict';import{pathToFileURL}from'node:url';
import{verifyPresetContract}from'./preset-contract.mjs';
export async function checkMigrationRejections(out,modelPath){
 fs.mkdirSync(out,{recursive:true});const m=await import(pathToFileURL(modelPath).href),cases=[];
 for(const[id,patch]of[
  ['wrong-id-label',{HUD_DATA_PRESETS:m.HUD_DATA_PRESETS.map(p=>p.value==='minimal'?{...p,label:'简约'}:p)}],
  ['unreviewed-visual-order',{HUD_DATA_PRESETS:[...m.HUD_DATA_PRESETS].reverse()}],
  ['wrong-default-id',{HUD_DEFAULT_DATA_PRESET:'minimal'}],
 ]){
  let rejected=false,message=null;try{await verifyPresetContract({...m,...patch},out);}catch(error){rejected=true;message=error.message;}
  assert(rejected,'Invalid preset contract must not authorize metadata migration');cases.push({id,status:'PASS',expected:'REJECTED',actual:'REJECTED',message,scope:'metadata guard negative control; not product behavior coverage'});
 }
 const result={schema:'ymcc.monitor-template-migration-rejections.v1',status:'PASS',ok:true,cases,productSourcesModified:false};
 fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));return result;
}
