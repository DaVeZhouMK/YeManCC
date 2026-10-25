import assert from 'node:assert/strict';
const equal=(actual,expected,code)=>assert.equal(actual,expected,code);
export async function installMemory(page,initialInert=false,noApp=false) {
  await page.setContent('<!doctype html><html><body>'+(!noApp?'<main id="app"><button id="outside">outside</button></main>':'<button id="outside">outside</button>')+'</body></html>');
  await page.evaluate(({initialInert})=>{
    const native=globalThis,app=document.getElementById('app');if(app)app.inert=initialInert;
    const listeners=new Set(),allListeners=[],calls={confirm:[],reset:0,reload:0,outside:0,add:0,remove:0};
    const deferred=()=>{let resolve,reject;const promise=new Promise((a,b)=>{resolve=a;reject=b;});return{promise,resolve,reject};};
    const decisions=[],resets=[];
    document.getElementById('outside').onclick=()=>calls.outside++;
    const memory={calls,listeners,allListeners,decisions,resets,
      confirm(title,message){calls.confirm.push({title,message});const d=deferred();decisions.push(d);return d.promise;},
      reset(){calls.reset++;const d=deferred();resets.push(d);return d.promise;},
      snapshot(){const node=document.getElementById('yemancc-route-fallback');return{
        nodes:document.querySelectorAll('#yemancc-route-fallback').length,nodePresent:Boolean(node),
        inert:document.getElementById('app')?.inert??null,subscriptions:listeners.size,
        active:document.activeElement?.getAttribute('data-gp'),busy:node?.dataset.recoveryBusy,
        startupFailure:node?.dataset.startupFailure,text:node?.querySelector('p')?.textContent,
        status:node?.querySelector('[role=status]')?.textContent,detail:node?.querySelector('pre')?.textContent,
        disabled:node?[...node.querySelectorAll('button')].map(button=>button.disabled):[],
        calls:{...calls,confirm:[...calls.confirm]},url:location.href};}};
    globalThis.__routeFallbackMemory=memory;
    globalThis.__routeFallbackOwnedWindow=new Proxy(native,{get(target,key){
      if(key==='location')return{reload(){calls.reload++;}};
      if(key==='addEventListener')return(type,listener,capture)=>{
        if(type==='ipc:gamepad.ui-input'){calls.add++;listeners.add(listener);if(!allListeners.includes(listener))allListeners.push(listener);}
        target.addEventListener(type,listener,capture);
      };
      if(key==='removeEventListener')return(type,listener,capture)=>{
        if(type==='ipc:gamepad.ui-input'){calls.remove++;listeners.delete(listener);}
        target.removeEventListener(type,listener,capture);
      };
      const value=Reflect.get(target,key,target);return typeof value==='function'?value.bind(target):value;
    }});
  },{initialInert});
}
const snap=page=>page.evaluate(()=>globalThis.__routeFallbackMemory.snapshot());
const show=(page,...args)=>page.evaluate(args=>globalThis.RouteFallbackProduct.showRouteFallback(...args),args);
const clear=page=>page.evaluate(()=>globalThis.RouteFallbackProduct.clearRouteFallback());
const click=(page,name)=>page.evaluate(name=>document.querySelector('[data-gp="'+name+'"]').click(),name);
const semantic=(page,action)=>page.evaluate(action=>window.dispatchEvent(new CustomEvent('ipc:gamepad.ui-input',{detail:{action}})),action);
async function resolve(page,queue,value,reject=false){await page.evaluate(async({queue,value,reject})=>{
  const d=globalThis.__routeFallbackMemory[queue].shift();if(!d)throw Error('MISSING_CONTROLLED_PROMISE:'+queue);
  if(reject)d.reject(new Error(value));else d.resolve(value);
  for(let i=0;i<6;i++)await Promise.resolve();
},{queue,value,reject});}
const cases=[
 ['show-repeat-unique-dom-subscription',async page=>{
  await show(page,'first','/a',false,'x'.repeat(1900));const first=await snap(page);
  equal(first.nodes,1,'SINGLE_FALLBACK_NODE');equal(first.subscriptions,1,'ONE_SUBSCRIPTION');equal(first.inert,true,'APP_INERT');
  equal(first.detail.length,1800,'DETAIL_BOUNDED');equal(first.active,'startup-retry','RETRY_INITIAL_FOCUS');
  await show(page,'second','/b');const s=await snap(page);equal(s.nodes,1,'REPEAT_SINGLE_NODE');equal(s.subscriptions,1,'REPEAT_SINGLE_SUBSCRIPTION');
  assert.match(s.text,/second（\/b）/);equal(s.calls.reset,0,'SHOW_NEVER_RESETS');return{s,first};
 }],
 ['semantic-valid-focused-reset-only',async page=>{
  await show(page,'message');await page.evaluate(()=>document.querySelector('[data-gp=startup-reset-settings]').focus());
  await semantic(page,'confirm');let s=await snap(page);equal(s.calls.confirm.length,1,'FOCUSED_RECOVER_ONLY');equal(s.calls.reload,0,'FOCUSED_RECOVER_NOT_RELOAD');
  await resolve(page,'decisions',false);s=await snap(page);equal(s.calls.reset,0,'CANCEL_NO_RESET');return s;
 }],
 ['semantic-invalid-focus-never-clicks-outside',async page=>{
  await show(page,'message');await page.evaluate(()=>{document.getElementById('app').inert=false;document.getElementById('outside').focus();});
  await semantic(page,'confirm');const s=await snap(page);equal(s.calls.outside,0,'OUTSIDE_NEVER_CLICKED');equal(s.calls.reload,1,'INVALID_FOCUS_DEFAULTS_TO_RETRY');equal(s.calls.confirm.length,0,'OUTSIDE_NOT_RECOVERY');return s;
 }],
 ['semantic-disabled-focus-and-other-actions',async page=>{
  await show(page,'message');await semantic(page,'cancel');equal((await snap(page)).calls.reload,0,'OTHER_ACTION_NO_CLICK');
  await page.evaluate(()=>document.querySelector('[data-gp=startup-reset-settings]').focus());await semantic(page,'confirm');
  await semantic(page,'confirm');const s=await snap(page);equal(s.calls.confirm.length,1,'DISABLED_CONFIRM_NO_REENTRY');equal(s.calls.reset,0,'NO_PRECONFIRM_RESET');
  await resolve(page,'decisions',false);return s;
 }],
 ['clear-returns-listener-inert-and-late-semantic',async page=>{
  await show(page,'message');await clear(page);let s=await snap(page);equal(s.nodes,0,'CLEAR_DOM_MUST_BE_ZERO');equal(s.subscriptions,0,'CLEAR_LISTENER_MUST_BE_ZERO');equal(s.inert,false,'CLEAR_INERT_RETURN');
  await semantic(page,'confirm');await page.evaluate(()=>{for(const listener of globalThis.__routeFallbackMemory.allListeners)listener(new CustomEvent('ipc:gamepad.ui-input',{detail:{action:'confirm'}}));});
  s=await snap(page);equal(s.calls.reload,0,'LATE_SEMANTIC_NO_RELOAD');equal(s.calls.confirm.length,0,'LATE_SEMANTIC_NO_DIALOG');await clear(page);return s;
 }],
 ['clear-restores-previously-inert-and-reopen',async page=>{
  await show(page,'first');await clear(page);equal((await snap(page)).inert,true,'PREEXISTING_INERT_RESTORED');
  await show(page,'second');equal((await snap(page)).subscriptions,1,'REOPEN_ONE_SUBSCRIPTION');await clear(page);const s=await snap(page);equal(s.inert,true,'REOPEN_RESTORES_PRIOR_INERT');equal(s.subscriptions,0,'REOPEN_CLEANUP');return s;
 },{initialInert:true}],
 ['fallback-without-app-clears-owned-dom',async page=>{
  await show(page,'no app');await clear(page);const s=await snap(page);equal(s.nodes,0,'NO_APP_CLEAR');equal(s.subscriptions,0,'NO_APP_UNSUBSCRIBE');return s;
 },{noApp:true}],
 ['startup-failure-legitimately-retains-owner',async page=>{
  await show(page,'startup','/boot',true);await clear(page);let s=await snap(page);equal(s.nodes,1,'STARTUP_FAILURE_RETAINS_MODAL');equal(s.inert,true,'STARTUP_FAILURE_RETAINS_INERT');equal(s.subscriptions,1,'STARTUP_FAILURE_RETAINS_SUBSCRIPTION');
  await show(page,'normal update');await clear(page);s=await snap(page);equal(s.startupFailure,'true','STARTUP_FAILURE_STICKY');equal(s.calls.reset,0,'STARTUP_NEVER_AUTO_RESETS');return s;
 }],
 ['busy-clear-and-show-retain-owner',async page=>{
  await show(page,'busy original');await click(page,'startup-reset-settings');await page.evaluate(()=>{globalThis.__originalFallback=document.getElementById('yemancc-route-fallback');});
  await show(page,'replacement not admitted');await clear(page);const s=await snap(page);
  equal(s.nodes,1,'BUSY_CLEAR_MUST_RETAIN_MODAL');equal(s.inert,true,'BUSY_OWNS_INERT');equal(s.busy,'true','CONFIRM_INFLIGHT');
  assert.match(s.text,/busy original/);equal(await page.evaluate(()=>globalThis.__originalFallback===document.getElementById('yemancc-route-fallback')),true,'BUSY_NO_NODE_REPLACE');
  await resolve(page,'decisions',false);await clear(page);return s;
 }],
 ['user-cancel-never-resets-and-reenables',async page=>{
  await show(page,'message');await click(page,'startup-reset-settings');await resolve(page,'decisions',false);const s=await snap(page);
  equal(s.calls.reset,0,'USER_CANCEL_ZERO_RESET');equal(s.calls.reload,0,'USER_CANCEL_ZERO_RELOAD');equal(s.busy,'false','CANCEL_UNLOCK');assert.deepEqual(s.disabled,[false,false]);assert.match(s.status,/已取消/);await clear(page);return s;
 }],
 ['reset-rejection-permits-explicit-retry',async page=>{
  await show(page,'message');await click(page,'startup-reset-settings');await resolve(page,'decisions',true);await resolve(page,'resets','backup-denied',true);
  let s=await snap(page);assert.match(s.status,/恢复失败.*backup-denied/);equal(s.busy,'false','RESET_ERROR_UNLOCK');assert.deepEqual(s.disabled,[false,false]);equal(s.calls.reload,0,'RESET_ERROR_NO_RELOAD');
  await click(page,'startup-reset-settings');await resolve(page,'decisions',true);await resolve(page,'resets',{backups:['memory-backup']});s=await snap(page);
  equal(s.calls.reset,2,'EXPLICIT_SECOND_RESET');equal(s.calls.reload,1,'SUCCESS_ONE_MEMORY_RELOAD');assert.match(s.status,/memory-backup/);await clear(page);return s;
 }],
 ['dialog-rejection-permits-cancel-retry',async page=>{
  await show(page,'message');await click(page,'startup-reset-settings');await resolve(page,'decisions','dialog-unavailable',true);
  let s=await snap(page);assert.match(s.status,/dialog-unavailable/);equal(s.calls.reset,0,'DIALOG_FAILURE_NO_RESET');equal(s.busy,'false','DIALOG_FAILURE_UNLOCK');
  await click(page,'startup-reset-settings');await resolve(page,'decisions',false);s=await snap(page);equal(s.calls.confirm.length,2,'DIALOG_RETRY_ADMITTED');await clear(page);return s;
 }],
 ['reset-inflight-duplicate-confirm-and-late-completion',async page=>{
  await show(page,'message');await click(page,'startup-reset-settings');await resolve(page,'decisions',true);
  await semantic(page,'confirm');await show(page,'new pending');await clear(page);let s=await snap(page);
  equal(s.calls.confirm.length,1,'PENDING_RESET_NO_NEW_CONFIRM');equal(s.calls.reset,1,'PENDING_RESET_NO_DUPLICATE');equal(s.nodes,1,'PENDING_RESET_RETAINS_NODE');equal(s.busy,'true','PENDING_RESET_BUSY');
  await resolve(page,'resets',{backups:[]});s=await snap(page);equal(s.calls.reload,1,'ONE_LATE_MEMORY_RELOAD');equal(s.busy,'false','LATE_COMPLETION_UNLOCK');await clear(page);await semantic(page,'confirm');equal((await snap(page)).calls.reload,1,'POST_CLEAR_SEMANTIC_NO_EXTRA_RELOAD');return s;
 }],
 ['cleared-detached-retry-callback-must-not-reload',async page=>{
  await show(page,'message');await page.evaluate(()=>{globalThis.__staleRetry=document.querySelector('[data-gp=startup-retry]');});await clear(page);
  await page.evaluate(()=>globalThis.__staleRetry.click());const s=await snap(page);equal(s.nodes,0,'DETACHED_RETRY_OWNER_GONE');equal(s.calls.reload,0,'CLEARED_DETACHED_RETRY_MUST_NOT_RELOAD');return s;
 }],
 ['cleared-detached-recovery-callback-must-not-reset',async page=>{
  await show(page,'message');await page.evaluate(()=>{globalThis.__staleRecover=document.querySelector('[data-gp=startup-reset-settings]');});await clear(page);
  await page.evaluate(()=>globalThis.__staleRecover.click());const pending=await snap(page);
  if(pending.calls.confirm.length){await resolve(page,'decisions',true);await resolve(page,'resets',{backups:['memory-only']});}
  const s=await snap(page);equal(s.calls.reset,0,'CLEARED_DETACHED_RECOVERY_MUST_NOT_RESET');equal(s.calls.reload,0,'CLEARED_DETACHED_RECOVERY_MUST_NOT_RELOAD');return s;
 }],
 ['superseded-owner-rejects-connected-old-buttons',async page=>{
  await show(page,'first');await page.evaluate(()=>{
    globalThis.__oldRetry=document.querySelector('[data-gp=startup-retry]');
    globalThis.__oldRecover=document.querySelector('[data-gp=startup-reset-settings]');
    globalThis.__sharedRoot=document.getElementById('yemancc-route-fallback');
  });
  await show(page,'second');await page.evaluate(()=>{
    document.getElementById('yemancc-route-fallback').append(globalThis.__oldRetry,globalThis.__oldRecover);
    globalThis.__oldRetry.click();globalThis.__oldRecover.click();
  });
  const s=await snap(page);equal(s.calls.reload,0,'SUPERSEDED_CONNECTED_RETRY_NO_RELOAD');equal(s.calls.confirm.length,0,'SUPERSEDED_CONNECTED_RECOVER_NO_DIALOG');
  equal(await page.evaluate(()=>globalThis.__sharedRoot===document.getElementById('yemancc-route-fallback')),true,'SAME_NODE_NEW_OWNER');
  assert.match(s.text,/second/);await clear(page);return s;
 }],
 ['retired-confirmation-cannot-start-reset',async page=>{
  await show(page,'old');await click(page,'startup-reset-settings');
  await page.evaluate(()=>{globalThis.__retiredRoot=document.getElementById('yemancc-route-fallback');globalThis.__retiredRoot.remove();});
  // The product clear guard is unchanged. Here the DOM owner has already been
  // removed externally inside this owned page; no busy live node is force-cleared.
  await clear(page);equal((await snap(page)).inert,false,'REMOVED_OWNER_CLEAR_RETURNS_INERT');await show(page,'replacement');
  await resolve(page,'decisions',true);const s=await snap(page);
  equal(s.calls.reset,0,'RETIRED_CONFIRMATION_MUST_NOT_RESET');equal(s.calls.reload,0,'RETIRED_CONFIRMATION_MUST_NOT_RELOAD');
  equal(s.status,'','REPLACEMENT_STATUS_UNCHANGED');equal(s.subscriptions,1,'REPLACEMENT_ONE_SUBSCRIPTION');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[data-gp=startup-reset-settings]').disabled),true,'RETIRED_FINALLY_NO_REENABLE');await clear(page);return s;
 }],
 ['retired-reset-cannot-publish-or-reload',async page=>{
  await show(page,'old');await click(page,'startup-reset-settings');await resolve(page,'decisions',true);
  await page.evaluate(()=>{globalThis.__retiredRoot=document.getElementById('yemancc-route-fallback');globalThis.__retiredRoot.remove();});
  await clear(page);await show(page,'replacement');await resolve(page,'resets',{backups:['old-memory-backup']});const s=await snap(page);
  equal(s.calls.reset,1,'ALREADY_STARTED_RESET_NOT_REPLAYED');equal(s.calls.reload,0,'RETIRED_RESET_MUST_NOT_RELOAD');equal(s.status,'','RETIRED_RESET_NO_CURRENT_UI_WRITE');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[data-gp=startup-reset-settings]').disabled),true,'RETIRED_RESET_FINALLY_NO_REENABLE');await clear(page);return s;
 }],
 ['retired-confirmation-error-cannot-reenable-or-write',async page=>{
  await show(page,'old');await click(page,'startup-reset-settings');
  await page.evaluate(()=>{globalThis.__retiredRoot=document.getElementById('yemancc-route-fallback');globalThis.__retiredRoot.remove();});
  await clear(page);await show(page,'replacement');await resolve(page,'decisions','late-dialog-error',true);const s=await snap(page);
  equal(s.calls.reset,0,'RETIRED_DIALOG_ERROR_NO_RESET');equal(s.status,'','RETIRED_DIALOG_ERROR_NO_CURRENT_UI');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[role=status]').textContent),'','RETIRED_CATCH_NO_STALE_UI_WRITE');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[data-gp=startup-reset-settings]').disabled),true,'RETIRED_ERROR_FINALLY_NO_REENABLE');await clear(page);return s;
 }],
 ['retired-reset-error-cannot-reenable-or-write',async page=>{
  await show(page,'old');await click(page,'startup-reset-settings');await resolve(page,'decisions',true);
  await page.evaluate(()=>{globalThis.__retiredRoot=document.getElementById('yemancc-route-fallback');globalThis.__retiredStatus=globalThis.__retiredRoot.querySelector('[role=status]').textContent;globalThis.__retiredRoot.remove();});
  await clear(page);await show(page,'replacement');await resolve(page,'resets','late-reset-error',true);const s=await snap(page);
  equal(s.calls.reset,1,'RETIRED_RESET_ERROR_NO_REPLAY');equal(s.calls.reload,0,'RETIRED_RESET_ERROR_NO_RELOAD');equal(s.status,'','RETIRED_RESET_ERROR_NO_CURRENT_UI');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[role=status]').textContent===globalThis.__retiredStatus),true,'RETIRED_RESET_CATCH_NO_OLD_UI_WRITE');
  equal(await page.evaluate(()=>globalThis.__retiredRoot.querySelector('[data-gp=startup-reset-settings]').disabled),true,'RETIRED_RESET_ERROR_NO_REENABLE');await clear(page);return s;
 }],
];
export const CASE_IDS=cases.map(([id])=>id);
export async function runCases(browser,bundle,{only,phase='normal'}={}) {
  const results=[];
  for(const [id,execute,options={}] of cases){if(only&&!only.includes(id))continue;
    const context=await browser.newContext({viewport:{width:1280,height:800},serviceWorkers:'block'});
    await context.route('**/*',route=>route.abort());const page=await context.newPage();const errors=[];page.on('pageerror',error=>errors.push(error.message));
    const started=Date.now();let row;
    try{await installMemory(page,options.initialInert,options.noApp);await page.addScriptTag({path:bundle});
      const observations=await execute(page);assert.deepEqual(errors,[],'NO_PAGE_ERRORS');
      equal(await page.evaluate(()=>location.href),'about:blank','BROWSER_LOCATION_UNCHANGED');
      row={id,status:'PASS',observations};
    }catch(error){row={id,status:'FAIL',assertionName:error.name,error:error.message,stack:error.stack,
      observations:await snap(page).catch(()=>null),pageErrors:errors};}
    finally{await context.close();}
    results.push({...row,phase,durationMs:Date.now()-started});
    console.log('CASE '+row.status+' '+phase+' '+id+(row.error?' '+row.error.replaceAll('\n',' '):''));
  }
  return results;
}
