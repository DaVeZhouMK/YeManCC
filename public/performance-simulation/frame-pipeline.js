// CPU -> render submission -> GPU -> presentation. Work units are benchmark proxies.
(function(root){
'use strict';
const EPS=1e-9;
function allocate({limit,base,cpuRequest,gpuRequest,cpuPriority=.5}){
 const idle=Math.max(0,Math.min(limit,base)),available=Math.max(0,limit-idle),cpu=Math.max(0,cpuRequest),gpu=Math.max(0,gpuRequest),weight=Math.max(.02,Math.min(.98,cpuPriority));
 let c=Math.min(cpu,available*weight),g=Math.min(gpu,available-c);c=Math.min(cpu,available-g);g=Math.min(gpu,available-c);
 return {cpuWatts:c,gpuWatts:g,baseWatts:idle,watts:idle+c+g,dynamicBudget:available,cpuRequest:cpu,gpuRequest:gpu,unusedWatts:Math.max(0,available-c-g),saturated:available>0&&c+g>=available-EPS,powerScale:cpu+gpu>0?Math.min(1,available/(cpu+gpu)):1};
}
function create({queueDepth=2}={}){
 let cpuFrame=null,gpuFrame=null,completedGpu=null,ready=[],sequence=0,nextPresent=0;
 const totals={issuedCpuLogicWork:0,issuedCpuSubmitWork:0,issuedGpuWork:0,cpuLogicWork:0,cpuSubmitWork:0,gpuWork:0,cpuBusySeconds:0,gpuBusySeconds:0,presented:0,gpuWaitCpuSeconds:0};
 function queued(){return ready.length+Number(!!gpuFrame)+Number(!!completedGpu);}
 function startCpu(now,scene){if(cpuFrame||queued()>=queueDepth)return;const work=scene(now);totals.issuedCpuLogicWork+=work.cpuLogic;totals.issuedCpuSubmitWork+=work.cpuSubmit;totals.issuedGpuWork+=work.gpu;cpuFrame={id:++sequence,cpuLogicLeft:work.cpuLogic,cpuSubmitLeft:work.cpuSubmit,gpuLeft:work.gpu,cpuLogicTotal:work.cpuLogic,cpuSubmitTotal:work.cpuSubmit,gpuTotal:work.gpu,contextKey:work.contextKey,started:now};}
 function promote(){if(!gpuFrame&&!completedGpu&&ready.length)gpuFrame=ready.shift();}
 function status(tasks){return {cpuReady:tasks.hasCpu()||!!cpuFrame,gpuReady:tasks.hasGpu()||!!gpuFrame,cpuTask:tasks.hasCpu(),gpuTask:tasks.hasGpu(),cpuFrameId:cpuFrame?.id??null,gpuFrameId:gpuFrame?.id??null,queued:queued(),waitingForCpu:!tasks.hasGpu()&&!gpuFrame&&!completedGpu&&!ready.length,logicRemaining:cpuFrame?.cpuLogicLeft??0,submitRemaining:cpuFrame?.cpuSubmitLeft??0,gpuRemaining:gpuFrame?.gpuLeft??0,...totals};}
 function advance(seconds,start,{scene,supply,tasks,onPresent,onInterval}){
  let at=start,end=start+seconds,guard=0;
  while(at<end-EPS){
   if(++guard>10000)throw Error('Frame pipeline did not advance');
   if(completedGpu&&at>=nextPresent-EPS){const frame=completedGpu;completedGpu=null;totals.presented++;onPresent(at,frame);nextPresent=at+1/Math.max(1,scene(at).frameLimit);}
   promote();startCpu(at,scene);const state=status(tasks),rates=supply(at,state),cpuRate=Math.max(0,rates.cpuService),gpuRate=Math.max(0,rates.gpuService);
   const cpuLeft=state.cpuTask?tasks.nextCpu():cpuFrame?cpuFrame.cpuLogicLeft>EPS?cpuFrame.cpuLogicLeft:cpuFrame.cpuSubmitLeft:0;
   const gpuLeft=state.gpuTask?tasks.nextGpu():gpuFrame?.gpuLeft??0;
   let span=end-at;if(cpuLeft>EPS&&cpuRate>EPS)span=Math.min(span,cpuLeft/cpuRate);if(gpuLeft>EPS&&gpuRate>EPS)span=Math.min(span,gpuLeft/gpuRate);if(completedGpu&&nextPresent>at+EPS)span=Math.min(span,nextPresent-at);
   if(span<=EPS){span=Math.min(end-at,1e-7);}
   const cpuUsed=Math.min(cpuLeft,cpuRate*span),gpuUsed=Math.min(gpuLeft,gpuRate*span);
   if(state.cpuTask)tasks.consumeCpu(cpuUsed);else if(cpuFrame){if(cpuFrame.cpuLogicLeft>EPS){cpuFrame.cpuLogicLeft=Math.max(0,cpuFrame.cpuLogicLeft-cpuUsed);totals.cpuLogicWork+=cpuUsed;}else{cpuFrame.cpuSubmitLeft=Math.max(0,cpuFrame.cpuSubmitLeft-cpuUsed);totals.cpuSubmitWork+=cpuUsed;}}
   if(state.gpuTask)tasks.consumeGpu(gpuUsed);else if(gpuFrame){gpuFrame.gpuLeft=Math.max(0,gpuFrame.gpuLeft-gpuUsed);totals.gpuWork+=gpuUsed;}
   const cpuBusy=cpuRate>EPS?Math.min(1,cpuUsed/(cpuRate*span)):0,gpuBusy=gpuRate>EPS?Math.min(1,gpuUsed/(gpuRate*span)):0;
   totals.cpuBusySeconds+=cpuBusy*span;totals.gpuBusySeconds+=gpuBusy*span;if(state.waitingForCpu&&state.cpuReady)totals.gpuWaitCpuSeconds+=span;
   onInterval(at,span,rates,{cpuBusy,gpuBusy,cpuUsed,gpuUsed,...state});at+=span;
   if(cpuFrame&&cpuFrame.cpuLogicLeft+cpuFrame.cpuSubmitLeft<=EPS){cpuFrame.submitted=at;ready.push(cpuFrame);cpuFrame=null;}
   if(gpuFrame&&gpuFrame.gpuLeft<=EPS){completedGpu=gpuFrame;gpuFrame=null;}
  }
  // A frame completing exactly at the integration boundary belongs to that boundary.
  if(completedGpu&&at>=nextPresent-EPS){const frame=completedGpu;completedGpu=null;totals.presented++;onPresent(at,frame);nextPresent=at+1/Math.max(1,scene(at).frameLimit);}
 }
 return {advance,status,snapshot:tasks=>({...status(tasks),queueDepth,nextPresent,created:sequence,pendingCpuLogicWork:cpuFrame?.cpuLogicLeft??0,pendingCpuSubmitWork:cpuFrame?.cpuSubmitLeft??0,pendingGpuWork:(cpuFrame?.gpuLeft??0)+(gpuFrame?.gpuLeft??0)+ready.reduce((sum,frame)=>sum+frame.gpuLeft,0)})};
}
const api={allocate,create,ASSUMPTIONS:{queueDepth:2,scope:'aggregate-critical-CPU-and-asynchronous-GPU-work-pipeline',measured:false}};if(typeof module!=='undefined')module.exports=api;else root.YmccFramePipeline=api;
})(globalThis);
