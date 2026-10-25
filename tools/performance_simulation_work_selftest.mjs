import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const out=process.argv[2]||path.resolve('../../../Build/Validation/performance-simulation-work');fs.mkdirSync(out,{recursive:true});
const ctx=loadSimulationModels(),M=ctx.YmccConsumerModel,D=ctx.YmccGameCpuDemand,T=ctx.YmccFrameWork,B=ctx.YmccBenchmarkData;
const report={pass:false,cases:[],beforeAfter:[],drain:[],matrix:[]};const check=(name,fn)=>{fn();report.cases.push(name);};
const near=(a,b,tolerance=1e-7)=>assert(Math.abs(a-b)<=tolerance,`${a} != ${b}`);
try{
check('Displayed CPU score already includes frequency; service capacity applies it exactly once',()=>{
 const result=D.resolve({maximumCpuScore:10000,actualCpuScore:7500,maximumGHz:5,actualGHz:2,workPerFrame:60,targetFps:120});near(result.availableService,7500);near(result.cpuFps,125);assert.equal(result.capacityBasis,'resolved-CPU-score-frequency-applied-once');
});
check('CPU feeding has a flat GPU-duty plateau once scene CPU demand is met; genuine starvation remains below it',()=>{
 for(const cpuFps of [85,100,200,1000]){const p=D.gpuFeeding({cpuFps,gpuFps:85,frameLimit:200,actualGHz:2,workPerFrame:60});near(p.gpuDuty,1);assert.equal(p.cpuStarved,false);}
 const low=D.gpuFeeding({cpuFps:42.5,gpuFps:85,frameLimit:200,actualGHz:1,workPerFrame:60});near(low.gpuDuty,.5);assert.equal(low.cpuStarved,true);
});
check('Burst work is reduced30%; effect GPU work follows pixel count; CPU/AI work does not',()=>{
 const base={cpu:100,gpu:200},effect={id:'effect',start:0,cpu:1,gpu:1,kind:'gpu-effect'},full=T.packet(effect,base),small=T.packet(effect,base,{cpu:1,gpu:1},4/9);
 near(full.cpuWork,100*60*.08*.7);near(full.gpuWork,200*60*.08*.7);near(small.gpuWork/full.gpuWork,4/9);near(full.cpuWork,small.cpuWork);
 const ai=T.packet({...effect,kind:'ai-burst'},base),aiSmall=T.packet({...effect,kind:'ai-burst'},base,{cpu:1,gpu:1},4/9);near(ai.cpuWork,aiSmall.cpuWork);near(ai.gpuWork,0);near(aiSmall.gpuWork,0);
});
check('Finite queue conserves work, rejects duplicate emission, and empties without a clock-based expiry',()=>{
 const queue=T.createQueue(),job={id:'one',cpuWork:180,gpuWork:75,kind:'gpu-effect'};assert.equal(queue.enqueue(job),true);assert.equal(queue.enqueue(job),false);
 assert.equal(queue.hasGpu(),false);near(queue.consumeGpu(100),0);near(queue.consumeCpu(180),180);assert.equal(queue.hasGpu(),true);near(queue.consumeGpu(75),75);
 const s=queue.snapshot();near(s.issuedCpu,s.consumedCpu);near(s.issuedGpu,s.consumedGpu);near(s.pendingCpu,0);near(s.pendingGpu,0);assert.equal(s.completed,1);
});
const beforeFile=path.join(out,'index.before.html'),old=fs.existsSync(beforeFile)&&fs.existsSync(path.join(out,'frame-work.js'))?loadSimulationModels(beforeFile).YmccConsumerModel:null;
check('Ryzen AI Max+395 at55W reaches the GPU/power ceiling once CPU is adequate; no preference-based watt top-up',()=>{
 for(const bias of [25,50,75]){
  const config={profile:'m395',budget:55,scene:'c',bias,saving:'none',target:200};const next=M.createSimulation(config).advance(6).worlds.auto,prev=old?.createSimulation(config).advance(6).worlds.auto;
  assert(next.watts>=54&&next.watts<=55+1e-6);assert(next.gpuUtil>.98);assert.equal(next.performanceDemand.gpuFeeding.cpuStarved,false);assert.equal(next.packagePower.opportunisticCpuWatts,0);
  report.beforeAfter.push({profile:'m395',budget:55,scene:'c',bias,before:prev?{fps:prev.fps,watts:prev.watts,cpu:prev.score.cpu,gpuBusy:prev.gpuUtil}:null,after:{fps:next.fps,watts:next.watts,cpu:next.score.cpu,gpuBusy:next.gpuUtil}});
 }
});
check('Three policies receive identical finite jobs;720P and1080P change GPU work only',()=>{
 const worlds={};for(const resolution of ['720p','1080p']){const s=M.createSimulation({profile:'m395',budget:55,scene:'c',target:200,saving:'none',resolution}).advance(11);worlds[resolution]=s.worlds;const ref=s.worlds.stock.taskWork;for(const id of ['auto','manual']){near(ref.issuedCpu,s.worlds[id].taskWork.issuedCpu);near(ref.issuedGpu,s.worlds[id].taskWork.issuedGpu);}}
 near(worlds['720p'].auto.taskWork.issuedCpu,worlds['1080p'].auto.taskWork.issuedCpu);near(worlds['720p'].auto.taskWork.issuedGpu/worlds['1080p'].auto.taskWork.issuedGpu,4/9);
});
check('Actual CPU capacity determines task completion; the old visual duration is not a fixed slowdown window',()=>{
 for(const manualMHz of [800,4000]){
  const sim=M.createSimulation({profile:'m395',budget:55,scene:'c',target:200,saving:'none',manualMHz,manualAggressiveness:100});sim.advance(5.5);const initial=sim.snapshot().worlds.manual.taskWork;near(initial.pendingCpu,0,1e-5);sim.inject('hitch');let seconds=0,snap;
  do{seconds+=.01;snap=sim.advance(.01);}while((snap.worlds.manual.taskWork.issuedCpu===initial.issuedCpu||snap.worlds.manual.taskWork.pendingCpu>1e-7||snap.worlds.manual.taskWork.pendingGpu>1e-7)&&seconds<2);
  assert(seconds<2);report.drain.push({manualMHz,seconds:Number(seconds.toFixed(3)),fps:snap.worlds.manual.fps,cpu:snap.worlds.manual.score.cpu});
 }
 assert(report.drain[1].seconds<report.drain[0].seconds,JSON.stringify(report.drain));
});
check('All profiles/scenes keep finite metrics, physical TDP/clock bounds, valid Low history, and the same task trace',()=>{
 for(const profile of Object.keys(B.PROFILES).filter(id=>id!=='reference'))for(const scene of ['a','b','c','d','e']){
  const p=B.PROFILES[profile],budget=Math.min(p.max,profile==='m395'?55:28);const sim=M.createSimulation({profile,budget,scene,target:200,saving:'none'});const result=sim.advance(4.5);
  for(const id of ['stock','auto','manual']){const w=result.worlds[id];for(const value of [w.fps,w.low,w.watts,w.gpuUtil,w.score.cpu,w.score.gpu,w.taskWork.pendingCpu,w.taskWork.pendingGpu])assert(Number.isFinite(value),`${profile}/${scene}/${id}`);assert(w.watts<=budget+1e-6);assert(w.frequency<=p.maxGHz+1e-6);assert(w.taskWork.pendingCpu>=0&&w.taskWork.pendingGpu>=0);assert(w.recordedLow===null||w.recordedLow>0);near(w.taskWork.issuedCpu,result.worlds.stock.taskWork.issuedCpu);near(w.taskWork.issuedGpu,result.worlds.stock.taskWork.issuedGpu);}
  report.matrix.push({profile,scene,autoFps:result.worlds.auto.fps,autoTdp:result.worlds.auto.watts});
 }
});
check('Chunked and continuous advancement issue and consume the same jobs',()=>{
 const config={profile:'z2e',budget:15,scene:'b',target:60,saving:'medium'},one=M.createSimulation(config),chunks=M.createSimulation(config);const a=one.advance(12);for(let i=0;i<120;i++)chunks.advance(.1);const b=chunks.snapshot();for(const id of ['stock','auto','manual']){assert.equal(a.worlds[id].totalFrames,b.worlds[id].totalFrames);near(a.worlds[id].taskWork.issuedCpu,b.worlds[id].taskWork.issuedCpu);near(a.worlds[id].averageWatts,b.worlds[id].averageWatts,1e-6);}
});
report.pass=true;
}finally{fs.writeFileSync(path.join(out,'model-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify({pass:report.pass,cases:report.cases,matrix:report.matrix.length,beforeAfter:report.beforeAfter,drain:report.drain},null,2));
