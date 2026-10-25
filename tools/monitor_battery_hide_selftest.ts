// Sole-publisher parser and display-only hiding. No sensors, processes or timers.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import { createMonitorDataHub } from '@/bridge/monitorData';
import { hudFromUniformSnapshot, hudValue, hudVisibleFields, normalizeMonitorOutput, boundedHudData, makeHudFixture, HUD_METRICS, sameHudData } from '@/bridge/monitorHudModel';
const now=1_700_000_000_000;
let sequence=0, assertions=0;
function check(value:unknown,label:string){assert(value,label);++assertions;}
const hub=createMonitorDataHub({now:()=>now,invoke:async()=>{throw Error('no IPC expected')},readTextFile:async()=>{throw Error('no file reader expected')},setTimer:()=>0,clearTimer:()=>{}});
function snapshot(patch:Record<string,unknown>={}) {
 return hub.admitDisplayPublication({schema:1,available:true,generation:1,seq:++sequence,collectedAt:now,hwinfoOkAt:now,heartbeatAt:0,topActive:true,fpsActive:false,hwDown:true,
 top:JSON.stringify({ts:now-10000,hwDown:true,hasBattery:true,batteryPercent:75,batteryStatusTs:now,batteryPowerValid:true,batteryPowerEstimated:true,batteryPowerTs:now,batteryPowerSource:'energy-delta',batteryPowerWindowMs:30000,chargeW:-24,...patch}),fps:null});
}
let view=snapshot();assert(view);let data=hudFromUniformSnapshot(view,now);
check(data.values.batteryPercent===75,'Windows percentage remains available when HWiNFO is down');
check(hudValue(data,'batteryPower')==='≈-24.0','30 s average is explicitly approximate');
check(boundedHudData(data).batteryPowerEstimated===true,'bounded IPC renderer data preserves approximate marker');
for(const invalid of [undefined,null,'-24','',NaN,Infinity,251]) {
 view=snapshot({chargeW:invalid});assert(view);check(view.top?.batteryPowerValid===false,'explicit validity cannot turn malformed power into fake 0');
 check(hudValue(hudFromUniformSnapshot(view,now),'batteryPower')==='—','malformed power displayed unknown');
}
view=snapshot({chargeW:0,batteryPowerEstimated:false,batteryPowerSource:'windows-rate'});assert(view);
check(view.top?.batteryPowerValid===true&&hudValue(hudFromUniformSnapshot(view,now),'batteryPower')==='0.0','valid idle zero stays visible');
view=snapshot({batteryPowerValid:false,chargeW:0});assert(view);check(hudValue(hudFromUniformSnapshot(view,now),'batteryPower')==='—','invalid default zero is not data');
for(const ts of [now-6000,now+1,0]) {
 view=snapshot({batteryPowerTs:ts,batteryStatusTs:ts});assert(view);const expired=hudFromUniformSnapshot(view,now);
 check(hudValue(expired,'batteryPower')==='—'&&hudValue(expired,'batteryPercent')==='—','past/future/unknown battery timestamps reject stale values');
}
view=snapshot({hasBattery:false});assert(view);check(hudValue(hudFromUniformSnapshot(view,now),'batteryPower')==='—','desktop cannot gain battery data');
const prefs=normalizeMonitorOutput({ymcc:{hideUnavailable:true,fields:Object.fromEntries(HUD_METRICS.map(metric=>[metric.id,true]))}}),selected=structuredClone(prefs.ymcc.fields);
const empty={...makeHudFixture(),stale:false,values:{},coreUsage:[],coreClocks:[],frameTimes:[]};
const invisible=hudVisibleFields(selected,empty,true);check(HUD_METRICS.filter(metric=>metric.id!=='fps').every(metric=>invisible[metric.id]===false),'missing non-FPS metrics and graphs all hide');
check(invisible.fps===selected.fps&&hudValue(empty,'fps')==='—','selected FPS retains only an unknown placeholder without measurements');
assert.deepEqual(prefs.ymcc.fields,selected);++assertions;
const restored={...empty,values:{batteryPower:0,cpuPower:0,gpuUsage:0},coreUsage:[0,null]};
const visible=hudVisibleFields(selected,restored,true);
check(visible.batteryPower&&visible.cpuPower&&visible.cpuCores&&visible.gpuBar,'available zero and zero core tile recover without changing selection');
check(!hudVisibleFields(selected,{...empty,coreUsage:[...Array(16).fill(null),50]},true).cpuCores,'unavailable first 16 visible core tiles hide; hidden core 17 cannot fabricate a visible rail');
check(hudVisibleFields(selected,empty,false).cpuPower,'disabled auto-hide retains user selection');
check(normalizeMonitorOutput(prefs).ymcc.hideUnavailable===true,'normalize/save/relay preserves generic preference');
assert.deepEqual(HUD_METRICS.map(metric=>metric.id),["fps","fpsLimit","fps1","frametime","frametimeGraph","tdpLimit","cpuPower","cpuTemp","cpuUsage","cpuUsageBar","cpuClock","cpuVoltage","fanSpeed","fanSpeedBar","cpuCores","gpuUsage","gpuBar","gpuPower","gpuTemp","gpuClock","gpuVoltage","vramUsed","vramTotal","vramAvailable","vramUsage","vramUsageBar","ramUsed","ramTotal","ramAvailable","ramUsage","ramUsageBar","netDown","netUp","diskRead","diskWrite","batteryPercent","batteryBar","batteryPower","batteryTime","clock"],'HUD metric selector roster remains exact');++assertions;

// Actual native publisher -> unchanged uniform hub -> shared HUD/bounded renderer.
if(process.env.YMCC_BATTERY_CHAIN_FIXTURE) {
 const chain=JSON.parse(fs.readFileSync(process.env.YMCC_BATTERY_CHAIN_FIXTURE,'utf8')) as Array<{name:string;top:Record<string,unknown>}>;
 for(const item of chain){view=snapshot(item.top);assert(view);const d=hudFromUniformSnapshot(view,now);const bounded=boundedHudData(d);
  if(item.name==='model'){
   check(view.top?.batteryPowerSource==='package-power-model','model source survives sole publisher parser');
   check(hudValue(d,'batteryPower')==='≈-57.0'&&hudValue(d,'batteryTime')==='≈63','model and runtime use independent explicit approximate markers');
   check(hudValue(bounded,'batteryTime')==='≈63','approximate runtime survives IPC bounding');
   check(!sameHudData(d,{...d,batteryTimeEstimated:false}),'runtime marker change triggers renderer update without breaking stable-data suppression');
  } else if(item.name==='real'){
   check(hudValue(d,'batteryPower')==='-24.0'&&hudValue(d,'batteryTime')==='≈150','real signed power and approximate runtime compatible while HWiNFO is down');
  } else if(item.name==='no-capacity'){
   check(hudValue(d,'batteryPower')==='≈-57.0'&&hudValue(d,'batteryTime')==='—','power model does not fabricate battery capacity or runtime');
  } else if(item.name==='percentage-with-reported-full-capacity'){
   check(hudValue(d,'batteryTime')==='≈63','percentage plus reported full capacity gives model-compatible runtime');
  } else if(item.name==='no-battery-stale-power'||item.name==='unknown-percent'){
   check(hudValue(d,'batteryPower')==='—'&&hudValue(d,'batteryTime')==='—','publisher rejects stale power and time when battery is not confirmed');
  } else if(item.name==='charging'){
   check(hudValue(d,'batteryPower')==='24.0'&&hudValue(d,'batteryTime')==='—','charging watts do not become discharge lifetime');
  } else if(item.name==='empty'){
   check(hudValue(d,'batteryTime')==='≈0','confirmed empty battery zero time remains valid and visible');
  }
 }
}
const timePatch={ac:0,chargeW:-24,batteryPowerEstimated:false,batteryPowerSource:'windows-rate',remainMin:125,batteryTimeValid:true,batteryTimeEstimated:true,batteryTimeTs:now,batteryTimeSource:'energy-power'};
for(const ts of [now-6000,now+1,0]){
 view=snapshot({...timePatch,batteryTimeTs:ts});assert(view);data=hudFromUniformSnapshot(view,now);
 check(hudValue(data,'batteryPower')==='-24.0'&&hudValue(data,'batteryTime')==='—','runtime own timestamp does not inherit fresh watt timestamp');
}
for(const invalid of [undefined,null,'125',NaN,Infinity,-1,10081]){
 view=snapshot({...timePatch,remainMin:invalid});assert(view);
 check(view.top?.batteryTimeValid===false&&hudValue(hudFromUniformSnapshot(view,now),'batteryTime')==='—','invalid minute payload is not coerced to fake zero');
}
view=snapshot({...timePatch,remainMin:0});assert(view);data=hudFromUniformSnapshot(view,now);
check(hudValue(data,'batteryTime')==='≈0'&&hudVisibleFields({...selected,batteryTime:true},data,true).batteryTime,'valid zero time survives auto-hide');
for(const patch of [{ac:1},{chargeW:24},{chargeW:0},{batteryPowerValid:false},{hasBattery:false}]){
 view=snapshot({...timePatch,...patch});assert(view);
 check(hudValue(hudFromUniformSnapshot(view,now),'batteryTime')==='—','AC/charging/zero/invalid power/no battery invalidate runtime');
}
console.log(JSON.stringify({ok:true,assertions,scope:'injected sole-publisher payloads and display-only field visibility; not real battery acceptance'}));
