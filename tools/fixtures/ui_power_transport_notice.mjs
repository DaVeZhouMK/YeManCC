import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {observeUiPowerBuilds} from './ui_power_build_receipt.mjs';
import {createMemoryHost} from '../ymcc_sim/global/monitor-notice-lifecycle/memory-boundary.mjs';

const sha = data => createHash('sha256').update(data).digest('hex');
const entry = `export * as life from './src/bridge/uiLifecycle';
export {startMonitorNoticeHost} from './src/bridge/monitorNoticeHost';
export {getSharedPowerLifecycle,getSharedPowerLifecycleDiagnostics} from './src/bridge/powerLifecycleState';
export {setLogSink} from './src/bridge/ipc';
export {SHORTCUT_FEEDBACK_EVENT} from './src/bridge/shortcutActionFeedback';`;
const appPower = (generation, phase) => {
  const ready = phase === 'ready';
  return {known:true,phase,generation,suspendSerial:generation-1,
    publicationRevision:generation*10+Number(ready),observedNotificationSequence:generation*10+Number(ready),
    notificationPending:false,inputReady:ready,hardwareWritesAllowed:ready};
};
const tuple = snapshot => ['generation','suspendSerial','observedNotificationSequence'].map(key => snapshot?.[key]);

async function exercise(bundle, omitStop) {
  const row = {name:omitStop?'omitted-monitor-stop-detected':'current-ready-only-off-and-stop',checks:[],stages:{}};
  const check = (name, fn) => { fn(); row.checks.push(name); };
  let reader, memory, freezeRead = false;
  try {
    memory = createMemoryHost(bundle, {decide:request => freezeRead&&request.cmd==='power.lifecycle'?'hold-power-read':'success'}, {
      start(api) { reader = api.life.startUiPowerReconciliation(); return api.startMonitorNoticeHost(); },
      reply(cmd) { throw Error('UNEXPECTED_NOTICE_TRANSPORT_COMMAND:'+cmd); },
    });
    const {api, host, clock} = memory;
    const observe = name => row.stages[name] = {
      tuple:tuple(api.getSharedPowerLifecycle()),ui:structuredClone(api.life.getUiVisibilityState()),notice:memory.snapshot(),
      creates:memory.count('monitorNotice.create'),
    };
    const expectPower = (stage, generation, phase) => {
      assert.deepEqual(stage.tuple,tuple(appPower(generation,phase)));
      assert.equal(stage.ui.powerGeneration,generation);
      assert.equal(stage.ui.powerReady,phase==='ready');
    };
    const publish = async (generation, phase) => {
      memory.publishPowerSnapshot(appPower(generation,phase)); await clock.flush();
    };
    const sleep = async generation => {memory.nextGeneration(); await publish(generation,'suspended');};
    const own = generation => {
      const health = memory.health('owned-ready-'+generation);
      assert(health.recovered);assert.equal(health.observedGeneration,generation);assert.equal(health.windows,1);
      return health.ownerId;
    };
    memory.setPowerReadSnapshot(appPower(1,'ready'));
    await memory.ready();
    const initial = observe('initial');
    let first;
    check('initial Ready has one acknowledged monitor owner',()=>{
      expectPower(initial,1,'ready');first=own(1);assert.equal(initial.creates,1);
    });
    memory.feedback();await clock.flush();
    check('initial notice actually presented a frame',()=>assert.equal(memory.windows.get(first)?.visible,true));
    await sleep(2);
    const suspended = observe('sleep-2');
    check('suspend revokes UI and hides the retained notice',()=>{
      expectPower(suspended,2,'suspended');assert(suspended.notice.host.recovery.powerHeld);
      assert.equal(memory.windows.get(first)?.visible,false);assert.equal(suspended.notice.host.cleanup.pending,0);
      assert.equal(suspended.creates,1);
    });
    memory.externalLoss('simulated carrier loss during sleep');await clock.flush();
    const oldReady = appPower(2,'ready');
    memory.setPowerReadSnapshot(oldReady);freezeRead=true;
    const beforeRead = memory.count('power.lifecycle');
    memory.emit('window.shown');await clock.advance(1000);freezeRead=false;
    const [held,...other] = [...memory.operations.values()].filter(request=>request.cmd==='power.lifecycle');
    check('one actual IPC read is held before the next suspend',()=>{
      assert(held);assert.equal(other.length,0);assert.equal(memory.count('power.lifecycle'),beforeRead+1);
    });
    await sleep(3);await memory.lateReplies();
    const stale = observe('released-stale-read');
    check('the held IPC id delivers old Ready without reopening UI or notice',()=>{
      assert.deepEqual(memory.trace.find(event=>event.type==='native.response'&&event.requestId===held.id)?.result,oldReady);
      expectPower(stale,3,'suspended');assert.equal(stale.creates,1);assert.equal(memory.windows.size,0);
      assert(stale.notice.host.recovery.powerHeld);assert.equal(stale.notice.host.recovery.committedGeneration,1);
    });
    await publish(3,'ready');
    const current = observe('current-ready');
    let currentOwner;
    check('current Ready recovers exactly once with a new owned notice',()=>{
      expectPower(current,3,'ready');currentOwner=own(3);assert.notEqual(currentOwner,first);assert.equal(current.creates,2);
      assert.equal(current.notice.host.recovery.committedGeneration,3);
    });
    await publish(3,'ready');
    check('duplicate current publication cannot allocate another owner',()=>{
      assert.equal(own(3),currentOwner);assert.equal(memory.count('monitorNotice.create'),2);
    });
    host.update({...memory.preferences,popupEnabled:false});await clock.flush();
    await sleep(4);await publish(4,'ready');
    const off = observe('explicit-off');
    check('explicit OFF stays off while the same source reopens UI',()=>{
      expectPower(off,4,'ready');assert.equal(memory.windows.size,0);assert.equal(off.creates,2);
      assert.equal(off.notice.host.owner,null);assert.equal(off.notice.host.cleanup.pending,0);
    });
    host.update(memory.preferences);await clock.flush();
    check('explicit ON owns a live notice before stop',()=>{own(4);assert.equal(memory.count('monitorNotice.create'),3);});

    // Negative control removes only the owner stop call; both arms deliver the same next generation.
    if(!omitStop)memory.stop();
    await clock.flush();
    const stopped = observe('after-stop-attempt');
    await sleep(5);memory.externalLoss('simulated carrier loss after stop attempt');await clock.flush();await publish(5,'ready');
    const late = observe('post-stop-current-ready');
    check('post-stop current Ready still reached the other real consumer',()=>expectPower(late,5,'ready'));
    if(omitStop){
      check('omission leaves an owned notice and the next Ready allocates again',()=>{
        assert.equal(stopped.notice.remainingOwnedResources.windows,1);assert.equal(late.creates,4);own(5);
      });
    }else{
      check('stop retires the owned notice and late Ready cannot revive it',()=>{
        assert.equal(stopped.notice.remainingOwnedResources.windows,0);assert.equal(stopped.notice.host.owner,null);
        assert.equal(stopped.notice.host.cleanup.pending,0);assert.equal(late.creates,3);
        assert.equal(late.notice.remainingOwnedResources.windows,0);assert.equal(late.notice.host.owner,null);
        assert.equal(late.notice.host.recovery.committedGeneration,4);
      });
    }
  }catch(error){row.error=String(error.stack??error);}
  finally{
    if(memory){
      try{
        memory.stop();reader.stop();await memory.lateReplies();await memory.clock.flush();
        const cleanup=memory.outerResources();row.cleanup=cleanup;
        check('owned resources are zero; VM lifetime infrastructure is separate',()=>{
          assert.equal(cleanup.windows,0);assert.equal(cleanup.timers,0);assert.equal(cleanup.nativeOperations,0);
          assert.deepEqual(cleanup.domSubscriptions.window.host,[]);assert.deepEqual(cleanup.domSubscriptions.document.host,[]);
          assert.equal(cleanup.host.owner,null);assert.equal(cleanup.host.cleanup.pending,0);
          assert.equal(cleanup.moduleLifetimeWebViewMessageListeners,1);
        });
      }catch(error){row.cleanupError=String(error.stack??error);}
      row.trace=memory.trace;
    }
  }
  row.passed=!row.error&&!row.cleanupError;
  return row;
}

export async function runMonitorNoticeRecovery(root,out){
  fs.mkdirSync(out);
  const {build,receipt}=observeUiPowerBuilds(root);
  const compiled=await build({stdin:{contents:entry,resolveDir:root,loader:'ts'},bundle:true,write:false,
    platform:'browser',format:'iife',globalName:'NoticeLifecycleActual',metafile:true});
  const bundle=compiled.outputFiles[0].text;
  const cases=[await exercise(bundle,false),await exercise(bundle,true)];
  const fileInputs=Object.keys(compiled.metafile.inputs).filter(name=>name!=='<stdin>');
  const sourceHashes=Object.fromEntries(fileInputs.map(name=>[name,sha(fs.readFileSync(path.resolve(root,name)))]));
  const pass=cases.every(row=>row.passed);
  const result={pass,scope:'monitor-notice-recovery',boundary:'Real IPC, UI and notice in one VM; logical power and monitor carrier resources are simulated. No native fan, input, OS or hardware.',
    bundleSha256:sha(bundle),cases,sourceHashes,observedBuilds:receipt()};
  fs.writeFileSync(path.join(out,'ui-power-transport.json'),JSON.stringify(result,null,2)+'\n',{flag:'wx',flush:true});
  console.log(JSON.stringify({pass,scope:result.scope,cases:cases.map(({name,passed,checks,error,cleanupError})=>({name,passed,checks:checks.length,error,cleanupError}))}));
  return pass?0:1;
}
