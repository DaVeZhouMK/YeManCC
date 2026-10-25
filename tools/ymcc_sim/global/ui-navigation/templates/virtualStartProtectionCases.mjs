import assert from 'node:assert/strict';

// Exercise the real ButtonMappingView, gameInputEditor, settingsRepository and engine.
// Memory IPC supplies explicit boundary facts, not native execution; native timing/token correctness is separate.
// Current user contract: ON, switch and OFF share a terminal+10s bounded lock,
// capped at30s from original admission; duplicate intent/feed never renews it. Historical
// 09/10 evidence retains the superseded OFF-overtake contract unchanged.
export async function virtualStartProtectionCases({page,height,open,command,press,actionCase,state}) {
 const buttons=f=>f.locator('.persona-actions .persona-btn');
 const active=f=>f.locator('.persona-actions .persona-btn.active').innerText();
 const reads=f=>f.evaluate(()=>window.__virtualStartFixture.read());
 const relevantCalls=f=>f.evaluate(()=>window.__previewCalls.filter(c=>['settings.write','input.shortcutRuntime.clear'].includes(c.cmd)));
 async function fresh(seed='global') {
  const f=await open('/button-mapping',height,{virtualStartSeed:seed});
  await f.waitForFunction(()=>document.querySelectorAll('.persona-actions .persona-btn').length===4&&document.querySelector('.persona-btn.active')?.textContent.trim()==='SteamDeck');
  await f.evaluate(()=>{window.__virtualStartFixture.armed=true});
  return f;
 }
 async function publish(f,patch) {await f.evaluate(patch=>window.__virtualStartFixture.publish(patch),patch);await page.waitForTimeout(30)}
 async function disabled(f) {
  for(let i=0;i<4;i++)assert.equal(await buttons(f).nth(i).isDisabled(),true,'ON, switch and OFF are all excluded while waiting');
 }
 async function locked(f) {
  await disabled(f);
  assert.deepEqual(await f.evaluate(()=>window.__gamepadAudit.focusables().filter(e=>e.matches('.persona-btn')).map(e=>e.textContent.trim())),[],'no persona button, including OFF, enters navigation');
  for(const direction of ['nav-right','nav-down','nav-up']){
   await command(f,direction);
   assert.equal(await f.evaluate(()=>!!document.activeElement?.closest('.persona-actions')),false,'navigation never enters the locked persona row');
  }
 }
 async function deliver(f,payload) {await f.evaluate(payload=>window.__virtualStartFixture.deliverPayload(payload),payload);await page.waitForTimeout(30)}
 async function noRepeatedMixedBurst(f,count) {
  const before=await f.evaluate(()=>{const h=window.__virtualStartFixture;return {accepted:h.accepted,rejected:h.rejected,replies:h.replies.length};});
  // Force delivery past the disabled DOM filter: six OFF/SteamDeck/PS5/Xbox
  // cycles must be rejected by the component guard without queued work.
  await f.evaluate(()=>{const buttons=Array.from(document.querySelectorAll('.persona-actions .persona-btn'));for(let i=0;i<24;i++)buttons[i%buttons.length].dispatchEvent(new MouseEvent('click',{bubbles:true}));});
  await page.waitForTimeout(30);assert.equal((await relevantCalls(f)).length,count,'mixed repeated OFF+ON cannot create a write/clear storm');
  assert.deepEqual(await f.evaluate(()=>{const h=window.__virtualStartFixture;return {accepted:h.accepted,rejected:h.rejected,replies:h.replies.length};}),before,'blocked OFF+ON never reaches backend admission or reply creation');
 }
 async function enabled(f) {
  for(let i=0;i<4;i++)assert.equal(await buttons(f).nth(i).isEnabled(),true,'UI wait has ended; native still decides every later explicit request');
 }
 async function replies(f) {return f.evaluate(()=>structuredClone(window.__virtualStartFixture.replies))}
 async function settledSave(f) {
  await f.waitForFunction(()=>{
   const rows=window.__virtualStartFixture.replies;
   return rows.some(r=>r.cmd==='input.shortcutRuntime.clear'&&r.status==='fulfilled')&&rows.every(r=>r.status==='fulfilled');
  },null,{timeout:5000});
 }
 async function accepted(f,count=1) {await f.waitForFunction(count=>window.__virtualStartFixture.accepted===count,count,{timeout:5000})}
 async function expireWithoutFeed(f,ms=32000) {
  const result=await f.evaluate(ms=>{
   const host=window.__virtualStartFixture,before=host.feeds.length;
   host.advanceBackend(ms);host.expireWithoutFeed();
   return {before,after:host.feeds.length,state:{...host.state},clock:host.backendNowMs,fact:host.facts.at(-1)};
  },ms);
  assert.equal(result.before,result.after,'authoritative expiry injection must not secretly deliver a feed');
  assert.equal(result.state.locked,false);assert.equal(result.fact.kind,'injected-authoritative-expiry-without-feed');
  return result;
 }
 async function evidence(f,label) {
  const result=await f.evaluate(label=>{
   const host=window.__virtualStartFixture;
   return {label,at:performance.now(),accepted:host.accepted,rejected:host.rejected,backendNowMs:host.backendNowMs,
    injectedBackend:{...host.state},feeds:host.feeds.length,heldFeeds:host.heldFeeds.length,wallNow:Date.now(),
    buttons:Array.from(document.querySelectorAll('.persona-actions .persona-btn')).map(el=>({text:el.textContent.trim(),disabled:el.disabled,active:el.classList.contains('active')})),
    replies:host.replies.map(row=>({id:row.id,cmd:row.cmd,requestId:row.requestId,status:row.status,
     persona:row.request.content?JSON.parse(row.request.content).input?.outputTarget?.persona:undefined,
     responseReason:row.response?.reason,responseRequestId:row.response?.startProtection?.requestId}))};
  },label);
  console.log('VIRTUAL_START_DEADLINE',height,JSON.stringify(result));return result;
 }
 async function timedCase(name,run,seed='global',holdInitialFeed=false) {
  await actionCase('Virtual start deadline: '+name,height,async()=>{
   // Clock is context-scoped in Playwright. Isolate it from the shared public
   // navigation page so advancing 32s never shifts another domain's timers.
   const frame=await fresh(seed),context=await page.context().browser().newContext({viewport:{width:Math.round(580*height/780),height}});
   try {
    const clockPage=await context.newPage();clockPage.setDefaultTimeout(5000);clockPage.setDefaultNavigationTimeout(5000);
    const pageErrors=[];clockPage.on('pageerror',error=>pageErrors.push(String(error)));
    await clockPage.clock.install();const url=new URL(frame.url());
    if(holdInitialFeed)url.searchParams.set('virtualStartInitialFeed','hold');
    await clockPage.goto(url.href);
    const f=clockPage;
    await f.waitForFunction(()=>window.__previewReady&&window.__gamepadAudit&&document.querySelectorAll('.persona-actions .persona-btn').length===4&&document.querySelector('.persona-btn.active')?.textContent.trim()==='SteamDeck',null,{timeout:5000});
    await f.evaluate(()=>{window.__virtualStartFixture.armed=true});
    const step=async ms=>{await clockPage.clock.fastForward(ms);await f.evaluate(()=>new Promise(resolve=>queueMicrotask(resolve)));};
    let sceneTimer;
    try {
     await Promise.race([run(f,step),new Promise((_,reject)=>{sceneTimer=setTimeout(()=>reject(new Error('Virtual start deadline scene exceeded its 15s wall-clock budget')),15000)})]);
     assert.deepEqual(pageErrors,[],'isolated actual Vue page has no page errors');
    } finally {clearTimeout(sceneTimer)}
   } finally {await context.close()}
  });
 }
 await actionCase('Virtual start: cached native lock excludes ON, switch and OFF from actions and navigation',height,async()=>{
  const f=await fresh('locked'),before=await reads(f),calls=(await relevantCalls(f)).length;
  assert.equal(await active(f),'SteamDeck');await locked(f);
  // Deliberately bypass the DOM disabled filter: the onPersona guard must still reject.
  await noRepeatedMixedBurst(f,calls);
  assert.equal((await relevantCalls(f)).length,calls);assert.deepEqual(await reads(f),before);
  assert.equal(await f.locator('[role="status"]').filter({hasText:'手柄切换处理中'}).count(),1);
  await state(f,'controller/start-protection/native-pending',height);
 });
 await actionCase('Virtual start: acceptance immediately locks; no optimistic selection or request replay',height,async()=>{
  const f=await fresh();await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});
  await press(f,buttons(f).nth(3));await f.waitForFunction(()=>window.__virtualStartFixture.accepted===1);
  assert.equal(await active(f),'SteamDeck','selection waits for a committed reply');await locked(f);
  const calls=(await relevantCalls(f)).length;await buttons(f).nth(2).dispatchEvent('click');assert.equal((await relevantCalls(f)).length,calls);
  await f.evaluate(()=>window.__virtualStartFixture.releaseWrite());
  await f.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent.trim()==='Xbox');
  await locked(f);
  const saved=await reads(f),count=(await relevantCalls(f)).length;
  await publish(f,{locked:true,phase:'success',remainingMs:10000,elapsedMs:800});await locked(f);
  await publish(f,{locked:true,phase:'success',remainingMs:0,elapsedMs:10800});await locked(f);
  await publish(f,{locked:false,phase:'success',remainingMs:0,elapsedMs:10800});
  assert.equal(await buttons(f).nth(2).isEnabled(),true);assert.equal((await relevantCalls(f)).length,count);assert.deepEqual(await reads(f),saved);
  await buttons(f).first().evaluate(el=>window.__gamepadAudit.focus(el));await command(f,'nav-right');
  assert.equal(await buttons(f).nth(1).evaluate(el=>document.activeElement===el),true);
  await state(f,'controller/start-protection/released',height);
 });
 await timedCase('OFF cannot overtake pending work; new OFF admits request3 and stop success settles10s',async(f,step)=>{
  // Archived 09/10 evidence remains unchanged. This test exercises only the
  // updated admission and explicit stop-receipt leaves, never cleanup work.
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.requestId),1,'initial seed is request1');
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});
  await press(f,buttons(f).nth(3));await accepted(f);await locked(f);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.requestId),2,'first ON is request2');
  const pendingCalls=(await relevantCalls(f)).length,persisted=await reads(f);
  await noRepeatedMixedBurst(f,pendingCalls);assert.deepEqual(await reads(f),persisted);
  assert.notEqual(persisted.input.outputTarget.persona,'disabled','blocked OFF did not commit a cancellation');
  await f.evaluate(()=>window.__virtualStartFixture.releaseWrite());await settledSave(f);await locked(f);
  const completedCalls=(await relevantCalls(f)).length;
  await publish(f,{locked:false,phase:'expired',remainingMs:0,elapsedMs:0});await enabled(f);
  assert.equal((await relevantCalls(f)).length,completedCalls,'unlock never replays blocked OFF');assert.equal(await active(f),'Xbox');
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextClear=true});await press(f,buttons(f).first());await accepted(f,2);
  await f.waitForFunction(()=>window.__virtualStartFixture.replies.some(r=>r.cmd==='input.shortcutRuntime.clear'&&r.status==='held'),null,{timeout:5000});
  const offState=await f.evaluate(()=>({...window.__virtualStartFixture.state}));
  assert.equal(offState.requestId,3,'explicit OFF is new request3');assert.equal(offState.persona,'disabled');assert.equal(offState.phase,'pending');assert.equal(offState.locked,true);assert.equal(offState.settleMs,10000);
  await locked(f);const offCalls=(await relevantCalls(f)).length;await noRepeatedMixedBurst(f,offCalls);
  const document=await reads(f);assert.equal(document.input.outputTarget.persona,'disabled');assert.equal(document.input.outputTarget.gyroEnabled,false);assert.equal(document.input.gyroMotion.enabled,false);
  await f.evaluate(()=>window.__virtualStartFixture.releaseClear());await settledSave(f);await locked(f);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.phase),'pending','OFF commit and clear reply do not finish stop');
  await f.clock.pauseAt(await f.evaluate(()=>Date.now()+1000));
  await f.evaluate(id=>window.__virtualStartFixture.injectStopTerminal('success',id,1000),offState.requestId);
  const terminal=await f.evaluate(()=>({mono:performance.now(),wall:Date.now(),expiry:window.__virtualStartFixture.state.expiresAtUnixMs}));
  assert.equal(terminal.expiry-terminal.wall,10000);await step(9999);await disabled(f);await noRepeatedMixedBurst(f,offCalls);
  await expireWithoutFeed(f,10000);await step(1);await enabled(f);
  assert.equal(await f.evaluate(()=>performance.now()),terminal.mono+10000);
  assert.equal((await relevantCalls(f)).length,offCalls);assert.equal(await active(f),'关闭虚拟手柄');
  await evidence(f,'OFF-request3-stop-success-exact10000ms');
  await state(f,'controller/start-protection/explicit-off-released',height);
 });
 await actionCase('Virtual start: native write rejection preserves configuration and highlight',height,async()=>{
  const f=await fresh(),before=await reads(f);
  await f.evaluate(()=>{window.__virtualStartFixture.rejectNextWrite=true});await press(f,buttons(f).nth(3));
  await f.waitForFunction(()=>window.__virtualStartFixture.rejected===1);await page.waitForTimeout(80);
  assert.deepEqual(await reads(f),before);assert.equal(await active(f),'SteamDeck');await locked(f);
  const calls=(await relevantCalls(f)).length;
  for(const phase of ['failure','cancelled']){await publish(f,{locked:true,phase,remainingMs:10000,elapsedMs:1000});await locked(f)}
  // The old requirement to remain locked here contradicted the user's total
  // 30s maximum. At max expiry, do not wait for a possibly lost unlock feed.
  await publish(f,{locked:true,phase:'pending',remainingMs:0,elapsedMs:30000});await enabled(f);
  await publish(f,{locked:false,phase:'expired',remainingMs:0,elapsedMs:30000});
  assert.equal(await buttons(f).nth(3).isEnabled(),true);assert.equal((await relevantCalls(f)).length,calls);assert.equal(await active(f),'SteamDeck');assert.deepEqual(await reads(f),before);
  await press(f,buttons(f).nth(2));await f.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent.trim()==='PS5');assert.equal((await reads(f)).input.outputTarget.persona,'dualsense-edge');
 });
 await actionCase('Virtual start: repository preserves gate reason, receipt and current document; unchanged target saves pass',height,async()=>{
  const f=await fresh();
  const result=await f.evaluate(async()=>{
   const repo=window.__virtualStartConsumer,host=window.__virtualStartFixture;
   const before=await repo.loadSettings();host.rejectNextWrite=true;
   const rejected=await repo.compareAndSwapInputSettings(before.input.revision,{outputTarget:{persona:'elite',buttonMappingEnabled:true}});
   const after=await repo.loadSettings();
   const ordinary=await repo.compareAndSwapInputSettings(after.input.revision,{ownership:{externalControllerPriority:true}});
   host.rejectNextWrite=true;let thrown;
   try{await repo.saveSettingsSection('gameCustom',{testOnlyGateConsumer:true})}catch(error){thrown={reason:error.reason,message:error.message,startProtection:error.startProtection,document:error.document}}
   return {before,rejected,after,ordinary,thrown,disk:host.read()};
  });
  assert.equal(result.rejected.ok,false);assert.equal(result.rejected.reason,'virtual-start-protection-active');assert.equal(result.rejected.startProtection.locked,true);
  assert.equal(result.rejected.startProtection.maxMs,30000);assert.equal(result.rejected.startProtection.settleMs,10000);
  assert.deepEqual(result.rejected.value,result.before.input);assert.deepEqual(result.after,result.before);
  assert.equal(result.ordinary.ok,true);assert.equal(result.disk.input.outputTarget.persona,'steamdeck');
  assert.equal(result.thrown.reason,'virtual-start-protection-active');assert.equal(result.thrown.startProtection.locked,true);
  assert.equal(result.disk.gameCustom?.testOnlyGateConsumer,undefined);assert.doesNotMatch(result.thrown.message,/requestId|virtual-start-protection/);
 });
 await actionCase('Virtual start: rejected shortcut clear retains runtime highlight until native observation',height,async()=>{
  const f=await fresh();
  await f.evaluate(()=>window.__emitNativeEvent('input.shortcutRuntime',{active:true,persona:'elite',virtualEnabled:true}));
  assert.equal(await active(f),'Xbox');await press(f,buttons(f).nth(2));
  await f.waitForFunction(()=>window.__previewCalls.some(c=>c.cmd==='input.shortcutRuntime.clear'));
  await page.waitForTimeout(80);assert.equal(await active(f),'Xbox','rejected clear cannot pretend the runtime override ended');assert.equal((await reads(f)).input.outputTarget.persona,'dualsense-edge');
  await locked(f);const count=(await relevantCalls(f)).length;
  await publish(f,{locked:false,phase:'success',remainingMs:0,elapsedMs:14000});assert.equal(await active(f),'Xbox');assert.equal((await relevantCalls(f)).length,count);
  await f.evaluate(()=>window.__emitNativeEvent('input.shortcutRuntime',{active:false}));assert.equal(await active(f),'PS5');
 });
 await timedCase('cached lock with no user action releases after 32s without an unlock feed',async(f,step)=>{
  const before=await reads(f),count=(await relevantCalls(f)).length;
  await disabled(f);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),0);
  await expireWithoutFeed(f);await step(32000);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'expiry never replays a request');assert.deepEqual(await reads(f),before);
  assert.equal(await active(f),'SteamDeck','expiry is not proof of successful switching');
  assert((await replies(f)).every(row=>row.status==='fulfilled'),'no held reply explains this cached-lock scenario');
  await evidence(f,'cached-no-action-lost-feed-32s');
 },'locked');
 await timedCase('accepted save has finished; lost feed cannot hold the UI beyond 32s',async(f,step)=>{
  await press(f,buttons(f).nth(3));await accepted(f);await settledSave(f);
  assert.equal(await active(f),'Xbox');const saved=await reads(f),count=(await relevantCalls(f)).length;
  const stale=await f.evaluate(()=>window.__virtualStartFixture.capturePayload());
  await expireWithoutFeed(f);await step(32000);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count);assert.deepEqual(await reads(f),saved);assert.equal(await active(f),'Xbox');
  assert((await replies(f)).every(row=>row.status==='fulfilled'),'ordinary write and clear have both returned');
  // A late duplicate observation cannot resurrect an already expired lease.
  await deliver(f,stale);await enabled(f);assert.equal((await relevantCalls(f)).length,count);
  await evidence(f,'accepted-save-complete-lost-feed-32s');
 });
 await timedCase('accepted held write releases UI at 32s and late confirmation cannot replay the expired workflow',async(f,step)=>{
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});
  await press(f,buttons(f).nth(3));await accepted(f);
  const held=(await replies(f)).find(row=>row.cmd==='settings.write'&&row.status==='held');assert(held);
  assert.equal(await active(f),'SteamDeck');await locked(f);const count=(await relevantCalls(f)).length;
  await expireWithoutFeed(f);await step(32000);await enabled(f);
  assert.equal(await active(f),'SteamDeck','unconfirmed durable selection must not become an optimistic UI selection');
  assert.equal((await relevantCalls(f)).length,count,'expiry does not replay or clear');
  const committed=await reads(f);
  assert.equal(committed.input.outputTarget.persona,'elite','backend already committed Xbox; UI expiry does not roll it back');
  const notice=await f.locator('.persona-actions + [role="status"]').allTextContents();
  assert.equal(notice.length,1,'expired held save retains a visible waiting-result notice');
  await evidence(f,'accepted-write-held-lost-feed-32s');
  await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),held.id);
  await f.waitForFunction(id=>window.__virtualStartFixture.replies.find(r=>r.id===id)?.status==='fulfilled',held.id,{timeout:5000});
  await page.waitForTimeout(50);
  // The repository verifies the document already committed by native, then
  // notifies this page to read it. A view timeout never cancels that commit.
  await f.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent.trim()==='Xbox',null,{timeout:5000});
  assert.equal(await active(f),'Xbox','late committed-document confirmation may refresh the saved selection');
  assert.equal((await relevantCalls(f)).length,count,'late write cannot execute the expired shortcut clear');await enabled(f);
  assert.deepEqual(await f.locator('.persona-actions + [role="status"]').allTextContents(),notice,'late success reply cannot dismiss the expired interaction notice');
  assert.deepEqual(await reads(f),committed,'ignoring the expired component workflow must not undo the backend commit');
  await press(f,buttons(f).first());
  await f.waitForFunction(()=>window.__virtualStartFixture.read().input.outputTarget.persona==='disabled'&&document.querySelector('.persona-btn.active')?.textContent.trim()==='关闭虚拟手柄',null,{timeout:5000});
  const off=await reads(f);assert.equal(off.input.outputTarget.gyroEnabled,false);assert.equal(off.input.gyroMotion.enabled,false);
  await accepted(f,2);await settledSave(f);await locked(f);
  const offState=await f.evaluate(()=>({...window.__virtualStartFixture.state}));
  assert.equal(offState.requestId,3);assert.equal(offState.persona,'disabled');assert.equal(offState.phase,'pending');
  const offCalls=(await relevantCalls(f)).length;await noRepeatedMixedBurst(f,offCalls);
  await f.evaluate(id=>window.__virtualStartFixture.injectStopTerminal('failure',id,1000),offState.requestId);
  await expireWithoutFeed(f,10200);await step(10200);await enabled(f);
  assert.equal((await relevantCalls(f)).length,offCalls);assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),2,'only the explicit ON and OFF were admitted; expiry never replays ON');
  assert.deepEqual(await reads(f),off,'stop failure never rolls back the committed OFF intent');
 });

 for(const phase of ['success','failure'])await timedCase('held write '+phase+' releases all actions at terminal+10000ms before the30s cap',async(f,step)=>{
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.requestId),1);
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});await press(f,buttons(f).nth(3));await accepted(f);
  const held=(await replies(f)).find(r=>r.cmd==='settings.write'&&r.status==='held');assert(held);assert.equal(held.requestId,2,'held ON is a fresh request after initial unlocked id1');
  assert.equal(await f.evaluate(id=>window.__virtualStartFixture.feeds.some(row=>row.state.requestId===id&&row.state.phase==='pending'),held.requestId),true,'pending feed was delivered before terminal');
  await locked(f);const count=(await relevantCalls(f)).length,committed=await reads(f);
  const originalCap=await f.evaluate(()=>window.__virtualStartFixture.state.expiresAtUnixMs);
  // Freeze before delivering terminal. Real Playwright/IPC time cannot turn
  // a +9999ms observation into +10000ms while assertions are executing.
  await f.clock.pauseAt(await f.evaluate(()=>Date.now()+1000));
  await f.evaluate(()=>window.__virtualStartFixture.advanceBackend(1000));
  await publish(f,{locked:true,phase,remainingMs:10000,elapsedMs:1000});
  const terminal=await f.evaluate(()=>({mono:performance.now(),wall:Date.now(),expiry:window.__virtualStartFixture.feeds.at(-1).state.expiresAtUnixMs}));
  assert.equal(terminal.expiry-terminal.wall,10000,'producer terminal budget is exactly10s in this isolated case');
  await step(9999);await disabled(f);
  assert.equal(await f.evaluate(()=>performance.now()),terminal.mono+9999);
  await noRepeatedMixedBurst(f,count);
  await expireWithoutFeed(f,10000);await step(1);await enabled(f);
  assert.equal(await f.evaluate(()=>performance.now()),terminal.mono+10000);
  assert(originalCap>await f.evaluate(()=>Date.now()),'terminal release occurs before the original30s maximum');
  assert((await replies(f)).some(r=>r.id===held.id&&r.status==='held'),'write reply is still held; terminal alone ended the UI wait');
  assert.equal((await relevantCalls(f)).length,count,'terminal deadline never replays or clears an action');
  assert.deepEqual(await reads(f),committed,'terminal waiting expiry does not roll back committed storage');
  await evidence(f,phase+'-held-write-terminal-exact10000ms-all-actions-enabled');
  // Release the old success ACK before any new action. Confirmed storage may
  // refresh selection, but the expired component must not continue its clear.
  await f.clock.resume();await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),held.id);
  await f.waitForFunction(id=>window.__virtualStartFixture.replies.find(r=>r.id===id)?.status==='fulfilled',held.id,{timeout:5000});
  await f.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent.trim()==='Xbox',null,{timeout:5000});
  await page.waitForTimeout(30);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'old reply after terminal cannot execute shortcut clear or replay');
  assert.deepEqual(await reads(f),committed);assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),1);
 });
 for(const phase of ['success','failure','cancelled'])await timedCase(phase+' settles at10s without renewal or a release feed',async(f,step)=>{
  const count=(await relevantCalls(f)).length;
  await f.clock.pauseAt(await f.evaluate(()=>Date.now()+1000));
  await publish(f,{locked:true,requestId:41,phase,persona:'steamdeck',remainingMs:10000,elapsedMs:1000});
  const initial=await f.evaluate(()=>({mono:performance.now(),payload:window.__virtualStartFixture.capturePayload()}));
  await step(5000);await deliver(f,initial.payload);await disabled(f);
  await step(4999);await disabled(f);assert.equal(await f.evaluate(()=>performance.now()),initial.mono+9999);
  await expireWithoutFeed(f,10000);await step(1);await enabled(f);
  assert.equal(await f.evaluate(()=>performance.now()),initial.mono+10000);assert.equal((await relevantCalls(f)).length,count);
  await deliver(f,initial.payload);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'duplicate terminal after expiry neither revives nor replays');
  await evidence(f,phase+'-terminal-exact10000ms-no-release-feed');
 });
 await timedCase('terminal at29s releases at original30s cap instead of becoming39s or40s',async(f,step)=>{
  const count=(await relevantCalls(f)).length;
  await f.clock.pauseAt(await f.evaluate(()=>Date.now()+1000));
  await publish(f,{locked:true,requestId:42,phase:'pending',persona:'steamdeck',remainingMs:30000,elapsedMs:0});
  const initial=await f.evaluate(()=>({mono:performance.now(),expiry:window.__virtualStartFixture.state.expiresAtUnixMs}));
  await step(29000);await publish(f,{locked:true,phase:'success',remainingMs:10000,elapsedMs:29000});
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.expiresAtUnixMs),initial.expiry,'late terminal preserves the original producer cap');
  await step(999);await disabled(f);
  await expireWithoutFeed(f,30000);await step(1);await enabled(f);
  assert.equal(await f.evaluate(()=>performance.now()),initial.mono+30000,'terminal does not turn the whole intent into40s');
  assert.equal((await relevantCalls(f)).length,count);await evidence(f,'near-30s-terminal-keeps-original-cap');
 });
 for(const budget of [{elapsedMs:27000,remainingMs:0,waitMs:3200},{elapsedMs:1000,remainingMs:1500,waitMs:1700}])await timedCase('first pending observation bounds remaining budget '+JSON.stringify(budget),async(f,step)=>{
  const count=(await relevantCalls(f)).length;
  await publish(f,{locked:true,requestId:43,phase:'pending',persona:'steamdeck',elapsedMs:budget.elapsedMs,remainingMs:budget.remainingMs});
  assert.equal(await buttons(f).nth(3).isDisabled(),true);
  await step(budget.waitMs);await enabled(f);assert.equal((await relevantCalls(f)).length,count);
 });
 await timedCase('duplicate same-request pending feeds never renew the first deadline',async(f,step)=>{
  const count=(await relevantCalls(f)).length,duplicate=await f.evaluate(()=>window.__virtualStartFixture.capturePayload());
  await step(10000);await deliver(f,duplicate);assert.equal(await buttons(f).nth(3).isDisabled(),true);
  await step(10000);await deliver(f,duplicate);assert.equal(await buttons(f).nth(3).isDisabled(),true);
  await step(11000);await enabled(f);await deliver(f,duplicate);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'duplicate feed and expiry never replay');
  await evidence(f,'same-request-duplicates-no-renewal-at-31s');
 },'locked');
 await timedCase('manual retry still needs backend admission; a new explicit request can succeed',async(f,step)=>{
  await press(f,buttons(f).nth(3));await accepted(f);await settledSave(f);
  const saved=await reads(f),oldId=await f.evaluate(()=>window.__virtualStartFixture.state.requestId),count=(await relevantCalls(f)).length;
  await expireWithoutFeed(f);await step(32000);await enabled(f);assert.equal((await relevantCalls(f)).length,count);
  // Inject an unseen external request only when the explicit retry reaches the
  // backend. UI expiry never bypasses this final admission decision.
  await f.evaluate(()=>{window.__virtualStartFixture.rejectNextWrite=true});await press(f,buttons(f).nth(2));
  await f.waitForFunction(()=>window.__virtualStartFixture.rejected===1,null,{timeout:5000});
  assert.deepEqual(await reads(f),saved);assert.equal(await active(f),'Xbox');await locked(f);
  assert((await f.evaluate(()=>window.__virtualStartFixture.state.requestId))>oldId,'external blocker has a fresh id, never revived old unlocked id');
  await expireWithoutFeed(f);const rejectedCount=(await relevantCalls(f)).length;
  await step(32000);await enabled(f);assert.equal((await relevantCalls(f)).length,rejectedCount);
  await press(f,buttons(f).nth(2));await accepted(f,2);await settledSave(f);
  assert.equal(await active(f),'PS5');assert.equal((await reads(f)).input.outputTarget.persona,'dualsense-edge');
  await locked(f);await evidence(f,'only-explicit-manual-next-request-admitted');
 });
 await timedCase('late old clear neither changes selection nor clears a newer held save busy',async(f,step)=>{
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextClear=true});
  await press(f,buttons(f).nth(3));await accepted(f);
  await f.waitForFunction(()=>window.__virtualStartFixture.replies.some(row=>row.cmd==='input.shortcutRuntime.clear'&&row.status==='held'),null,{timeout:5000});
  const old=(await replies(f)).find(row=>row.cmd==='input.shortcutRuntime.clear'&&row.status==='held');assert(old);
  assert.equal(await active(f),'Xbox');const count=(await relevantCalls(f)).length;
  await expireWithoutFeed(f);await step(32000);await enabled(f);assert.equal((await relevantCalls(f)).length,count);
  await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});await press(f,buttons(f).nth(2));await accepted(f,2);
  const next=(await replies(f)).find(row=>row.cmd==='settings.write'&&row.status==='held');assert(next);assert(next.requestId>old.requestId);
  assert.equal(await active(f),'Xbox','new held write retains the confirmed choice');
  // A delayed terminal for the older id must not shorten the new action's busy.
  const staleTerminal=await f.evaluate(id=>{const payload=window.__virtualStartFixture.capturePayload();Object.assign(payload.virtualOutput.startProtection,{requestId:id,locked:true,phase:'success',remainingMs:10000,elapsedMs:1000,expiresAtUnixMs:Date.now()+10000});return payload;},old.requestId);
  await deliver(f,staleTerminal);await step(10200);await locked(f);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.requestId),next.requestId,'wire replay does not mutate backend identity');
  // A nonterminal authoritative release removes native lock as an explanation:
  // all four actions must still be held by the newer outstanding write.
  await publish(f,{locked:false,phase:'expired',remainingMs:0,elapsedMs:0});await locked(f);
  const newCount=(await relevantCalls(f)).length;
  await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),old.id);
  await f.waitForFunction(id=>window.__virtualStartFixture.replies.find(row=>row.id===id)?.status==='fulfilled',old.id,{timeout:5000});
  await page.waitForTimeout(50);await locked(f);
  assert.equal(await active(f),'Xbox','old clear reply cannot alter the confirmed selection');
  assert.equal((await relevantCalls(f)).length,newCount,'old reply cannot start follow-up actions');
  const oldReply=(await replies(f)).find(row=>row.id===old.id);
  assert.equal(oldReply.response.reason,'virtual-start-protection-active');assert.equal(oldReply.response.startProtection.requestId,old.requestId);
  assert((await replies(f)).some(row=>row.id===next.id&&row.status==='held'),'new busy still belongs to the new held write');
  await evidence(f,'old-reply-cannot-clear-new-busy');
  await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),next.id);await settledSave(f);await enabled(f);
  assert.equal(await active(f),'PS5');assert.equal((await reads(f)).input.outputTarget.persona,'dualsense-edge');
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),2);
 });

 await timedCase('successful opt-in clear observes protection despite a missing first gamepad feed',async(f,step)=>{
  await f.evaluate(()=>{const h=window.__virtualStartFixture;h.holdNextFeed=true;h.sameTargetClearSucceeds=true;});
  await press(f,buttons(f).nth(3));await accepted(f);await settledSave(f);
  const clear=(await replies(f)).find(row=>row.cmd==='input.shortcutRuntime.clear');assert(clear);
  assert.equal(clear.request.observeProtection,true,'actual Vue asks for an observation with successful clear');
  assert.equal(clear.response.ok,true);assert.equal(clear.response.startProtection.locked,true);
  assert(clear.response.startProtection.expiresAtUnixMs>0,'successful receipt carries the producer deadline');
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.feeds.length),0,'first gamepad feed remains undelivered');
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.heldFeeds.length),1);
  assert((await replies(f)).every(row=>row.status==='fulfilled'),'lock is not a held save promise');
  assert.equal(await active(f),'Xbox');await locked(f);
  const saved=await reads(f),count=(await relevantCalls(f)).length;await noRepeatedMixedBurst(f,count);
  await expireWithoutFeed(f);await step(32000);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'expiry never replays a successful save');assert.deepEqual(await reads(f),saved);
  await evidence(f,'successful-clear-receipt-first-feed-lost-32s');
 });
 await timedCase('first pending payload delayed20s still releases at original request32s',async(f,step)=>{
  await f.evaluate(()=>{const h=window.__virtualStartFixture;h.holdNextFeed=true;h.holdNextStart=true;});
  await press(f,buttons(f).nth(3));await accepted(f);
  const held=(await replies(f)).find(row=>row.cmd==='settings.write'&&row.status==='held');assert(held);
  const payload=await f.evaluate(()=>structuredClone(window.__virtualStartFixture.heldFeeds[0]));assert(payload);
  const protection=payload.virtualOutput.startProtection;
  assert.equal(protection.elapsedMs,0);assert.equal(protection.remainingMs,30000);assert(protection.expiresAtUnixMs>0);
  const count=(await relevantCalls(f)).length;
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.feeds.length),0);
  await step(20000);await f.evaluate(()=>window.__virtualStartFixture.releaseFeed());
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.feeds.at(-1).state.expiresAtUnixMs),protection.expiresAtUnixMs,'delivery preserves the original producer timestamp');
  await locked(f);await noRepeatedMixedBurst(f,count);
  await evidence(f,'first-pending-produced0s-delivered20s');
  await expireWithoutFeed(f);await step(12000);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count,'delayed feed and repeated OFF+ON do not enqueue another operation');
  assert((await replies(f)).some(row=>row.id===held.id&&row.status==='held'),'save reply remains held at original request32s');
  assert.equal(await active(f),'SteamDeck','no success ACK has arrived');
  const notice=await f.locator('.persona-actions + [role="status"]').allTextContents();assert.equal(notice.length,1);
  await evidence(f,'delayed-first-feed-original-request32s-enabled');
  await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),held.id);
  await f.waitForFunction(id=>window.__virtualStartFixture.replies.find(row=>row.id===id)?.status==='fulfilled',held.id,{timeout:5000});
  await f.waitForFunction(()=>document.querySelector('.persona-btn.active')?.textContent.trim()==='Xbox',null,{timeout:5000});
  assert.equal((await reads(f)).input.outputTarget.persona,'elite','confirmed native commit is retained');
  assert.equal((await relevantCalls(f)).length,count,'late successful ACK cannot clear or replay an expired component workflow');
  assert.deepEqual(await f.locator('.persona-actions + [role="status"]').allTextContents(),notice,'late ACK preserves the expiry notice');
 });
 await timedCase('old initial cached payload delayed20s releases by32s without any user action',async(f,step)=>{
  await enabled(f);
  const payload=await f.evaluate(()=>structuredClone(window.__virtualStartFixture.heldFeeds[0]));assert(payload);
  const saved=await reads(f),count=(await relevantCalls(f)).length,expiry=payload.virtualOutput.startProtection.expiresAtUnixMs;
  assert(expiry>0);assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),0);
  await step(20000);await f.evaluate(()=>window.__virtualStartFixture.releaseFeed());
  await page.waitForTimeout(30);
  await disabled(f);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.feeds.at(-1).state.expiresAtUnixMs),expiry);
  await expireWithoutFeed(f);await step(12000);await enabled(f);
  assert.equal((await relevantCalls(f)).length,count);assert.deepEqual(await reads(f),saved);
  assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),0,'delivery and expiry never create a request');
  assert((await replies(f)).every(row=>row.status==='fulfilled'));
  await evidence(f,'no-action-initial-payload-produced0s-delivered20s-released32s');
 },'locked',true);

 await timedCase('verifying10s alone never unlocks; only a final native result releases held work',async(f,step)=>{
  // These are explicit candidate/result IPC facts, never execution of native
  // verification or recovery. Keep the old terminal-reply compatibility cases.
  for(const [index,outcome] of ['success','failure'].entries()){
   await f.evaluate(()=>{window.__virtualStartFixture.holdNextStart=true});
   await press(f,buttons(f).nth(index===0?3:2));await accepted(f,index+1);
   const held=(await replies(f)).find(row=>row.cmd==='settings.write'&&row.status==='held');assert(held);
   assert.equal(held.requestId,index+2,'each intent has a new id after the initial unlocked seed1');
   const count=(await relevantCalls(f)).length,committed=await reads(f);
   await f.clock.pauseAt(await f.evaluate(()=>Date.now()+1000));
   await publish(f,{requestId:held.requestId,locked:true,phase:'verifying',remainingMs:30000,elapsedMs:0,
    verificationPending:true,verificationRemainingMs:10000,candidateOutcome:'success'});
   const observedAt=await f.evaluate(()=>performance.now());
   await step(10000);await disabled(f);
   assert.equal(await f.evaluate(()=>performance.now()),observedAt+10000);
   assert.equal(await f.evaluate(()=>window.__virtualStartFixture.state.phase),'verifying','ten seconds is not an invented final outcome');
   assert((await replies(f)).some(row=>row.id===held.id&&row.status==='held'));
   await noRepeatedMixedBurst(f,count);
   assert.equal((await relevantCalls(f)).length,count,'verification time and mixed OFF+ON never replay or clear');
   await evidence(f,outcome+'-candidate-after10s-no-native-result-still-locked');
   await publish(f,{requestId:held.requestId,locked:false,phase:outcome,remainingMs:0,elapsedMs:10000,
    verificationPending:false,verificationRemainingMs:0,candidateOutcome:'success'});
   await enabled(f);
   assert((await replies(f)).some(row=>row.id===held.id&&row.status==='held'),'final result also releases component busy before the write ACK');
   assert.equal((await relevantCalls(f)).length,count,'final result never invokes clear or replays the intent');
   await f.clock.resume();await f.evaluate(id=>window.__virtualStartFixture.releaseReply(id),held.id);
   await f.waitForFunction(id=>window.__virtualStartFixture.replies.find(row=>row.id===id)?.status==='fulfilled',held.id,{timeout:5000});
   await f.waitForFunction(expected=>document.querySelector('.persona-btn.active')?.textContent.trim()===expected,index===0?'Xbox':'PS5',{timeout:5000});
   await page.waitForTimeout(30);await enabled(f);
   assert.equal((await relevantCalls(f)).length,count,'late write ACK cannot continue the finished component workflow with shortcut clear');
   assert.deepEqual(await reads(f),committed,'final target result never rolls back a verified committed settings document');
   assert.equal(await f.evaluate(()=>window.__virtualStartFixture.accepted),index+1,'only the explicit user intent was admitted');
   await evidence(f,outcome+'-final-native-unlocked-held-ACK-no-clear');
  }
 });

}
