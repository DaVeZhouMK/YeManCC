// Execute the production host -> relay -> mirror adapter -> original owner.
// Only in-memory file/process/window boundaries are installed; no real settings IO.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
// Register callbacks before fixture imports so invalid exact selections have no side effects.
const gamePath='C:\\Games\\Game.exe',target={pid:101,processCreated:'123456789',path:gamePath};
const results=[],registered=[],flush=async()=>{for(let i=0;i<400;i++)await Promise.resolve();};
async function check(name,run){registered.push({name,run});}
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
   Object.assign(host.state.lossless,{configuredEnabled:true,running:true,enabled:null,controlAvailable:true});const initial=await host.read();hold=true;
   if(transition==='game'){host.state.game={...host.state.game,pid:102,processCreated:'new',path:'C:/Games/Other.exe'};host.gameChanged();}
   else{host.state.power.phase='suspending';host.state.power.hardwareWritesAllowed=false;host.powerChanged(true);}
   const snapshot=await host.read();assert.equal(snapshot.game.fields.losslessScaling.value,'unknown');
   if(transition==='game')assert.equal(snapshot.game.fields.losslessScaling.supported,false);
   else{
    // Native-not-ready retains same-game display, never write admissions.
    assert.equal(snapshot.ready,false);assert.equal(snapshot.actions.game,false);
    const denied=await host.set('off',initial);assert.equal(denied.ok,false);
    assert.match(denied.error,/状态已变化|状态正在恢复/);
    assert.equal(snapshot.gameAdmission,undefined);assert.equal(snapshot.controlAdmission,undefined);
   }
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

await check('open close open uses actual receipts and preserves the existing game parameters',async()=>{
 const f=createOriginalLosslessMemoryOwner(),original=f.readMemoryXml();
 const on=await f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target);assert.equal(on.confirmed,true);
 assert.equal((await f.owner.getLosslessGameState(gamePath)).enabled,true);
 const off=await f.owner.setLosslessScalingEnabled(gamePath,false,()=>{},target);assert.equal(off.confirmed,true);
 assert.equal(f.stats().profilePresent,true);assert.equal(f.stats().profileEnabled,false);
 assert.equal(f.readMemoryXml(),original.replace('<AutoScale>true</AutoScale>','<AutoScale>false</AutoScale>'));
 const reopened=await f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target);assert.equal(reopened.confirmed,true);
 assert.equal(f.readMemoryXml(),original);assertInert(f);
});
await check('remove active owned EXE after normal exit even when UIAutomation becomes unreadable',async()=>{
 const f=createOriginalLosslessMemoryOwner();await f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target);f.setUiReadable(false);
 const result=await f.owner.removeLosslessGameProfile(gamePath,()=>{},target);assert.equal(result.confirmed,true);
 assert.match(result.notice,/已解除/);assert.equal(f.stats().running,false);assert.equal(f.stats().profilePresent,false);
 assert.equal(f.stats().events.filter(e=>e==='original-normal-close').length,1);
 assert.match(f.readMemoryXml(),/<Unknown>retained<\/Unknown>/);assertInert(f);
});
await check('remove disabled EXE preserves the foreign game default and every unrelated byte',async()=>{
 const other='<Profile><Title>Other</Title><Path>D:/Other/Other.exe</Path><AutoScale>true</AutoScale><Custom>retain</Custom></Profile>';
 const current='<Profile><Title>Game</Title><Path>C:/Games/Game.exe</Path><AutoScale>false</AutoScale><Custom>7</Custom></Profile>';
 const xml='<Settings><Unknown>retained</Unknown><GameProfiles>'+current+other+'</GameProfiles></Settings>';
 const f=createOriginalLosslessMemoryOwner({settingsXml:xml});
 const result=await f.owner.removeLosslessGameProfile(gamePath,()=>{},target);assert.equal(result.confirmed,true);
 assert.equal(f.readMemoryXml(),xml.replace(current,''));assert.equal(f.stats().events.length,0);assertInert(f);
});
await check('already unbound is idempotent and does not close a foreign running instance',async()=>{
 const f=createOriginalLosslessMemoryOwner({withProfile:false,initialRunning:true,uiReadable:false}),original=f.readMemoryXml();
 const result=await f.owner.removeLosslessGameProfile(gamePath);assert.equal(result.confirmed,true);
 assert.equal(f.stats().running,true);assert.equal(f.stats().events.length,0);assert.equal(f.readMemoryXml(),original);assertInert(f);
});
await check('remove refuses foreign scaling and unowned unreadable sessions without profile writes',async()=>{
 for(const uiReadable of [true,false]){
  const f=createOriginalLosslessMemoryOwner({initialRunning:true,uiReadable});
  if(uiReadable)f.setObservedRuntime({button:'取消缩放',title:'Unrelated'});
  const original=f.readMemoryXml();await assert.rejects(f.owner.removeLosslessGameProfile(gamePath),/未确认|未关闭|另一 EXE|暂不可读/);
  assert.equal(f.stats().running,true);assert.equal(f.stats().memoryProfileWrites,0);assert.equal(f.readMemoryXml(),original);assertInert(f);
 }
});
await check('normal close refusal prevents removal and releases queue for a later explicit retry',async()=>{
 const f=createOriginalLosslessMemoryOwner();await f.owner.setLosslessScalingEnabled(gamePath,true);f.setCloseDenied(true);
 const original=f.readMemoryXml();await assert.rejects(f.owner.removeLosslessGameProfile(gamePath),/normal close refused/);
 assert.equal(f.readMemoryXml(),original);assert.equal(f.stats().running,true);f.setCloseDenied(false);
 assert.equal((await f.owner.removeLosslessGameProfile(gamePath)).confirmed,true);assertInert(f);
});
await check('duplicate unbind requests join one action and later ON recreates only an explicit binding',async()=>{
 const f=createOriginalLosslessMemoryOwner();const a=f.owner.removeLosslessGameProfile(gamePath),b=f.owner.removeLosslessGameProfile(gamePath);
 assert.equal(a,b);assert.equal((await a).confirmed,true);assert.equal(f.stats().memoryProfileWrites,1);
 assert.equal((await f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target)).confirmed,true);
 assert.equal(f.stats().profilePresent,true);assert.equal(f.stats().profileEnabled,true);assertInert(f);
});
await check('newer user intent supersedes an unstarted removal and target revocation cannot remove a profile',async()=>{
 const f=createOriginalLosslessMemoryOwner();const removal=f.owner.removeLosslessGameProfile(gamePath);
 const refused=assert.rejects(removal,/较新的/),on=f.owner.setLosslessScalingEnabled(gamePath,true,()=>{},target);
 await refused;assert.equal((await on).confirmed,true);assert.equal(f.stats().profilePresent,true);
 const before=f.readMemoryXml();await assert.rejects(f.owner.removeLosslessGameProfile(gamePath,()=>{throw Error('GAME_CHANGED');}),/GAME_CHANGED/);
 assert.equal(f.readMemoryXml(),before);assert.equal(f.stats().running,true);assertInert(f);
});
await check('production host converges open close and removal observations through the original owner',async()=>{
 const owner=createOriginalLosslessMemoryOwner(),host=await hostFixture({losslessOwner:owner});
 try{
  let snapshot=await host.read();assert.equal(snapshot.game.fields.losslessScaling.nextValue,'on');
  assert.equal((await host.set('on',snapshot)).result.applied,true);snapshot=await host.read();
  assert.equal(snapshot.game.fields.losslessScaling.value,'on');assert.equal(snapshot.game.fields.losslessScaling.nextValue,'off');
  assert.equal((await host.set('off',snapshot)).result.applied,true);snapshot=await host.read();
  assert.equal(snapshot.game.fields.losslessScaling.nextValue,'on');assert.equal(owner.stats().profilePresent,true);
  await owner.owner.removeLosslessGameProfile(gamePath);snapshot=await host.read();
  assert.equal(snapshot.game.fields.losslessScaling.nextValue,'on');assert.equal(owner.stats().profilePresent,false);
  assert.equal(snapshot.game.fields.losslessScaling.supported,true);assertInert(owner);
 }finally{host.stop();}
});

await check('actual Y-panel buttons open close remove and reopen using the shared original owner',async()=>{
 const owner=createOriginalLosslessMemoryOwner(),game={...target,name:'Game'},ui=createLosslessTopActions(owner,game);
 try{
  await ui.refresh();let toggle=ui.find('lossless-scaling'),remove=ui.find('lossless-remove');
  assert.equal(toggle.props['data-gp-col'],'0');assert.equal(remove.props['data-gp-col'],'1');
  assert.equal(ui.text(toggle).startsWith('一键开启小黄鸭'),true);assert.equal(toggle.props.disabled,false);
  assert.equal(ui.navigate('lossless-scaling',1),'trainer-menu');assert.equal(ui.navigate('lossless-remove',1),'speed-factor');
  assert.equal(ui.navigate('trainer-menu',-1),'lossless-scaling');assert.equal(ui.navigate('speed-factor',-1),'lossless-remove');
  const starting=toggle.props.onClick();assert.equal(ui.find('lossless-scaling').props.disabled,true);
  await toggle.props.onClick();await starting;await ui.refresh();toggle=ui.find('lossless-scaling');
  assert.equal(owner.stats().events.filter(e=>e==='original-launch').length,1);
  assert.equal(owner.stats().running,true);assert.equal(ui.text(toggle).startsWith('一键关闭小黄鸭'),true);
  await toggle.props.onClick();await ui.refresh();assert.equal(owner.stats().running,false);assert.equal(owner.stats().profilePresent,true);
  remove=ui.find('lossless-remove');assert.equal(remove.props.disabled,false);await remove.props.onClick();await ui.refresh();
  assert.equal(owner.stats().profilePresent,false);assert.equal(ui.find('lossless-remove').props.disabled,true);
  toggle=ui.find('lossless-scaling');assert.equal(toggle.props.disabled,false);await toggle.props.onClick();await ui.refresh();
  assert.equal(owner.stats().running,true);assert.equal(owner.stats().profilePresent,true);assert.equal(ui.events.filter(e=>e[0]==='status'&&e[1].error).length,0);
  ui.raw.policyGame.value=null;const count=owner.stats().events.length;
  assert.equal(ui.find('lossless-scaling').props.disabled,true);assert.equal(ui.find('lossless-remove').props.disabled,true);
  await ui.find('lossless-scaling').props.onClick();await ui.find('lossless-remove').props.onClick();assert.equal(owner.stats().events.length,count);assertInert(owner);
 }finally{ui.stop();}
});
const requested=[],args=process.argv.slice(2);
for(let i=0;i<args.length;i+=2){
 if(args[i]!=='--case'||!args[i+1]||args[i+1].startsWith('--')){console.error('Expected --case <exact original case name>');process.exit(2);}
 requested.push(args[i+1]);
}
if(new Set(requested).size!==requested.length||requested.some(name=>!registered.some(test=>test.name===name))){
 console.error('Unknown or duplicate exact Lossless case');process.exit(2);
}
const selected=requested.length?registered.filter(test=>requested.includes(test.name)):registered;
const {task}=await import('./fixtures/decky_task_paths.mjs');
const {createOriginalLosslessMemoryOwner,createLosslessTopActions}=await import('./fixtures/decky_lossless_memory_owner.mjs');
const {createMirrorHostFixture}=await import('./fixtures/decky_mirror_host_fixture.mjs');
for(const {name,run} of selected){await run();results.push(name);}
fs.writeFileSync(path.join(task,'validation/lossless-contract-selftest.json'),JSON.stringify({cases:results.length,results,
 selectedCases:selected.map(test=>test.name),skippedCases:registered.filter(test=>!selected.includes(test)).map(test=>test.name),
 fullSuite:selected.length===registered.length,productionHostAndOwnerExecuted:true,realFilesWritten:0,realProcessesStarted:0,hardwareWrites:0},null,2));
console.log('Decky Lossless contract PASS '+results.length);
