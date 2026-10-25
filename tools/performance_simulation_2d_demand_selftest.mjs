import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const out=process.argv[2]||'G:/YeManCC-Work/Build/Validation/2d-demand';fs.mkdirSync(out,{recursive:true});
const ctx=loadSimulationModels(),M=ctx.YmccConsumerModel,B=ctx.YmccBenchmarkData,priorFile=path.join(out,'before/public/performance-simulation/index.html'),old=loadSimulationModels(priorFile,path.resolve('public/performance-simulation')).YmccConsumerModel;
const report={pass:false,cases:[],comparisons:[]},near=(a,b,eps=1e-6)=>assert(Math.abs(a-b)<=eps,a+' != '+b),check=(name,fn)=>{fn();report.cases.push(name);};
try{
 check('Every2Dsubscene increases CPU logic/submission and GPU work80% at both resolutions, without altering geometry or other scenes',()=>{
  near(M.SCENES.e.demandScale,1.8);
  for(const resolution of ['720p','1080p'])for(let time=0;time<48;time+=.37){const config=M.validate({scene:'e',resolution}),a=old.workload('e',time,[],resolution,config.sceneWeights,'z2e'),b=M.workload('e',time,[],resolution,config.sceneWeights,'z2e');for(const key of ['cpu','gpu','cpuLogic','cpuSubmit'])near(b[key],a[key]*1.8);near(b.pixelScale,a.pixelScale);assert.equal(b.stage.id,a.stage.id);assert.equal(b.nextBoundary,a.nextBoundary);}
  for(const scene of ['a','b','c','d'])for(const time of [0,4.5,8.25,17,31]){const config=M.validate({scene}),a=old.workload(scene,time,[],config.resolution,config.sceneWeights,'z2e'),b=M.workload(scene,time,[],config.resolution,config.sceneWeights,'z2e');near(b.cpu,a.cpu);near(b.gpu,a.gpu);}
 });
 check('All policies receive the same increased finite work; measured watts are bounded by the shared TDP rather than multiplied80%',()=>{
  for(const profile of ['deck','z2e','c8','m395'])for(const budget of [profile==='m395'?13:5,profile==='deck'?16:30]){
   const settings={profile,budget,scene:'e',target:200,saving:'none',manualMHz:2000,manualAggressiveness:60},a=old.createSimulation(settings).advance(12).worlds,b=M.createSimulation(settings).advance(12).worlds,row={profile,budget,worlds:{}};
   for(const id of ['stock','auto','manual']){const w=b[id],v=a[id];near(w.taskWork.issuedCpu,v.taskWork.issuedCpu*1.8);near(w.taskWork.issuedGpu,v.taskWork.issuedGpu*1.8);near(w.taskWork.issuedCpu,b.stock.taskWork.issuedCpu);near(w.taskWork.issuedGpu,b.stock.taskWork.issuedGpu);assert(w.averageWatts<=budget+1e-6);assert.equal(w.totalFrames,w.pipeline.presented);assert(Number.isFinite(w.low)&&w.low>0);row.worlds[id]={beforeFps:v.totalFrames/12,afterFps:w.totalFrames/12,beforeTdp:v.averageWatts,afterTdp:w.averageWatts};}
   report.comparisons.push(row);
  }
 });
 check('The change does not raise GPU capability, remove Z2E3682/Deck1400 ceilings, or rewrite real scheduler settings',()=>{near(B.PROFILES.z2e.gpuCeiling,3682);near(B.GPU_CAPABILITIES.score('z2e',35),3682);near(B.PROFILES.deck.gpuCeiling,1400);near(B.GPU_CAPABILITIES.score('c8',30),4118);const c=M.validate({scene:'e',budget:30,target:60,saving:'medium'}),s=M.createSimulation(c).advance(8);near(s.config.budget,30);near(s.config.target,60);assert.equal(s.config.saving,'medium');for(const w of Object.values(s.worlds))assert(w.watts<=30+1e-6);});report.pass=true;
}finally{fs.writeFileSync(path.join(out,'2d-demand-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,cases:report.cases,comparisons:report.comparisons},null,2));
