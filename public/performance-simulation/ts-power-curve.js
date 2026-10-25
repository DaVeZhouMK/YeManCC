// Read-only watt sweep of the existing CPU/GPU supply model; it does not run or write a TDP controller.
(function(root){
'use strict';
function signature(snapshot,mode){const pick=world=>{const c=snapshot.worlds[world].control;return Object.fromEntries(['frequencyMHz','aggressiveness','minState','turbo','coreMode','smt','workEfficiencyPct'].map(key=>[key,c[key]]));};return JSON.stringify([snapshot.config,pick('stock'),pick(mode),mode]);}
function sample(snapshot,mode='auto'){
 if(!['auto','manual'].includes(mode))throw Error('Expected an automatic or manual comparison');
 const model=root.YmccConsumerModel,bench=root.YmccBenchmarkData,library=bench.GPU_CAPABILITIES,entry=library.entry(snapshot.config.profile),watts=entry.points.map(row=>row[0]);
 const rows=watts.map(w=>{
  const config={...snapshot.config,budget:w},controls={stock:{...snapshot.worlds.stock.control,tdp:w},optimized:{...snapshot.worlds[mode].control,tdp:w}};
  if(mode==='auto')controls.optimized.coreMode=w<=config.autoCoreThreshold?config.autoCoreLow:config.autoCoreHigh;
  const activity={cpuReady:true,gpuReady:true},reference=model.performance(config,controls.stock,0,[],{activity}),value=model.performance(config,controls.optimized,0,[],{activity});
  // Use the exact supply solver for all four traces. A second GPU-retention ratio here caused extra high-power losses.
  return {watts:w,stock:{cpu:reference.score.cpu,gpu:reference.score.gpu,rawGpu:bench.stockScores(config.profile,w).gpu},optimized:{cpu:value.score.cpu,gpu:value.score.gpu,packageWatts:value.packageWatts,cpuWatts:value.cpuWatts,gpuWatts:value.gpuWatts,frequencyGHz:value.score.frequency,gpuGainPct:value.optimization.appliedGainPct,baselineCatchupScore:value.optimization.baselineCatchupScore}};
 });
 const series=[['stock-cpu','stock','cpu'],['stock-gpu','stock','gpu'],['optimized-cpu','optimized','cpu'],['optimized-gpu','optimized','gpu']].map(([key,world,part])=>({key,world,part,points:rows.map(row=>({watts:row.watts,score:row[world][part]}))}));
 return {profile:snapshot.config.profile,mode,minWatts:entry.minWatts,maxWatts:entry.maxWatts,rows,series,scope:'shared-CPU-GPU-supply-solver-at-each-TDP-with-current-policy-controls',measured:false};
}
const api=Object.freeze({sample,signature});if(typeof module!=='undefined')module.exports=api;else root.YmccTsPowerCurve=api;
})(globalThis);
