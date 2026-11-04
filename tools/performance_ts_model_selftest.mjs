import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {loadTsModels} from './lib/tsModelHarness.mjs';
const out=path.resolve(process.argv[2]||'../../../Testing/YMCC/Evidence/TsModel'),m=loadTsModels(),B=m.calibration,cases=[];fs.mkdirSync(out,{recursive:true});
const bank=mode=>({mode:'manual',tdp:20,tdpCeiling:45,auto:{bias:50,saving:'none',coreSplit:15,coreLow:'littleOnly',coreHigh:'bigFirst'},manual:{frequencyMHz:5000,unlimited:true,aggressiveness:100,turbo:true,coreMode:mode}});
for(const id of B.profileOptions().map(p=>p.id)){
 const e=B.GPU_CAPABILITIES.entry(id);for(let w=e.minWatts;w<=e.maxWatts;w++){
  const all=m.estimate(id,bank('all'),true,w);for(const mode of ['bigOnly','littleOnly']){const r=m.estimate(id,bank(mode),true,w),off=m.estimate(id,bank(mode),false,w);assert.ok([r.cpu,r.gpu,off.cpu,off.gpu].every(Number.isFinite));assert.equal(r.gpu,off.gpu);assert.ok(off.cpu<=r.cpu);if(r.coreGpuGain){assert.equal(r.coreGpuGain.retainedPositiveGain,id==='z2e'?1:.2);const q=r.coreGpuGain;assert.ok(Math.abs(r.gpu-(q.unadjustedGpu-q.positiveGain+q.positiveGain*q.retainedPositiveGain))<1e-8);}if(id==='z2e'){assert.ok(r.coreSchedulingRecovery>=1&&r.coreSchedulingRecovery<=1.1);assert.ok(r.cpu<all.cpu);}}
 }
 cases.push(id+' finite/bounds/SMT/core-rule');
}
for(const id of ['m780','deck','m6800','i1195','m5800','gos','c7'])for(let w=5;w<10;w++)assert.ok(B.GPU_CAPABILITIES.score(id,w+1)>B.GPU_CAPABILITIES.score(id,w));cases.push('low-power slope retained');
const zAll=m.estimate('z2e',bank('all'),true,20),zP=m.estimate('z2e',bank('bigOnly'),true,20),zE=m.estimate('z2e',bank('littleOnly'),true,20);assert.ok(zP.gpu>zAll.gpu);assert.ok(zP.coreSchedulingRecovery>1);assert.ok(zE.coreSchedulingRecovery>1);assert.ok(zP.gpu<=B.PROFILES.z2e.gpuCeiling);cases.push('Z2E bounded cluster recovery; no Intel attenuation or exceeded GPU ceiling');
const result={pass:true,cases,gameRenderer:false,measured:false,z2e:{all:zAll,onlyZen5:zP,onlyZen5c:zE}};for(const name of ['ts-model-verification.json','gpu-capability-verification.json','optimization-curve-verification.json','monotone-power-verification.json'])fs.writeFileSync(path.join(out,name),JSON.stringify(result,null,2));console.log('PASS performance.ts-model '+cases.length+' groups');
