import assert from 'node:assert/strict';import fs from 'node:fs';import path from 'node:path';import vm from 'node:vm';
export async function frontendBootTests(root,out,onlyCase=null,excludeCase=null){
 const offCase='OFF cancels bounded recovery before import',ownerCase='retired socket or page cannot complete first load',windowCase='Win5 reopen ended before the existing first-load probe',queueCase='official queued load remains official owner',uncertainCase='uncertain import is never replayed';assert(onlyCase===null||[offCase,ownerCase,windowCase,queueCase,uncertainCase].includes(onlyCase),'FRONTEND_BOOT_CASE_UNKNOWN');assert(excludeCase===null||excludeCase===offCase,'FRONTEND_BOOT_EXCLUDE_UNKNOWN');assert(onlyCase===null||excludeCase===null,'FRONTEND_BOOT_SELECTION_CONFLICT');
 const source=fs.readFileSync(path.join(root,'native/decky_sidebar_frontend_update.js'),'utf8');const results=[],omitted=[],readCancellationObservations=[],readCancellationFacts=[];const flush=async()=>{for(let i=0;i<30;i++)await Promise.resolve();};
 function fixture(options={},script=source){let now=0,id=0;const timers=new Map,listeners=new Map,calls=[];let reads=0,enabled=options.enabled!==false;
 const value={recoverFrontend:options.authorized!==false,loaderVersion:options.expectedVersion||'v3.3.0',runId:'a'.repeat(32),frontendBuildId:'b'.repeat(64)};
 const host={__YMCC_DECKY_MIRROR__:value,DeckyBackend:{ws:{readyState:options.offline?3:1}},setTimeout(fn,delay){const n=++id;timers.set(n,{fn,due:now+delay});return n;},clearTimeout(n){timers.delete(n);},addEventListener(k,fn){const s=listeners.get(k)||new Set;s.add(fn);listeners.set(k,s);},removeEventListener(k,fn){listeners.get(k)?.delete(fn);}};
 const ready=(build=value.frontendBuildId)=>{host.__YMCC_DECKY_FRONTEND__={active:true,buildId:build};for(const fn of [...(listeners.get('ymcc-decky-frontend-ready')||[])])fn();};
 const loader={plugins:[],pluginReloadQueue:[],reloadLock:false,deckyState:{publicState:()=>({versionInfo:{current:options.version||'v3.3.0'}})},getPluginsFromBackend(){reads++;if(options.readError)throw Error('backend read failed');if(options.readPending)return new Promise(()=>{});if(options.race)loader.plugins.push({name:'ymcc-sidebar'});return Promise.resolve(options.absent?[]:[{name:'ymcc-sidebar',version:'0.0.1',load_type:options.loadType??1,disabled:!enabled}]);},importPlugin(...args){calls.push(args);if(options.importError)throw Error('uncertain import');if(options.queued)return;loader.plugins=loader.plugins.filter(p=>p.name!==args[0]);if(options.wrongBuild)ready('c'.repeat(64));else ready();loader.plugins.push({name:args[0]});}};
 if(!options.noLoader)host.DeckyPluginLoader=loader;
 let executed=script;if(options.observeProbes){const call=/void probe\((owner)?\);/g;assert.equal([...script.matchAll(call)].length,2);executed=script.replace(call,'host.__fixtureProbes.push(probe($1));');assert.equal(executed.replace(/host\.__fixtureProbes\.push\(probe\((owner)?\)\);/g,'void probe($1);'),script);}host.__fixtureProbes=[];const fn=vm.runInNewContext(executed,{window:host,Error});const inject=(next=value)=>{host.__YMCC_DECKY_MIRROR__=next;fn(next);};
 const advance=async(ms)=>{const end=now+ms;for(;;){const row=[...timers].filter(([,x])=>x.due<=end).sort((a,b)=>a[1].due-b[1].due)[0];if(!row)break;now=row[1].due;timers.delete(row[0]);row[1].fn();await flush();}now=end;await flush();};
 const fireNext=()=>{const row=[...timers].sort((a,b)=>a[1].due-b[1].due)[0];assert(row);now=row[1].due;timers.delete(row[0]);row[1].fn();};const cancel=()=>{for(const fn of [...(listeners.get('ymcc-decky-health-stop')||[])])fn();};
 return{host,loader,value,calls,ready,inject,advance,fireNext,cancel,timers,listeners,reads:()=>reads,state:()=>host.__YMCC_DECKY_FRONTEND_BOOT__,enabled:x=>enabled=x};}
 const ownerBoundaryFacts=[],officialLockFacts=[],revocationFacts=[],retainedUpdateFacts=[];let win5Window=null;
 const report=(passed,failure=null)=>({passed,cases:results.length+(failure?1:0),negativeDetected:!onlyCase,results:failure?[...results,failure.name]:results,onlyCase,excludeCase,omitted,readCancellationObservations,readCancellationFacts,ownerBoundaryFacts,officialLockFacts,revocationFacts,retainedUpdateFacts,win5Window,probeObservationAdapter:'two original void probe caller sites retain returned promises only; exact inverse checked; original probe body unchanged',actualSteam:false,productionSource:true,...(failure?{failure}:{} )});
 const write=value=>{if(out){fs.mkdirSync(out,{recursive:true});fs.writeFileSync(path.join(out,'frontend-boot.json'),JSON.stringify(value,null,2),{flag:'wx'});}};
 const run=async(name,fn)=>{if(onlyCase!==null&&name!==onlyCase||name===excludeCase){omitted.push(name);return;}try{await fn();results.push(name);}catch(error){write(report(false,{name,error:String(error.stack||error)}));throw error;}};
 await run('canceled first load cannot reselect its panel from a late official completion',async()=>{
  const f=fixture();let complete,sets=0;
  f.loader.deckyState.publicState=()=>({versionInfo:{current:f.value.loaderVersion},activePlugin:{name:'ymcc-sidebar'}});
  f.loader.deckyState.setActivePlugin=()=>{sets++;};
  f.loader.importPlugin=(...args)=>{f.calls.push(args);f.loader.plugins.push({name:'ymcc-sidebar'});return new Promise(resolve=>{complete=()=>{f.ready();resolve();};});};
  f.inject();await f.advance(12000);assert.equal(f.calls.length,1);assert.equal(f.state().phase,'waiting-receipt');
  for(const fn of [...f.listeners.get('ymcc-decky-health-stop')])fn();await flush();assert.equal(f.state().phase,'canceled');
  complete();await flush();f.inject();await flush();console.log(JSON.stringify({firstLoadStopBoundary:true,reselects:sets,phase:f.state().phase,timers:f.timers.size}));
  assert.equal(sets,0,'canceled first-load completion cannot reselect the panel');assert.equal(f.state().phase,'canceled');assert.equal(f.timers.size,0);
  assert.equal(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0);assert.equal(f.listeners.get('ymcc-decky-health-stop')?.size||0,0);
 });
 await run('official UI-mode callback never fires; production adapter restores enabled plugin without mode switch',async()=>{const f=fixture();let officialFinished=false;const registration={unregister(){}};const SteamClient={UI:{RegisterForUIModeChanged(){return registration;}}};const waiting=vm.runInNewContext('(async()=>{await new Promise(r=>SteamClient.UI.RegisterForUIModeChanged(r));return true;})()',{SteamClient});waiting.then(()=>officialFinished=true);f.inject();await f.advance(11999);assert.equal(f.calls.length,0);await f.advance(1);assert.equal(officialFinished,false);assert.equal(f.reads(),1);assert.deepEqual(f.calls,[['ymcc-sidebar','0.0.1',1,true,10000]]);assert.equal(f.state().phase,'current');assert.equal(f.timers.size,0);});
 await run('normal official loading during grace avoids duplicate import',async()=>{const f=fixture();f.inject();f.loader.plugins.push({name:'ymcc-sidebar'});f.ready();await f.advance(60000);assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);});
 for(const [name,options] of Object.entries({'no native grant':{authorized:false},'disabled official plugin':{enabled:false},'absent official plugin':{absent:true},'wrong official version':{version:'v3.2.10'},'backend socket closed':{offline:true},'unsupported future load contract':{loadType:6},'bad expected version':{expectedVersion:'x'},'unresponsive backend read':{readPending:true},'failed backend read':{readError:true},'official loader absent':{noLoader:true}}))await run(name,async()=>{const f=fixture(options);f.inject();await f.advance(60000);assert.equal(f.calls.length,0,name);assert.equal(f.timers.size,0,name+' no resident polling');assert(f.reads()<=3);});
 await run('plugin appears during backend read; no import replay',async()=>{const f=fixture({race:true});f.inject();await f.advance(60000);assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);});
 await run(queueCase,async()=>{
  const official=fixture();official.inject();official.loader.pluginReloadQueue.push({name:'ymcc-sidebar'});
  await official.advance(60000);assert.equal(official.calls.length,0);assert.equal(official.timers.size,0);
  for(const edge of ['released-before-retry','locked-after-list','queued-before-retry','ready-before-retry','off-before-retry','pagehide-before-retry','lock-stays-busy']){
   const f=fixture();let complete;
   if(edge==='locked-after-list'){
    const get=f.loader.getPluginsFromBackend.bind(f.loader);
    f.loader.getPluginsFromBackend=()=>{const rows=get();return complete?rows:new Promise(resolve=>{complete=()=>resolve(rows);});};
   }else f.loader.reloadLock=true;
   f.inject();await f.advance(12000);
   if(edge==='locked-after-list'){assert.equal(f.reads(),1);f.loader.reloadLock=true;complete();await f.advance(0);}
   const first={probes:f.state().probes,reads:f.reads(),imports:f.calls.length,timers:f.timers.size};
   assert.equal(first.imports,0,edge+' may not dispatch while the lock is held');
   assert.equal(first.reads,edge==='locked-after-list'?1:0);
   if(edge!=='lock-stays-busy')f.loader.reloadLock=false;
   if(edge==='queued-before-retry')f.loader.pluginReloadQueue.push({name:'ymcc-sidebar'});
   if(edge==='ready-before-retry'){f.loader.plugins.push({name:'ymcc-sidebar'});f.ready();}
   if(edge==='off-before-retry')f.cancel();
   if(edge==='pagehide-before-retry')for(const fn of [...(f.listeners.get('pagehide')||[])])fn();
   await f.advance(3999);assert.equal(f.calls.length,0,edge+' waits for the original retry slot');
   await f.advance(44001);
   const row={edge,first,reads:f.reads(),imports:f.calls.length,probes:f.state().probes,phase:f.state().phase,timers:f.timers.size};officialLockFacts.push(row);
   const recovered=edge==='released-before-retry'||edge==='locked-after-list';
   assert.equal(row.imports,recovered?1:0,edge+' must resume only after the official lock clears');
   assert.equal(row.reads,edge==='locked-after-list'?2:recovered?1:0);
   assert.equal(row.probes,edge==='lock-stays-busy'?3:recovered||edge==='queued-before-retry'?2:1);
   assert.equal(row.phase,recovered||edge==='ready-before-retry'?'current':edge==='queued-before-retry'?'official-load-in-progress':edge==='lock-stays-busy'?'failed':'canceled');
   assert.equal(row.timers,0);
   for(const name of ['pagehide','ymcc-decky-health-stop','ymcc-decky-frontend-ready'])assert.equal(f.listeners.get(name)?.size||0,0);
  }
 });
 await run(windowCase,async()=>{
  // 2026-11-18 Win5 11:18:48.136 inject, 11:18:53.050 socket/hooks verified,
  // 11:18:57.677 reopen selected. Replay observation offsets, not a new threshold.
  const f=fixture({offline:true});f.inject();await f.advance(4914);f.host.DeckyBackend.ws.readyState=1;
  const records=[];const prefix='[YMCC:DeckyHealth] ';
  f.host.console={info:line=>{assert(line.startsWith(prefix));records.push(JSON.parse(line.slice(prefix.length)));}};
  await f.advance(4627);
  win5Window={observedSocketAndHooksMs:4914,reopenSelectedMs:9541,probesAtReopen:f.state().probes,readsAtReopen:f.reads(),importsAtReopen:f.calls.length};
  assert.deepEqual([win5Window.probesAtReopen,win5Window.readsAtReopen,win5Window.importsAtReopen],[0,0,0]);
  // Counterfactual: the original probe can run if the same document stays live.
  await f.advance(2459);assert.equal(f.reads(),1);assert.equal(f.calls.length,1);assert.equal(f.state().phase,'current');
  const expected={schema:1,stage:'frontend-fallback-import-submitted',probe:1};
  win5Window.fallbackImportRecords=records;
  assert.deepEqual(records,[expected],'One submitted fallback import must have one distinct diagnostic');
  const official=fixture();const notSubmitted=[];official.host.console={info:line=>notSubmitted.push(line)};
  official.inject();official.loader.plugins.push({name:'ymcc-sidebar'});official.ready();await official.advance(60000);
  const revoked=fixture();revoked.host.console=official.host.console;revoked.inject();revoked.cancel();await revoked.advance(60000);
  assert.deepEqual(notSubmitted,[]);assert.equal(official.calls.length+revoked.calls.length,0);
  const queued=fixture({queued:true});const queuedRecords=[];queued.host.console={info:line=>{queuedRecords.push(JSON.parse(line.slice(prefix.length)));throw Error('diagnostic sink unavailable');}};
  queued.inject();await queued.advance(12000);assert.equal(queued.calls.length,1);assert.equal(queued.state().phase,'waiting-receipt');
  await queued.advance(30000);assert.equal(queued.calls.length,1);assert.equal(queued.state().phase,'failed');assert.equal(queued.timers.size,0);
  assert.deepEqual(queuedRecords,[expected]);win5Window.noFallbackRecords=notSubmitted.length;win5Window.queuedRecords=queuedRecords;
 });
 await run(ownerCase,async()=>{
  const rows=[{name:'ymcc-sidebar',version:'0.0.1',load_type:1,disabled:false}];
  const fire=(f,event)=>{for(const fn of [...(f.listeners.get(event)||[])])fn();};
  const probeSettled=async f=>{let count=0;for(const probe of f.host.__fixtureProbes)probe.then(()=>++count,()=>++count);await flush();return count;};
  for(const edge of ['registry-becomes-array','registry-stays-invalid','registry-no-first-load-grant','registry-null-then-registered','registry-corrupt-during-read','registry-corrupt-before-retained-import']){
   const f=fixture({authorized:edge!=='registry-no-first-load-grant'});
   const retained=edge==='registry-corrupt-before-retained-import';
   const registered={name:'ymcc-sidebar',version:'0.0.1',loadType:1};
   f.loader.plugins=edge==='registry-null-then-registered'?[null,registered]:retained?[registered]:edge==='registry-corrupt-during-read'?[]:{};
   let settleList;
   if(edge==='registry-corrupt-during-read'){
    const originalRead=f.loader.getPluginsFromBackend.bind(f.loader);
    f.loader.getPluginsFromBackend=()=>{const initial=originalRead();return new Promise(resolve=>{settleList=()=>resolve(initial);});};
   }
   let injectionError=null;
   try{f.inject();}catch(error){injectionError=error.name;}
   const observed={edge,injectionError,timersAfterInjection:f.timers.size};ownerBoundaryFacts.push(observed);
   assert.equal(injectionError,null,'an incomplete plugin registry must not throw out of bootstrap');
   if(retained)f.loader.plugins=[null,registered];
   await f.advance(12000);
   if(edge==='registry-becomes-array'||edge==='registry-null-then-registered'){
    assert.equal(f.reads(),0);assert.equal(f.calls.length,0);
    assert.deepEqual([...f.timers.values()].map(t=>t.due),[16000]);
    f.loader.plugins=edge==='registry-null-then-registered'?[registered]:[];
   }
   if(edge==='registry-corrupt-during-read'){
    assert.equal(f.reads(),1);assert.equal(f.calls.length,0);
    f.loader.plugins=Array(1); // find() visits an array hole as undefined.
    settleList();
    await f.advance(0); // Deliver the reply before advancing to its deadline.
    assert.equal(f.state().phase,'canceled','a timely reply cannot authorize import against a corrupt registry');
    assert.equal(f.timers.size,0);
   }
   await f.advance(48000);
   const phase=retained?f.host.__YMCC_DECKY_FRONTEND_UPDATE__?.phase:f.state()?.phase??null;
   const listeners=[...f.listeners.values()].reduce((n,set)=>n+set.size,0);
   Object.assign(observed,{reads:f.reads(),imports:f.calls.length,phase,timers:f.timers.size,listeners});
   assert.deepEqual([observed.reads,observed.imports,phase],{
    'registry-becomes-array':[1,1,'current'],'registry-stays-invalid':[0,0,'failed'],
    'registry-no-first-load-grant':[0,0,null],'registry-null-then-registered':[0,0,'official-load-in-progress'],
    'registry-corrupt-during-read':[1,0,'canceled'],'registry-corrupt-before-retained-import':[0,0,'canceled'],
   }[edge]);
   assert.equal(observed.timers+observed.listeners,0);
  }
  for(const edge of ['preflight-throws-once','preflight-throws-through-retry-budget','pagehide-after-preflight-fault','official-ready-after-preflight-fault']){
   const f=fixture({observeProbes:true}),original=f.loader.deckyState.publicState;
   let faults=0;
   f.loader.deckyState.publicState=()=>{
    if(faults===0||edge==='preflight-throws-through-retry-budget'){faults++;throw Error('transient official state read');}
    return original();
   };
   const takeProbe=async()=>{f.fireNext();return f.host.__fixtureProbes.at(-1).then(()=>true,()=>false);};
   f.inject();
   const first=await takeProbe();
   const attempt={edge,first,reads:f.reads(),imports:f.calls.length,nextDue:[...f.timers.values()].map(t=>t.due)};
   ownerBoundaryFacts.push(attempt);
   assert.equal(first,true,'preflight exception must stay inside the existing first-load retry');
   assert.deepEqual([attempt.reads,attempt.imports,attempt.nextDue],[0,0,[16000]]);
   if(edge==='pagehide-after-preflight-fault')fire(f,'pagehide');
   else if(edge==='official-ready-after-preflight-fault'){f.loader.plugins.push(rows[0]);f.ready();}
   else{
    assert.equal(await takeProbe(),true);
    if(edge==='preflight-throws-through-retry-budget'){
     assert.deepEqual([...f.timers.values()].map(t=>t.due),[24000]);
     assert.equal(await takeProbe(),true);
    }
   }
   await flush();
   const listeners=[...f.listeners.values()].reduce((n,set)=>n+set.size,0);
   Object.assign(attempt,{faults,finalReads:f.reads(),finalImports:f.calls.length,phase:f.state().phase,timers:f.timers.size,listeners});
   assert.deepEqual([attempt.faults,attempt.finalReads,attempt.finalImports,attempt.phase],{
    'preflight-throws-once':[1,1,1,'current'],
    'preflight-throws-through-retry-budget':[3,0,0,'failed'],
    'pagehide-after-preflight-fault':[1,0,0,'canceled'],
    'official-ready-after-preflight-fault':[1,0,0,'current'],
   }[edge]);
   assert.equal(attempt.timers+attempt.listeners,0);
  }
  for(const edge of ['same-router-replaced-socket','same-socket-control','pagehide-before-probe','pagehide-inflight']){
   const f=fixture({observeProbes:true});let complete,reads=0;
   f.loader.getPluginsFromBackend=()=>{reads++;return new Promise(resolve=>{complete=()=>resolve(rows);});};
   f.inject();
   if(edge==='pagehide-before-probe')fire(f,'pagehide');
   else{f.fireNext();await flush();assert.equal(reads,1);}
   if(edge==='same-router-replaced-socket')f.host.DeckyBackend.ws={readyState:1};
   if(edge==='pagehide-inflight')fire(f,'pagehide');
   if(edge.startsWith('pagehide')){
    assert.equal(await probeSettled(f),edge==='pagehide-inflight'?1:0,'retired page releases the read before any late response');
    f.inject(); // The same grant cannot return after disposal.
   }
   complete?.();await flush();
   const facts={edge,reads,imports:f.calls.length,phase:f.state().phase,timers:f.timers.size,pagehideListeners:f.listeners.get('pagehide')?.size||0};ownerBoundaryFacts.push(facts);
   assert.equal(facts.imports,edge==='same-socket-control'?1:0,edge+' must not reuse a late reply');
    if(edge==='same-router-replaced-socket'){
     assert.equal(facts.phase,'waiting-loader','same router with a new socket must retain the bounded first-load attempt');
     assert.equal(facts.timers,1);assert.equal(facts.pagehideListeners,1);
     await f.advance(3999);assert.equal(reads,1);assert.equal(f.calls.length,0);
     await f.advance(1);assert.equal(reads,2);assert.equal(f.calls.length,0);
     complete();await flush();
     assert.equal(f.calls.length,1);assert.equal(f.state().phase,'current');assert.equal(f.timers.size,0);assert.equal(f.listeners.get('pagehide')?.size||0,0);
     Object.assign(facts,{freshReads:reads-1,finalImports:f.calls.length,finalPhase:f.state().phase,finalTimers:f.timers.size});
    }else{
     assert.equal(facts.phase,edge==='same-socket-control'?'current':'canceled');assert.equal(facts.timers,0);assert.equal(facts.pagehideListeners,0);
    }
  }
   for(const edge of ['disabled-on-reconnect','timed-out-read','off-before-retry','ready-before-retry','queued-before-retry','new-grant-before-retry','router-replaced-between-retries','socket-churn-limit']){
    const f=fixture({observeProbes:true});const pending=[];
    f.loader.getPluginsFromBackend=()=>new Promise((resolve,reject)=>pending.push({resolve,reject}));
    f.inject();f.fireNext();await flush();assert.equal(pending.length,1);
    if(edge==='router-replaced-between-retries')f.host.DeckyBackend.ws.readyState=3;
    else f.host.DeckyBackend.ws={readyState:1};
    if(edge==='timed-out-read')await f.advance(3000);
    else{pending[0].resolve(rows);await flush();}
    assert.equal(f.calls.length,0);assert.equal(f.state().phase,'waiting-loader');assert.equal(f.timers.size,1);
    if(edge==='off-before-retry'){f.cancel();f.host.__YMCC_DECKY_MIRROR__={...f.value,revoked:true};}
    if(edge==='ready-before-retry'){f.loader.plugins.push({name:'ymcc-sidebar'});f.ready();}
    if(edge==='queued-before-retry')f.loader.pluginReloadQueue.push({name:'ymcc-sidebar'});
    if(edge==='new-grant-before-retry')f.host.__YMCC_DECKY_MIRROR__={...f.value,runId:'d'.repeat(32)};
    await f.advance(4000);
    if(edge==='router-replaced-between-retries'){
     assert.equal(f.state().probes,2);assert.equal(pending.length,1);assert.equal(f.timers.size,1);
     f.host.DeckyBackend={ws:{readyState:1}};
    }
    if(edge==='disabled-on-reconnect')pending[1].resolve([{...rows[0],disabled:true}]);
    if(edge==='timed-out-read'){
     pending[0].resolve(rows);await flush();assert.equal(f.calls.length,0,'late timeout cannot authorize import');
     pending[1].resolve(rows);
    }
    if(edge==='socket-churn-limit'){
     for(const [index,delay] of [[1,8000],[2,0]]){
      f.host.DeckyBackend.ws={readyState:1};pending[index].resolve(rows);await flush();await f.advance(delay);
     }
    }
    await flush(); // Deliver explicit replies before advancing past their read deadlines.
    await f.advance(60000);
    const expectations={
     'disabled-on-reconnect':[2,2,0,'absent-or-disabled'],'timed-out-read':[2,2,1,'current'],
     'off-before-retry':[1,1,0,'canceled'],'ready-before-retry':[1,1,0,'current'],
     'queued-before-retry':[1,2,0,'official-load-in-progress'],'new-grant-before-retry':[1,1,0,'canceled'],
     'router-replaced-between-retries':[1,2,0,'canceled'],'socket-churn-limit':[3,3,0,'failed'],
    };
    const facts={edge,reads:pending.length,probes:f.state().probes,imports:f.calls.length,phase:f.state().phase,timers:f.timers.size};
    ownerBoundaryFacts.push(facts);assert.deepEqual([facts.reads,facts.probes,facts.imports,facts.phase],expectations[edge],edge);
    assert.equal(facts.timers,0);assert.equal(f.listeners.get('pagehide')?.size||0,0);
    assert.equal(await probeSettled(f),f.host.__fixtureProbes.length);
   }
  for(const edge of ['socket-replaced-after-import','pagehide-after-import','same-owner-control','ready-before-registration','pagehide-between-ready-and-registration']){
   const f=fixture();let complete,reselects=0,active={name:'ymcc-sidebar'};
   const early=edge==='ready-before-registration'||edge==='pagehide-between-ready-and-registration';
   f.loader.deckyState.publicState=()=>({versionInfo:{current:f.value.loaderVersion},activePlugin:active});
   f.loader.deckyState.setActivePlugin=()=>{reselects++;active=f.loader.plugins[0];};
   f.loader.importPlugin=(...args)=>{f.calls.push(args);return new Promise(resolve=>{
    if(!early)f.loader.plugins.push({name:'ymcc-sidebar'});
    complete=()=>{if(early)f.loader.plugins.push({name:'ymcc-sidebar'});else f.ready();resolve();};
   });};
   f.inject();await f.advance(12000);assert.equal(f.calls.length,1);assert.equal(f.state().phase,'waiting-receipt');
   if(early)f.ready(); // Decky invokes default() before adding the plugin to its list.
   if(edge==='socket-replaced-after-import')f.host.DeckyBackend.ws={readyState:1};
   if(edge==='pagehide-after-import'||edge==='pagehide-between-ready-and-registration')fire(f,'pagehide');
   complete();await flush();
   ownerBoundaryFacts.push({edge,imports:f.calls.length,reselects,phase:f.state().phase,timers:f.timers.size});
   const keep=edge==='same-owner-control'||edge==='ready-before-registration';
   assert.equal(reselects,keep?1:0,edge);assert.equal(f.state().phase,keep?'current':'canceled');assert.equal(f.calls.length,1);assert.equal(f.timers.size,0);assert.equal(f.listeners.get('pagehide')?.size||0,0);
  }
  const receiptRows=[];
  for(const edge of ['ready-without-row','duplicate-rows','queued-inline-publication','row-at-deadline','retired-socket-reinject','off-before-inline-publication']){
   const f=fixture({queued:true});let active={name:'ymcc-sidebar'},reselects=0;
   const published={name:'ymcc-sidebar'};
   f.loader.deckyState.publicState=()=>({versionInfo:{current:f.value.loaderVersion},activePlugin:active});
   f.loader.deckyState.setActivePlugin=()=>{reselects++;active=f.loader.plugins[0];};
   f.inject();await f.advance(12000);
   assert.equal(f.state().phase,'waiting-receipt');assert.equal(f.calls.length,1);
   if(edge==='duplicate-rows')f.loader.plugins.push(published,{name:'ymcc-sidebar'});
   f.ready();
   if(edge==='off-before-inline-publication')f.cancel();
   if(edge==='queued-inline-publication'||edge==='off-before-inline-publication')f.loader.plugins.push(published);
   if(edge==='retired-socket-reinject'){
    f.host.DeckyBackend.ws={readyState:1};f.loader.plugins.push(published);f.inject();
   }
   await flush();
   const earlyPhase=f.state().phase;
   if(edge==='row-at-deadline')f.loader.plugins.push(published);
   await f.advance(10000);
   const receiptReselects=reselects;
   f.inject();await flush();
   const listeners=['ymcc-decky-bootstrap','ymcc-decky-health-stop','ymcc-decky-frontend-ready','pagehide'].reduce((n,k)=>n+(f.listeners.get(k)?.size||0),0);
   const facts={edge,earlyPhase,phase:f.state().phase,imports:f.calls.length,reads:f.reads(),receiptReselects,reselects,timers:f.timers.size,listeners};
   receiptRows.push(facts);ownerBoundaryFacts.push(facts);
  }
  assert.deepEqual(receiptRows.map(x=>[x.earlyPhase,x.phase,x.reselects]),[
   ['waiting-receipt','failed',0],['waiting-receipt','failed',0],['current','current',1],
   ['waiting-receipt','current',1],['canceled','canceled',0],['canceled','canceled',0],
  ],'only one registered row on the live first-load owner can complete the receipt');
  assert.deepEqual(receiptRows.map(x=>x.receiptReselects),[0,0,1,1,0,0],'own receipt must settle without a new bootstrap');
  assert(receiptRows.every(x=>x.imports===1&&x.reads===1&&x.timers===0&&x.listeners===0));
 });
 for(const [name,options] of Object.entries({'queue is not receipt':{queued:true},'wrong bundle receipt':{wrongBuild:true},[uncertainCase]:{importError:true}}))await run(name,async()=>{
  const f=fixture(options);for(let i=0;i<30;i++)f.inject();await f.advance(60000);assert.equal(f.calls.length,1);assert.equal(f.state().phase,'failed');assert.equal(f.timers.size,0);f.inject();await f.advance(60000);assert.equal(f.calls.length,1);
  if(name!==uncertainCase)return;
  const row=()=>({name:'ymcc-sidebar',version:'0.0.1',loadType:1});
  for(const edge of ['queued-return','throw-after-call','rejected-after-call','hung-call','official-queued','row-absent-on-reinjection','first-load-handoff']){
   const f=fixture({queued:true});
   if(edge==='first-load-handoff')f.inject();
   f.loader.plugins.push(row());
   if(edge==='official-queued')f.loader.pluginReloadQueue.push(row());
   f.loader.importPlugin=(...args)=>{
    f.calls.push(args);
    if(edge==='throw-after-call')throw Error('official already entered');
    if(edge==='rejected-after-call')return Promise.reject(Error('official completion unknown'));
    if(edge==='hung-call')return new Promise(()=>{});
    if((edge==='row-absent-on-reinjection'||edge==='first-load-handoff')&&f.calls.length===1)f.loader.plugins.length=0;
    return undefined;
   };
   f.inject();await flush();for(let i=0;i<30;i++)f.inject();await f.advance(60000);
   const update=f.host.__YMCC_DECKY_FRONTEND_UPDATE__;
   retainedUpdateFacts.push({edge,imports:f.calls.length,reads:f.reads(),phase:update?.phase,timers:f.timers.size});
  }
  assert.deepEqual(retainedUpdateFacts.map(x=>x.imports),[1,1,1,1,0,1,1],'retained update must never re-submit an uncertain import or take a queued load');
  assert(retainedUpdateFacts.every(x=>x.reads===0&&x.phase==='failed'&&x.timers===0),'timer retirement and first-load handoff');
  for(const edge of ['slot-released','slot-stays-busy','queued-ready','loader-replaced','plugin-replaced','different-grant']){
   const f=fixture();f.loader.plugins.push(row());
   if(edge==='queued-ready')f.loader.pluginReloadQueue.push(row());else if(edge!=='different-grant')f.loader.reloadLock=true;
   if(edge==='different-grant'){
    f.loader.importPlugin=(...args)=>{f.calls.push(args);if(f.calls.length===2){f.loader.plugins=[];f.ready();f.loader.plugins.push(row());}};
    f.inject();await f.advance(10000);assert.equal(f.calls.length,1);
    f.inject({...f.value,runId:'d'.repeat(32)});await flush();
   }else{
    f.inject();await flush();assert.equal(f.calls.length,0,edge);
    if(edge==='queued-ready'){f.loader.plugins=[];f.ready();f.loader.plugins.push(row());await flush();}
    else{
     await f.advance(4999);assert.equal(f.calls.length,0,edge);
     if(edge!=='slot-stays-busy')f.loader.reloadLock=false;
     if(edge==='loader-replaced')f.host.DeckyPluginLoader={...f.loader};
     if(edge==='plugin-replaced')f.loader.plugins=[row()];
     await f.advance(1);
    }
   }
   await f.advance(60000);
   const update=f.host.__YMCC_DECKY_FRONTEND_UPDATE__;
   const imports=edge==='slot-released'?1:edge==='different-grant'?2:0;
   const phase=['slot-released','queued-ready','different-grant'].includes(edge)?'current':edge==='slot-stays-busy'?'failed':'canceled';
   const listeners=['ymcc-decky-bootstrap','ymcc-decky-health-stop','ymcc-decky-frontend-ready','pagehide'].reduce((n,k)=>n+(f.listeners.get(k)?.size||0),0);
   retainedUpdateFacts.push({edge,imports:f.calls.length,reads:f.reads(),phase:update.phase,attempts:update.attempts,timers:f.timers.size,listeners});
   assert.equal(f.calls.length,imports,edge);assert.equal(update.phase,phase,edge);assert.equal(f.timers.size+listeners,0,edge);
   if(edge==='slot-stays-busy')assert.equal(update.attempts,3,'busy retries stay bounded');
  }
  const receiptFacts=[];
  for(const edge of ['ready-without-row','import-rejects-after-ready','ready-with-old-row','retired-loader-receipt','owned-ready-before-row','queued-ready-before-row','off-before-row','duplicate-registered-rows','another-panel-active']){
   const f=fixture(),original=row(),published=row();let finish,active=original;
   const reselected=[];f.loader.plugins=[original];if(edge==='another-panel-active')active={name:'another-plugin'};
   f.loader.deckyState.publicState=()=>({versionInfo:{current:f.value.loaderVersion},activePlugin:active});
   f.loader.deckyState.setActivePlugin=name=>{active=f.host.DeckyPluginLoader.plugins.find(item=>item.name===name);reselected.push(active===published?'new':'wrong');};
   f.loader.importPlugin=(...args)=>{
    f.calls.push(args);
    if(edge!=='ready-with-old-row')f.loader.plugins=[];
    return new Promise((resolve,reject)=>{finish=(error=false)=>error?reject(Error('registration failed')):resolve();});
   };
   const queued=edge==='queued-ready-before-row';if(queued)f.loader.pluginReloadQueue=[row()];
   f.inject();await flush();assert.equal(f.calls.length,queued?0:1,edge);
   if(queued)f.loader.plugins=[];
   if(edge==='retired-loader-receipt')f.host.DeckyPluginLoader={...f.loader,plugins:[published]};
   f.ready(); // The real module emits this before the official Loader pushes its row.
   if(queued)f.loader.plugins.push(published); // Official queued import publishes inline, in the same turn.
   if(edge==='import-rejects-after-ready')finish(true);
   if(edge==='duplicate-registered-rows')f.loader.plugins.push(published,row());
   await flush();
   const update=f.host.__YMCC_DECKY_FRONTEND_UPDATE__;
   const earlyPhase=update.phase;
   // A same-grant bootstrap while registration is pending cannot reselect a stale row.
   if(!queued)f.inject();
   if(edge==='off-before-row')f.cancel();
   if(['owned-ready-before-row','off-before-row','another-panel-active'].includes(edge)){f.loader.plugins.push(published);finish();}
   await flush();await f.advance(10000);
   f.inject();await flush();
   const listeners=['ymcc-decky-bootstrap','ymcc-decky-health-stop','ymcc-decky-frontend-ready','pagehide'].reduce((n,k)=>n+(f.listeners.get(k)?.size||0),0);
   const facts={edge,earlyPhase,phase:update.phase,imports:f.calls.length,reads:f.reads(),reselected,timers:f.timers.size,listeners};
   receiptFacts.push(facts);retainedUpdateFacts.push(facts);
  }
  assert.deepEqual(receiptFacts.map(x=>x.phase),['failed','failed','failed','canceled','current','current','canceled','failed','current'],'module ready must not substitute for a replacement row on the retained Loader');
  assert.deepEqual(receiptFacts.map(x=>x.reselected),[[],[],[],[],['new'],['new'],[],[],[]],'only the published replacement may be selected, and only for the active YMCC panel');
  assert(receiptFacts.every(x=>x.reads===0&&x.timers===0&&x.listeners===0&&x.imports===(x.edge==='queued-ready-before-row'?0:1)),'no replay or retained observer');
  assert.equal(receiptFacts[4].earlyPhase,'pending','owned ready before publication must remain pending');
 });
 await run('OFF cancels bounded recovery before import',async()=>{const f=fixture();f.inject();for(const fn of [...f.listeners.get('ymcc-decky-health-stop')])fn();await f.advance(60000);assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);
  for(const edge of ['first-grace','first-list','first-receipt','update-before-dispatch','update-receipt','update-retry']){
   const first=edge.startsWith('first-'),f=fixture({readPending:edge==='first-list',queued:true,observeProbes:true});
   if(!first)f.loader.plugins.push({name:'ymcc-sidebar',version:'0.0.1',loadType:1});
   if(edge==='update-retry')f.loader.reloadLock=true;
   let settled=0;
   const notify=()=>{for(const fn of [...(f.listeners.get('ymcc-decky-bootstrap')||[])])fn();};
   f.inject();
   if(edge==='first-list'||edge==='first-receipt')f.fireNext();
   if(edge!=='update-before-dispatch')await flush();
   for(const p of f.host.__fixtureProbes)Promise.resolve(p).then(()=>settled++,()=>settled++);
   const state=first?f.state():f.host.__YMCC_DECKY_FRONTEND_UPDATE__;
   const beforeTimers=f.timers.size,beforeImports=f.calls.length,beforePhase=state.phase;
   notify(); // A same-run/same-build bootstrap is an ordinary refresh.
   assert.equal(f.timers.size,beforeTimers);assert.equal(state.phase,beforePhase);
   assert.equal(beforeTimers,1);assert.equal(beforeImports,['first-receipt','update-receipt'].includes(edge)?1:0);
   f.host.__YMCC_DECKY_MIRROR__=Object.freeze({runId:f.value.runId,frontendBuildId:f.value.frontendBuildId,revoked:true});
   notify();await flush();
   const row={edge,beforeTimers,beforeImports,beforePhase,timers:f.timers.size,phase:state.phase,probes:f.host.__fixtureProbes.length,settled,
     listeners:['ymcc-decky-bootstrap','ymcc-decky-health-stop','ymcc-decky-frontend-ready','pagehide'].reduce((n,k)=>n+(f.listeners.get(k)?.size||0),0)};
   f.ready();await f.advance(60000);row.lateImports=f.calls.length;revocationFacts.push(row);
   assert.equal(row.lateImports,beforeImports,'revoked frontend cannot submit or replay an import');
  }
  assert(revocationFacts.every(r=>r.timers===0&&r.listeners===0&&r.phase==='canceled'&&r.probes===r.settled),'revoked grant must release all frontend waits immediately');
  const observed=async f=>{let settled=0;for(const promise of f.host.__fixtureProbes)Promise.resolve(promise).then(()=>settled++,()=>settled++);await flush();return {started:f.host.__fixtureProbes.length,settled};};
  const verify=async(name,body)=>{try{await body();readCancellationObservations.push({name,status:'passed'});}catch(error){readCancellationObservations.push({name,status:'failed',error:String(error.stack||error)});}};
  for(const edge of ['off','ready'])await verify('inflight-'+edge,async()=>{
   const f=fixture({readPending:true,observeProbes:true});f.inject();f.fireNext();await flush();assert.equal(f.reads(),1);assert.equal((await observed(f)).settled,0);
   if(edge==='off'){f.cancel();f.host.__YMCC_DECKY_MIRROR__={...f.value,revoked:true};}else f.ready();
   await flush();const result=await observed(f);assert.deepEqual(result,{started:1,settled:1},'retired own frontend read wait must settle without upstream reply');
   assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);assert.equal(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0);assert.equal(f.listeners.get('ymcc-decky-health-stop')?.size||0,0);assert.equal(f.state().phase,edge==='off'?'canceled':'current');
   readCancellationFacts.push({edge:'inflight-'+edge,...result,reads:f.reads(),imports:f.calls.length,timers:f.timers.size,phase:f.state().phase,upstreamReplyDelivered:false});
  });
  for(const edge of ['off','ready','new-run','backend-replaced','loader-replaced'])await verify('queued-'+edge,async()=>{
   const f=fixture({observeProbes:true});f.inject();f.fireNext();
   if(edge==='off'){f.cancel();f.host.__YMCC_DECKY_MIRROR__={...f.value,revoked:true};}
   else if(edge==='ready')f.ready();
   else if(edge==='new-run')f.host.__YMCC_DECKY_MIRROR__={...f.value,runId:'d'.repeat(32)};
   else if(edge==='backend-replaced')f.host.DeckyBackend={ws:{readyState:1}};
   else f.host.DeckyPluginLoader={...f.loader};
   await flush();const result=await observed(f);assert.equal(f.reads(),0,'retired queued frontend read must not call the official backend');assert.equal(f.calls.length,0);assert.deepEqual(result,{started:1,settled:1});assert.equal(f.timers.size,0);
   assert.equal(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0);assert.equal(f.listeners.get('ymcc-decky-health-stop')?.size||0,0);
   readCancellationFacts.push({edge:'queued-'+edge,...result,reads:f.reads(),imports:f.calls.length,timers:f.timers.size,phase:f.state().phase});
  });
  for(const [name,options] of [['normal',{}],['read-timeout',{readPending:true}],['read-error',{readError:true}]])await verify(name,async()=>{
   const f=fixture({...options,observeProbes:true});f.inject();await f.advance(60000);const result=await observed(f);
   assert.equal(result.started,result.settled);assert.equal(f.reads(),name==='normal'?1:3);assert.equal(f.calls.length,name==='normal'?1:0);assert.equal(f.timers.size,0);assert.equal(f.state().phase,name==='normal'?'current':'failed');
   assert.equal(f.listeners.get('ymcc-decky-frontend-ready')?.size||0,0);assert.equal(f.listeners.get('ymcc-decky-health-stop')?.size||0,0);
   readCancellationFacts.push({edge:name,...result,reads:f.reads(),imports:f.calls.length,timers:f.timers.size,phase:f.state().phase});
  });
  assert.equal(readCancellationObservations.length,10);assert(readCancellationObservations.every(row=>row.status==='passed'),'frontend read cancellation controls failed: '+readCancellationObservations.filter(row=>row.status!=='passed').map(row=>row.name).join(','));
 });
 await run('new native run fences old document completion',async()=>{const f=fixture();f.inject();f.host.__YMCC_DECKY_MIRROR__={...f.value,runId:'d'.repeat(32)};await f.advance(60000);assert.equal(f.calls.length,0);assert.equal(f.timers.size,0);});
 if(onlyCase===null){const negative=source.replace('value.recoverFrontend !== true','true || value.recoverFrontend !== true');assert.notEqual(negative,source);const broken=fixture({},negative);broken.inject();await broken.advance(60000);assert.throws(()=>assert.equal(broken.calls.length,1,'missing enabled frontend was not recovered'),/missing enabled frontend/);results.push('disabled recovery negative reproduces empty enabled plugin');}else omitted.push('disabled recovery negative reproduces empty enabled plugin');
 const value=report(true);write(value);return value;
}
if(process.argv[1]&&path.resolve(process.argv[1])===path.resolve(import.meta.filename)){let output=null,onlyCase=null,excludeCase=null;const args=process.argv.slice(2);for(let n=0;n<args.length;n++){const a=args[n];if(a==='--case'||a==='--exclude-case'){assert(n+1<args.length,'FRONTEND_BOOT_SELECTION_EMPTY');const value=args[++n];assert(value&&!value.startsWith('--'),'FRONTEND_BOOT_SELECTION_EMPTY');if(a==='--case'){assert(onlyCase===null,'FRONTEND_BOOT_SELECTION_DUPLICATE');onlyCase=value;}else{assert(excludeCase===null,'FRONTEND_BOOT_SELECTION_DUPLICATE');excludeCase=value;}}else{assert(a&&!a.startsWith('-')&&output===null,'FRONTEND_BOOT_ARGUMENT_UNKNOWN');output=a;}}const root=path.resolve(import.meta.dirname,'..'),out=path.resolve(output||'work/frontend-boot');console.log(JSON.stringify(await frontendBootTests(root,out,onlyCase,excludeCase)));}
