import {coreModel as K} from './calibration';

export const SMT_ASSUMPTIONS=Object.freeze({
 // HX 370's broad Linux workload average, not a paired Time Spy measurement.
 // Reused as an explicit fallback for other CPUs until model-specific TS A/B data exists.
 perEligibleCoreUplift:.18,
 anchor:'phoronix-2024-08-02-hx370-smt-workload-average',
 matchedTimeSpyCalibration:false,
 intelCalibration:'shared-workload-prior-not-an-Intel-TS-measurement',
 graphicsPowerCalibration:false,
 gpuScoreMultiplier:1,
});

const capacity=(g:any)=>g.count*(g.kind==='P'?1:g.kind==='E'?.68:.4);
export function smtEffect(id:string,core:any,enabled:boolean){
 const active=core.active||[],total=active.reduce((n:number,g:any)=>n+capacity(g),0);
 const eligible=active.filter((g:any)=>g.smt).reduce((n:number,g:any)=>n+capacity(g),0);
 const eligibleShare=total>0?eligible/total:0,available=K.topology(id).smt;
 // Baseline CPU curves already include SMT: turning it off is reciprocal uplift,
 // not subtraction of the uplift, and E/LP-E cores without HT are never penalized.
 const cpuFactor=enabled?1:1/(1+SMT_ASSUMPTIONS.perEligibleCoreUplift*eligibleShare);
 return {
  available,enabled:available&&enabled,activeSupported:eligible>0,eligibleShare,cpuFactor,
  activeThreads:active.reduce((n:number,g:any)=>n+g.count*(g.smt&&enabled?2:1),0),
  gpuScoreMultiplier:1,
  graphicsEfficiency:!enabled&&eligible>0?'possible-small-saving':'unchanged',
  graphicsEfficiencyPercent:null,
  calibration:available?'workload-prior-not-matched-TS':'not-applicable',
  measured:false,
 };
}
