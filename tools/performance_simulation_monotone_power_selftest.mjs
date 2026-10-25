import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const out=process.argv[2]||'G:/YeManCC-Work/Build/Validation/monotone-power';
fs.mkdirSync(out,{recursive:true});
const ctx=loadSimulationModels(),M=ctx.YmccConsumerModel,B=ctx.YmccBenchmarkData,O=ctx.YmccOptimizationCurve,C=ctx.YmccTsPowerCurve;
const priorFile=path.join(out,'before/public/performance-simulation/index.html');
const before=fs.existsSync(priorFile)?loadSimulationModels(priorFile,path.resolve('public/performance-simulation')).YmccConsumerModel:null;
const report={pass:false,measured:false,cases:[],fixedSupplyComparisons:0,chartComparisons:0,z2e:[],scenes:[]};
const check=(name,fn)=>{fn();report.cases.push(name);},near=(a,b,eps=1e-6)=>assert(Math.abs(a-b)<=eps,a+' != '+b);
const steady={activity:{cpuReady:true,gpuReady:true}};
const control=(tdp,frequencyMHz=2000,aggressiveness=60)=>({tdp,frequencyMHz,aggressiveness,minState:40,turbo:true,coreMode:'all',smt:true});
try{
 check('Both original and CPU-restricted Z2E GPU supply are nondecreasing30-45W and respect the user3682 ceiling',()=>{
  for(const watts of [24,30,35,40,43,45]){
   const config={profile:'z2e',scene:'a',budget:watts,target:200,saving:'none',manualMHz:2000,manualAggressiveness:60},now=M.createSimulation(config).advance(4).worlds,prior=before?.createSimulation(config).advance(4).worlds;
   report.z2e.push({watts,before:prior?{stock:prior.stock.score.gpu,auto:prior.auto.score.gpu,manual:prior.manual.score.gpu}:null,stock:now.stock.score.gpu,auto:now.auto.score.gpu,manual:now.manual.score.gpu,gapPct:(now.manual.score.gpu/now.stock.score.gpu-1)*100});
  }
  const rows=report.z2e.filter(row=>row.watts>=30);
  for(let i=1;i<rows.length;i++){assert(rows[i].stock+1e-6>=rows[i-1].stock);assert(rows[i].manual+1e-6>=rows[i-1].manual);}
  near(rows[0].manual,3682);for(const row of report.z2e)for(const key of ['stock','auto','manual'])assert(row[key]<=3682+1e-6);assert(rows.at(-1).manual/rows.find(row=>row.watts===35).manual<=1.02);near(rows.at(-1).gapPct,0);assert(rows[0].gapPct>rows.at(-1).gapPct);
 });
 check('11profiles x5scenes x4fixed CPU settings: all integer TDP points preserve GPU supply and power bounds',()=>{
  for(const profile of Object.keys(B.PROFILES).filter(id=>id!=='reference'))for(const scene of ['a','b','c','d','e'])for(const [frequencyMHz,aggressiveness]of [[1200,25],[2000,60],[3000,100],[0,100]]){
   let prior=null;
   for(let watts=B.GPU_CAPABILITIES.minimumWatts(profile);watts<=B.PROFILES[profile].max;watts++){
    const config=M.validate({profile,scene,budget:watts,target:200,saving:'none'}),p=M.performance(config,control(watts,frequencyMHz,aggressiveness),0,[],steady);
    assert(Number.isFinite(p.score.gpu));assert(p.packageWatts<=watts+1e-6);assert(p.optimization.appliedGainPct<=O.gpuGainPct(watts)+1e-6);assert(p.optimization.baselineCatchupScore>=-1e-6);
    if(prior){assert(p.score.gpu+1e-6>=prior.gpu,JSON.stringify({profile,scene,frequencyMHz,aggressiveness,watts,prior:prior.gpu,gpu:p.score.gpu}));report.fixedSupplyComparisons++;}
    prior={gpu:p.score.gpu};
   }
  }
 });
 check('Stock/automatic/manual TS traces converge without regression above30W across11profiles and12manual settings',()=>{
  for(const profile of Object.keys(B.PROFILES).filter(id=>id!=='reference'))for(const manualMHz of [1600,2000,3000,0])for(const manualAggressiveness of [25,60,100]){
   const snap=M.createSimulation({profile,budget:B.PROFILES[profile].max,target:200,saving:'none',manualMHz,manualAggressiveness}).snapshot();
   for(const mode of ['auto','manual']){
    const rows=C.sample(snap,mode).rows.filter(row=>row.watts>=30);
    for(let i=1;i<rows.length;i++)for(const world of ['stock','optimized']){assert(rows[i][world].gpu+1e-6>=rows[i-1][world].gpu,JSON.stringify({profile,mode,manualMHz,manualAggressiveness,world,from:rows[i-1],to:rows[i]}));report.chartComparisons++;}
   }
  }
 });
 check('Capability envelope is independent of request order, includes internal extrema, and preserves raw anchors',()=>{
  for(const profile of Object.keys(B.PROFILES).filter(id=>id!=='reference')){
   let prior=0;const e=B.GPU_CAPABILITIES.entry(profile),points=[];
   for(let watts=e.minWatts;watts<=e.maxWatts;watts+=.25){const value=O.gpuEnvelope(B.GPU_CAPABILITIES,profile,watts);assert(value+1e-6>=prior);assert(value+1e-6>=B.GPU_CAPABILITIES.score(profile,watts)*(1+O.gpuGainPct(watts)/100));points.push([watts,value]);prior=value;}
   for(const [watts,value]of points.reverse())near(O.gpuEnvelope(B.GPU_CAPABILITIES,profile,watts),value);
  }
  near(B.GPU_CAPABILITIES.score('z2e',30),3576);near(B.GPU_CAPABILITIES.score('z2e',35),3682);near(B.GPU_CAPABILITIES.score('deck',16),1400);near(B.GPU_CAPABILITIES.score('m395',55),10014);assert.equal(O.ASSUMPTIONS.thermalThrottling,false);assert.equal(B.GPU_CAPABILITIES.entry('g3e').unverifiedGpuAboveWatts,35);
 });
 check('Five complete scene pipelines:30/35/40/45W preserve presented FPS for all three policies',()=>{
  for(const scene of ['a','b','c','d','e']){
   let prior=null;
   for(const watts of [30,35,40,45]){
    const result=M.createSimulation({profile:'z2e',budget:watts,scene,target:200,saving:'none',manualMHz:2000,manualAggressiveness:60}).advance(4),row={scene,watts,worlds:{}};
    for(const id of ['stock','auto','manual']){
     const w=result.worlds[id];assert.equal(w.totalFrames,w.pipeline.presented);assert(w.watts<=watts+1e-6);
     if(prior)assert(w.totalFrames>=prior.worlds[id].frames,JSON.stringify({scene,watts,id,before:prior.worlds[id],after:{fps:w.fps,frames:w.totalFrames}}));
     row.worlds[id]={fps:w.fps,frames:w.totalFrames,gpu:w.score.gpu,cpu:w.score.cpu,tdp:w.watts,low:w.low};
    }
    report.scenes.push(row);prior=row;
   }
  }
 });
 report.pass=true;
}finally{fs.writeFileSync(path.join(out,'monotone-power-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,cases:report.cases,fixedSupplyComparisons:report.fixedSupplyComparisons,chartComparisons:report.chartComparisons,z2e:report.z2e},null,2));
