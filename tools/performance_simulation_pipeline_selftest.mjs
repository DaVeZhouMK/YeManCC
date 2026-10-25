import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import {loadSimulationModels} from './lib/simulationModelHarness.mjs';
const out=process.argv[2]||path.resolve('../../../Build/Validation/performance-simulation-pipeline');fs.mkdirSync(out,{recursive:true});
const ctx=loadSimulationModels(),P=ctx.YmccFramePipeline,T=ctx.YmccFrameWork,B=ctx.YmccBenchmarkData,M=ctx.YmccConsumerModel;
const report={pass:false,cases:[],points:[]},check=(name,fn)=>{fn();report.cases.push(name);};
const near=(a,b,eps=1e-7)=>assert(Math.abs(a-b)<=eps,`${a} != ${b}`);
const frame={cpuLogic:1,cpuSubmit:.25,gpu:2,frameLimit:300,contextKey:'fixed'};
function run({cpu=125,gpu=120,cap=300,seconds=2,job=null,chunks=1}={}){
 const tasks=T.createQueue(),pipe=P.create(),frames=[],intervals=[];if(job)tasks.enqueue(job);
 for(let i=0;i<chunks;i++)pipe.advance(seconds/chunks,i*seconds/chunks,{tasks,scene:()=>({...frame,frameLimit:cap}),supply:()=>({cpuService:cpu,gpuService:gpu}),onPresent:(at,f)=>frames.push({at,...f}),onInterval:(at,span,rates,state)=>intervals.push({at,span,...state})});
 return {frames,intervals,pipe:pipe.snapshot(tasks),tasks:tasks.snapshot()};
}
try {
 check('Shared ceiling is conserved; priority reallocates only during contention, and unmet demand receives spare power',()=>{
  for(const limit of [1,5,15,30,55,150])for(const cpu of [0,3,15,100])for(const gpu of [0,7,30,150])for(const cpuPriority of [0,.25,.5,.75,1]){const x=P.allocate({limit,base:2,cpuRequest:cpu,gpuRequest:gpu,cpuPriority});assert(x.watts<=limit+1e-9);assert(x.cpuWatts<=cpu&&x.gpuWatts<=gpu);near(x.watts,Math.min(limit,2+cpu+gpu));}
  const a=P.allocate({limit:30,base:2,cpuRequest:30,gpuRequest:30,cpuPriority:.25}),b=P.allocate({limit:30,base:2,cpuRequest:30,gpuRequest:30,cpuPriority:.75});assert(a.cpuWatts<b.cpuWatts&&a.gpuWatts>b.gpuWatts);
  const spare=P.allocate({limit:30,base:2,cpuRequest:1,gpuRequest:5,cpuPriority:.9});near(spare.cpuWatts,1);near(spare.gpuWatts,5);near(spare.unusedWatts,22);
 });
 check('GPU receives only submitted frames; GPU completion alone creates FPS; the queue remains bounded',()=>{
  const x=run();assert(x.frames.length>115&&x.frames.length<121);near(x.pipe.presented,x.frames.length);
  for(const f of x.frames){assert(f.started<f.submitted);assert(f.submitted<f.at);}
  assert(x.intervals.every(row=>row.queued<=2));assert(x.frames.every((f,i)=>i===0||f.id>x.frames[i-1].id));
  const blocked=run({cpu:0});assert.equal(blocked.frames.length,0);near(blocked.pipe.gpuWork,0);near(blocked.pipe.gpuBusySeconds,0);
 });
 check('Every issued frame work unit is either consumed or pending, with no phantom GPU frames',()=>{
  for(const x of [run(),run({cpu:20}),run({gpu:20}),run({cap:30}),run({cpu:0}),run({gpu:0})]){const s=x.pipe;near(s.issuedCpuLogicWork,s.cpuLogicWork+s.pendingCpuLogicWork);near(s.issuedCpuSubmitWork,s.cpuSubmitWork+s.pendingCpuSubmitWork);near(s.issuedGpuWork,s.gpuWork+s.pendingGpuWork);assert(s.presented<=s.created);}
 });
 check('CPU starvation drains submission buffers; sufficient CPU reaches the same GPU throughput plateau',()=>{
  const low=run({cpu:25}),ok=run({cpu:125}),high=run({cpu:250});assert(low.frames.length<ok.frames.length/2);assert(low.pipe.gpuWaitCpuSeconds>1);assert(high.frames.length-ok.frames.length<=1);assert(high.pipe.gpuBusySeconds>1.98);
  report.points.push({kind:'fixed-GPU-capacity',lowCpuFrames:low.frames.length,adequateCpuFrames:ok.frames.length,highCpuFrames:high.frames.length});
 });
 check('CPU setup precedes GPU event work; ready GPU event work is not labeled CPU waiting',()=>{
  const x=run({job:{id:'effect',cpuWork:25,gpuWork:30}});near(x.tasks.issuedCpu,x.tasks.consumedCpu);near(x.tasks.issuedGpu,x.tasks.consumedGpu);assert.equal(x.tasks.completed,1);
  const taskIntervals=x.intervals.filter(row=>row.gpuTask);assert(taskIntervals.length);assert(taskIntervals.every(row=>!row.waitingForCpu));assert(taskIntervals[0].at>=.2-1e-8);
 });
 check('Finite CPU and GPU bursts finish faster with more service; no duration-based penalty remains',()=>{
  const cpuJob={id:'AI',cpuWork:100,gpuWork:0},gpuJob={id:'explosion',cpuWork:0,gpuWork:120};
  const cpuSlow=run({cpu:125,job:cpuJob}),cpuFast=run({cpu:250,job:cpuJob});assert(cpuSlow.frames[0].at>cpuFast.frames[0].at+.3);
  const gpuSlow=run({gpu:120,job:gpuJob}),gpuFast=run({gpu:240,job:gpuJob});assert(gpuSlow.frames[0].at>gpuFast.frames[0].at+.4);near(gpuFast.tasks.pendingGpu,0);
 });
 check('Frame caps apply to completed GPU frames and create genuine idle time',()=>{
  const x=run({cpu:1000,gpu:1000,cap:30});assert(x.frames.length>=59&&x.frames.length<=60);for(let i=1;i<x.frames.length;i++)assert(x.frames[i].at-x.frames[i-1].at>=1/30-1e-8);assert(x.pipe.gpuBusySeconds<.2);
 });
 check('Integration chunk boundaries do not alter presented frames, work, or power demand',()=>{
  const a=run({job:{id:'one',cpuWork:21,gpuWork:37}}),b=run({job:{id:'one',cpuWork:21,gpuWork:37},chunks:200});assert.equal(a.frames.length,b.frames.length);for(let i=0;i<a.frames.length;i++)near(a.frames[i].at,b.frames[i].at);near(a.pipe.cpuLogicWork,b.pipe.cpuLogicWork);near(a.pipe.gpuWork,b.pipe.gpuWork);
 });
 check('Calibrated TS curves retain Steam Deck1400/16W anchor and device release limits',()=>{
  near(B.stockScores('deck',16).gpu,1400);assert.equal(B.PROFILES.deck.max,16);assert.equal(B.PROFILES.g3e.max,80);for(const p of Object.values(B.PROFILES))for(const watts of [p.min,p.anchor.watts,p.max]){const s=B.stockScores(p.id??Object.keys(B.PROFILES).find(key=>B.PROFILES[key]===p),watts);assert(s.cpu>=0&&s.gpu>=0);}
 });
 check('Sub-idle TDP never fabricates capacity, frames or power above its ceiling',()=>{
  const x=M.createSimulation({profile:'m395',budget:1,target:60,saving:'none',scene:'c'}).advance(3);for(const w of Object.values(x.worlds)){near(w.totalFrames,0);near(w.watts,1);near(w.score.cpu,0);near(w.score.gpu,0);}
 });
 check('First-person order moves only the first stage to the finale; melee lateral/yaw/pitch sway is halved',()=>{
  const story=ctx.YmccSceneStory,camera=ctx.YmccSceneCamera;assert.equal(story.state('a',0).id,'factory');assert.equal(story.state('a',44).id,'melee');
  const oldCameraFile=path.join(out,'YmccSceneCamera.before.js');if(fs.existsSync(oldCameraFile)){
   // Reuse the current story so the comparison is at the same melee stage and elapsed time.
   const oldContext=vm.createContext({console});vm.runInContext(fs.readFileSync(oldCameraFile,'utf8'),oldContext);
   for(const elapsed of [.1,.5,1,1.8,3.7]){const stage={...story.state('a',44),elapsed},old=oldContext.YmccSceneCamera.pose('a',stage,44+elapsed),now=camera.pose('a',stage,44+elapsed);near(now.x,old.x*.5);near(now.yaw,old.yaw*.5);near(now.pitch,old.pitch*.5);near(now.y-1.65,(old.y-1.65)*.5);near(now.z,old.z);}
  }
 });
 report.pass=true;
} finally {fs.writeFileSync(path.join(out,'pipeline-verification.json'),JSON.stringify(report,null,2));}
console.log(JSON.stringify(report,null,2));
