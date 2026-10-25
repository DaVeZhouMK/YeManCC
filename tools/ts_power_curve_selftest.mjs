import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const ctx=loadSimulationModels(),M=ctx.YmccConsumerModel,C=ctx.YmccTsPowerCurve,B=ctx.YmccBenchmarkData,out=process.argv[2]||'G:/YeManCC-Work/Build/Validation/ts-power-curve';
fs.mkdirSync(out,{recursive:true});
const report={pass:false,cases:[],profiles:[]},check=(name,fn)=>{fn();report.cases.push(name);},near=(a,b)=>assert(Math.abs(a-b)<1e-7,a+' != '+b);
try{
 check('Four watt curves obey device limits and exactly reuse live CPU/GPU supply for both policies',()=>{
  for(const id of Object.keys(B.PROFILES).filter(id=>id!=='reference')){
   const snap=M.createSimulation({profile:id,budget:Math.min(B.PROFILES[id].max,30),saving:'none'}).advance(1);
   for(const mode of ['auto','manual']){
    const result=C.sample(snap,mode);assert.equal(result.series.length,4);assert.equal(result.minWatts,id==='m395'?13:3);assert.equal(result.maxWatts,B.PROFILES[id].max);
    for(const line of result.series){assert.equal(line.points[0].watts,result.minWatts);assert.equal(line.points.at(-1).watts,result.maxWatts);for(const point of line.points)assert(Number.isFinite(point.score)&&point.score>=0);}
    for(const row of result.rows){
     const config={...snap.config,budget:row.watts};
     for(const [world,key]of [['stock','stock'],[mode,'optimized']]){
      const control={...snap.worlds[world].control,tdp:row.watts};if(world==='auto')control.coreMode=row.watts<=config.autoCoreThreshold?config.autoCoreLow:config.autoCoreHigh;
      const value=M.performance(config,control,0,[],{activity:{cpuReady:true,gpuReady:true}});
      near(row[key].cpu,value.score.cpu);near(row[key].gpu,value.score.gpu);assert(value.packageWatts<=row.watts+1e-7);
     }
     near(row.stock.rawGpu,B.GPU_CAPABILITIES.score(id,row.watts));
    }
   }
   report.profiles.push(id);
  }
 });
 check('Suppressing CPU changes CPU/GPU supply without a policy-name bonus',()=>{
  const settings={profile:'m780',budget:20,saving:'none',manualAggressiveness:60};
  const a=C.sample(M.createSimulation({...settings,manualMHz:2000}).advance(2),'manual').rows.find(row=>row.watts===20),b=C.sample(M.createSimulation({...settings,manualMHz:4500}).advance(2),'manual').rows.find(row=>row.watts===20);
  assert(a.optimized.cpu<b.optimized.cpu);assert(a.optimized.gpu>b.optimized.gpu);near(a.stock.cpu,b.stock.cpu);near(a.stock.gpu,b.stock.gpu);report.trade={low:a.optimized,high:b.optimized};
 });
 check('Watt curves are independent of elapsed time and do not mutate live settings',()=>{
  const snap=M.createSimulation({profile:'z2e',budget:30,saving:'none'}).advance(1),before=JSON.stringify(snap),sig=C.signature(snap,'manual'),curve=C.sample(snap,'manual');assert.equal(JSON.stringify(snap),before);
  const shifted=structuredClone(snap);shifted.time+=100;assert.equal(C.signature(shifted,'manual'),sig);assert.equal(JSON.stringify(C.sample(shifted,'manual')),JSON.stringify(curve));shifted.worlds.manual.control.frequencyMHz=1600;assert.notEqual(C.signature(shifted,'manual'),sig);
 });
 check('Raw capability anchors are preserved separately from modeled supply; capability data remain read-only',()=>{
  const deck=C.sample(M.createSimulation({profile:'deck',budget:16}).advance(1),'auto');near(deck.rows.at(-1).stock.gpu,1400);near(deck.rows.at(-1).stock.rawGpu,1400);near(B.GPU_CAPABILITIES.score('deck',16),1400);near(B.GPU_CAPABILITIES.score('z2e',30),3576);near(B.GPU_CAPABILITIES.score('m395',55),10014);assert.equal(B.GPU_CAPABILITIES.setCurve,undefined);
 });report.pass=true;
}finally{fs.writeFileSync(path.join(out,'ts-power-curve-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify(report,null,2));
