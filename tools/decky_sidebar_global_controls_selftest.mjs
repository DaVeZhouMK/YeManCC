// Complete production host/relay/global adapter with memory-only IO owners.
import fs from 'node:fs';import path from 'node:path';import assert from 'node:assert/strict';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';import {task} from './fixtures/decky_task_paths.mjs';
const runId='a'.repeat(32),peer={connection:'1',runId},cases=[];let sequence=0;const flush=async()=>{for(let i=0;i<120;i++)await Promise.resolve();};
async function fixture(options={}){const f=await createMirrorHostFixture(options);f.state.game=null;f.ready();f.read=async()=>{f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return f.replies.at(-1).message.snapshot;};f.edit=async(field,value,expected)=>{const s=expected??await f.read(),n=++sequence;f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'global.setField',runId,clientId:'global',id:'global:'+n,sequence:n,generation:s.generation,revision:s.revision,identity:'',args:{field,value}}});await flush();return f.replies.findLast(r=>r.message.id==='global:'+n)?.message;};return f;}
async function check(name,fn){await fn();cases.push(name);}
await check('no-game real global schedule and persona publish without fake EXE or admission leak',async()=>{const f=await fixture(),s=await f.read();assert.equal(s.game,null);assert.equal(s.actions.global,true);assert.equal(s.global.fields.padPersona.value,'steamdeck');assert.deepEqual(Object.keys(s.global.fields).sort(),['gyroEnabled','gyroPreset','padPersona']);assert.ok(s.performance&&s.performance.scope==='global');assert.ok(!s.performance.controls.some(o=>o.key==='tdpCeiling'));assert.ok(!JSON.stringify(s).includes('controlAdmission'));assert.equal(f.counts.saves,0);f.stop();});
await check('legacy global AC mode is rejected; continuous performance uses its own owner',async()=>{const f=await fixture();const r=await f.edit('acMode','performance');assert.equal(r.ok,false);assert.equal(f.counts.globalPowerApplies,0);assert.equal(f.counts.saves,0);f.stop();});
await check('global input remains independent across AC/DC source changes',async()=>{const f=await fixture();f.state.powerSource='dc';const r=await f.edit('padPersona','elite');assert.equal(r.ok,true);assert.equal(f.counts.inputSaves,1);f.stop();});
await check('global persona uses original revision CAS and does not create an EXE profile',async()=>{const f=await fixture();const r=await f.edit('padPersona','elite');assert.equal(r.ok,true);assert.equal(f.state.input.outputTarget.persona,'elite');assert.equal(f.state.input.outputTarget.buttonMappingEnabled,true);assert.equal(f.counts.inputSaves,1);assert.equal(f.counts.saves,0);assert.equal(r.result.applied,false);assert.equal(r.result.pending,true);f.stop();});
await check('native persona stops mapping and gyro through original coupled settings patch',async()=>{const f=await fixture();f.state.input.outputTarget.gyroEnabled=true;f.state.input.gyroMotion.enabled=true;assert.equal((await f.edit('padPersona','disabled')).ok,true);assert.equal(f.state.input.outputTarget.buttonMappingEnabled,false);assert.equal(f.state.input.outputTarget.gyroEnabled,false);assert.equal(f.state.input.gyroMotion.enabled,false);f.stop();});
await check('global on transition respects the original opt-out gyro link',async()=>{const f=await fixture();f.state.input.outputTarget={persona:'disabled',buttonMappingEnabled:false,gyroEnabled:false};f.state.input.gyroMotion={enabled:false,virtualPadLink:false};assert.equal((await f.edit('padPersona','elite')).ok,true);assert.equal(f.state.input.gyroMotion.enabled,false);assert.equal(f.state.input.outputTarget.gyroEnabled,false);f.stop();});
await check('foreign input edit after snapshot is rejected at private source validation',async()=>{const f=await fixture(),s=await f.read();f.state.input.revision++;assert.equal((await f.edit('padPersona','elite',s)).ok,false);assert.equal(f.counts.inputSaves,0);f.stop();});
await check('global packet cannot be used with a current confirmed game',async()=>{const f=await fixture(),s=await f.read();f.state.game={name:'Other',path:'C:/Other.exe',pid:123,processCreated:'other'};f.gameChanged();assert.equal((await f.edit('padPersona','elite',s)).ok,false);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.saves,0);f.stop();});
await check('power transition refuses all global input entry points',async()=>{for(const [field,value] of [['padPersona','elite'],['gyroEnabled','on'],['gyroPreset','racing']]){const f=await fixture();f.state.power.phase='pending';f.state.power.hardwareWritesAllowed=false;assert.equal((await f.edit(field,value)).ok,false);assert.equal(f.counts.inputSaves,0);f.stop();}});
await check('malformed and legacy values never reach original owners',async()=>{for(const [field,value] of [['padPersona','follow'],['tdpHack','999'],['acMode','follow'],['gyroPreset','invented']]){const f=await fixture();assert.equal((await f.edit(field,value)).ok,false);assert.equal(f.counts.inputSaves,0);assert.equal(f.counts.globalPowerApplies,0);f.stop();}});

await check('global virtual-controller dropdown matches exactly the four reachable primary YMCC choices',async()=>{const f=await fixture(),s=await f.read();assert.deepEqual(s.global.fields.padPersona.choices.map(o=>o.data),['disabled','steamdeck','dualsense-edge','elite']);assert.deepEqual(s.global.fields.padPersona.choices.map(o=>o.label),['关闭虚拟手柄','SteamDeck','PS5','Xbox']);for(const value of ['dualsense','dualshock4','xbox360'])assert.equal((await f.edit('padPersona',value)).ok,false);assert.equal(f.counts.inputSaves,0);f.stop();});
await check('no-game global gyro exposes ON switch and exactly the four original preset IDs',async()=>{const f=await fixture(),s=await f.read();assert.equal(s.global.fields.gyroEnabled.supported,true);assert.deepEqual(s.global.fields.gyroPreset.choices.map(o=>o.data),['fps','racing','custom','steam']);assert.equal((await f.edit('gyroEnabled','on')).ok,true);assert.equal(f.state.input.outputTarget.gyroEnabled,true);assert.equal(f.state.input.gyroMotion.enabled,true);assert.equal(f.state.input.gyroMotion.outputMode,'virtual-stick');assert.equal(f.counts.saves,0);f.stop();});
await check('global gyro preset expands the existing selected preset memory rather than inventing params or changing persona',async()=>{const f=await fixture();Object.assign(f.state.input.outputTarget,{gyroEnabled:true});Object.assign(f.state.input.gyroMotion,{enabled:true,preset:'fps',presets:{racing:{motionMode:'suppress',motionTrigger:'LT',gyroMultiplier:77,outputStick:'left',outputAxis:'x'}}});const target=structuredClone(f.state.input.outputTarget),memories=structuredClone(f.state.input.gyroMotion.presets);assert.equal((await f.edit('gyroPreset','racing')).ok,true);assert.equal(f.state.input.gyroMotion.preset,'racing');assert.equal(f.state.input.gyroMotion.activePreset,'racing');assert.equal(f.state.input.gyroMotion.motionMode,'on');assert.equal(f.state.input.gyroMotion.motionTrigger,'LT');assert.equal(f.state.input.gyroMotion.gyroMultiplier,77);assert.deepEqual(f.state.input.gyroMotion.presets,memories);assert.deepEqual(f.state.input.outputTarget,target);assert.equal(f.counts.saves,0);f.stop();});
await check('global gyro OFF keeps selected preset and its memories; ON restores original virtual-stick mode',async()=>{const f=await fixture();f.state.input.outputTarget.gyroEnabled=true;f.state.input.gyroMotion={enabled:true,preset:'steam',activePreset:'steam',virtualPadLink:false,presets:{steam:{motionSensitivityX:42}}};const keep=structuredClone(f.state.input.gyroMotion);assert.equal((await f.edit('gyroEnabled','off')).ok,true);assert.deepEqual({...f.state.input.gyroMotion,enabled:false}, {...keep,enabled:false});assert.equal((await f.edit('gyroEnabled','on')).ok,true);assert.equal(f.state.input.gyroMotion.preset,'steam');assert.equal(f.state.input.gyroMotion.virtualPadLink,false);f.stop();});
await check('disabled virtual target and stale global gyro source are not bypassed',async()=>{for(const disabled of [true,false]){const f=await fixture();if(disabled)f.state.input.outputTarget.persona='disabled';const s=await f.read();if(!disabled)f.state.input.revision++;assert.equal((await f.edit('gyroEnabled','on',s)).ok,false);assert.equal(f.counts.inputSaves,0);f.stop();}});


const activeController=()=>({startProtection:{locked:false,requestId:0,phase:'idle',persona:'steamdeck',remainingMs:0,elapsedMs:0,maxMs:30000,settleMs:10000},state:'active',requestedPersona:'steamdeck',boundPersona:'steamdeck',hostAlive:true,hostEpoch:1,prepared:true,neutralized:true,frameAccepted:true,sourceLive:true,sourceCurrent:true,steamRunning:true});
const protection=(requestId,phase='pending',remainingMs=30000)=>({locked:true,requestId,phase,persona:'steamdeck',remainingMs,elapsedMs:0,maxMs:30000,settleMs:10000,expiresAtUnixMs:Date.now()+remainingMs});
await check('saved controller intent alone stays unconfirmed; real native ready evidence produces the same success status as YMCC',async()=>{
 const f=await fixture(),pending=await f.read();assert.equal(pending.virtualController.tone,'pending');assert.equal(pending.virtualController.locked,false);
 f.state.gamepadState={virtualOutput:activeController()};f.emit('gamepad.state',f.state.gamepadState);await flush();
 const ready=await f.read();assert.equal(ready.virtualController.text,'虚拟手柄-成功开启');assert.equal(ready.virtualController.tone,'on');assert.equal(f.counts.inputSaves,0);f.stop();
});
await check('native startup protection disables controller selection and denies a forged input command with zero saves',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:{...activeController(),state:'starting',startProtection:protection(4)}};
 const s=await f.read();assert.equal(s.virtualController.locked,true);assert.equal(s.global.fields.padPersona.supported,false);assert.equal(s.virtualController.tone,'pending');
 assert.equal((await f.edit('padPersona','elite',s)).ok,false);assert.equal(f.counts.inputSaves,0);assert.equal(f.controllerTimerCount(),1);f.stop();assert.equal(f.controllerTimerCount(),0);
});
await check('startup view expires at its bounded deadline, does not revive on duplicate telemetry and never reports a saved intent as success',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:{state:'starting',requestedPersona:'steamdeck',startProtection:protection(7)}};
 assert.equal((await f.read()).virtualController.locked,true);f.advanceController(29000);assert.equal((await f.read()).virtualController.locked,true);
 f.advanceController(1000);await flush();const expired=await f.read();assert.equal(expired.virtualController.locked,false);assert.notEqual(expired.virtualController.tone,'on');assert.equal(expired.global.fields.padPersona.supported,true);
 f.emit('gamepad.state',f.state.gamepadState);assert.equal((await f.read()).virtualController.locked,false);assert.equal(f.controllerTimerCount(),0);assert.equal(f.counts.inputSaves,0);f.stop();
});
await check('terminal native success retains the same ten-second settling protection then releases without any automatic input write',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:{...activeController(),startProtection:protection(8,'success')}};
 const s=await f.read();assert.equal(s.virtualController.text,'虚拟手柄-成功开启');assert.equal(s.virtualController.locked,true);
 f.advanceController(10000);assert.equal((await f.read()).virtualController.locked,false);assert.equal(f.counts.inputSaves,0);f.stop();
});
await check('older native request cannot replace a newer startup generation or release its UI lock',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:{state:'starting',requestedPersona:'steamdeck',startProtection:protection(10)}};await f.read();
 f.state.gamepadState={virtualOutput:{...activeController(),startProtection:{...protection(9,'success'),locked:false}}};f.emit('gamepad.state',f.state.gamepadState);
 const s=await f.read();assert.equal(s.virtualController.locked,true);assert.notEqual(s.virtualController.tone,'on');f.stop();assert.equal(f.controllerTimerCount(),0);
});
await check('identical controller telemetry does not invalidate snapshots at input sample rate',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:activeController()};await f.read();f.emit('gamepad.state',f.state.gamepadState);await flush();
 const before=f.replies.filter(r=>r.message.type==='snapshot').length;
 for(let i=0;i<120;i++){f.state.gamepadState.virtualOutput.sampledAt=i;f.emit('gamepad.state',f.state.gamepadState);}await flush();
 assert.equal(f.replies.filter(r=>r.message.type==='snapshot').length,before);assert.equal(f.controllerTimerCount(),0);f.stop();
});
await check('closing sidebar releases its request-scoped controller deadline timer and never issues a retry',async()=>{
 const f=await fixture();f.state.gamepadState={virtualOutput:{state:'starting',requestedPersona:'steamdeck',startProtection:protection(11)}};await f.read();assert.equal(f.controllerTimerCount(),1);
 f.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId,subscribe:false}});await flush();assert.equal(f.controllerTimerCount(),0);assert.equal(f.counts.inputSaves,0);f.stop();
});

await check('own native startup lease acquired by a successful input commit cannot turn the saved intent into a failed command',async()=>{
 let owner;const f=await fixture({readHooks:{inputCommit:async()=>{owner.state.gamepadState={virtualOutput:{state:'starting',requestedPersona:'elite',startProtection:{...protection(51),persona:'elite'}}};owner.emit('gamepad.state',owner.state.gamepadState);}}});owner=f;
 const r=await f.edit('padPersona','elite');assert.equal(f.counts.inputSaves,1);assert.equal(f.state.input.outputTarget.persona,'elite');assert.equal(r.ok,true,JSON.stringify(r));assert.equal(r.result.saved,true);assert.equal(r.result.applied,false);assert.equal(r.result.pending,true);assert.equal((await f.read()).virtualController.locked,true);f.stop();assert.equal(f.controllerTimerCount(),0);
});
for(const mode of ['refused','throws'])await check('a '+mode+' original shortcut-clear keeps durable input truth but never reports active output or automatically retries',async()=>{
 const f=await fixture();f.state.input.applyStatus='active';if(mode==='refused')f.state.shortcutClearResult={ok:false,reason:'virtual-start-protection-active'};else f.state.shortcutClearError=true;
 const r=await f.edit('padPersona','elite');assert.equal(r.ok,true,JSON.stringify(r));assert.equal(r.result.saved,true);assert.equal(r.result.applied,false);assert.equal(r.result.pending,true);assert.equal(f.counts.inputSaves,1);assert.equal(f.counts.shortcutClears,1);f.stop();
});


await check('native clear ACK alone arms the same bounded startup timer when the gamepad event feed is delayed',async()=>{
 const f=await fixture();f.state.shortcutClearResult={ok:true,startProtection:{...protection(61),persona:'elite'}};
 const r=await f.edit('padPersona','elite');assert.equal(r.ok,true);assert.equal(r.result.saved,true);assert.equal(f.controllerTimerCount(),1);assert.equal((await f.read()).virtualController.locked,true);
 f.advanceController(30000);await flush();assert.equal((await f.read()).virtualController.locked,false);assert.equal(f.counts.shortcutClears,1);assert.equal(f.counts.inputSaves,1);f.stop();assert.equal(f.controllerTimerCount(),0);
});

fs.writeFileSync(path.join(task,'validation/GLOBAL-CONTROLS.json'),JSON.stringify({pass:true,cases:cases.length,results:cases,actualProductionHostRelayAdapter:true,memoryIoBoundaries:true,physicalHardwareWrites:0,realConfigWrites:0},null,2));console.log('Global controls PASS '+cases.length);
