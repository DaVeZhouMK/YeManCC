import fs from 'node:fs';import path from 'node:path';import {pathToFileURL} from 'node:url';
const root=process.cwd(),out=process.env.YMCC_APPEARANCE_TEST_OUT;fs.mkdirSync(out,{recursive:true});
const base=fs.readFileSync('tools/steam_appearance_browser_selftest.mjs','utf8'),end=base.indexOf('let browser,activePage;');
assertSetup(end>=0,'Browser fixture boundary missing');
const aliasesEnd='const styles=[];';
const setup=base.slice(0,end).replace(aliasesEnd,"delete mocks['@/bridge/uiLifecycle'];\n"+aliasesEnd)
 .replace('const entry=`', "const entry=`import {getUiVisibilityState,startUiPowerReconciliation} from '@/bridge/uiLifecycle';\n")
 .replace("app.provide('globalRefreshKey',ref(0));", "const generalRefresh=ref(0);app.provide('globalRefreshKey',generalRefresh);window.__generalRefresh=()=>++generalRefresh.value;")
 .replace('const app=createApp(Page);', `const probe=window.__sessionProbe;if(probe){Object.assign(window.__fixture.snapshot,probe.status);window.__fixture.failRead=probe.failStatus;if(probe.power){window.__powerVisibility=getUiVisibilityState;startUiPowerReconciliation();}}const app=createApp(Page);`)
 .replace('const app=createApp(Page);', 'function mountPage(){const app=createApp(Page);')
 .replace('window.__unmount=()=>app.unmount();', 'window.__unmount=()=>app.unmount();}window.__mount=mountPage;mountPage();')
 .replace("if(method==='steamAppearance.idlePolicy')return", "if(method==='steamAppearance.idlePolicy'&&window.__sessionProbe){const p=window.__sessionProbe;p.policyReads=(p.policyReads||0)+1;if(p.holdPolicy||p.holdPolicyAt===p.policyReads)await new Promise(r=>{p.releasePolicy=r;});}if(method==='steamAppearance.idlePolicy')return")
 .replace("repeat(32),pid:42,birth:'100'};", "repeat(32),pid:window.__sessionProbe?.policyFromStatus?f.snapshot.steamPid:42,birth:window.__sessionProbe?.policyFromStatus?f.snapshot.steamBirth:'100',automaticRepairActive:window.__sessionProbe?.automaticRepairActive,automaticAppliedRevision:window.__sessionProbe?.automaticAppliedRevision};")
 .replace("if(action==='Status'){if(f().failRead)", "if(action==='Status'){if(window.__sessionProbe?.holdStatus)await new Promise(r=>{window.__sessionProbe.releaseStatus=r;});if(f().failRead)")
 .replace("if(action==='Apply'||action==='ScopedApply')", "if(action==='Status'&&window.__sessionProbe?.holdStatusReceipt)await new Promise(r=>{window.__sessionProbe.releaseStatusReceipt=r;});\n if(action==='Apply'||action==='ScopedApply')")
 .replace("if(action==='Refresh'){f().refreshes++;result={", "if(action==='Refresh'){f().refreshes++;result={renderConfirmed:f().failRefresh?undefined:true,renderContextsComplete:window.__sessionProbe?.fullRefreshReceipt,")
 .replace("if(action==='Idle'){", "if(action==='Reconcile'&&window.__sessionProbe?.holdReconcileReceipt){const p=window.__sessionProbe;await new Promise(r=>{p.releaseReconcileReceipt=r;});Object.assign(result.snapshot,p.reconcileReceiptScope);}\n if(action==='Idle'){")
 .replace('if(f().delayApply)await new Promise(r=>setTimeout(r,120));', 'if(f().holdApply)await new Promise(r=>{f().releaseApply=r;});if(f().delayApply)await new Promise(r=>setTimeout(r,120));');
assertSetup(setup.includes("delete mocks['@/bridge/uiLifecycle'];"),'Real UI visibility adapter not selected');
assertSetup(setup.includes('window.__generalRefresh=')&&setup.includes('f().releaseApply=r;'),'Missing general refresh or Apply fixture port');
assertSetup(setup.includes('probe.failStatus')&&setup.includes('__sessionProbe.releaseStatus=r;')&&setup.includes('__sessionProbe.releaseStatusReceipt=r;'),'Missing Steam session fixture port');
assertSetup(setup.includes('p.releasePolicy=r;')&&setup.includes('window.__mount=mountPage;'),'Missing retirement fixture ports');
assertSetup(setup.includes('policyFromStatus?f.snapshot.steamBirth'),'Missing scoped Status policy fixture port');
assertSetup(setup.includes('renderContextsComplete:window.__sessionProbe?.fullRefreshReceipt'),'Missing Refresh receipt fixture port');
assertSetup(setup.includes('p.releaseReconcileReceipt=r;'),'Missing Reconcile receipt fixture port');
function assertSetup(condition,message){if(!condition)throw Error(message);}
const tail=String.raw`
let browser;const cases=[],errors=[],readProbes={},sessionProbes={},sectionProbes={},powerProbes={},retirementProbes={},supersededReadProbes={};
const repairOwnerOnly=process.env.YMCC_APPEARANCE_TEST_REPAIR_OWNER_ONLY==='1',repairOwnerProbes={};
const scopeOnly=process.env.YMCC_APPEARANCE_TEST_SCOPE_ONLY==='1';
const resumeOnly=process.env.YMCC_APPEARANCE_TEST_RESUME_ONLY==='1';
const manualPriorityOnly=process.env.YMCC_APPEARANCE_TEST_MANUAL_PRIORITY_ONLY==='1',manualPriorityProbes={};
const dispatchOnly=process.env.YMCC_APPEARANCE_TEST_DISPATCH_ONLY==='1'||manualPriorityOnly;
try{
 browser=await chromium.launch({headless:true,executablePath:'C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'});
 const steamSession=(birth='100',mode='desktop',pid=42)=>({known:true,running:true,mode,pid,birth,reason:''});
 const absent={known:true,running:false,mode:'stopped',pid:0,birth:'0',reason:''};
 const unknown={...absent,known:false,mode:'unknown'};
 const statusSession=state=>({steamRunning:state.running,steamPid:state.pid,steamBirth:state.birth});
 async function sessionPage({status=statusSession(steamSession()),holdUi=false,holdStatus=false,holdStatusReceipt=false,holdPolicy=false,failStatus=false,power=null,policyFromStatus=false}={}) {
  const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));
  await page.clock.install();
  await page.addInitScript(options=>{
   const p=window.__sessionProbe={...options,calls:[]};
   window.chrome.webview={
    addEventListener(_name,receive){p.deliver=data=>receive({data});},
    postMessage({id,cmd,args}) {
     if(id===undefined)return;
     p.calls.push({cmd,args});
     if(cmd==='steam.uiState'){
      const reply=()=>p.deliver({id,result:p.state});
      if(p.holdUi)p.releaseUi=reply;else queueMicrotask(reply);
     }else if(cmd==='power.lifecycle'&&p.power){
      queueMicrotask(()=>p.deliver({id,result:p.power}));
     }else queueMicrotask(()=>p.deliver({id,error:'fixture has no handler for '+cmd}));
    },
   };
  },{status,holdUi,holdStatus,holdStatusReceipt,holdPolicy,failStatus,power,policyFromStatus,state:steamSession()});
  await page.goto('http://127.0.0.1:'+server.address().port);
  return {
   reads:()=>page.evaluate(()=>window.__fixture.calls.map(c=>c.action)),
   snapshot:()=>page.waitForFunction(()=>window.__draft.getState().snapshot),
   mode:mode=>page.waitForFunction(mode=>document.querySelector('.steam-state-card .sc-text')?.textContent===mode,mode,{timeout:5000}),
   setStatus:state=>page.evaluate(status=>Object.assign(window.__fixture.snapshot,status),statusSession(state)),
   release:kind=>page.evaluate(kind=>{const p=window.__sessionProbe;p['hold'+kind]=false;p['release'+kind]();},kind),
   publish:state=>page.evaluate(state=>{const p=window.__sessionProbe;p.state=state;p.deliver({event:'steam.uiStateChanged',data:{}});},state),
   advance:()=>page.clock.fastForward(6000),
   close:async()=>{
    assert.deepEqual(await page.evaluate(()=>window.__sessionProbe.calls.filter(c=>c.cmd.startsWith('steam.')&&c.cmd!=='steam.uiState')),[]);
    await page.close();
   },page,
  };
 }
 const assertSessionReads=async(test,count,message)=>assert.deepEqual(await test.reads(),Array(count).fill('Status'),message);
 const powerState=(generation,publicationRevision,phase='ready',notificationPending=false)=>({
  generation,publicationRevision,phase,notificationPending,known:true,observedNotificationSequence:generation,
  hardwareWritesAllowed:phase==='ready'&&!notificationPending,inputReady:phase==='ready'&&!notificationPending,
 });
 const publishPower=(test,state)=>test.page.evaluate(state=>{
  const p=window.__sessionProbe;p.power=state;p.deliver({event:'power.lifecycleChanged',data:state});
 },state);
 if(!manualPriorityOnly&&(dispatchOnly||!scopeOnly&&!resumeOnly&&!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY&&!repairOwnerOnly)){
 for(const scenario of ['ready-before-dispatch','unconfirmed-before-dispatch','ready-after-dispatch','independent-notice','failed-status','hidden','joined-status']){
  const post=scenario==='ready-after-dispatch',joined=scenario==='joined-status';
  const test=await sessionPage({holdPolicy:!post&&!joined,holdStatusReceipt:post});
  const settled=()=>test.page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
  const release=kind=>test.page.waitForFunction(kind=>typeof window.__sessionProbe['release'+kind]==='function',kind);
  if(joined){
   await test.snapshot();await settled();await test.mode('桌面Steam运行中');
   await test.page.evaluate(()=>{const p=window.__sessionProbe;p.holdPolicy=true;void window.__draft.read();window.dispatchEvent(new Event('global:refresh'));});
  }
  const port=post?'StatusReceipt':'Policy';
  await release(port);
  if(!joined)await test.mode('桌面Steam运行中');
  await assertSessionReads(test,Number(post||joined),'Status must be held at the intended boundary');
  await test.page.evaluate(scenario=>{
   const p=window.__sessionProbe,f=window.__fixture,unconfirmed=scenario==='unconfirmed-before-dispatch';
   f.snapshot.themes.find(row=>row.name==='Round').configured=false;
   f.snapshot.themes.find(row=>row.name==='Round').backendEnabled=false;
   p.deliver({event:'steamAppearance.backendChanged',data:{phase:unconfirmed?'waiting-appearance':'ready',reason:unconfirmed?'appearance-unconfirmed':'appearance-applied-resident',linked:true,steamRunning:true,pid:812}});
   if(scenario==='independent-notice')window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged'));
   if(scenario==='hidden')window.dispatchEvent(new Event('ipc:window.hidden'));
   if(scenario==='failed-status')f.failRead=true;
  },scenario);
  await test.release(port);
  await settled();
  if(scenario==='failed-status'){
   assert.match(await test.page.locator('.appearance-status-line').textContent(),/外观控制暂未确认/);
   await test.page.evaluate(()=>window.__fixture.failRead=false);
  }
  if(post)assert.equal(await test.page.locator('.appearance-count').textContent(),'7 / 7','in-flight stale receipt is not the fresh observation');
  for(let i=0;i<2;i++)await test.advance();
  const count=scenario==='hidden'?0:scenario.endsWith('before-dispatch')?1:joined?3:2;
  await assertSessionReads(test,count,scenario+': only an actual fresh dispatch can cover the terminal notice');
  if(count)assert.equal(await test.page.locator('.appearance-count').textContent(),'6 / 7');
  cases.push('Status dispatch coverage: '+scenario);await test.close();
 }
  for(const scenario of ['toggle-before-dispatch','reapply-before-dispatch','toggle-after-dispatch']){
   const test=await sessionPage(),page=test.page,post=scenario.endsWith('after-dispatch'),reapply=scenario.startsWith('reapply');
   await test.snapshot();await test.mode('桌面Steam运行中');
   const settled=()=>page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
   await settled();await assertSessionReads(test,1);
   const start=await page.evaluate(post=>{
    const f=window.__fixture,p=window.__sessionProbe;
    const start={calls:f.calls.length,revision:f.snapshot.revision};
    if(post){p.holdStatusReceipt=true;f.snapshot.revision='b'.repeat(64);}
    else p.holdPolicyAt=p.policyReads+1;
    window.dispatchEvent(new Event('global:refresh'));
    return start;
   },post);
   const port=post?'StatusReceipt':'Policy';
   await page.waitForFunction(port=>typeof window.__sessionProbe['release'+port]==='function',port);
   await assertSessionReads(test,post?2:1);
   const actions=()=>page.evaluate(start=>window.__fixture.calls.slice(start).map(({action,args})=>{
    if(action!=='Status')return action;
    return JSON.parse(atob(args[args.indexOf('-Payload')+1])).lockWait==='single-v1'?'CardStatus':'WriteStatus';
   }),start.calls);
   if(reapply){
    await page.locator('.appearance-refresh').click();
    await page.waitForFunction(()=>window.__fixture.refreshes===1&&!window.__draft.getState().saving);
    assert.deepEqual(await actions(),['WriteStatus','Refresh'],'manual reapply retains its own fresh read');
    await test.release(port);await settled();
    supersededReadProbes[scenario]=await actions();
   }else{
    await page.clock.pauseAt(await page.evaluate(()=>Date.now()+150));
    await page.evaluate(revision=>{
     window.__fixture.snapshot.revision=revision;
     document.querySelector('.appearance-master').click();
    },start.revision);
    await test.release(port);
    assert.equal(await page.locator('.appearance-master').getAttribute('aria-checked'),'false');
    assert.equal(await page.locator('.appearance-count').textContent(),'0 / 7');
    assert.deepEqual(await page.evaluate(()=>{
     const draft=window.__draft.getState();return [draft.pendingCount,draft.phase,draft.snapshot.revision];
    }),[7,'pending',start.revision],'stale receipt must not replace the confirmed snapshot or the draft');
    await page.clock.fastForward(2999);
    assert.equal(await page.evaluate(()=>window.__fixture.applyRequests.length),0,'the three-second debounce must remain intact');
    const beforeSubmit=await actions();
    await page.clock.fastForward(1);
    assert.deepEqual(await page.evaluate(()=>{
     const draft=window.__draft.getState();return [window.__fixture.applyRequests.length,draft.pendingCount,draft.phase,draft.saving];
    }),[1,0,'idle',false]);
    supersededReadProbes[scenario]={beforeSubmit,afterSubmit:await actions()};
   }
   await test.close();
  }
  console.log(JSON.stringify({supersededReadProbes}));
  assert.deepEqual(supersededReadProbes,{
   'toggle-before-dispatch':{beforeSubmit:[],afterSubmit:['WriteStatus','Apply']},
   'reapply-before-dispatch':['WriteStatus','Refresh'],
   'toggle-after-dispatch':{beforeSubmit:['CardStatus'],afterSubmit:['CardStatus','WriteStatus','Apply']},
  });
  cases.push('Draft toggle retires an undispatched card Status without changing the debounce',
   'Manual reapply retires an undispatched card Status but keeps its own fresh Status',
   'A dispatched card Status can finish without overwriting the newer draft');
  const replacements={};
  for(const hide of [false,true]){
   const test=await sessionPage(),page=test.page;
   const settled=()=>page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
   await test.snapshot();await test.mode('桌面Steam运行中');await settled();await assertSessionReads(test,1);
   await page.evaluate(()=>{
    const p=window.__sessionProbe;p.holdPolicyAt=p.policyReads+1;
    window.dispatchEvent(new Event('global:refresh'));
   });
   await page.waitForFunction(()=>typeof window.__sessionProbe.releasePolicy==='function');
   await page.locator('.appearance-refresh').click();
   await page.waitForFunction(()=>window.__fixture.refreshes===1&&!window.__draft.getState().saving);
   assert.deepEqual(await test.reads(),['Status','Status','Refresh']);
   await page.evaluate(()=>{
    const round=window.__fixture.snapshot.themes.find(row=>row.name==='Round');
    round.configured=round.backendEnabled=false;
    window.__unmount();window.__mount();
   });
   await page.waitForFunction(()=>document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
   if(hide)await page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
   await test.release('Policy');await settled();
   const actions=await test.reads(),count=await page.locator('.appearance-count').textContent();
   await test.advance();
   replacements[hide?'hidden':'visible']={actions,count,afterBudget:await test.reads()};
   await test.close();
  }
  console.log(JSON.stringify({supersededReadReplacements:replacements}));
  assert.deepEqual(replacements,{
   visible:{actions:['Status','Status','Refresh','Status'],count:'6 / 7',afterBudget:['Status','Status','Refresh','Status']},
   hidden:{actions:['Status','Status','Refresh'],count:'7 / 7',afterBudget:['Status','Status','Refresh']},
  });
  Object.assign(supersededReadProbes,{replacements});
  cases.push('Visible replacement reads once after an older write-superseded Status retires',
   'Hidden replacement after a write does not dispatch a Status');
  const changedTarget=await sessionPage(),page=changedTarget.page;
  await changedTarget.snapshot();await changedTarget.mode('桌面Steam运行中');
  await page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
  await assertSessionReads(changedTarget,1);
  await page.evaluate(()=>{
   const f=window.__fixture;
   f.snapshot.targetId='b'.repeat(16);
   const round=f.snapshot.themes.find(row=>row.name==='Round');
   round.configured=round.backendEnabled=f.configs.Round.active=false;
  });
  await page.locator('.appearance-refresh').click();
  await page.waitForFunction(()=>window.__draft.getState().phase==='error'&&document.querySelector('.appearance-count')?.textContent==='6 / 7');
  await assertSessionReads(changedTarget,2,'first reapply must only publish the fresh scope');
  await changedTarget.advance();
  await assertSessionReads(changedTarget,2,'no automatic reapply or repeat Status after the refusal');
  await page.locator('.appearance-refresh').click();
  await page.waitForFunction(()=>window.__fixture.refreshes===1&&!window.__draft.getState().saving);
  assert.deepEqual(await changedTarget.reads(),['Status','Status','Status','Refresh']);
  await changedTarget.close();
  cases.push('Fallback reapply shows the new directory without a second read or write until a new click');
 }
 if(dispatchOnly||!scopeOnly&&!resumeOnly&&!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY&&!repairOwnerOnly){
  for(const scenario of ['toggle','reapply','reset','reapply-after-dispatch','reapply-after-scope-change']){
   const test=await sessionPage({status:{...statusSession(steamSession()),packOwned:true}}),page=test.page;
   const afterDispatch=scenario.startsWith('reapply-after-'),moved=scenario.endsWith('scope-change');
   await test.snapshot();await test.mode('桌面Steam运行中');
   await page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
   await assertSessionReads(test,1);
   await page.evaluate(afterDispatch=>{
    const p=window.__sessionProbe,f=window.__fixture;
    if(afterDispatch)p.holdReconcileReceipt=true;
    else p.holdPolicyAt=p.policyReads+2;
    f.snapshot.configuredMatchesBackend=false;
    f.snapshot.themes.find(row=>row.name==='Round').backendEnabled=false;
    window.dispatchEvent(new Event('global:refresh'));
   },afterDispatch);
   const port=afterDispatch?'ReconcileReceipt':'Policy';
   await page.waitForFunction(port=>typeof window.__sessionProbe['release'+port]==='function',port);
   assert.deepEqual(await test.reads(),afterDispatch?['Status','Status','Reconcile']:['Status','Status']);
   if(scenario==='reset'){
    await page.getByRole('button',{name:'编辑外观'}).click();
    await page.getByRole('button',{name:'恢复预设',exact:true}).click();
   }
   await page.clock.pauseAt(await page.evaluate(()=>Date.now()+150));
   await page.evaluate(({scenario,moved})=>{
    if(moved)window.__sessionProbe.reconcileReceiptScope={steamBirth:'101'};
    const selector=scenario==='toggle'?'.appearance-master':scenario==='reset'?'.preset-confirm':'.appearance-refresh';
    const button=document.querySelector(selector);
    if(button.disabled)throw Error('Manual control is disabled while Reconcile is pending');
    button.click();
   },{scenario,moved});
   if(afterDispatch)assert.deepEqual(await test.reads(),['Status','Status','Reconcile'],'do not start a second write while Reconcile owns the lock');
   await test.release(port);
   let beforeSubmit;
   if(scenario==='toggle'){
    assert.equal(await page.locator('.appearance-count').textContent(),'0 / 7');
    await page.clock.fastForward(2999);
    assert.equal(await page.evaluate(()=>window.__fixture.applyRequests.length),0);
    beforeSubmit=await test.reads();
    await page.clock.fastForward(1);
   }
   await page.waitForFunction(({scenario,moved})=>{
    const f=window.__fixture,s=window.__draft.getState();
    return !s.saving&&!s.locked&&!s.pendingCount&&(moved?s.phase==='error':scenario==='toggle'?f.applyRequests.length===1:scenario==='reset'?f.presets.length===1:f.refreshes===1);
   },{scenario,moved});
   const actions=await test.reads();
   const state=await page.evaluate(()=>{
    const f=window.__fixture,s=window.__draft.getState();
    return {phase:s.phase,birth:s.snapshot.steamBirth,enabled:s.snapshot.themes.filter(row=>row.configured).length,hiddenCalls:f.hiddenCalls};
   });
   await test.advance();
   manualPriorityProbes[scenario]={beforeSubmit,actions,state,afterBudget:await test.reads()};
   assert.equal(state.hiddenCalls,0);
   assert.deepEqual(manualPriorityProbes[scenario].afterBudget,actions);
   assert.equal(state.phase,moved?'error':'idle');
   assert.equal(state.birth,moved?'101':'100','an already dispatched Reconcile receipt still updates the observed scope');
   assert.equal(state.enabled,scenario==='toggle'?0:7);
   await test.close();
  }
  console.log(JSON.stringify({manualPriorityProbes}));
  const {toggle,reapply,reset,'reapply-after-dispatch':dispatched,'reapply-after-scope-change':moved}=manualPriorityProbes;
  assert.deepEqual(toggle.beforeSubmit,['Status','Status'],'an undispatched Reconcile must yield before the debounce expires');
  assert.deepEqual(toggle.actions,['Status','Status','Status','Apply']);
  assert.deepEqual(reapply.actions,['Status','Status','Status','Refresh']);
  assert.deepEqual(reset.actions,['Status','Status','Status','Reset','Reload']);
  assert.deepEqual(dispatched.actions,['Status','Status','Reconcile','Status','Refresh']);
  assert.deepEqual(moved.actions,['Status','Status','Reconcile'],'a changed receipt must cancel the queued reapply');
  cases.push('Manual total toggle retires an undispatched Reconcile and keeps the three-second debounce',
   'Manual reapply and reset retire an undispatched Reconcile but retain their own required checks',
   'An already dispatched Reconcile completes before the queued reapply',
   'A changed Steam lifetime in the dispatched Reconcile receipt prevents replay into the new lifetime');
 }
 if(!dispatchOnly){
 if(!scopeOnly&&!resumeOnly&&!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY){
 for(const timing of ['active-at-policy','completion-during-policy','completion-before-status-reply','completion-without-notice','completion-notice-before-recheck'])for(const fixed of [true,false]){
  const test=await sessionPage({status:{...statusSession(steamSession()),packOwned:true}});
  await test.snapshot();await test.mode('桌面Steam运行中');
  await test.publish(steamSession('100','bigpicture'));await test.mode('大屏Steam运行中');
  await assertSessionReads(test,1,'one quiet initial read');
  await test.page.evaluate(timing=>{
   const p=window.__sessionProbe,f=window.__fixture;
   p.automaticRepairActive=true;
   if(timing==='completion-during-policy'||timing.startsWith('completion-without')||timing.endsWith('before-recheck'))p.holdPolicyAt=p.policyReads+2;
   if(timing==='completion-without-notice'||timing==='completion-notice-before-recheck')p.automaticAppliedRevision=6;
   if(timing==='completion-before-status-reply')p.holdStatusReceipt=true;
   f.snapshot.configuredMatchesBackend=false;
   const row=f.snapshot.themes.find(row=>row.name==='Round');row.backendEnabled=!row.configured;
   window.dispatchEvent(new Event('global:refresh'));
  },timing);
  const nativeReceipt=timing==='completion-without-notice'||timing==='completion-notice-before-recheck';
  if(timing==='completion-during-policy'||nativeReceipt)await test.page.waitForFunction(()=>typeof window.__sessionProbe.releasePolicy==='function');
  else if(timing==='completion-before-status-reply')await test.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatusReceipt==='function');
  else await test.page.waitForFunction(()=>window.__draft.getState().snapshot?.configuredMatchesBackend===false&&!window.__draft.getState().saving);
  await assertSessionReads(test,2,'the card must not start Reconcile while native owns the repair');
  await test.page.evaluate(({fixed,nativeReceipt})=>{
   const p=window.__sessionProbe,f=window.__fixture;p.automaticRepairActive=false;
   if(fixed){f.snapshot.configuredMatchesBackend=true;const row=f.snapshot.themes.find(row=>row.name==='Round');row.backendEnabled=row.configured;}
   const detail={phase:fixed?'ready':'waiting-appearance',reason:fixed?'appearance-applied-resident':'appearance-unconfirmed',pid:812,linked:true,steamRunning:true};
   if(nativeReceipt)p.automaticAppliedRevision=7;
   else p.deliver({event:'steamAppearance.backendChanged',data:detail});
   p.terminalNotice=detail;
   p.releasePolicy?.();
   p.holdStatusReceipt=false;p.releaseStatusReceipt?.();
  },{fixed,nativeReceipt});
  await test.page.waitForFunction(()=>!window.__draft.getState().saving);
  const before=await test.reads();
  assert.deepEqual(before,['Status','Status'],'a terminal notice supersedes the old mismatch before dispatch');
  if(timing==='completion-notice-before-recheck')await test.page.evaluate(()=>{
   const p=window.__sessionProbe;p.deliver({event:'steamAppearance.backendChanged',data:p.terminalNotice});
  });
  await test.advance();
  await test.page.waitForFunction(()=>window.__draft.getState().snapshot?.configuredMatchesBackend&&!window.__draft.getState().saving);
  await test.advance();await test.advance();
  const actions=await test.reads();
  assert.deepEqual(actions,fixed?['Status','Status','Status']:['Status','Status','Status','Reconcile']);
  assert.equal(await test.page.evaluate(()=>window.__fixture.hiddenCalls),0);
  const label=timing+(fixed?'-healthy':'-still-mismatched');
  repairOwnerProbes[label]={before,actions,policyReads:await test.page.evaluate(()=>window.__sessionProbe.policyReads)};cases.push(label);
  await test.close();
 }
 }
 if(!repairOwnerOnly){
 if(!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY){
  const settle=['waiting-steam-ui','resume-css-settle-delay'];
  const recheck=['applying','resume-css-recheck'];
  const wait=['waiting-steam-ui','appearance-needs-confirmed-bigpicture'];
  const ready=['ready','appearance-applied-resident'];
  const tick=(test,ms)=>test.page.clock.fastForward(ms);
  const setTheme=(test,configured)=>test.page.evaluate(configured=>{
   const theme=window.__fixture.snapshot.themes.find(row=>row.name==='Round');
   theme.configured=configured;theme.backendEnabled=configured;
  },configured);
  const notice=(test,[phase,reason])=>test.page.evaluate(detail=>window.dispatchEvent(
   new CustomEvent('ipc:steamAppearance.backendChanged',{detail})),{phase,reason,linked:true,steamRunning:true});
  const burst=async test=>{for(const event of [settle,recheck,wait])await notice(test,event);};
  async function resumePage({bigpicture=true,powered=false}={}){
   const test=await sessionPage(powered?{power:powerState(1,1)}:{});
   await test.snapshot();await test.mode('桌面Steam运行中');
   if(powered)await test.page.waitForFunction(()=>window.__powerVisibility()?.powerGeneration===1);
   if(bigpicture){await test.publish(steamSession('100','bigpicture'));await test.mode('大屏Steam运行中');}
   await test.advance();await assertSessionReads(test,1,'resume replay starts with a quiet, confirmed card');
   assert.equal(await test.page.locator('.appearance-status-dot.ready').count(),1);
   return test;
  }
  const accountProgress=['applying','appearance-account-recheck'];
  const accountReplay=await resumePage();
  // Win5 005840: account recheck, helper notice 6 ms later, then Ready at 1721 ms.
  await notice(accountReplay,accountProgress);await tick(accountReplay,6);
  await notice(accountReplay,['applying','temporary-css-use']);await tick(accountReplay,594);
  await tick(accountReplay,1121);
  const accountBefore=(await accountReplay.reads()).length;
  await setTheme(accountReplay,false);await notice(accountReplay,ready);await tick(accountReplay,600);
  const accountAfter=(await accountReplay.reads()).length,accountCount=await accountReplay.page.locator('.appearance-count').textContent();
  await accountReplay.advance();const accountSettled=(await accountReplay.reads()).length;
  console.log(JSON.stringify({accountReplay:{before:accountBefore,after:accountAfter,count:accountCount,settled:accountSettled}}));
  assert.deepEqual({before:accountBefore,after:accountAfter,count:accountCount,settled:accountSettled},
   {before:1,after:2,count:'6 / 7',settled:2},'account progress must not spend a Status or defer Ready to the next read budget');
  cases.push('Win5 account recheck reads once after the 1721 ms result');await accountReplay.close();

  const accountLost=await resumePage();
  await notice(accountLost,accountProgress);await tick(accountLost,3200);
  await assertSessionReads(accountLost,1,'account progress alone must not start an early Status');
  await notice(accountLost,accountProgress);await tick(accountLost,1650);
  await assertSessionReads(accountLost,2,'a lost account result gets one read at the original fallback deadline');
  await accountLost.advance();await assertSessionReads(accountLost,2,'the account fallback must not poll');
  cases.push('A lost account result has one fallback and repeats cannot extend it');await accountLost.close();

  for(const [scenario,outcome] of [
   ['budget exhausted',['waiting-appearance','appearance-account-recheck']],
   ['helper unavailable',['waiting-appearance','appearance-helper-unavailable']],
   ['failure',['failed','guard-check-failed']],
   ['cancellation',['canceled','appearance-ready-scope-changed']],
   ['legacy event',null],
   ['explicit refresh',null],
   ['already queued',null],
  ]){
   const test=await resumePage();
   const legacy=()=>test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
   await setTheme(test,false);
   if(scenario==='already queued')await legacy();
   await notice(test,accountProgress);
   if(outcome)await notice(test,outcome);
   if(scenario==='legacy event')await legacy();
   if(scenario==='explicit refresh')await test.page.evaluate(()=>window.dispatchEvent(new Event('global:refresh')));
   await tick(test,850);await assertSessionReads(test,2,scenario+' must read without the account fallback delay');
   assert.equal(await test.page.locator('.appearance-count').textContent(),'6 / 7');
   await test.advance();await assertSessionReads(test,2,scenario+' must clear the account fallback');
   cases.push('Account recheck preserves '+scenario);await test.close();
  }
  const accountHidden=await resumePage();
  await notice(accountHidden,accountProgress);
  await accountHidden.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  await notice(accountHidden,accountProgress);await notice(accountHidden,ready);await accountHidden.advance();
  await assertSessionReads(accountHidden,1,'hidden account recheck must not run its fallback or read its result');
  await accountHidden.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
  await tick(accountHidden,850);await assertSessionReads(accountHidden,2,'show must read with normal timing');
  await accountHidden.advance();await assertSessionReads(accountHidden,2,'show must not re-arm the account fallback');
  cases.push('Hidden account recheck retires the fallback and one show reads once');await accountHidden.close();

  // Win5 archive 162101: same-tick settle/recheck/wait, then helper and Ready.
  for(const [helperMs,readyMs,powered] of [[621,2573,false],[618,2894,true],[622,4177,true]]){
   const test=await resumePage({powered});
   if(powered){
    await publishPower(test,powerState(2,2,'suspended'));
    assert.equal(await test.page.evaluate(()=>window.__powerVisibility()?.visible),false);
    await publishPower(test,powerState(2,3));
    assert.equal(await test.page.evaluate(()=>window.__powerVisibility()?.visible),true);
   }
   await burst(test);await tick(test,helperMs);
   await notice(test,['applying','temporary-css-use']);await tick(test,readyMs-helperMs);
   const before=(await test.reads()).length;
   await setTheme(test,false);await notice(test,ready);await tick(test,600);
   const after=(await test.reads()).length,count=await test.page.locator('.appearance-count').textContent();
   console.log(JSON.stringify({resumeReplay:{helperMs,readyMs,powered,before,after,count}}));
   assert.deepEqual({before,after,count},{before:1,after:2,count:'6 / 7'},'the interim wake burst must not spend a Status or delay the final result');
   await test.advance();await assertSessionReads(test,2,'no trailing read or polling after Ready');
   cases.push('Win5 resume replay '+readyMs+' ms reads only once after Ready');await test.close();
  }
  const early=await resumePage({powered:true});
  await publishPower(early,powerState(2,2,'resuming'));
  await burst(early);await tick(early,180);
  await assertSessionReads(early,1,'no appearance read before public Ready');
  await publishPower(early,powerState(2,3));
  await tick(early,438);await notice(early,['applying','temporary-css-use']);
  await tick(early,2276);
  const earlyBefore=(await early.reads()).length;
  await setTheme(early,false);await notice(early,ready);await tick(early,600);
  const earlyAfter=(await early.reads()).length,earlyCount=await early.page.locator('.appearance-count').textContent();
  console.log(JSON.stringify({earlyResume:{before:earlyBefore,after:earlyAfter,count:earlyCount}}));
  assert.deepEqual({before:earlyBefore,after:earlyAfter,count:earlyCount},{before:1,after:2,count:'6 / 7'},
   'a guard notice queued ahead of public Ready must not trigger a pre-result Status');
  await early.advance();await assertSessionReads(early,2,'no trailing read after cross-order Ready');
  cases.push('Guard starts before public Ready: one read after the result');await early.close();

  for(const scenario of ['expired','new-generation','terminal-before-ready','hidden-at-start','legacy-notice','lost-outcome']){
   const test=await resumePage({powered:true});
   if(scenario==='hidden-at-start')await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
   await publishPower(test,powerState(2,2,'resuming'));
   await burst(test);
   if(scenario==='expired')await tick(test,4700);
   if(scenario==='terminal-before-ready')await notice(test,['canceled','appearance-ready-scope-changed']);
   if(scenario==='legacy-notice')await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
   if(scenario==='lost-outcome')await tick(test,2500);
   if(scenario==='hidden-at-start')await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
   await assertSessionReads(test,1,scenario+' must not read before public Ready');
   await publishPower(test,powerState(scenario==='new-generation'?3:2,3));
   if(scenario==='lost-outcome'){
    await tick(test,1250);await assertSessionReads(test,1,'early progress still defers the initial visibility read');
    await tick(test,1100);await assertSessionReads(test,2,'fallback uses the early notice deadline, not a new window after Ready');
   }else{
    await tick(test,850);await assertSessionReads(test,2,scenario+' must not postpone the normal visibility read');
   }
   await test.advance();await assertSessionReads(test,2,scenario+' must not leave a trailing timer');
   cases.push('Early guard notice: '+scenario+' fallback');await test.close();
  }

  const noResult=await resumePage();
  await burst(noResult);await tick(noResult,3200);
  await assertSessionReads(noResult,1,'short resume progress is not a second Status');
  await burst(noResult);await tick(noResult,1650);
  await assertSessionReads(noResult,2,'missing result falls back once at the original absolute deadline');
  await noResult.advance();await assertSessionReads(noResult,2,'fallback is not periodic');
  cases.push('A lost resume result falls back once, without extending the deadline');await noResult.close();

  for(const scenario of ['desktop','desktop-edge','failure','canceled','other-notice','already-queued','manual','replacement']){
   const test=await resumePage({bigpicture:scenario!=='desktop'});
   if(scenario==='already-queued')await notice(test,['waiting-appearance','appearance-unconfirmed']);
   await burst(test);
   if(scenario==='desktop-edge'){await test.publish(steamSession());await test.mode('桌面Steam运行中');}
   if(scenario==='failure')await notice(test,['waiting-appearance','appearance-unconfirmed']);
   if(scenario==='canceled')await notice(test,['canceled','appearance-ready-scope-changed']);
   if(scenario==='other-notice')await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
   if(scenario==='manual')await test.page.evaluate(()=>window.dispatchEvent(new Event('global:refresh')));
   if(scenario==='replacement'){
    await test.setStatus(steamSession('101','bigpicture'));await test.publish(steamSession('101','bigpicture'));
    await test.page.waitForFunction(()=>window.__sessionProbe.calls.filter(c=>c.cmd==='steam.uiState').length>=3);
   }
   await tick(test,850);await assertSessionReads(test,2,scenario+' must use the ordinary observation path');
   await test.advance();await assertSessionReads(test,2,scenario+' must not leave a wake fallback queued');
   cases.push('Resume coalescing preserves '+scenario+' refresh');await test.close();
  }
  for(const scenario of ['power-only-without-guard','window-show-after-power-return']){
   const test=await resumePage({powered:true});
   await publishPower(test,powerState(2,2,'suspended'));
   if(scenario==='window-show-after-power-return')await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
   await publishPower(test,powerState(2,3));
   if(scenario==='window-show-after-power-return'){
    assert.equal(await test.page.evaluate(()=>window.__powerVisibility()?.visible),false);
    await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
    await burst(test);
   }
   await tick(test,850);await assertSessionReads(test,2,scenario+' must keep the ordinary visibility read');
   await test.advance();await assertSessionReads(test,2,scenario+' must not leave a second timer');
   cases.push('Resume coalescing preserves '+scenario+' refresh');await test.close();
  }
  const hiddenResume=await resumePage();
  await burst(hiddenResume);
  await hiddenResume.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  await notice(hiddenResume,ready);await hiddenResume.advance();
  await assertSessionReads(hiddenResume,1,'hidden card must retire the fallback and drop Ready');
  await hiddenResume.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
  await tick(hiddenResume,850);await assertSessionReads(hiddenResume,2,'return reads once with normal timing');
  await hiddenResume.advance();await assertSessionReads(hiddenResume,2,'visibility return has no second read');
  cases.push('Hidden resume retires deferred status and restores one read on show');await hiddenResume.close();

  const manualEntry=['applying','manual-bigpicture-entry'];
  const manual=await resumePage({bigpicture:false});
  await notice(manual,manualEntry);await tick(manual,650);
  await notice(manual,wait);
  await manual.publish(steamSession('100','bigpicture'));await manual.mode('大屏Steam运行中');
  await tick(manual,1450);
  const manualBefore=(await manual.reads()).length;
  await setTheme(manual,false);await notice(manual,ready);await tick(manual,650);
  const manualAfter=(await manual.reads()).length,manualCount=await manual.page.locator('.appearance-count').textContent();
  await manual.advance();const manualSettled=(await manual.reads()).length;
  console.log(JSON.stringify({manualReplay:{before:manualBefore,after:manualAfter,count:manualCount,settled:manualSettled}}));
  assert.deepEqual({before:manualBefore,after:manualAfter,count:manualCount,settled:manualSettled},
   {before:1,after:2,count:'6 / 7',settled:2},'manual entry progress must not start a second helper or push the result past the read budget');
  cases.push('Manual Big Picture entry updates the matching card once on Ready');await manual.close();

  for(const scenario of ['lost','late','failure','cancellation','legacy','explicit','already queued','replacement','already stale','becomes stale','hidden']){
   const test=await resumePage({bigpicture:false});
   const legacy=()=>test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
   const setUnknown=async()=>{await test.publish(unknown);await test.mode('桌面Steam运行中（状态待复核）');};
   if(scenario==='already queued')await legacy();
   if(scenario==='already stale')await setUnknown();
   await notice(test,manualEntry);
   if(scenario==='lost'||scenario==='late'){
    await tick(test,3300);await notice(test,manualEntry);await notice(test,wait);
    await assertSessionReads(test,1,'manual progress and mode waiting cannot spend the read budget');
    await tick(test,1500);
   }else if(scenario==='hidden'){
    await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
    await notice(test,wait);await notice(test,ready);await test.advance();
    await assertSessionReads(test,1,'a hidden card retires the manual entry wait');
    await test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));await tick(test,850);
   }else{
    if(scenario==='failure')await notice(test,['waiting-appearance','appearance-helper-unavailable']);
    if(scenario==='cancellation')await notice(test,['canceled','appearance-ready-scope-changed']);
    if(scenario==='legacy')await legacy();
    if(scenario==='explicit')await test.page.evaluate(()=>window.dispatchEvent(new Event('global:refresh')));
    if(scenario==='becomes stale')await setUnknown();
    if(scenario==='replacement'){
     const next=steamSession('101','bigpicture');await test.setStatus(next);await test.publish(next);await test.mode('大屏Steam运行中');
    }
    await tick(test,850);
   }
   await assertSessionReads(test,2,scenario+' must retain the appropriate Status timing during a manual entry');
   if(scenario==='replacement')await test.page.waitForFunction(()=>window.__draft.getState().snapshot?.steamBirth==='101');
   if(scenario==='late'){
    await setTheme(test,false);await notice(test,ready);await tick(test,850);
    await assertSessionReads(test,2,'late Ready still honors the regular read budget');
    await test.advance();await test.page.waitForFunction(()=>document.querySelector('.appearance-count')?.textContent==='6 / 7');
   }
   await test.advance();await assertSessionReads(test,scenario==='late'?3:2,scenario+' must not leave a periodic Status');
   cases.push('Manual entry retains '+scenario+' observation');await test.close();
  }
 }
 if(!resumeOnly){
 const reapply=await sessionPage();
 await reapply.snapshot();await reapply.mode('桌面Steam运行中');
 await assertSessionReads(reapply,1,'one initial Status before repeated reapply');
 await reapply.page.evaluate(()=>{
  const p=window.__sessionProbe;p.holdStatus=true;p.fullRefreshReceipt=true;
  document.querySelector('.appearance-refresh').click();
 });
 await reapply.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatus==='function');
 await reapply.page.evaluate(()=>{for(let i=0;i<10;i++)document.querySelector('.appearance-refresh').click();});
 assert.deepEqual(await reapply.reads(),['Status','Status'],'held preparation has not dispatched Refresh');
 await reapply.release('Status');await reapply.page.waitForFunction(()=>!window.__draft.getState().saving);await reapply.advance();
 assert.deepEqual(await reapply.reads(),['Status','Status','Refresh'],'only the confirmed Refresh covers duplicate clicks made before dispatch');
 await reapply.page.locator('.appearance-refresh').click();await reapply.page.waitForFunction(()=>window.__fixture.refreshes===2&&!window.__draft.getState().saving);
 assert.deepEqual(await reapply.reads(),['Status','Status','Refresh','Status','Refresh']);
 assert.equal(await reapply.page.evaluate(()=>window.__fixture.hiddenCalls),0,'the verified existing backend does not need an Ensure helper');
 cases.push('Actual reapply button coalesces pre-dispatch clicks and preserves a new click after completion');await reapply.close();
 if(!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY){
 const hidden=async test=>test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
 const shown=async test=>test.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
 const waiting=async(test,kind)=>test.page.waitForFunction(kind=>typeof window.__sessionProbe['release'+kind]==='function',kind);
 const busyRead=async test=>test.page.evaluate(()=>window.__fixture.busyReads=2);
 const beforeDispatch=await sessionPage({holdPolicy:true});
 await waiting(beforeDispatch,'Policy');await hidden(beforeDispatch);await beforeDispatch.release('Policy');await beforeDispatch.advance();
 retirementProbes.hiddenBeforeDispatch=await beforeDispatch.reads();
 await shown(beforeDispatch);await beforeDispatch.advance();await beforeDispatch.snapshot();await beforeDispatch.advance();
 retirementProbes.backAfterHidden=await beforeDispatch.reads();await beforeDispatch.close();

 const afterDispatch=await sessionPage({holdStatus:true});
 await waiting(afterDispatch,'Status');await busyRead(afterDispatch);await hidden(afterDispatch);await afterDispatch.release('Status');
 for(let i=0;i<3;i++)await afterDispatch.advance();
 retirementProbes.hiddenDuringBusy=await afterDispatch.reads();
 assert.equal(await afterDispatch.page.evaluate(()=>window.__draft.getState().snapshot),null);
 await afterDispatch.close();

 const visible=await sessionPage({holdStatus:true});
 await waiting(visible,'Status');await busyRead(visible);await visible.release('Status');
 for(let i=0;i<3;i++)await visible.advance();
 await visible.snapshot();retirementProbes.visibleBusy=await visible.reads();await visible.close();

 for(const leave of [false,true]){
  const replaced=await sessionPage({holdStatus:true,holdUi:true});
  await waiting(replaced,'Status');
  await replaced.page.evaluate(()=>{window.__unmount();window.__mount();});
  await replaced.page.waitForFunction(()=>document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
  await assertSessionReads(replaced,1,'Mounts may not issue concurrent Status helpers');
  if(leave)await hidden(replaced);
  await replaced.release('Status');await replaced.advance();await replaced.advance();
  retirementProbes[leave?'replacementHidden':'replacementVisible']={actions:await replaced.reads(),snapshot:await replaced.page.evaluate(()=>!!window.__draft.getState().snapshot)};
  await replaced.close();
 }
 console.log(JSON.stringify({retirementProbes}));
 assert.deepEqual(retirementProbes,{
  hiddenBeforeDispatch:[],backAfterHidden:['Status'],hiddenDuringBusy:['Status'],visibleBusy:['Status','Status','Status'],
  replacementVisible:{actions:['Status','Status'],snapshot:true},replacementHidden:{actions:['Status'],snapshot:false},
 });
 cases.push('Retired card skips Status before shell dispatch and show reads once',
  'A hidden card stops busy retries without changing the visible legacy retry path',
  'One visible replacement rechecks a discarded read; a hidden replacement does not');
 }
 if(!scopeOnly){
 const powered=await sessionPage({power:powerState(1,1)});
 const powerVisibility=()=>powered.page.evaluate(()=>window.__powerVisibility());
 const publishPower=state=>powered.page.evaluate(state=>{
  const p=window.__sessionProbe;p.power=state;p.deliver({event:'power.lifecycleChanged',data:state});
 },state);
 const appearanceNotice=()=>powered.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
 const statusCount=async()=>(await powered.reads()).filter(action=>action==='Status').length;
 const powerReadCount=()=>powered.page.evaluate(()=>window.__sessionProbe.calls.filter(c=>c.cmd==='power.lifecycle').length);
 await powered.snapshot();await powered.mode('桌面Steam运行中');
 await powered.page.waitForFunction(()=>window.__powerVisibility()?.powerGeneration===1);
 const initialPowered=await statusCount();
 assert.equal(initialPowered,1);
 await appearanceNotice();
 await publishPower(powerState(2,2,'suspended'));
 assert.equal((await powerVisibility()).visible,false);
 await powered.advance();
 powerProbes.queuedDuringSuspend=(await statusCount())-initialPowered;
 await publishPower(powerState(2,3,'resuming'));
 await appearanceNotice();await powered.advance();
 powerProbes.newDuringResume=(await statusCount())-initialPowered-powerProbes.queuedDuringSuspend;
 await publishPower(powerState(2,4,'ready',true));
 assert.equal((await powerVisibility()).powerReady,false);
 await appearanceNotice();await powered.advance();
 powerProbes.newDuringPending=(await statusCount())-initialPowered-powerProbes.queuedDuringSuspend-powerProbes.newDuringResume;
 console.log(JSON.stringify({powerProbes}));
 assert.deepEqual(powerProbes,{queuedDuringSuspend:0,newDuringResume:0,newDuringPending:0},'public power closure must drop queued and new theme Status reads');
 cases.push('Public suspend, resume and pending notification suppress queued and new theme reads');

 await publishPower(powerState(2,5));
 assert.equal((await powerVisibility()).visible,true);
 const readyPowerReads=await powerReadCount();
 await publishPower(powerState(2,6));await appearanceNotice();
 await powered.advance();await powered.advance();
 await assertSessionReads(powered,initialPowered+1,'public Ready in the visible window must request exactly one fresh Status');
 powerProbes.powerReadsWhileReady=(await powerReadCount())-readyPowerReads;
 assert.equal(powerProbes.powerReadsWhileReady,0,'a theme Status and backend notice while Ready do not request a power read');
 cases.push('Public Ready while visible coalesces to one fresh Status');

 await publishPower(powerState(3,7,'suspended'));
 await powered.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
 await publishPower(powerState(3,8));await appearanceNotice();await powered.advance();
 await assertSessionReads(powered,initialPowered+1,'public Ready in a hidden window must not start theme Status');
 assert.deepEqual(await powered.page.evaluate(()=>[...new Set(window.__sessionProbe.calls.map(c=>c.cmd))].sort()),['diagnostics.frontendTrace','power.lifecycle','settings.location','steam.uiState']);
 assert.equal(await powered.page.evaluate(()=>window.__fixture.hiddenCalls),0);
 cases.push('Public Ready while hidden issues no theme read or Steam/CSS control');
 await powered.close();

 const late=await sessionPage({power:powerState(1,1),holdStatus:true,status:{...statusSession(steamSession()),packOwned:true,configuredMatchesBackend:false}});
 const publishLate=state=>late.page.evaluate(state=>{
  const p=window.__sessionProbe;p.power=state;p.deliver({event:'power.lifecycleChanged',data:state});
 },state);
 await late.mode('桌面Steam运行中');
 await late.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatus==='function'&&window.__powerVisibility().powerGeneration===1);
 await publishLate(powerState(2,2,'suspended'));await publishLate(powerState(2,3));
 await late.release('Status');
 // Before the fresh Status timer expires, the old result must not submit a repair.
 await late.page.clock.fastForward(20);
 powerProbes.lateRepairs=(await late.reads()).filter(action=>action==='Reconcile').length;
 console.log(JSON.stringify({powerProbes}));
 await assertSessionReads(late,1,'a pre-suspend Status response cannot submit a repair after a complete suspend/Ready cycle');
 assert.equal(await late.page.evaluate(()=>window.__draft.getState().snapshot),null,'the old snapshot must not be displayed');
 await late.advance();await late.advance();
 assert.deepEqual(await late.reads(),['Status','Status','Reconcile'],'the fresh Ready Status owns the one observed mismatch repair');
 assert.equal(await late.page.evaluate(()=>window.__fixture.hiddenCalls),0);
 cases.push('An in-flight Status from before suspend cannot repair after Ready; the fresh Status can');
 await late.close();

 for(const boundary of ['suspend-serial','notification-sequence']){
 const priorReady={...powerState(7,1),suspendSerial:7};
 const merged=await sessionPage({power:priorReady,holdStatusReceipt:true,status:{...statusSession(steamSession()),packOwned:true,configuredMatchesBackend:false}});
 await merged.mode('桌面Steam运行中');
 await merged.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatusReceipt==='function'&&window.__powerVisibility().powerGeneration===7);
 const mergedReady={...priorReady,publicationRevision:3,...(boundary==='suspend-serial'?{suspendSerial:8}:{observedNotificationSequence:8})};
 const publishMerged=state=>merged.page.evaluate(state=>{
  const p=window.__sessionProbe;p.power=state;p.deliver({event:'power.lifecycleChanged',data:state});
 },state);
 // Keep the retired reply held while permitting the next request to complete.
 await merged.page.evaluate(()=>window.__sessionProbe.holdStatusReceipt=false);
 if(boundary==='notification-sequence'){
  await publishMerged({...priorReady,publicationRevision:2});await merged.advance();
  await assertSessionReads(merged,1,'a publication revision alone cannot retire the held read');
 }
 await publishMerged(mergedReady);await merged.page.clock.fastForward(20);
 assert.deepEqual(await merged.reads(),['Status'],'held Status from before coalesced Ready must not start a repair');
 assert.equal(await merged.page.evaluate(()=>window.__draft.getState().snapshot),null,'held Status from before coalesced Ready must not publish');
 await merged.advance();await merged.advance();
 assert.deepEqual(await merged.reads(),['Status','Status','Reconcile'],boundary+': new Status and repair must finish before the retired reply arrives');
 await merged.snapshot();
 const mergedSnapshot=()=>merged.page.evaluate(()=>window.__draft.getState().snapshot);
 const confirmedMerged=await mergedSnapshot();
 await merged.release('StatusReceipt');await merged.advance();
 assert.deepEqual(await mergedSnapshot(),confirmedMerged,'the retired receipt must not replace the recovered snapshot');
 await publishMerged({...mergedReady,publicationRevision:4,inputReady:false});
 await publishMerged({...priorReady,publicationRevision:5});
 await merged.advance();assert.deepEqual(await merged.reads(),['Status','Status','Reconcile']);
 if(boundary==='notification-sequence'){
  await publishMerged({...mergedReady,publicationRevision:6,observedNotificationSequence:9});
  await merged.advance();assert.deepEqual(await merged.reads(),['Status','Status','Reconcile'],'idle cards do not read on sequence advancement');
  await merged.page.evaluate(()=>{
   window.__sessionProbe.holdStatusReceipt=true;
   window.__sessionProbe.releaseStatusReceipt=null;
   window.dispatchEvent(new Event('global:refresh'));
  });
  await merged.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatusReceipt==='function');
  await merged.page.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
  await publishMerged({...mergedReady,publicationRevision:7,observedNotificationSequence:10});
  await merged.release('StatusReceipt');await merged.advance();
  assert.deepEqual(await merged.reads(),['Status','Status','Reconcile','Status'],'hidden cards neither read again nor repair');
  assert.deepEqual(await mergedSnapshot(),confirmedMerged);
 }
 cases.push('Coalesced Ready '+boundary+' retires a held theme Status before either publication or automatic repair');
 await merged.close();
 }
 const scoped=await sessionPage({power:{...powerState(8,1),suspendSerial:9},holdStatusReceipt:true,status:{...statusSession(steamSession()),packOwned:true,configuredMatchesBackend:false}});
 await scoped.mode('桌面Steam运行中');
 await scoped.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatusReceipt==='function'&&window.__powerVisibility().powerGeneration===8);
 await scoped.page.evaluate(()=>{
  const p=window.__sessionProbe;
  p.deliver({event:'power.resumed',data:{generation:8,readbackScope:{suspendSerial:10,notificationSequence:9}}});
 });
 await scoped.release('StatusReceipt');await scoped.advance();
 assert.equal(await scoped.page.evaluate(()=>window.__powerVisibility().visible),true,'keep the cached presentation visible');
 assert.deepEqual(await scoped.reads(),['Status'],'a late Status cannot repair while the new public scope still needs readback');
 assert.equal(await scoped.page.evaluate(()=>window.__draft.getState().snapshot),null,'a late Status cannot fill the draft during public readback');
 await scoped.page.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));
 await scoped.advance();
 assert.deepEqual(await scoped.reads(),['Status'],'backend notices cannot bypass the public scope fence');
 // Recover via the original power.lifecycle retry; omit the final broadcast.
 await scoped.page.evaluate(()=>{
  const p=window.__sessionProbe;
  p.power={...p.power,suspendSerial:10,observedNotificationSequence:9,publicationRevision:2};
 });
 await scoped.advance();await scoped.advance();await scoped.advance();
 assert.deepEqual(await scoped.reads(),['Status','Status','Reconcile'],'the matching readback resumes one fresh Status and repair');
 await scoped.snapshot();
 cases.push('New public scope retires old theme work before Ready catch-up and resumes from readback alone');
 await scoped.close();
 if(!process.env.YMCC_APPEARANCE_TEST_POWER_ONLY){
 const sections=await sessionPage();
 await sections.snapshot();await sections.mode('桌面Steam运行中');await sections.advance();
 const tab=id=>sections.page.locator('[data-settings-section="'+id+'"]');
 const card=sections.page.locator('.steam-appearance-card');
 const backendNotice=()=>sections.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail:{phase:'ready',reason:'appearance-applied-resident'}})));
 const countSectionReads=async()=>(await sections.reads()).filter(action=>action==='Status').length;
 const beforeTabs=await countSectionReads();
 // Queue one read just before hiding the panel; then send independent events through both other tabs.
 await sections.page.evaluate(()=>{
  window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged'));
  document.querySelector('[data-settings-section="wallpaper"]').click();
 });
 assert.equal(await card.isVisible(),false);
 await sections.advance();
 await backendNotice();await sections.advance();
 await tab('integration').click();
 await sections.setStatus(steamSession('101','bigpicture'));
 await sections.publish(steamSession('101','bigpicture'));await sections.advance();await sections.mode('大屏Steam运行中');
 await sections.page.evaluate(()=>{window.dispatchEvent(new Event('ipc:window.hidden'));window.dispatchEvent(new Event('ipc:window.shown'));});
 await sections.advance();
 sectionProbes.hiddenStatus=(await countSectionReads())-beforeTabs;
 console.log(JSON.stringify({sectionProbes}));
 assert.equal(sectionProbes.hiddenStatus,0,'hidden appearance section must cancel queued reads and ignore CSS, Steam and window events');
 cases.push('Hidden appearance section reads nothing after queued work, CSS completion, Steam replacement or window show');

 await sections.page.evaluate(()=>{window.__fixture.snapshot.themes.find(row=>row.name==='Round').configured=false;});
 await tab('library').click();
 await sections.advance();await sections.advance();
 assert.equal(await countSectionReads(),beforeTabs+1);
 assert.equal(await card.locator('.appearance-count').textContent(),'6 / 7');
 cases.push('Returning to the appearance section reads once and shows current selections');

 await sections.page.locator('.appearance-edit').click();
 assert.equal(await sections.page.locator('.appearance-editor').isVisible(),true);
 await sections.page.evaluate(()=>document.querySelector('[data-settings-section="wallpaper"]').click());
 assert.equal(await sections.page.locator('.appearance-editor').count(),0,'teleported editor must not outlive the selected section');
 cases.push('Switching away retires a teleported editor without unmounting the draft');
 await tab('library').click();await sections.advance();
 const beforeSaveRead=await countSectionReads();
 await card.locator('.appearance-master').click();
 await sections.page.waitForFunction(()=>window.__draft.getState().pendingCount>0);
 await tab('wallpaper').click();
 await backendNotice();
 await sections.advance();
 await sections.page.waitForFunction(()=>window.__fixture.applyRequests.length===1&&!window.__draft.getState().saving);
 await sections.advance();
 assert.deepEqual(await sections.page.evaluate(()=>window.__fixture.applyRequests[0].states),Object.fromEntries(themeNames.filter(name=>name!=='Round').map(name=>[name,false])));
 assert.equal(await countSectionReads(),beforeSaveRead+1,'the user save needs one fresh preflight Status but must not schedule an extra hidden read');
 cases.push('Leaving the appearance section preserves one pending user write and drops hidden follow-up reads');
 await sections.close();

 const statusFirst=await sessionPage({holdUi:true});
 await statusFirst.snapshot();await statusFirst.release('Ui');await statusFirst.mode('桌面Steam运行中');
 await statusFirst.advance();sessionProbes.alreadyRunning=(await statusFirst.reads()).length;
 console.log(JSON.stringify({sessionProbes}));
 await assertSessionReads(statusFirst,1,'the first known running sample must not reread when the initial Status already observed that PID and birth');
 cases.push('First known running session covered by completed mount Status stays quiet');
 const modeFirst=await sessionPage({holdStatus:true});
 await modeFirst.mode('桌面Steam运行中');await modeFirst.release('Status');await modeFirst.snapshot();await modeFirst.advance();
 await assertSessionReads(modeFirst,1,'the first known sample should join a matching in-flight mount Status');
 cases.push('First known running session arriving during Status waits for its answer without scheduling a helper');
 await modeFirst.close();

 const replacement=await sessionPage({holdStatus:true});
 await replacement.mode('桌面Steam运行中');
 await replacement.publish(steamSession('101','bigpicture'));await replacement.advance();await replacement.mode('大屏Steam运行中');
 await assertSessionReads(replacement,1,'replacement while a Status is in flight must not overlap helpers');
 await replacement.release('Status');await replacement.snapshot();await replacement.setStatus(steamSession('101','bigpicture'));
 await replacement.advance();await replacement.advance();
 await assertSessionReads(replacement,2,'first-session coverage must not swallow a newer session edge');
 assert.equal(await replacement.page.evaluate(()=>window.__draft.getState().snapshot.steamBirth),'101');
 cases.push('Same-PID replacement arriving during the mount Status keeps one trailing read');await replacement.close();

 for(const [order,keepFollowUp] of [['completed',false],['in-flight',false],['backend-completion',true],['multiple-edges',true]]){
  const covered=await sessionPage({policyFromStatus:true});
  await covered.snapshot();await covered.mode('桌面Steam运行中');await covered.advance();
  const next=steamSession('101','bigpicture');
  await covered.setStatus(next);
  await covered.page.evaluate(hold=>{window.__sessionProbe.holdStatus=hold;window.dispatchEvent(new Event('global:refresh'));},order!=='completed');
  const statusBirth=expected=>covered.page.waitForFunction(expected=>window.__draft.getState().snapshot?.steamBirth===expected,expected);
  if(order==='completed')await statusBirth(next.birth);
  else await covered.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatus==='function');
  assert.deepEqual(await covered.page.evaluate(()=>window.__fixture.calls.map(call=>call.args[call.args.indexOf('-SessionBirth')+1])),['100','101']);
  await covered.publish(next);await covered.mode('大屏Steam运行中');
  if(order==='backend-completion')await covered.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail:{phase:'ready',reason:'appearance-applied-resident'}})));
  if(order==='multiple-edges'){
   await covered.publish(steamSession('100'));await covered.advance();await covered.mode('桌面Steam运行中');
   await covered.setStatus(steamSession('100'));
   await covered.page.evaluate(()=>window.__fixture.failRead=true);
  }
  if(order!=='completed')await covered.release('Status');
  if(order==='multiple-edges'){
   await covered.page.waitForFunction(()=>window.__draft.getState().phase==='error');
   await covered.page.evaluate(()=>window.__fixture.failRead=false);
  }
  await covered.advance();await covered.advance();
  sessionProbes[order]=(await covered.reads()).length;
  console.log(JSON.stringify({sessionProbes}));
  await assertSessionReads(covered,keepFollowUp?3:2,order+': only an uncovered Steam edge or independent backend completion gets a follow-up Status');
  await statusBirth(order==='multiple-edges'?'100':'101');
  cases.push(order+': Steam identity coverage preserves independent or multiple changes');await covered.close();
 }

 for(const scenario of ['completed','in-flight','stale-running','backend-completion','restart']){
  const stopped=await sessionPage({policyFromStatus:true});
  await stopped.snapshot();await stopped.mode('桌面Steam运行中');await stopped.advance();
  const isStale=scenario==='stale-running';
  if(!isStale)await stopped.setStatus(absent);
  await stopped.page.evaluate(hold=>{
   const probe=window.__sessionProbe;delete probe.releaseStatus;probe.holdStatus=hold;
   window.dispatchEvent(new Event('global:refresh'));
  },scenario!=='completed');
  const knownStopped=()=>stopped.page.waitForFunction(()=>window.__draft.getState().snapshot?.steamRunning===false);
  if(scenario==='completed')await knownStopped();
  else await stopped.page.waitForFunction(()=>typeof window.__sessionProbe.releaseStatus==='function');
  await stopped.publish(absent);await stopped.mode('未启动');
  if(scenario==='backend-completion')await stopped.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail:{phase:'ready',reason:'appearance-applied-resident'}})));
  const restart=steamSession('199','bigpicture');
   if(scenario==='restart'){
    await stopped.publish(restart);
    await stopped.advance(); // Let the published-state cooldown elapse while Status is held.
    await stopped.mode('大屏Steam运行中');
   }
  if(scenario!=='completed'){
   await stopped.release('Status');
   if(isStale){
    await stopped.page.waitForFunction(()=>!document.querySelector('.appearance-status-line')?.textContent.includes('检测中'));
    await stopped.setStatus(absent);
   }else{
    await knownStopped();
    if(scenario==='restart')await stopped.setStatus(restart);
   }
  }
  await stopped.advance();await stopped.advance();
  const expected=['completed','in-flight'].includes(scenario)?2:3;
  sessionProbes['stop-'+scenario]=(await stopped.reads()).length;
  await assertSessionReads(stopped,expected,scenario+': only an uncovered Steam stop, new lifetime or independent backend notification needs another Status');
  if(scenario==='restart')await stopped.page.waitForFunction(()=>window.__draft.getState().snapshot?.steamBirth==='199');
  else await knownStopped();
  cases.push('Stop '+scenario+': complete session coverage preserves unrelated observations');await stopped.close();
 }

 const terminal=await sessionPage({holdStatus:true});
 await terminal.mode('桌面Steam运行中');
 await terminal.page.evaluate(()=>window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail:{phase:'ready',reason:'appearance-applied-resident'}})));
 await terminal.release('Status');await terminal.snapshot();await terminal.advance();await terminal.advance();
 await assertSessionReads(terminal,2,'a matching first session cannot swallow an independent backend completion');
 cases.push('Backend completion remains independently observable even when the first session matches');await terminal.close();

 for(const [label,options] of [
  ['unavailable',{failStatus:true}],
  ['different PID',{status:statusSession(steamSession('100','desktop',41))}],
  ['same PID with a different birth',{status:statusSession(steamSession('99'))}],
  ['stopped',{status:statusSession(absent)}],
  ['legacy with no identity',{status:{steamRunning:true}}],
 ]){
  const test=await sessionPage({...options,holdUi:true});
  if(options.failStatus)await test.page.waitForFunction(()=>window.__draft.getState().phase==='error');else await test.snapshot();
  await test.page.evaluate(()=>{window.__fixture.failRead=false;});await test.setStatus(steamSession());
  await test.release('Ui');await test.mode('桌面Steam运行中');await test.advance();await test.snapshot();await test.advance();
  await assertSessionReads(test,2,'first session must recheck '+label+' Status');
  cases.push('First running sample retains one follow-up after '+label+' Status');await test.close();
 }
 await statusFirst.publish(unknown);await statusFirst.advance();
 await statusFirst.mode('桌面Steam运行中（状态待复核）');
 await assertSessionReads(statusFirst,1,'unknown is not a stop');
 await statusFirst.publish(steamSession('100','bigpicture'));await statusFirst.advance();
 await statusFirst.mode('大屏Steam运行中');await assertSessionReads(statusFirst,1,'mode-only changes need no appearance Status');
 cases.push('Unknown or same-session mode changes do not reread appearance');
 for(const [label,state,mode] of [
  ['PID replacement',steamSession('101','desktop',43),'桌面Steam运行中'],
  ['same-PID birth replacement',steamSession('102','bigpicture',43),'大屏Steam运行中'],
  ['stop',absent,'未启动'],
  ['start',steamSession('103'),'桌面Steam运行中'],
 ]){
  const before=(await statusFirst.reads()).length;
  await statusFirst.setStatus(state);await statusFirst.publish(state);await statusFirst.advance();await statusFirst.mode(mode);await statusFirst.advance();
  await assertSessionReads(statusFirst,before+1,label+' must trigger exactly one appearance read');
  cases.push('A confirmed Steam '+label+' triggers one appearance read');
 }
 await statusFirst.close();

 const quietPage=await browser.newPage();quietPage.on('pageerror',e=>errors.push(e.message));
 await quietPage.clock.install();await quietPage.goto('http://127.0.0.1:'+server.address().port);await quietPage.waitForFunction(()=>window.__draft.getState().snapshot);
 const quietActions=()=>quietPage.evaluate(()=>window.__fixture.calls.map(c=>c.action));
 assert.deepEqual(await quietActions(),['Status']);
 await quietPage.clock.fastForward(6500);
 assert.deepEqual(await quietActions(),['Status'],'the initial visibility subscription must not queue a second Status');
 cases.push('Quiet mount performs one Status even after the initial visibility sample and one full cooldown');
 await quietPage.evaluate(()=>{window.dispatchEvent(new Event('ipc:window.hidden'));window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged'))});
 await quietPage.clock.fastForward(6000);assert.deepEqual(await quietActions(),['Status']);
 await quietPage.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));
 await quietPage.clock.fastForward(1000);assert.deepEqual(await quietActions(),['Status','Status']);
 await quietPage.clock.fastForward(6500);assert.deepEqual(await quietActions(),['Status','Status']);
 cases.push('Real native-window hide suppresses reads and one show schedules exactly one fresh Status');
 const notice=(phase,reason)=>quietPage.evaluate(detail=>window.dispatchEvent(new CustomEvent('ipc:steamAppearance.backendChanged',{detail})),{phase,reason,pid:42,linked:true,steamRunning:true});
 for(const [start,startReason,end,endReason,ready] of [
  ['applying','temporary-css-use','ready','appearance-applied-resident',true],
  ['verifying','manual-use-read-only-confirmation','waiting-appearance','appearance-unconfirmed',false],
  ['applying','temporary-css-use','canceled','helper-completion-lifecycle-changed',false],
 ]){
  const before=(await quietActions()).length;
  await notice(start,startReason);
  await quietPage.clock.fastForward(2500);
  assert.equal((await quietActions()).length,before,'guard progress must not start a Status helper');
  await quietPage.evaluate(ready=>{
   Object.assign(window.__fixture.snapshot,{backendReady:ready,configuredMatchesBackend:ready});
   const theme=window.__fixture.snapshot.themes.find(row=>row.name==='Round');
   theme.configured=false;theme.backendEnabled=ready?false:null;
  },ready);
  await notice(end,endReason);
  await quietPage.clock.fastForward(1000);
  assert.equal((await quietActions()).length,before+1,'guard result must not inherit an unnecessary Status cooldown');
  assert.equal(await quietPage.evaluate(()=>window.__draft.getState().snapshot?.backendReady),ready);
  assert.equal(await quietPage.locator('.appearance-count').textContent(),'6 / 7');
  await quietPage.clock.fastForward(6500);
  assert.equal((await quietActions()).length,before+1,'guard result must not create another read');
  cases.push(startReason+' alone stays quiet; '+end+' rereads exactly once');
 }
 assert((await quietActions()).every(action=>action==='Status'));
 await quietPage.close();
 const scopePage=await browser.newPage();scopePage.on('pageerror',e=>errors.push(e.message));
 await scopePage.clock.install();await scopePage.goto('http://127.0.0.1:'+server.address().port);await scopePage.waitForFunction(()=>window.__draft.getState().snapshot);
 const scopeActions=()=>scopePage.evaluate(()=>window.__fixture.calls.map(c=>c.action));
 const scopeReads=async()=>(await scopeActions()).filter(action=>action==='Status').length;
 for(let i=0;i<8;i++){await scopePage.evaluate(()=>window.__generalRefresh());await scopePage.clock.fastForward(1000);}
 readProbes.generalRefreshCommands=(await scopeReads())-1;
 const beforeExplicit=await scopeReads();
 await scopePage.evaluate(()=>window.dispatchEvent(new Event('global:refresh')));
 await scopePage.waitForFunction(n=>window.__fixture.calls.filter(c=>c.action==='Status').length===n,beforeExplicit+1);
 cases.push('Dedicated user refresh still reads immediately');
 async function holdSave(value){
  await scopePage.evaluate(value=>{const f=window.__fixture;f.holdApply=true;f.releaseApply=null;window.__draft.set({Round:value});void window.__draft.flush(false);},value);
  await scopePage.waitForFunction(()=>typeof window.__fixture.releaseApply==='function');
 }
 async function emitBackend(){await scopePage.evaluate(()=>window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged')));}
 async function finishSave(value){
  await scopePage.evaluate(()=>{const f=window.__fixture;f.holdApply=false;f.releaseApply();});
  await scopePage.waitForFunction(value=>{const d=window.__draft.getState();return !d.saving&&!d.pendingCount&&d.snapshot?.themes.find(row=>row.name==='Round')?.configured===value;},value);
 }
 await holdSave(false);const beforeHidden=await scopeReads();
 await emitBackend();await scopePage.clock.fastForward(6000);
 await scopePage.evaluate(()=>window.dispatchEvent(new Event('ipc:window.hidden')));
 await finishSave(false);await scopePage.clock.fastForward(6000);
 readProbes.hiddenAfterSaveCommands=(await scopeReads())-beforeHidden;
 const beforeShow=await scopeReads();
 await scopePage.evaluate(()=>window.dispatchEvent(new Event('ipc:window.shown')));await scopePage.clock.fastForward(1000);
 assert.equal(await scopeReads(),beforeShow+1);
 await scopePage.clock.fastForward(5500);assert.equal(await scopeReads(),beforeShow+1);
 cases.push('Deferred UI observation is dropped on hide, with one fresh Status on real show');
 await holdSave(true);const beforeVisible=await scopeReads();
 for(let i=0;i<5;i++)await emitBackend();
 await scopePage.clock.fastForward(6000);
 assert.equal(await scopeReads(),beforeVisible,'a draft in flight must not start any observation helper');
 await finishSave(true);await scopePage.clock.fastForward(5500);
 assert.equal(await scopeReads(),beforeVisible+1);
 await scopePage.clock.fastForward(6000);assert.equal(await scopeReads(),beforeVisible+1);
 cases.push('Visible commit coalesces waiting backend notices and reads once after saving');
 assert((await scopeActions()).every(action=>action==='Status'||action==='Apply'));
 console.log(JSON.stringify({readProbes}));
 assert.deepEqual(readProbes,{generalRefreshCommands:0,hiddenAfterSaveCommands:0});
 cases.push('Unrelated general refresh keys do not start PowerShell theme reads');
 await scopePage.close();
 const page=await browser.newPage();page.on('pageerror',e=>errors.push(e.message));await page.goto('http://127.0.0.1:'+server.address().port);await page.waitForFunction(()=>window.__draft.getState().snapshot);await page.waitForTimeout(400);
 const reads=()=>page.evaluate(()=>window.__fixture.calls.filter(c=>c.action==='Status').length);
 const events=()=>page.evaluate(()=>{for(let i=0;i<1000;i++)window.dispatchEvent(new Event('ipc:steamAppearance.backendChanged'))});
 const initial=await reads();await events();await page.waitForTimeout(2000);assert.equal(await reads(),initial);await page.waitForTimeout(3300);assert.equal(await reads(),initial+1);cases.push('1000 backend events produce one Status read after the 5s gap');
 const quiet=await reads();await page.waitForTimeout(6000);assert.equal(await reads(),quiet);cases.push('no events means no appearance Status poll or recurring timer');
 await page.evaluate(()=>window.__fixture.failRead=true);
 for(let i=0;i<3;i++){await events();await page.waitForTimeout(5400)}
 const failed=await reads();assert.equal(failed,quiet+3);await events();await page.waitForTimeout(5500);assert.equal(await reads(),failed);cases.push('three failures exhaust automatic reads even under continued event storms');
 await page.evaluate(()=>{window.__fixture.failRead=false;window.dispatchEvent(new Event('global:refresh'))});await page.waitForFunction(()=>window.__draft.getState().snapshot);assert.equal(await reads(),failed+1);cases.push('one explicit refresh renews the stopped read budget');
 await events();await page.evaluate(()=>{Object.defineProperty(document,'visibilityState',{configurable:true,get:()=>window.__fixture.uiVisible===false?'hidden':'visible'});window.__fixture.uiVisible=false;document.dispatchEvent(new Event('visibilitychange'))});await page.waitForTimeout(5400);assert.equal(await reads(),failed+1);cases.push('hiding cancels the queued read and backend events cannot search while hidden');
 await page.evaluate(()=>{window.__fixture.uiVisible=true;document.dispatchEvent(new Event('visibilitychange'))});await page.waitForTimeout(700);assert.equal(await reads(),failed+2);cases.push('restoring visibility schedules one bounded read, not a Steam launch or CSS restart');
 }
 }
 }
 }
 }
 assert.equal(errors.length,0);const result={ok:true,passed:cases.length,cases,readProbes,sessionProbes,sectionProbes,powerProbes,retirementProbes,repairOwnerProbes,supersededReadProbes,manualPriorityProbes,pageErrors:errors,realSteamOperations:0,realCssOperations:0};fs.writeFileSync(path.join(out,'result.json'),JSON.stringify(result,null,2));console.log(JSON.stringify(result,null,2));
}finally{await browser?.close();await new Promise(r=>server.close(r))}
`;
const runner=path.join(out,'read-budget-runner.mjs');fs.writeFileSync(runner,setup+tail);await import(pathToFileURL(runner).href);
