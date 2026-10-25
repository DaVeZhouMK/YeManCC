// Deterministic render-state comparisons; no timing, sensors, IPC or installed app.
import assert from 'node:assert/strict';
import {HUD_METRICS,makeHudFixture,boundedHudData,normalizeMonitorOutput,sameHudFields,sameHudAppearance,sameHudData,hudValue} from '@/bridge/monitorHudModel';
let assertions=0;function check(value:unknown,label:string){assert(value,label);++assertions;}
const prefs=normalizeMonitorOutput(),appearance=prefs.ymcc,baseline=boundedHudData(makeHudFixture());
check(sameHudAppearance(appearance,normalizeMonitorOutput(structuredClone(prefs)).ymcc),'equal appearance in different objects');
for(const [key,value] of Object.entries({position:2,scale:1.4,saturation:.5,opacity:.3,layout:'bar',radius:18,gap:16,hideUnavailable:!appearance.hideUnavailable})){
 const next=normalizeMonitorOutput(prefs);Object.assign(next.ymcc,{[key]:value});check(!sameHudAppearance(appearance,next.ymcc),'appearance delta '+key);
}
const showUnavailable=normalizeMonitorOutput(prefs).ymcc,hideUnavailable=normalizeMonitorOutput(prefs).ymcc;
showUnavailable.hideUnavailable=false;hideUnavailable.hideUnavailable=true;
check(!sameHudAppearance(showUnavailable,hideUnavailable),'hideUnavailable false -> true');
check(!sameHudAppearance(hideUnavailable,showUnavailable),'hideUnavailable true -> false');
for(const metric of HUD_METRICS){const next=normalizeMonitorOutput(prefs);next.ymcc.fields[metric.id]=!next.ymcc.fields[metric.id];check(!sameHudFields(appearance.fields,next.ymcc.fields)&&!sameHudAppearance(appearance,next.ymcc),'field selection '+metric.id);}
check(sameHudData(baseline,boundedHudData(structuredClone(baseline))),'equal owned data in distinct objects');
for(const metric of HUD_METRICS){const next=structuredClone(baseline);next.values[metric.id]=metric.id==='clock'?'23:59':typeof next.values[metric.id]==='number'?Number(next.values[metric.id])+1:1;check(!sameHudData(baseline,next),'metric delta '+metric.id);}
for(const [key,value] of Object.entries({source:'ymcc-uniform',stale:true,batteryPowerEstimated:true})){
 const next=structuredClone(baseline);Object.assign(next,{[key]:value});check(!sameHudData(baseline,next),'source/health/estimate delta '+key);
}
for(const key of ['coreUsage','coreClocks','frameTimes'] as const){const next=structuredClone(baseline);next[key][0]=987;check(!sameHudData(baseline,next),'array data '+key);next[key]=[];check(!sameHudData(baseline,next),'array length '+key);}
const labelled=structuredClone(baseline);labelled.coreLabels=['logical CPU 0'];check(!sameHudData(baseline,labelled),'publisher identities/aria labels');
const raw=makeHudFixture(),owned=boundedHudData(raw);raw.values.fps=1000;raw.coreUsage[0]=100;check(!sameHudData(owned,boundedHudData(raw)),'same input object mutated => new values still detected');check(owned.values.fps!==raw.values.fps,'input copy isolation stays intact');
const a=boundedHudData({...baseline,values:{batteryPower:NaN},coreUsage:[NaN,-1,0]}),b=boundedHudData({...baseline,values:{batteryPower:null},coreUsage:[null,null,0]});check(sameHudData(a,b),'unchanged normalized unknown/valid zero');
check(hudValue({...a,values:{batteryPower:-0}},'batteryPower')===hudValue({...a,values:{batteryPower:0}},'batteryPower'),'numeric signed zero equality is visually identical');
console.log(JSON.stringify({ok:true,assertions,scope:'owned normalized/bounded state comparisons; no I/O or CPU usage measurement'}));
