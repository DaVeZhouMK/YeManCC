// Actual original FanHostLifecycle with delayed inert launcher, no real devices.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';import {createRequire} from 'node:module';
import {root,task} from './fixtures/decky_task_paths.mjs';import {load} from './fixtures/decky_game_memory_store.mjs';
const require=createRequire(path.join(root,'package.json')),{build}=require('esbuild');const out=path.join(task,'Build/decky-sidebar-slow-fan-fixture.cjs');
await build({entryPoints:[path.join(root,'tools/decky_sidebar_fan_fixture.ts')],outfile:out,bundle:true,platform:'node',format:'cjs',alias:{'@':path.join(root,'src')},logLevel:'warning'});
const {createOriginalFanFixture}=require(out);const {createFanMirrorActions}=load(fs.readFileSync(path.join(root,'src/bridge/deckyFanActions.ts'),'utf8'));
let resolve;const wait=new Promise(r=>resolve=r),fan=createOriginalFanFixture({startWait:wait}),displays=[];
const api=fan.featureBindings,adapter=createFanMirrorActions({owner:fan.lifecycle,enabled:()=>api.fanControlActive.value,currentPreset:()=>api.getFanFeatureSettings().preset,curve:api.getFanPresetCurve,save:api.saveFanCurve,updateActive:api.setFanControlActive,updateDuty:api.setFanNavigationDuty,power:async()=>({generation:1,phase:'ready',hardwareWritesAllowed:true}),featureAllowed:()=>true,uiBusy:()=>false,display:value=>displays.push(value)});
const flush=async()=>{for(let i=0;i<500;i++)await Promise.resolve();},context={generation:1,checkpoint:()=>{}};let report;
try{
 const ack=await adapter.request('fan.setEnabled',{enabled:true},context);await flush();assert.equal(ack.pending,true);assert.equal(ack.applied,false);assert.equal(fan.stats().active,true);assert.equal(fan.stats().lease,null);assert.equal(fan.stats().calls.filter(c=>c==='enable').length,0);
 const preset=await adapter.request('fan.setPreset',{preset:'aggressive'},context);assert.equal(preset.pending,true);assert.equal(fan.stats().saveCount,1);resolve();await flush();
 // Yield only to the original lifecycle's own queue; no mirror-side polling/task.
 await new Promise(r=>setImmediate(r));await flush();
 assert.equal(fan.stats().active,true);assert.equal(fan.stats().preset,'aggressive');assert.ok(fan.stats().lease);assert.equal(fan.lifecycle.controlReady,true);assert.equal(fan.stats().calls.filter(c=>c==='handshake').length,1);assert.equal(fan.stats().calls.filter(c=>c==='acquire').length,1);assert.equal(displays.at(-1).pending,false);
 const off=await adapter.request('fan.setEnabled',{enabled:false},context);assert.equal(off.pending,true);assert.equal(off.applied,false);assert.equal(fan.stats().active,false);await flush();await new Promise(r=>setImmediate(r));await flush();assert.equal(fan.stats().lease,null);
 report={status:'passed',cases:3,actualOriginalFanHostLifecycle:true,delayedColdLauncher:true,immediateOnIntentAcknowledged:true,latestPresetAppliedByOriginalOwner:true,oneLeaseAndHandshake:true,explicitOffConfirmed:true,physicalHardwareWrites:0,realFanProcessLaunched:false};
}finally{adapter.stop();resolve();await fan.close();}
fs.writeFileSync(path.join(task,'validation/SLOW-ORIGINAL-FAN.json'),JSON.stringify(report,null,2));console.log('Slow original FanHostLifecycle PASS '+report.cases);
