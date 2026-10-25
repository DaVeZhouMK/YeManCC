import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';
import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const out=process.argv[2]||'G:/YeManCC-Work/Build/Validation/game-gpu-efficiency';fs.mkdirSync(out,{recursive:true});
const ctx=loadSimulationModels(),M=ctx.YmccConsumerModel,B=ctx.YmccBenchmarkData,S=ctx.YmccSceneData;
const baselineFile=path.join(out,'before/public/performance-simulation/index.html'),prior=fs.existsSync(baselineFile)?loadSimulationModels(baselineFile):null;
const report={pass:false,measured:false,cases:[],coefficient:{profile:'c8',retainedPct:3682/4118*100,reductionPct:(1-3682/4118)*100,scope:'game-GPU-throughput-only',source:'user-game-parity-calibration-not-universal-Intel-law'},shortComparisons:[],fullSceneComparisons:[]};
const near=(a,b,eps=1e-6)=>assert(Math.abs(a-b)<=eps,a+' != '+b),check=(name,fn)=>{fn();report.cases.push(name);};
const ids=['stock','auto','manual'];
try{
 check('258V has an explicit89.4123%3Dgame coefficient; anime/2D remain100%, and other Intel generations are not assigned an unsupported blanket penalty',()=>{
  for(const scene of ['a','c','d'])near(S.gpuFactor('c8',30,scene).factor,3682/4118);
  for(const scene of ['b','e'])near(S.gpuFactor('c8',30,scene).factor,1);
  for(const id of ['g3e','c7','i1195','z2e'])for(const scene of ['a','b'])near(S.gpuFactor(id,30,scene).factor,1);
  assert.equal(S.GAME_GPU_EFFICIENCY.c8.measured,false);assert(Object.isFrozen(S.GAME_GPU_EFFICIENCY.c8));
  if(prior){const before=prior.YmccSceneData.gpuFactor('c8',30,'c').factor;near(before,.6295355203320235);report.previousHeavy258VFactor=before;}
 });
 check('TS graphics data stay intact; a game conversion is applied once to real GPU service and not to TS scores or CPU service',()=>{
  const config=M.validate({profile:'c8',scene:'a',budget:30,target:200,saving:'none'}),control={tdp:30,frequencyMHz:2000,aggressiveness:60,minState:40,turbo:true,coreMode:'all',smt:true},runtime={activity:{cpuReady:true,gpuReady:true,cpuTask:true}},value=M.performance(config,control,0,[],runtime),original=S.gpuFactor;
  let reference;try{S.gpuFactor=(id,watts,scene)=>id==='c8'?{factor:1,origin:'test-only-unconverted-throughput'}:original(id,watts,scene);reference=M.performance(config,control,0,[],runtime);}finally{S.gpuFactor=original;}
  near(value.score.gpu,reference.score.gpu);near(value.score.cpu,reference.score.cpu);near(value.cpuService,reference.cpuService);near(value.gpuService/reference.gpuService,3682/4118);near(value.gpuService,value.score.gpu*value.gpuCalibration.factor);near(value.packageWatts,reference.packageWatts);
  near(B.GPU_CAPABILITIES.score('c8',30),4118);near(B.GPU_CAPABILITIES.score('z2e',35),3682);near(B.GPU_CAPABILITIES.score('z2e',30),3576);near(B.PROFILES.z2e.gpuCeiling,3682);
 });
 check('Z2E graphics remain capped3682 for all policies, core settings, and scene demands',()=>{
  for(const scene of ['a','b','c','d','e'])for(const budget of [15,20,30,35,45])for(const coreMode of ['all','littleOnly','bigFirst']){
   const config=M.validate({profile:'z2e',budget,scene,target:200,saving:'none'});
   for(const [frequencyMHz,aggressiveness]of [[1200,25],[2000,60],[0,100]]){const p=M.performance(config,{tdp:budget,frequencyMHz,aggressiveness,minState:40,turbo:true,coreMode,smt:true},0,[],{activity:{cpuReady:true,gpuReady:true}});assert(p.score.gpu<=3682+1e-6);assert(p.packageWatts<=budget+1e-6);}
  }
 });
 check('30W720P/1080P: stock/auto/manual FPS remain comparable in both3Dscenes, while the anime advantage is retained',()=>{
  for(const resolution of ['720p','1080p'])for(const scene of ['a','c','b']){
   const now={},before={};for(const profile of ['z2e','c8']){
    const settings={profile,budget:30,scene,resolution,target:200,saving:'none',manualMHz:2000,manualAggressiveness:60};now[profile]=M.createSimulation(settings).advance(4);if(prior)before[profile]=prior.YmccConsumerModel.createSimulation(settings).advance(4);
   }
   const row={resolution,scene,worlds:{}};for(const id of ids){const amd=now.z2e.worlds[id],intel=now.c8.worlds[id],ratio=intel.fps/amd.fps;row.worlds[id]={z2eFps:amd.fps,c8Fps:intel.fps,ratio,z2eGpu:amd.score.gpu,c8Gpu:intel.score.gpu,beforeZ2eFps:before.z2e?.worlds[id].fps,beforeC8Fps:before.c8?.worlds[id].fps};if(scene==='b')assert(ratio>1);else assert(ratio>.88&&ratio<1.12,JSON.stringify(row));}
   report.shortComparisons.push(row);
  }
 });
 check('Every subscene is consumed over48seconds with identical job traces, bounded power, and no forced FPS equality',()=>{
  for(const resolution of ['720p','1080p'])for(const scene of ['a','c','b']){
   const worlds={};for(const profile of ['z2e','c8']){worlds[profile]=M.createSimulation({profile,budget:30,scene,resolution,target:200,saving:'none',manualMHz:2000,manualAggressiveness:60}).advance(48).worlds;}
   const row={resolution,scene,seconds:48,worlds:{}};
   for(const id of ids){const amd=worlds.z2e[id],intel=worlds.c8[id],amdFps=amd.totalFrames/48,intelFps=intel.totalFrames/48,ratio=intelFps/amdFps;row.worlds[id]={z2eFps:amdFps,c8Fps:intelFps,ratio,z2eTdp:amd.averageWatts,c8Tdp:intel.averageWatts,z2eLow:amd.low,c8Low:intel.low};
    for(const w of [amd,intel]){assert(w.averageWatts<=30+1e-6);assert.equal(w.totalFrames,w.pipeline.presented);assert(Number.isFinite(w.low)&&w.low>0);near(w.taskWork.issuedCpu,worlds.z2e.stock.taskWork.issuedCpu);near(w.taskWork.issuedGpu,worlds.z2e.stock.taskWork.issuedGpu);}
    if(scene!=='b')assert(ratio>.85&&ratio<1.15,JSON.stringify(row));
   }
   report.fullSceneComparisons.push(row);
  }
 });report.pass=true;
}finally{fs.writeFileSync(path.join(out,'game-gpu-efficiency-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,cases:report.cases,coefficient:report.coefficient,shortComparisons:report.shortComparisons,fullSceneComparisons:report.fullSceneComparisons},null,2));
