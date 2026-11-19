import assert from 'node:assert/strict';
import path from 'node:path';
// Reuses the original R1 eight semantic/lifecycle controls with the current
// complete engine and real Vue components, through the existing UI entry.
export async function overlayPresentationCases({height,open,actionCase,state,page,task}){
  const f=await open('/schedule',height,{nativeWidth:'1'});
  await f.evaluate(()=>{window.__gamepadAudit.startNative();Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false});});
  let session='1001',seq=0;
  const test=async(name,work)=>actionCase('Overlay: '+name,height,work);
  const enter=async()=>{await f.evaluate(session=>{
    window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:true,session}}));
    window.dispatchEvent(new CustomEvent('ipc:window.shown'));
    window.dispatchEvent(new CustomEvent('ipc:gamepad.summon'));
  },session);await page.waitForTimeout(80);};
  const send=async(action,override=session)=>{await f.evaluate(({action,id,token})=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action,uiSeq:id,provenance:{session:'owned-h5-probe',overlaySession:token}}})),{action,id:++seq,token:override});await page.waitForTimeout(70);};
  const press=async locator=>{await locator.evaluate(el=>window.__gamepadAudit.focus(el));await send('confirm');};
  const route=()=>f.evaluate(()=>window.__currentAuditRoute());
  await enter();
  await test('native-summon-retained-without-document-focus',async()=>{
    const s=await f.evaluate(()=>window.__overlaySessionAudit());assert.ok(s.overlay&&s.summon);assert.equal(await f.evaluate(()=>document.hasFocus()),false);
    { // native-overlay-survives-document-hidden-before-summon
    const ordered=f;
    await ordered.evaluate(()=>window.__navigate('/schedule'));
    await ordered.locator('.continuous-performance').waitFor();
    const ready=await ordered.evaluate(()=>{
      window.__gamepadAudit.startNative();
      window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
      Object.defineProperty(document,'hasFocus',{configurable:true,value:()=>false});
      window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:true,session:'2301'}}));
      // Browser visibility is a controlled port; native presentation remains current.
      Object.defineProperty(document,'visibilityState',{configurable:true,value:'hidden'});
      document.dispatchEvent(new Event('visibilitychange'));
      window.dispatchEvent(new CustomEvent('ipc:window.shown'));
      window.dispatchEvent(new CustomEvent('ipc:gamepad.summon'));
      return window.__overlaySessionAudit();
    });
    assert.ok(ready.overlay&&ready.summon);
    assert.equal(ready.session,'2301');
    await ordered.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'page-next',uiSeq:1,provenance:{session:'visibility-order',overlaySession:'2301'}}})));
    await ordered.waitForFunction(()=>window.__currentAuditRoute()==='/fan');
    await ordered.evaluate(()=>{delete document.visibilityState;document.dispatchEvent(new Event('visibilitychange'));});
    }
    { // explicit-native-hide-cancels-overlay-and-queued-action
    const ordered=f;
    await ordered.evaluate(()=>window.__navigate('/schedule'));
    await ordered.locator('.continuous-performance').waitFor();
    const closed=await ordered.evaluate(()=>{
      window.__gamepadAudit.startNative();
      window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
      window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:true,session:'2302'}}));
      window.dispatchEvent(new CustomEvent('ipc:window.shown'));
      window.dispatchEvent(new CustomEvent('ipc:gamepad.summon'));
      window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'page-next',uiSeq:1,provenance:{session:'visibility-order',overlaySession:'2302'}}}));
      window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
      return window.__overlaySessionAudit();
    });
    assert.equal(closed.overlay,false);assert.equal(closed.summon,false);
    await page.waitForTimeout(100);
    assert.equal(await ordered.evaluate(()=>window.__currentAuditRoute()),'/schedule');
    }
    { // ordinary-document-hidden-still-clears-owner
    const ordered=f;
    await ordered.evaluate(()=>window.__navigate('/schedule'));
    await ordered.locator('.continuous-performance').waitFor();
    const result=await ordered.evaluate(()=>{
      window.__gamepadAudit.startNative();
      window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
      window.dispatchEvent(new CustomEvent('ipc:window.shown'));
      const before=window.__overlaySessionAudit();
      Object.defineProperty(document,'visibilityState',{configurable:true,value:'hidden'});
      document.dispatchEvent(new Event('visibilitychange'));
      return {before,after:window.__overlaySessionAudit()};
    });
    assert.equal(result.after.overlay,false);assert.equal(result.after.summon,false);
    assert.ok(result.after.owner.epoch>result.before.owner.epoch);
    await ordered.evaluate(()=>{delete document.visibilityState;document.dispatchEvent(new Event('visibilitychange'));});
    }
    await f.evaluate(()=>window.__navigate('/schedule'));
    await f.locator('.continuous-performance').waitFor();
    await enter();
  });
  await test('old-session-native-event-rejected',async()=>{await send('page-next','1000');assert.equal(await route(),'/schedule');});
  await test('slider-edit-survives-nonactivating-blur',async()=>{
   const slider=f.locator('.continuous-performance input[type=range]').first();await slider.waitFor();const before=await slider.inputValue();await press(slider);
   assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),true);await send('slider-decrease');assert.notEqual(await slider.inputValue(),before);
   const epoch=await f.evaluate(()=>window.__overlaySessionAudit().owner.epoch);
   await f.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.blur')));
   assert.equal(await f.evaluate(()=>window.__overlaySessionAudit().owner.epoch),epoch);assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),true);
   await send('back');assert.equal(await slider.inputValue(),before);assert.equal(await f.evaluate(()=>window.__gamepadAudit.editing()),false);
  });
  await test('formal-native-navigation-switches-one-page',async()=>{await send('page-next');assert.equal(await route(),'/fan');await f.locator('.fan-page').waitFor();});
  await test('queued-message-cancelled-by-close-and-new-session',async()=>{
   await f.evaluate(({id,old})=>{
    window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'page-next',uiSeq:id,provenance:{session:'owned-h5-probe',overlaySession:old}}}));
    window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:false,session:''}}));
    window.dispatchEvent(new CustomEvent('ipc:window.hidden'));
    window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:true,session:'1002'}}));
    window.dispatchEvent(new CustomEvent('ipc:window.shown'));window.dispatchEvent(new CustomEvent('ipc:gamepad.summon'));
   },{id:++seq,old:session});session='1002';await page.waitForTimeout(100);assert.equal(await route(),'/fan');
   await send('page-next','1001');assert.equal(await route(),'/fan');
  });
  await test('monitor-editor-retains-draft-and-focus-session',async()=>{
   await f.evaluate(()=>window.__navigate('/rtss'));await f.locator('[data-monitor-edit]').waitFor();await press(f.locator('[data-monitor-edit]'));
   const modal=f.locator('.monitor-editor-window');await modal.waitFor();
   const before=await f.evaluate(()=>window.__overlaySessionAudit().owner.epoch);
   await f.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.blur')));
   assert.equal(await modal.count(),1);assert.equal(await f.evaluate(()=>window.__overlaySessionAudit().owner.epoch),before);
   await send('nav-down');await send('nav-right');
   const bounds=await modal.boundingBox();const viewport=await f.evaluate(()=>({width:innerWidth,height:innerHeight}));
   assert.ok(bounds.width<=viewport.width+1&&bounds.height<=viewport.height+1,JSON.stringify({bounds,viewport}));
   await page.screenshot({path:path.join(task,`overlay-editor-${height}.png`)});
   for(let n=0;n<5&&await modal.count();++n)await send('back');assert.equal(await modal.count(),0);
  });
  await test('hidden-session-rejects-late-native-message',async()=>{
   const before=await route();await f.evaluate(()=>{window.dispatchEvent(new CustomEvent('ipc:window.overlayPresentation',{detail:{active:false,session:''}}));window.dispatchEvent(new CustomEvent('ipc:window.hidden'));});
   assert.equal(await f.evaluate(()=>window.__overlaySessionAudit().summon),false);
   await send('page-next');assert.equal(await route(),before);
  });
  await test('ordinary-native-blur-still-revokes',async()=>{
   await f.evaluate(()=>{window.dispatchEvent(new CustomEvent('ipc:window.shown'));window.dispatchEvent(new CustomEvent('ipc:gamepad.summon'));window.dispatchEvent(new CustomEvent('input:motion-telemetry',{detail:{hostFrame:{hostActive:true,firstFrame:true}}}));});
   // Explicitly seed the original virtual-target fact through its real listener.
   await f.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:window.blur')));
   const s=await f.evaluate(()=>window.__overlaySessionAudit());assert.equal(s.overlay,false);assert.equal(s.summon,false);
  });

  await test('floating-summon-default-and-gamepad-toggle-persist',async()=>{
   await f.evaluate(()=>window.__navigate('/settings'));
   const toggle=f.getByRole('switch',{name:'悬浮呼出',exact:true});await toggle.waitFor();
   assert.equal(await toggle.getAttribute('aria-checked'),'true');
   const before=await f.evaluate(async()=>JSON.stringify((await window.__virtualStartConsumer.loadSettings()).input));
   await enter();
   await press(toggle);
   await f.waitForFunction(()=>document.querySelector('[data-floating-summon-toggle] [role="switch"]')?.getAttribute('aria-checked')==='false');
   const stored=await f.evaluate(async()=>{const s=await window.__virtualStartConsumer.loadSettings();return {enabled:s.ui.floatingSummonEnabled,input:JSON.stringify(s.input)};});
   assert.equal(stored.enabled,false);assert.equal(stored.input,before,'appearance switch cannot enable a virtual controller');
   await page.screenshot({path:path.join(task,`floating-summon-settings-${height}.png`)});
  });
  await test('floating-summon-off-survives-reentry-and-can-return',async()=>{
   const toggle=f.getByRole('switch',{name:'悬浮呼出',exact:true});
   await f.evaluate(()=>window.__navigate('/fan'));await f.locator('.fan-page').waitFor();
   await f.evaluate(()=>window.__navigate('/settings'));await toggle.waitFor();
   assert.equal(await toggle.getAttribute('aria-checked'),'false');
   await press(toggle);
   await f.waitForFunction(()=>document.querySelector('[data-floating-summon-toggle] [role="switch"]')?.getAttribute('aria-checked')==='true');
   assert.equal(await f.evaluate(async()=>(await window.__virtualStartConsumer.loadSettings()).ui.floatingSummonEnabled),true);
   await send('back');
   assert.ok(await f.locator('[data-settings-section="appearance"]').evaluate(el=>document.activeElement===el),'B returns to appearance category');
  });

  assert.deepEqual(await f.evaluate(()=>window.__previewErrors),[]);
  // Ordinary route direction checks retain the original state/edge assertions.
  for(const route of ['/schedule','/fan','/button-mapping','/touch-mouse','/gyro-motion','/steam','/power','/sleep','/rtss','/settings','/quick']){
    const pageFrame=await open(route,height);await state(pageFrame,'overlay/ordinary'+route,height);
  }
}
