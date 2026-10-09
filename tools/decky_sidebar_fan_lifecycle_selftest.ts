// Actual unchanged FanHostLifecycle with inert adapter; no real Host or hardware.
import { strict as assert } from 'node:assert';
import fs from 'node:fs';
import { FanHostLifecycle, type FanHostLauncher } from '../src/bridge/fanHost';
import { createFanMirrorActions } from '../src/bridge/deckyFanActions';
import { NoIoHost } from './fixtures/fan_no_io_host';
import type { FanPreset } from '../src/bridge/fanFeature';
import type { PowerLifecycleState } from '../src/bridge/api';
const cases: string[] = [];
function check(value: unknown,name: string) { assert.ok(value,name);cases.push(name); }
const curves = {
  soft:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:15},{tempC:70,dutyPercent:30},{tempC:100,dutyPercent:70}],
  balanced:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:20},{tempC:70,dutyPercent:45},{tempC:100,dutyPercent:90}],
  aggressive:[{tempC:0,dutyPercent:0},{tempC:40,dutyPercent:30},{tempC:70,dutyPercent:75},{tempC:100,dutyPercent:100}],
};
const host = new NoIoHost(); const launcher:FanHostLauncher = {start:async()=>({pid:943,executable:'no-io-fixture.exe'}),stop:async()=>{}};
let power:PowerLifecycleState = {generation:1,phase:'ready',hardwareWritesAllowed:true,inputReady:true,resumeReady:false,hibernateAvailable:true};
const life = new FanHostLifecycle({enabled:true,launcher,adapter:host,heartbeatIntervalMs:5000,readNativePowerState:async()=>power,resumeWaitDeadlineMs:800});
let active = false, preset:FanPreset = 'balanced', saved = 0;
const action = createFanMirrorActions({owner:life,enabled:()=>active,currentPreset:()=>preset,curve:name=>structuredClone(curves[name]),
  save:async(_nodes,name)=>{preset=name;saved++;},updateActive:value=>{active=value;},updateDuty:()=>{},power:async()=>power,
  featureAllowed:()=>true,uiBusy:()=>false,display:()=>{}});
const context={generation:1,checkpoint:()=>{}};
async function main(){
  const offPreset=await action.execute('fan.setPreset',{preset:'soft'},context);
  check(offPreset.saved&&!offPreset.applied,'off preset saves without claiming active hardware');
  check(host.calls.length===0,'off preset never starts or calls adapter');
  const enabled=await action.execute('fan.setEnabled',{enabled:true},context);
  check(enabled.applied&&active,'original lifecycle acknowledged enable');
  check(host.calls.filter(x=>x==='enable').length===1,'exactly one original enable command');
  check(host.calls.filter(x=>x==='open').length===1&&host.calls.filter(x=>x==='events').length===1,'original Open/OpenEvents sequence retained');
  const lease=life.currentLease?.leaseId;
  check(Boolean(lease),'existing owner alone holds one lease');
  await action.execute('fan.setPreset',{preset:'aggressive'},context);
  check(preset==='aggressive'&&saved===2,'global settings retain selected existing preset');
  check(host.calls.filter(x=>x==='preset').length===1,'exactly one original applyPreset command');
  check(host.calls.filter(x=>x==='enable').length===1,'preset does not create a second enable path');
  check(life.currentLease?.leaseId===lease,'preset uses same original lease owner');
  await action.execute('fan.setEnabled',{enabled:false},context);
  check(!active,'mirror global active presentation follows disable');
  check(host.calls.includes('restore')&&host.calls.includes('release'),'original OEM restore/release sequence retained');
  check(life.currentLease===null,'original lifecycle releases its lease');
  const calls=host.calls.length;power={...power,generation:2,phase:'suspending',hardwareWritesAllowed:false};
  await action.execute('fan.setEnabled',{enabled:true},context).then(()=>{throw new Error('unsafe mutation unexpectedly succeeded');},()=>{});
  check(host.calls.length===calls,'power fence prevents all adapter calls');
  await life.close();
  check(host.calls.includes('close')&&host.calls.includes('shutdown'),'only original close path releases the inert fixture');
  fs.writeFileSync('G:/YeManCC-Work/Isolated/Tasks/YMCC-Decky-Sidebar/validation/FAN-ORIGINAL-LIFECYCLE-INTEGRATION.json',JSON.stringify({project:'YMCC Decky 侧边栏',cases:cases.length,results:cases,actualOriginalLifecycle:true,inertAdapter:true,realHardware:false,realHost:false},null,2));
  console.log(`Original FanHostLifecycle mirror integration passed: ${cases.length}`);
}
void main().catch(error=>{console.error(error);process.exitCode=1;});
