// 用户指定：优化曲线 = GPU优化幅度。A synthetic same-TDP CPU/GPU exchange calibration.
(function(root){
'use strict';
const POINTS=Object.freeze([[3,3],[20,20],[30,5],[43,0]].map(row=>Object.freeze(row)));
const ASSUMPTIONS=Object.freeze({name:'优化曲线',meaning:'GPU优化幅度',fullCpuSuppressionFraction:.2,fullCpuPowerSavingFraction:.2,origin:'user-specified-2026-10-17-GPU-optimization-curve',measured:false,cpuCost:'existing-clock-and-supplied-CPU-score-reduction-applied-once',powerEnvelope:'maximum-feasible-GPU-capability-at-or-below-available-power',highPowerReference:'baseline-catches-up-as-optimization-headroom-shrinks',thermalThrottling:false});
const clamp=(v,min,max)=>Math.max(min,Math.min(max,v));
function gpuGainPct(watts){if(!Number.isFinite(watts)||watts<3||watts>=43)return 0;for(let i=1;i<POINTS.length;i++)if(watts<=POINTS[i][0]){const [a,x]=POINTS[i-1],[b,y]=POINTS[i];return x+(y-x)*(watts-a)/(b-a);}return 0;}
// A larger power ceiling retains every lower-power operating point. These are model envelopes, not new measurements.
const envelopeCache=new WeakMap();
function gpuEnvelope(library,profile,watts,gpuCeiling=Infinity){
 let byProfile=envelopeCache.get(library);if(!byProfile){byProfile=new Map();envelopeCache.set(library,byProfile);}
 let data=byProfile.get(profile);
 if(!data){
  const entry=library.entry(profile),knots=[...new Set([...entry.points.map(row=>row[0]),...POINTS.map(row=>row[0])].filter(w=>w>=entry.minWatts&&w<=entry.maxWatts))].sort((a,b)=>a-b),candidates=[...knots];
  // On each linear score/gain interval their product is quadratic; include any internal maximum.
  for(let i=1;i<knots.length;i++){const a=knots[i-1],b=knots[i],s=library.score(profile,a),ds=(library.score(profile,b)-s)/(b-a),g=gpuGainPct(a)/100,dg=(gpuGainPct(b)-gpuGainPct(a))/100/(b-a),curvature=ds*dg;if(curvature<0){const offset=-(ds*(1+g)+s*dg)/(2*curvature);if(offset>0&&offset<b-a)candidates.push(a+offset);}}
  let best=0;data=candidates.sort((a,b)=>a-b).map(w=>{best=Math.max(best,library.score(profile,w)*(1+gpuGainPct(w)/100));return {watts:w,best};});data={points:data,minWatts:entry.minWatts,maxWatts:entry.maxWatts};byProfile.set(profile,data);
 }
 if(watts<data.minWatts)return 0;const w=Math.min(watts,data.maxWatts),points=data.points;let lo=0,hi=points.length;while(lo<hi){const mid=(lo+hi)>>>1;if(points[mid].watts<=w)lo=mid+1;else hi=mid;}
 const raw=library.score(profile,w)*(1+gpuGainPct(w)/100);return Math.min(gpuCeiling,Math.max(lo?points[lo-1].best:0,raw));
}
function resolve({watts,cpuScore,maximumCpuScore,gpuScore,cpuReferenceWatts,cpuActualWatts,active=true,gpuCeiling=Infinity,gpuEnvelopeScore=null}){
 const retained=maximumCpuScore>0?clamp(cpuScore/maximumCpuScore,0,1):1;
 const suppression=active?1-retained:0;
 const releasedCpuWatts=active&&Number.isFinite(cpuReferenceWatts)&&Number.isFinite(cpuActualWatts)?Math.max(0,cpuReferenceWatts-cpuActualWatts):0;
 const powerSavingFraction=cpuReferenceWatts>0?releasedCpuWatts/cpuReferenceWatts:0;
 const activation=Math.min(clamp(suppression/ASSUMPTIONS.fullCpuSuppressionFraction,0,1),clamp(powerSavingFraction/ASSUMPTIONS.fullCpuPowerSavingFraction,0,1));
 const maximumGainPct=gpuGainPct(watts),requestedGainPct=active?maximumGainPct*activation:0,rawGpuScore=Math.max(0,gpuScore);
 const hasEnvelope=active&&Number.isFinite(gpuEnvelopeScore);
 const envelopeScore=hasEnvelope?Math.max(rawGpuScore,Math.min(gpuCeiling,gpuEnvelopeScore)):rawGpuScore;
 const baseGpuScore=Math.max(rawGpuScore,envelopeScore/(1+maximumGainPct/100));
 // Blend two feasible capabilities instead of multiplying then clipping. Early clipping followed by a fading multiplier caused a false decline.
 const eligibleGainPct=hasEnvelope?clamp(baseGpuScore>0?(envelopeScore/baseGpuScore-1)*100:0,0,maximumGainPct):maximumGainPct;
 const optimizedGpuScore=Math.min(gpuCeiling,baseGpuScore*(1+(active?eligibleGainPct*activation:0)/100));
 const appliedGainPct=baseGpuScore>0?(optimizedGpuScore/baseGpuScore-1)*100:0;
 return {name:ASSUMPTIONS.name,meaning:ASSUMPTIONS.meaning,watts,maximumGainPct,eligibleGainPct,activation,requestedGainPct,appliedGainPct,cpuReferenceWatts,cpuActualWatts,releasedCpuWatts,cpuPowerSavingPct:powerSavingFraction*100,cpuRetainedRatio:retained,cpuSuppressionPct:suppression*100,cpuScore,gpuScore:optimizedGpuScore,baseGpuScore,rawGpuScore,gpuEnvelopeScore:envelopeScore,baselineCatchupScore:baseGpuScore-rawGpuScore,cpuPenaltyAppliedAgain:false,origin:ASSUMPTIONS.origin,measured:false};
}
const api=Object.freeze({POINTS,ASSUMPTIONS,gpuGainPct,gpuEnvelope,resolve});if(typeof module!=='undefined')module.exports=api;else root.YmccOptimizationCurve=api;
})(globalThis);
