// Finite CPU/GPU jobs. The event's visual lifetime never selects a performance penalty.
(function(root){
'use strict';
const ASSUMPTIONS=Object.freeze({demandScale:.7,referenceFps:60,referenceBurstSeconds:.08,origin:'finite-work-proxy-not-measured-game-task-cost',measured:false});
function packet(event,base,tail={cpu:1,gpu:1},pixelScale=1){
 const reference=ASSUMPTIONS.referenceFps*ASSUMPTIONS.referenceBurstSeconds;
 // Former stall milliseconds become reference work once, never a wall-clock freeze.
 const cpuFrames=reference*Math.max(0,event.cpu??0)*tail.cpu+Math.max(0,event.stallSeconds??0)*ASSUMPTIONS.referenceFps;
 return {id:event.id,label:event.label,kind:event.kind,start:event.start,cpuWork:base.cpu*cpuFrames*ASSUMPTIONS.demandScale,gpuWork:event.kind==='ai-burst'?0:base.gpu*reference*Math.max(0,event.gpu??0)*tail.gpu*ASSUMPTIONS.demandScale*pixelScale,pixelScale,cpuResolutionIndependent:true,origin:ASSUMPTIONS.origin,measured:false};
}
function createQueue(){
 const seen=new Set(),pending=[];let issuedCpu=0,issuedGpu=0,consumedCpu=0,consumedGpu=0,completed=0;
 function enqueue(job){if(seen.has(job.id))return false;for(const key of ['cpuWork','gpuWork'])if(!Number.isFinite(job[key])||job[key]<0)throw Error('Invalid finite work packet');seen.add(job.id);if(seen.size>512)seen.delete(seen.values().next().value);pending.push({...job,cpuLeft:job.cpuWork,gpuLeft:job.gpuWork});issuedCpu+=job.cpuWork;issuedGpu+=job.gpuWork;return true;}
 const cpuJob=()=>pending.find(job=>job.cpuLeft>1e-9),gpuJob=()=>pending.find(job=>job.cpuLeft<=1e-9&&job.gpuLeft>1e-9);
 function cleanup(){for(let i=pending.length-1;i>=0;i--)if(pending[i].cpuLeft+pending[i].gpuLeft<=1e-9){pending.splice(i,1);completed++;}}
 function consumeCpu(work){const job=cpuJob();if(!job)return 0;const used=Math.min(Math.max(0,work),job.cpuLeft);job.cpuLeft=Math.max(0,job.cpuLeft-used);consumedCpu+=used;cleanup();return used;}
 function consumeGpu(work){const job=gpuJob();if(!job)return 0;const used=Math.min(Math.max(0,work),job.gpuLeft);job.gpuLeft=Math.max(0,job.gpuLeft-used);consumedGpu+=used;cleanup();return used;}
 function snapshot(){return {pendingCpu:Math.max(0,issuedCpu-consumedCpu),pendingGpu:Math.max(0,issuedGpu-consumedGpu),issuedCpu,issuedGpu,consumedCpu,consumedGpu,completed,pending:pending.length,event:pending[0]?{id:pending[0].id,label:pending[0].label,kind:pending[0].kind,start:pending[0].start,progress:1-(pending[0].cpuLeft+pending[0].gpuLeft)/Math.max(1e-10,pending[0].cpuWork+pending[0].gpuWork)}:null,origin:ASSUMPTIONS.origin,measured:false};}
 return {enqueue,consumeCpu,consumeGpu,hasCpu:()=>!!cpuJob(),hasGpu:()=>!!gpuJob(),nextCpu:()=>cpuJob()?.cpuLeft??0,nextGpu:()=>gpuJob()?.gpuLeft??0,snapshot};
}

const api={ASSUMPTIONS,packet,createQueue};if(typeof module!=='undefined')module.exports=api;else root.YmccFrameWork=api;
})(globalThis);
