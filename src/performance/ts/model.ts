import {calibration as B,coreModel as K} from './calibration';
import {optimizationCurve as O} from './optimizationCurve';
import {automaticCpuControls,continuousCoreAt} from '@/bridge/performanceControls';
import {smtEffect,SMT_ASSUMPTIONS} from './smt';
export {SMT_ASSUMPTIONS};
const clamp=(n:number,a:number,b:number)=>Math.max(a,Math.min(b,n));
export const calibration=B;
export const CORE_DISABLE_GAIN_RULE=Object.freeze({profiles:['c8','g3e','z2e'],modes:['bigOnly','littleOnly'],retainedPositiveGain:{c8:.2,g3e:.2,z2e:1},origin:'user-calibrated-2026-11-04',measured:false});
export const Z2E_SCHEDULING_RULE=Object.freeze({maximumCpuRecovery:.1,origin:'bounded-core-cluster-scheduling-assumption',measured:false});
const gpuFeed=(retained:number)=>retained>=.3?1:.35+.65*Math.pow(retained/.3,.7);
export function reference(id:string,watts:number){const p=B.PROFILES[id],w=clamp(watts,B.GPU_CAPABILITIES.entry(id).minWatts,p.max),raw=B.stockScores(id,w);const ceiling=p.gpuCeiling??Infinity,envelope=O.gpuEnvelope(B.GPU_CAPABILITIES,id,w,ceiling),gain=O.gpuGainPct(w),gpu=Math.max(raw.gpu,envelope/(1+gain/100));return{watts:w,cpu:raw.cpu,gpu,frequency:p.maxGHz,coreMode:'all',cores:K.topology(id).cores,threads:K.topology(id).threads,gpuGain:0,cpuRetained:1,drawRatio:1,feed:1,smt:smtEffect(id,K.evaluate(id,'all',true,w),true),origin:'frozen-reference-curve',measured:false};}
export function estimate(id:string,bank:any,smt=true,overrideWatts?:number){
 const p=B.PROFILES[id],w=clamp(overrideWatts??bank.tdp,B.GPU_CAPABILITIES.entry(id).minWatts,(overrideWatts===undefined?Math.min(bank.tdpCeiling,p.max):p.max)),base=reference(id,w),policy=bank.mode==='auto'?automaticCpuControls(bank):{maxMHz:bank.manual.unlimited?0:bank.manual.frequencyMHz,minMHz:bank.manual.frequencyMHz,aggressiveness:bank.manual.aggressiveness,turbo:bank.manual.turbo};
 const mode=continuousCoreAt(bank,w),cores=K.evaluate(id,mode,true,w),threading=smtEffect(id,cores,smt),requestedGHz=policy.maxMHz===0?p.maxGHz:policy.maxMHz/1000;
 const frequency=Math.max(.1,Math.min(p.maxGHz,Number.isFinite(cores.maxGHz)?cores.maxGHz:p.maxGHz,requestedGHz,policy.turbo?p.maxGHz:p.maxGHz*.62));
 // Use TS CPU capacity only; none of the old gameFactor, FPS, scene or utilization terms are consumed.
 const epp=.85+.15*policy.aggressiveness/100,trade=B.tradeoffAt(frequency/p.maxGHz*4.8),clockRetained=clamp(trade.cpuRatio*epp,0,1),coreSchedulingRecovery=id==='z2e'&&CORE_DISABLE_GAIN_RULE.modes.includes(mode)?1+Math.min(Z2E_SCHEDULING_RULE.maximumCpuRecovery,Math.max(0,(cores.latencyGain??1)-1)):1,policyRetained=clamp(clockRetained*cores.cpuMulti*coreSchedulingRecovery,0,1),retained=policyRetained*threading.cpuFactor,cpu=base.cpu*retained;
 // Evaluate the core policy with SMT on, then apply SMT to CPU throughput only.
 // Its lost parallel work is not released GPU watts. The existing frequency/core
 // optimization stays independent of the HT toggle. Relative draw is NOT watts.
 const clockDraw=clamp(Math.pow(frequency/p.maxGHz,2.4)*epp,0,1),drawRatio=clamp(clockDraw*cores.powerRatio,0,1);
 const raw=B.stockScores(id,w),envelope=O.gpuEnvelope(B.GPU_CAPABILITIES,id,w,p.gpuCeiling??Infinity);
 const allocate=(cpuRetained:number,draw:number)=>O.resolve({watts:w,cpuScore:base.cpu*cpuRetained,maximumCpuScore:base.cpu,gpuScore:raw.gpu,cpuReferenceWatts:1,cpuActualWatts:draw,active:true,gpuCeiling:p.gpuCeiling??Infinity,gpuEnvelopeScore:envelope});
 const allocation=allocate(policyRetained,drawRatio);
 // Only extreme CPU suppression reduces draw submission. This 30% threshold is a declared prototype assumption.
 const feed=gpuFeed(policyRetained),unadjustedGpu=allocation.gpuScore*feed;
 let gpu=unadjustedGpu,coreGpuGain:any=null;
 if(CORE_DISABLE_GAIN_RULE.profiles.includes(id)&&CORE_DISABLE_GAIN_RULE.modes.includes(mode)){
  // Hold the realized CPU frequency, turbo and aggressiveness fixed in the
  // all-core comparison, including when disabling P cores caps the clock.
  // This separates core-removal gain from the existing clock-policy gain.
  // Dampen the *net positive* GPU improvement after submission limits, once.
  // Existing losses remain losses; clock-policy gains and CPU scores are preserved.
  const allCoreAllocation=allocate(clockRetained,clockDraw),allCoreGpu=allCoreAllocation.gpuScore*gpuFeed(clockRetained);
  const positiveGain=Math.max(0,unadjustedGpu-allCoreGpu),gainRetention=CORE_DISABLE_GAIN_RULE.retainedPositiveGain[id],keptGain=positiveGain*gainRetention;
  gpu=unadjustedGpu-positiveGain+keptGain;
  coreGpuGain={...CORE_DISABLE_GAIN_RULE,retainedPositiveGain:gainRetention,mode,disabledGroup:mode==='bigOnly'?'little':'big',comparisonFrequencyGHz:frequency,allCoreGpu,unadjustedGpu,positiveGain,keptGain,removedGain:positiveGain-keptGain};
 }
 return{watts:w,cpu,gpu,frequency,minGHz:Math.min(frequency,policy.minMHz/1000),aggressiveness:policy.aggressiveness,turbo:policy.turbo,coreMode:mode,coreLabel:K.policyLabel(id,mode),cores:cores.activeCores,threads:threading.activeThreads,smt:threading,gpuGain:base.gpu>0?(gpu/base.gpu-1)*100:0,cpuRetained:retained,policyCpuRetained:policyRetained,coreSchedulingRecovery,drawRatio,feed,coreGpuGain,maxGpuGain:allocation.maximumGainPct,allocation,origin:'TS-only-policy-estimate',measured:false};
}
export function baselineValue(id:string,baseline:any,overrideWatts?:number){return baseline.kind==='captured'?estimate(id,baseline.bank,baseline.smt,overrideWatts??baseline.watts):reference(id,overrideWatts??baseline.watts);}
export function changePercent(a:number,b:number){return a>0?(b/a-1)*100:0;}
// Watt-axis comparisons cover the hardware domain, independently of the editor TDP ceiling.
export function sweep(id:string,bank:any,smt:boolean,baseline:any){
 const entry=B.GPU_CAPABILITIES.entry(id),points=[...new Set<number>([entry.minWatts,entry.maxWatts,...entry.points.map((r:any)=>r[0]),...Array.from({length:Math.floor(entry.maxWatts-entry.minWatts)+1},(_,i)=>entry.minWatts+i)])].filter(w=>w>=entry.minWatts&&w<=entry.maxWatts).sort((a,b)=>a-b);
 return points.map(w=>({watts:w,a:baselineValue(id,baseline,w),b:estimate(id,bank,smt,w)}));
}
export function checkpoints(id:string,bank:any,smt:boolean,baseline:any,viewWatts=20){
 const entry=B.GPU_CAPABILITIES.entry(id),min=entry.minWatts,max=entry.maxWatts,selected=clamp(Math.round(viewWatts),min,max),values=id==='deck'?[3,5,8,10,13,16]:max>45?[min,15,20,30,45,max]:[min,10,15,20,30,max];
 let rows=[...new Set(values)].filter(w=>w>=min&&w<=max);
 if(!rows.includes(selected)){
  if(rows.length>=6){const nearest=rows.filter(w=>w!==min&&w!==max).sort((a,b)=>Math.abs(a-selected)-Math.abs(b-selected))[0];rows=rows.filter(w=>w!==nearest);}
  rows.push(selected);
 }
 return rows.sort((a,b)=>a-b).map(w=>({watts:w,a:baselineValue(id,baseline,w),b:estimate(id,bank,smt,w)}));
}
