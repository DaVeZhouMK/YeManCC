import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';
const out=process.argv[2];if(!out)throw Error('fresh evidence directory required');fs.mkdirSync(out,{recursive:true});
const root=path.resolve(import.meta.dirname,'..'),require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');
const {chromium}=require('playwright'),browser=await chromium.launch({channel:'msedge',headless:true}),page=await browser.newPage({viewport:{width:1280,height:800}});const errors=[];page.on('pageerror',e=>errors.push(e.message));let checks=0;const check=(ok,label)=>{checks++;if(!ok)throw Error(label);};
try{
 await page.clock.install({time:Date.now()});
 await page.addInitScript(()=>{
  const listeners=new Set();window.__downloadCommands=[];window.__dropManual=false;window.__dropStatus=false;window.__holdState=false;window.__heldState=[];
  window.__state={enabled:true,completed:45,total:48,remaining:3,activeCount:0,workerPid:0,phase:'deferred',pendingReason:'media-size-invalid',nextDueAt:Date.now()+3600000,statusRevision:1};
  window.__send=message=>{for(const listener of listeners)listener({data:message});};
  window.__respond=(message,result)=>window.__send({kind:'response',id:message.id,ok:true,result});
  Object.defineProperty(window,'chrome',{configurable:true,value:{webview:{addEventListener(type,listener){listeners.add(listener);},postMessage(message){
   window.__downloadCommands.push(message);if(message.command==='uiError')return;
   if(['mediaDownloadNow','mediaDownloadCancel'].includes(message.command)&&window.__dropManual)return;
   if(message.command==='mediaDownloadStatus'&&window.__dropStatus)return;
   if(message.command==='state'&&window.__holdState){window.__heldState.push(message);return;}
   queueMicrotask(()=>window.__respond(message,message.command==='steamAccounts'?{accounts:[],steamStatus:'not-running'}:message.command.startsWith('mediaDownload')?window.__state:{config:{},manualOverrides:{items:{}},library:{games:[]},steamPlan:{items:[],summary:{}},gameMedia:{entries:{}},mediaDownloads:window.__state}));
  }}}});
 });
 await page.route('http://app.localhost/**',route=>{const name=path.basename(new URL(route.request().url()).pathname)||'index.html';if(!['index.html','app.js','styles.css'].includes(name))return route.fulfill({status:404,body:''});return route.fulfill({contentType:name.endsWith('.js')?'text/javascript':name.endsWith('.css')?'text/css':'text/html',body:fs.readFileSync(root+'/CustomSteamLibrary/workspace-ui/'+name)});});
 await page.goto('http://app.localhost/index.html');await page.waitForFunction(()=>typeof renderMediaDownloads==='function');
 await page.evaluate(()=>renderMediaDownloads(window.__state));
 check((await page.locator('#media-download-label').textContent()).includes('等待重试'),'45/48 deferred queue is shown as retry wait');
 check(!(await page.locator('#media-download-label').textContent()).includes('正在准备本轮队列'),'deferred state never advertises endless preparation');
 await page.evaluate(()=>{window.__dropManual=true;document.querySelector('#media-download-now').click();});
 check(await page.locator('#media-download-now').isDisabled(),'manual button waits for native acknowledgement');
 await page.clock.runFor(8001);await page.evaluate(()=>Promise.resolve());
 check(!(await page.locator('#media-download-now').isDisabled()),'lost manual return releases the button within eight seconds');
 check(await page.evaluate(()=>!Array.from(pending.values()).some(x=>x.command==='mediaDownloadNow')),'timed out request is removed');
 const lateId=await page.evaluate(()=>window.__downloadCommands.find(x=>x.command==='mediaDownloadNow').id);
 await page.evaluate(id=>window.__send({kind:'response',id,ok:true,result:{...window.__state,completed:0,statusRevision:999}}),lateId);
 check(!(await page.locator('#media-download-label').textContent()).includes('0 / 48'),'late response cannot replace current queue');
 const count=()=>page.evaluate(()=>window.__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length);
 const before=await count();await page.clock.runFor(20001);await page.evaluate(()=>Promise.resolve());
 check((await count())===before+1,'retained queue performs one bounded status check after twenty seconds');
 await page.evaluate(()=>{window.__state={...window.__state,completed:46,remaining:2,statusRevision:2};window.__send({event:'media-download-progress',mediaDownloads:window.__state});});
 const stateCallsBefore=await page.evaluate(()=>window.__downloadCommands.filter(x=>x.command==='state').length);await page.clock.runFor(300);await page.evaluate(()=>Promise.resolve());
 check((await page.evaluate(()=>window.__downloadCommands.filter(x=>x.command==='state').length))===stateCallsBefore+1,'completion-count change repairs a lost media publication event');
 await page.evaluate(()=>{window.__state={...window.__state,completed:48,remaining:0,statusRevision:3};renderMediaDownloads(window.__state);});
 const doneBefore=await count();await page.clock.runFor(120001);check((await count())===doneBefore,'completion removes idle status polling');
 await page.evaluate(()=>{window.__state={...window.__state,enabled:false,remaining:3,statusRevision:4};renderMediaDownloads(window.__state);});await page.clock.runFor(120001);check((await count())===doneBefore,'disabled automatic download never recreates status polling');
 await page.evaluate(()=>{window.__state={...window.__state,enabled:true,statusRevision:5};renderMediaDownloads(window.__state);window.__dropStatus=true;});
 await page.clock.runFor(20001);await page.clock.runFor(5001);check(await page.evaluate(()=>!Array.from(pending.values()).some(x=>x.command==='mediaDownloadStatus')),'status timeout also releases its request');
 const retryBefore=await count();await page.clock.runFor(20001);check((await count())===retryBefore+1,'unavailable status channel gets another bounded slow retry');
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pagehide')));await page.clock.runFor(5001);const hiddenBefore=await count();await page.clock.runFor(120001);check((await count())===hiddenBefore,'page lifecycle teardown cancels future recovery');
 // Exercise the existing silent refresh owner against event bursts and late
 // native replies. The native transport is held; the production IPC timers run.
 await page.evaluate(()=>{
  window.__refreshMetrics={entry:0,library:0};window.__refreshFocused=true;document.hasFocus=()=>window.__refreshFocused;
  renderEntry=()=>{__refreshMetrics.entry++;};renderLibrary=()=>{__refreshMetrics.library++;};
  document.querySelector('#library-view').classList.remove('hidden');window.__holdState=true;
  window.dispatchEvent(new PageTransitionEvent('pageshow'));
 });
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 for(let n=0;n<4;n++){await page.evaluate(()=>__send({event:'game-media-changed'}));await page.clock.runFor(300);}
 check(await page.evaluate(()=>__heldState.length===1),'snapshot event burst must share one pending native read');
 await page.evaluate(()=>__respond(__heldState[0],{config:{},library:{games:[]},refreshProbe:'first'}));await page.evaluate(()=>Promise.resolve());await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===2),'changes arriving during a read trigger exactly one follow-up');
 check(await page.evaluate(()=>__refreshMetrics.entry===0&&__refreshMetrics.library===0),'superseded intermediate snapshot does not rebuild the card tree');
 await page.evaluate(()=>__respond(__heldState[1],{config:{},library:{games:[]},refreshProbe:'latest'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='latest'&&__refreshMetrics.entry===1&&__refreshMetrics.library===1),'latest burst snapshot renders once');
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>{__refreshFocused=false;window.dispatchEvent(new Event('blur'));__respond(__heldState[2],{config:{},library:{games:[]},refreshProbe:'background-result'});});await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='background-result'&&__refreshMetrics.entry===1),'reply after blur updates data without rebuilding background UI');
 await page.evaluate(()=>{__refreshFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===3&&__refreshMetrics.entry===2),'focus reuses a fresh background result without another native read');
 await page.evaluate(()=>{__refreshFocused=false;window.dispatchEvent(new Event('blur'));for(let n=0;n<100;n++)__send({event:'game-media-changed'});});await page.clock.runFor(1001);
 check(await page.evaluate(()=>__heldState.length===3),'one hundred background changes perform no native snapshot reads');
 await page.evaluate(()=>{__refreshFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===4),'focus combines background changes into one read');
 await page.evaluate(()=>__respond(__heldState[3],{config:{},library:{games:[]},refreshProbe:'foreground-repaired'}));await page.evaluate(()=>Promise.resolve());
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>{window.dispatchEvent(new PageTransitionEvent('pagehide'));__respond(__heldState[4],{config:{},library:{games:[]},refreshProbe:'obsolete-page'});});await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='foreground-repaired'),'page teardown rejects a reply from the previous lifecycle');
 await page.clock.runFor(1001);check(await page.evaluate(()=>__heldState.length===5),'page teardown cancels later snapshot work');
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pageshow')));await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===6),'restored page requests a fresh snapshot once');
 await page.clock.runFor(5001);await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>!Array.from(pending.values()).some(p=>p.command==='state')),'missing snapshot response releases its IPC request within five seconds');
 await page.clock.runFor(20001);check(await page.evaluate(()=>__heldState.length===6),'failed snapshot read does not create an automatic retry storm');
 await page.evaluate(()=>window.dispatchEvent(new Event('focus')));await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===7),'a later focus can recover a timed out snapshot read');
 await page.evaluate(()=>__respond(__heldState[6],{config:{},library:{games:[]},refreshProbe:'recovered'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='recovered'),'snapshot recovery publishes current data');
 // Even a continuous change stream must publish bounded intermediate data.
 const streamBefore=await page.evaluate(()=>({reads:__heldState.length,renders:__refreshMetrics.entry}));
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 for(let n=0;n<2;n++){
  await page.evaluate(n=>{__send({event:'game-media-changed'});__respond(__heldState.at(-1),{config:{},library:{games:[]},refreshProbe:'stream-'+n});},n);
  await page.evaluate(()=>Promise.resolve());await page.clock.runFor(251);
 }
 check(await page.evaluate(before=>__refreshMetrics.entry>before.renders,streamBefore),'continuous changes cannot starve all visible snapshot rendering');
 await page.evaluate(()=>__respond(__heldState.at(-1),{config:{},library:{games:[]},refreshProbe:'stream-final'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='stream-final'),'continuous stream settles on its final snapshot');
 const lifecycleBase=await page.evaluate(()=>__heldState.length);
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>{window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));});await page.clock.runFor(251);
 await page.evaluate(index=>{__respond(__heldState[index],{config:{},library:{games:[]},refreshProbe:'old-lifecycle-late'});__send({event:'game-media-changed'});},lifecycleBase);await page.evaluate(()=>Promise.resolve());await page.clock.runFor(251);
 check(await page.evaluate(base=>__heldState.length===base+2&&snapshot.refreshProbe!=='old-lifecycle-late',lifecycleBase),'old lifecycle completion cannot clear or overwrite the newer request owner');
 await page.evaluate(index=>__respond(__heldState[index],{config:{},library:{games:[]},refreshProbe:'new-lifecycle'}),lifecycleBase+1);await page.evaluate(()=>Promise.resolve());await page.clock.runFor(251);
 check(await page.evaluate(base=>__heldState.length===base+3,lifecycleBase),'new lifecycle still receives its one pending follow-up');
 await page.evaluate(index=>__respond(__heldState[index],{config:{},library:{games:[]},refreshProbe:'current-lifecycle'}),lifecycleBase+2);await page.evaluate(()=>Promise.resolve());
 check(errors.length===0,'actual library script has no browser exceptions');
 await page.screenshot({path:out+'/download-recovery.png'});const result={ok:true,checks,errors,scope:'actual production library HTML/CSS/JS; bounded native transport fixture and browser clock, no installed cache writes'};fs.writeFileSync(out+'/results.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}catch(error){await page.screenshot({path:out+'/failure.png'});fs.writeFileSync(out+'/failure.json',JSON.stringify({error:String(error),errors,commands:await page.evaluate(()=>window.__downloadCommands),refreshMetrics:await page.evaluate(()=>({held:window.__heldState?.length,renders:window.__refreshMetrics}))},null,2));throw error;}finally{await browser.close();}
