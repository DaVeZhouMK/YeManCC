// Execute the production host -> relay -> mirror adapter -> original owner.
// Only in-memory file/process/window boundaries are installed; no real settings IO.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {task} from './fixtures/decky_task_paths.mjs';
import {createOriginalLosslessMemoryOwner} from './fixtures/decky_lossless_memory_owner.mjs';
import {createMirrorHostFixture} from './fixtures/decky_mirror_host_fixture.mjs';
const gamePath='C:\\Games\\Game.exe',target={pid:101,processCreated:'123456789',path:gamePath};
const results=[],flush=async()=>{for(let i=0;i<400;i++)await Promise.resolve();};
async function check(name,run){await run();results.push(name);}
function deferred(){let resolve;const promise=new Promise(yes=>{resolve=yes;});return {promise,resolve};}
function assertInert(f){const s=f.stats();assert.equal(s.realFilesWritten,0);assert.equal(s.realProcessesStarted,0);assert.equal(s.physicalHardwareWrites,0);}
await check('Xbox UIA unavailable: original owner does not falsely confirm scaling when UIA is unavailable',async()=>{
 const f=createOriginalLosslessMemoryOwner({uiReadable:false});
 const on=await f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target);
 assert.equal(on.confirmed,false);assert.equal(on.runtimeConfirmed,null);assert.match(on.notice,/配置已开启.*实际插帧.*待确认/);
 const state=await f.owner.getLosslessGameState(gamePath);
 assert.equal(state.configuredEnabled,true);assert.equal(state.running,true);assert.equal(state.enabled,null);assert.equal(state.controlAvailable,true);
 const off=await f.owner.setLosslessScalingEnabled(gamePath,false,()=>{},target);
 assert.equal(off.confirmed,true);assert.equal(off.runtimeConfirmed,false);assert.equal(f.stats().running,false);assert.equal(f.stats().profilePresent,true);assert.equal(f.stats().profileEnabled,false);
 assert.match(f.readMemoryXml(),/<Unknown>retained<\/Unknown>/);assert.match(off.notice,/参数已保留/);
 assert.equal(f.stats().events.filter(e=>e==='original-launch').length,1);assert.equal(f.stats().events.filter(e=>e==='original-normal-close').length,1);
 assert.equal(f.stats().memoryProfileWrites,1);assert.deepEqual(f.stats().returnTargets,[]);assertInert(f);
});
await check('valid XML without Profile can be enabled then precisely disabled through original owner',async()=>{
 const f=createOriginalLosslessMemoryOwner({withProfile:false,uiReadable:false});
 const initial=await f.owner.getLosslessGameState(gamePath);assert.equal(initial.profile,null);assert.equal(initial.controlAvailable,true);assert.equal(initial.configuredEnabled,false);
 const on=await f.owner.setLosslessScalingEnabled(gamePath,true);assert.equal(on.confirmed,false);assert.equal(on.runtimeConfirmed,null);assert.equal(f.stats().profilePresent,true);
 const off=await f.owner.setLosslessScalingEnabled(gamePath,false);assert.equal(off.confirmed,true);assert.equal(f.stats().running,false);assert.equal(f.stats().profilePresent,true);assert.equal(f.stats().profileEnabled,false);assert.equal(f.stats().memoryProfileWrites,2);assertInert(f);
});
await check('unknown runtime without current owner proof refuses to close or write',async()=>{
 const f=createOriginalLosslessMemoryOwner({initialRunning:true,uiReadable:false});
 await assert.rejects(f.owner.setLosslessScalingEnabled(gamePath,false),/状态|未确认|不可读/);
 assert.equal(f.stats().running,true);assert.equal(f.stats().profileXmlUnchanged,true);assert.equal(f.stats().memoryProfileWrites,0);assert.ok(!f.stats().events.includes('original-normal-close'));assertInert(f);
});
await check('PID or birth replacement invalidates ownership before unknown-runtime off',async()=>{
 for(const replacement of [{pid:72},{processCreated:'new-birth'}]){
  const f=createOriginalLosslessMemoryOwner({uiReadable:false});await f.owner.setLosslessScalingEnabled(gamePath,true);f.replaceProcess(replacement);
  await assert.rejects(f.owner.setLosslessScalingEnabled(gamePath,false),/状态|未确认|不可读/);assert.equal(f.stats().running,true);assert.equal(f.stats().memoryProfileWrites,0);assertInert(f);
 }
});
await check('normal-close failure cannot delete configuration or claim success',async()=>{
 const f=createOriginalLosslessMemoryOwner({uiReadable:false});await f.owner.setLosslessScalingEnabled(gamePath,true);f.setCloseDenied(true);
 await assert.rejects(f.owner.setLosslessScalingEnabled(gamePath,false),/normal close refused/);assert.equal(f.stats().running,true);assert.equal(f.stats().profilePresent,true);assert.equal(f.stats().memoryProfileWrites,0);assertInert(f);
});
await check('launch refusal preserves configured ON while keeping process and actual runtime OFF',async()=>{
 const f=createOriginalLosslessMemoryOwner({uiReadable:false});f.setLaunchDenied(true);await assert.rejects(f.owner.setLosslessScalingEnabled(gamePath,true),/启动|失败/);
 const state=await f.owner.getLosslessGameState(gamePath);assert.equal(state.configuredEnabled,true);assert.equal(state.running,false);assert.equal(state.enabled,false);assert.equal(f.stats().memoryProfileWrites,0);assertInert(f);
});
const runId='b'.repeat(32),peer={connection:'7',runId,connected:true};
async function hostFixture(options={}){
 const host=await createMirrorHostFixture(options);host.ready();let sequence=0;
 host.read=async()=>{host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'snapshot',runId}});await flush();return host.replies.filter(r=>r.message.type==='snapshot').at(-1)?.message.snapshot;};
 host.set=async(value,snapshot)=>{sequence++;const before=host.replies.length,s=snapshot??await host.read();
  host.emit('deckySidebar.mirrorRequest',{...peer,request:{command:'game.setField',runId,id:'lossless:'+sequence,clientId:'lossless',sequence,generation:s.generation,revision:s.revision,identity:s.game.identity,args:{field:'losslessScaling',value}}});
  await flush();return host.replies.slice(before).find(r=>r.message.type==='reply')?.message;
 };return host;
}
await check('production host success reply and field agree while actual runtime stays unknown',async()=>{
 const owner=createOriginalLosslessMemoryOwner({uiReadable:false}),host=await hostFixture({losslessOwner:owner});
 try{
  const initial=await host.read();assert.equal(initial.game.fields.losslessScaling.supported,true);assert.equal(initial.game.fields.losslessScaling.value,'off');
  const on=await host.set('on',initial);assert.equal(on.ok,true);assert.equal(on.result.applied,false);assert.equal(on.result.pending,false);assert.match(on.notice,/配置已开启.*实际插帧.*待确认/);
  const enabled=await host.read();assert.equal(enabled.game.fields.losslessScaling.value,'unknown');assert.equal(enabled.game.fields.losslessScaling.supported,true);assert.equal(enabled.notice,'');assert.equal(enabled.losslessProgram.known,true);assert.equal(enabled.losslessProgram.running,true);
  const off=await host.set('off',enabled);assert.equal(off.ok,true);assert.equal(off.result.applied,true);assert.equal(off.result.pending,false);assert.match(off.notice,/参数已保留/);
  const disabled=await host.read();assert.equal(disabled.game.fields.losslessScaling.value,'off');assert.equal(disabled.game.fields.losslessScaling.supported,true);assert.doesNotMatch(disabled.notice,/配置正在读取/);
  assert.equal(owner.stats().profilePresent,true);assert.equal(owner.stats().profileEnabled,false);assert.equal(owner.stats().running,false);assert.equal(owner.stats().returnTargets.length,0);assertInert(owner);
 }finally{host.stop();assert.equal(host.observerStats().ipc,0);}
});
await check('configured profile with LS exited displays OFF and still permits explicit idempotent cleanup',async()=>{
 const owner=createOriginalLosslessMemoryOwner({uiReadable:false}),host=await hostFixture({losslessOwner:owner});
 try{
  const state=await owner.owner.getLosslessGameState(gamePath);assert.equal(state.running,false);assert.equal(state.configuredEnabled,true);assert.equal(state.enabled,false);
  const initial=await host.read();assert.equal(initial.game.fields.losslessScaling.value,'off');assert.equal(initial.game.fields.losslessScaling.supported,true);
  const off=await host.set('off',initial);assert.equal(off.ok,true);assert.equal(off.result.applied,true);assert.equal(off.result.pending,false);assert.match(off.notice,/参数已保留/);
  const updated=await host.read();assert.equal(updated.game.fields.losslessScaling.value,'off');assert.equal(updated.game.fields.losslessScaling.supported,true);
  assert.equal(owner.stats().profilePresent,true);assert.equal(owner.stats().profileEnabled,false);assert.equal(owner.stats().memoryProfileWrites,1);assert.ok(!owner.stats().events.includes('original-normal-close'));assert.ok(!owner.stats().events.includes('original-launch'));assertInert(owner);
 }finally{host.stop();}
});
await check('production host valid no-Profile document exposes a live OFF toggle without fake reading notice',async()=>{
 const owner=createOriginalLosslessMemoryOwner({withProfile:false,uiReadable:false}),host=await hostFixture({losslessOwner:owner});
 try{const initial=await host.read();assert.equal(initial.game.fields.losslessScaling.supported,true);assert.equal(initial.game.fields.losslessScaling.value,'off');assert.doesNotMatch(initial.notice,/配置正在读取/);const on=await host.set('on',initial);assert.equal(on.ok,true);assert.equal((await host.read()).game.fields.losslessScaling.value,'unknown');assertInert(owner);}finally{host.stop();}
});
await check('production host retains same-key value during pending observation and disables only that field',async()=>{
 const pending=deferred();let hold=false;const host=await hostFixture({readHooks:{lossless:async()=>{if(hold)await pending.promise;}}});
 try{
  Object.assign(host.state.lossless,{configuredEnabled:true,running:true,enabled:null,controlAvailable:true});const first=await host.read();assert.equal(first.game.fields.losslessScaling.value,'unknown');assert.equal(first.game.fields.losslessScaling.supported,true);
  hold=true;const refreshing=await host.read();assert.equal(refreshing.game.fields.losslessScaling.value,'unknown');assert.equal(refreshing.game.fields.losslessScaling.supported,false);assert.equal(refreshing.ready,true);assert.equal(refreshing.actions.performance,true);
  Object.assign(host.state.lossless,{configuredEnabled:false,running:false,enabled:false});hold=false;pending.resolve();await flush();const updated=await host.read();assert.equal(updated.game.fields.losslessScaling.value,'off');assert.equal(updated.game.fields.losslessScaling.supported,true);
 }finally{pending.resolve();host.stop();}
});
await check('production host drops previous game and power-stage cached display immediately',async()=>{
 for(const transition of ['game','power']){
  const pending=deferred();let hold=false;const host=await hostFixture({readHooks:{lossless:async()=>{if(hold)await pending.promise;}}});
  try{
   Object.assign(host.state.lossless,{configuredEnabled:true,running:true,enabled:null,controlAvailable:true});await host.read();hold=true;
   if(transition==='game'){host.state.game={...host.state.game,pid:102,processCreated:'new',path:'C:/Games/Other.exe'};host.gameChanged();}
   else{host.state.power.phase='suspending';host.state.power.hardwareWritesAllowed=false;host.powerChanged(true);}
   const snapshot=await host.read();assert.equal(snapshot.game.fields.losslessScaling.value,'unknown');assert.equal(snapshot.game.fields.losslessScaling.supported,false);
  }finally{pending.resolve();host.stop();await flush();}
 }
});

await check('production mirror reports a manually running unbound instance without claiming scaling or writing rules',async()=>{
 const owner=createOriginalLosslessMemoryOwner({initialRunning:true,withProfile:false,uiReadable:false}),host=await hostFixture({losslessOwner:owner});
 try{const snapshot=await host.read();assert.equal(snapshot.losslessProgram.known,true);assert.equal(snapshot.losslessProgram.running,true);
  assert.equal(snapshot.game.fields.losslessScaling.supported,true);assert.equal(snapshot.game.fields.losslessScaling.value,'unknown');
  assert.equal(owner.stats().memoryProfileWrites,0);assert.equal(owner.stats().profilePresent,false);assert.equal(owner.stats().running,true);assertInert(owner);
 }finally{host.stop();}
});
await check('program lamp remains available without a confirmed game and does not start anything',async()=>{
 const host=await hostFixture();host.state.game=null;host.state.losslessProgram={known:true,running:true};host.gameChanged();
 try{const snapshot=await host.read();assert.equal(snapshot.game,null);assert.equal(snapshot.actions.game,false);assert.equal(snapshot.losslessProgram.known,true);assert.equal(snapshot.losslessProgram.running,true);}finally{host.stop();}
});
await check('failed process refresh cannot keep a stale green program lamp or block unrelated controls',async()=>{
 let fail=false;const host=await hostFixture({readHooks:{losslessProgram:async()=>{if(fail)throw Error('process receipt unavailable');}}});host.state.losslessProgram={known:true,running:true};
 try{let snapshot=await host.read();assert.equal(snapshot.losslessProgram.running,true);fail=true;host.emit('test.losslessChanged');snapshot=await host.read();
  assert.equal(snapshot.losslessProgram.known,false);assert.equal(snapshot.losslessProgram.running,false);assert.equal(snapshot.ready,true);assert.equal(snapshot.actions.performance,true);
 }finally{host.stop();}
});

await check('host preserves a returned pause hint without automatic restart and releases the field',async()=>{
 const owner=createOriginalLosslessMemoryOwner();owner.failActivations(1);const host=await hostFixture({losslessOwner:owner});
 try{const before=await host.read();const result=await host.set('on',before);assert.equal(result.result.applied,false);assert.equal(result.result.pending,false);assert.match(result.notice,/已保留LosslessScaling运行/);const after=await host.read();assert.equal(after.losslessProgram.running,true);assert.equal(after.game.fields.losslessScaling.supported,true);assert.equal(owner.stats().events.filter(e=>e==='original-launch').length,1);assert.equal(owner.stats().events.filter(e=>e==='original-timeout-reset').length,0);assert.equal(owner.stats().profileXmlUnchanged,true);assertInert(owner);}finally{host.stop();}
});
await check('host stops after one retry and releases field for another click',async()=>{
 const owner=createOriginalLosslessMemoryOwner();owner.failActivations(2,true);const host=await hostFixture({losslessOwner:owner});
 try{const before=await host.read();const result=await host.set('on',before);assert.equal(result.result.applied,false);assert.equal(result.result.pending,false);assert.match(result.notice,/自动重试 1 次/);const after=await host.read();assert.equal(after.game.fields.losslessScaling.supported,true);assert.equal(after.losslessProgram.running,false);assert.equal(owner.stats().events.filter(e=>e==='original-launch').length,2);assertInert(owner);}finally{host.stop();}
});
await check('host no-response path uses exact watchdog and retries once',async()=>{
 const owner=createOriginalLosslessMemoryOwner();owner.failActivations(1,true);const host=await hostFixture({losslessOwner:owner});
 try{const before=await host.read();const result=await host.set('on',before);assert.equal(result.result.applied,true);assert.equal(owner.stats().events.filter(e=>e==='original-timeout-reset').length,1);assert.equal(owner.stats().events.filter(e=>e==='original-launch').length,2);assertInert(owner);}finally{host.stop();}
});
await check('host retains the running instance when foreground changes after the request',async()=>{
 const owner=createOriginalLosslessMemoryOwner();owner.failActivations(1,false,'target-not-foreground');const host=await hostFixture({losslessOwner:owner});
 try{const before=await host.read();const result=await host.set('on',before);assert.equal(result.result.applied,false);assert.equal(result.result.pending,false);assert.equal(owner.stats().running,true);assert.equal(owner.stats().events.filter(e=>e==='original-timeout-reset').length,0);assert.equal(owner.stats().events.filter(e=>e==='original-launch').length,1);assert.equal((await host.read()).game.fields.losslessScaling.supported,true);assertInert(owner);}finally{host.stop();}
});
fs.writeFileSync(path.join(task,'validation/lossless-contract-selftest.json'),JSON.stringify({cases:results.length,results,productionHostAndOwnerExecuted:true,realFilesWritten:0,realProcessesStarted:0,hardwareWrites:0},null,2));
console.log('Decky Lossless contract PASS '+results.length);
