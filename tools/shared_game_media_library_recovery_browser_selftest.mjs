import fs from 'node:fs';import path from 'node:path';import {createRequire} from 'node:module';
const out=process.argv[2];if(!out)throw Error('fresh evidence directory required');
const selectedOrderingCase=process.argv[3]||'';
if(selectedOrderingCase&&!['snapshot-order','status-lifecycle','manual-status'].includes(selectedOrderingCase))throw Error('unknown media ordering case');
fs.mkdirSync(out,{recursive:true});
const root=path.resolve(import.meta.dirname,'..'),require=createRequire('C:/Users/DaVe/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/package.json');
const {chromium}=require('playwright'),browser=await chromium.launch({channel:'msedge',headless:true}),page=await browser.newPage({viewport:{width:1280,height:800}});const errors=[];page.on('pageerror',e=>errors.push(e.message));let checks=0;const assertions=[];const check=(ok,label)=>{checks++;assertions.push({label,ok:Boolean(ok)});if(!ok)throw Error(label);};

const orderingCaseRuns=[];
async function runMediaOrderingCase(name){
 const run={name,startCheck:checks+1,status:'running'};orderingCaseRuns.push(run);
 await page.reload();await page.waitForFunction(()=>typeof renderMediaDownloads==='function');await page.evaluate(()=>Promise.resolve());await page.clock.runFor(251);
 await page.evaluate(()=>{
  window.__orderingFocused=true;document.hasFocus=()=>window.__orderingFocused;document.querySelector('#library-view').classList.remove('hidden');
  window.__orderingGood={enabled:true,completed:10,total:10,remaining:0,statusRevision:100};window.__orderingError={enabled:false,recoveryRequired:true,reason:'queue-recovery-required',recoveryReason:'media-checkpoint-unreadable',phase:'deferred',completed:0,total:0,remaining:0,nextDueAt:-1};
  renderMediaDownloads(__orderingGood,{currentSnapshot:true});__holdState=true;window.__orderingStateBase=__heldState.length;window.__orderingCommandBase=__downloadCommands.length;
 });
 if(name==='snapshot-order'){
  await page.evaluate(()=>renderMediaDownloads(__orderingError));
  for(const advisory of [true,false]){
   await page.evaluate(advisory=>scheduleSilentRefresh({advisory}),advisory);await page.clock.runFor(251);
   await page.evaluate(advisory=>__respond(__heldState.at(-1),{config:{},library:{games:[]},refreshProbe:advisory?'advisory-current-other-fields':'ordinary-current-other-fields',mediaDownloads:{enabled:true,completed:9,total:10,remaining:1,statusRevision:99}}),advisory);await page.evaluate(()=>Promise.resolve());
   check((await page.locator('#media-download-label').textContent()).includes('暂不可读'),'older full snapshot cannot clear a current media read error');
   check(await page.evaluate(()=>snapshot.mediaDownloads.recoveryRequired===true&&lastMediaDownloadDisplay.statusRevision===100),'full snapshot admission preserves unavailable state and last valid revision');
   check(await page.evaluate(advisory=>snapshot.refreshProbe===(advisory?'advisory-current-other-fields':'ordinary-current-other-fields'),advisory),'media admission still accepts other current snapshot fields');
  }
  await page.evaluate(()=>scheduleSilentRefresh({advisory:true}));await page.clock.runFor(251);
  await page.evaluate(()=>__respond(__heldState.at(-1),{config:{},library:{games:[]},refreshProbe:'newer-full',mediaDownloads:{...__orderingGood,statusRevision:101}}));await page.evaluate(()=>Promise.resolve());
  check(await page.evaluate(()=>snapshot.mediaDownloads.statusRevision===101&&!snapshot.mediaDownloads.recoveryRequired),'newer full snapshot can recover the current media read error');
  check((await page.locator('#media-download-label').textContent()).includes('10 / 10'),'recovered full snapshot paints current progress');
  await page.clock.runFor(60001);
  check(await page.evaluate(()=>__heldState.length===__orderingStateBase+3),'full snapshot recovery settles after the three selected reads');
 }else{
  await page.evaluate(()=>{__dropStatus=true;window.__orderingStatusBase=__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length;window.__orderingOldRequest=recheckMediaDownloads();window.__orderingOldCommand=__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').at(-1);});
  await page.evaluate(()=>{window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));renderMediaDownloads(__orderingError);window.__orderingSharedRequest=recheckMediaDownloads();});
  check(await page.evaluate(()=>__orderingOldRequest===__orderingSharedRequest&&__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__orderingStatusBase+1),'restored page retains one status request until the retired read settles');
  await page.evaluate(()=>__respond(__orderingOldCommand,{enabled:true,completed:11,total:11,remaining:0,statusRevision:101}));await page.evaluate(()=>Promise.resolve());
  check(await page.evaluate(()=>snapshot.mediaDownloads.recoveryRequired===true&&lastMediaDownloadDisplay.statusRevision===100),'retired lifecycle status reply cannot replace current media state');
  check((await page.locator('#media-download-label').textContent()).includes('暂不可读'),'retired lifecycle status reply cannot clear current error presentation');
  await page.evaluate(()=>{window.__orderingCurrentRequest=recheckMediaDownloads();window.__orderingCurrentCommand=__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').at(-1);});
  check(await page.evaluate(()=>__orderingCurrentRequest!==__orderingOldRequest&&__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__orderingStatusBase+2),'settled retired read releases the status owner for current recovery');
  await page.evaluate(()=>__respond(__orderingCurrentCommand,{enabled:true,completed:11,total:11,remaining:0,statusRevision:102}));await page.evaluate(()=>Promise.resolve());
  check(await page.evaluate(()=>snapshot.mediaDownloads.statusRevision===102&&!snapshot.mediaDownloads.recoveryRequired),'current lifecycle status read still recovers real progress');
  await page.clock.runFor(251);await page.evaluate(()=>{if(__heldState.length>__orderingStateBase)__respond(__heldState.at(-1),{config:{},library:{games:[]},mediaDownloads:{enabled:true,completed:11,total:11,remaining:0,statusRevision:102}});});await page.evaluate(()=>Promise.resolve());
  await page.evaluate(()=>{window.__orderingTimeoutRequest=recheckMediaDownloads();window.dispatchEvent(new PageTransitionEvent('pagehide'));window.dispatchEvent(new PageTransitionEvent('pageshow'));renderMediaDownloads(__orderingError);});
  await page.clock.runFor(5001);await page.evaluate(()=>Promise.resolve());
  check(await page.evaluate(()=>mediaRecoveryRequest===null&&snapshot.mediaDownloads.recoveryRequired===true),'retired status timeout releases its owner without changing restored state');
  await page.evaluate(()=>window.__orderingBeforeDelayedStatus=__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length);await page.clock.runFor(15001);
  check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__orderingBeforeDelayedStatus),'retired status timeout does not create an immediate recovery storm');
  await page.clock.runFor(6001);
  check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__orderingBeforeDelayedStatus+1),'restored error gets the original slow status retry after timeout');
  await page.evaluate(()=>__respond(__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').at(-1),{enabled:true,completed:11,total:11,remaining:0,statusRevision:103}));await page.evaluate(()=>Promise.resolve());
  check(await page.evaluate(()=>snapshot.mediaDownloads.statusRevision===103),'slow current status retry can settle after retired timeout');
 }
 check(await page.evaluate(()=>!__downloadCommands.slice(__orderingCommandBase).some(x=>['commit','saveEditor','mediaEdit','mediaImport','mediaCreate','mediaSelectVideo','mediaDownloadNow','mediaDownloadCancel','identifyOne','scan','prepare','addRoot','removeRoot'].includes(x.command))),'media ordering recovery does not replay business writes');
 run.checks=checks-run.startCheck+1;run.status='PASS';
}

const manualStatusCaseRuns=[];
async function runManualStatusCase(){
 const run={name:'manual-status',startCheck:checks+1,status:'running'};manualStatusCaseRuns.push(run);
 await page.reload();await page.waitForFunction(()=>typeof renderMediaDownloads==='function');await page.clock.runFor(251);
 await page.evaluate(()=>{
  document.hasFocus=()=>true;document.querySelector('#library-view').classList.remove('hidden');
  window.__settledBase={enabled:true,phase:'pending',pauseReason:'game-running',resourcePauseReason:'',completed:71,total:71,remaining:0,failed:0,activeCount:0,workerPid:0,nextDueAt:-1,catalogPending:false,statusRevision:900};
  window.__settledCommandBase=__downloadCommands.length;
  renderMediaDownloads(__settledBase,{currentSnapshot:true});
 });
 const settledLabel=await page.locator('#media-download-label').textContent();
 check(settledLabel.includes('本轮已处理 · 71 / 71')&&!settledLabel.includes('已暂停'),'Win5 71/71 with no work does not report a background pause; actual: '+settledLabel);
 await page.clock.runFor(60001);
 check(await page.evaluate(()=>!__downloadCommands.slice(__settledCommandBase).some(x=>x.command.startsWith('mediaDownload'))),'settled queue never starts a status poll or download');
 await page.evaluate(()=>renderMediaDownloads({...__settledBase,pauseReason:'capacity-unavailable',resourcePauseReason:'cpu-busy',statusRevision:901},{currentSnapshot:true}));
 check((await page.locator('#media-download-label').textContent()).includes('本轮已处理'),'settled queue ignores unused resource gates');
 for(const [index,change] of [{completed:70,remaining:1},{catalogPending:true},{inventoryAwaitUntil:Date.now()+120000,inventoryReadyId:'older',inventoryRequestId:'new'},{workerPid:27,activeCount:1}].entries()){
  await page.evaluate(({index,change})=>renderMediaDownloads({...__settledBase,...change,statusRevision:902+index},{currentSnapshot:true}),{index,change});
  check((await page.locator('#media-download-label').textContent()).includes('后台下载已暂停'),'unsettled automatic queue keeps the pause: '+JSON.stringify(change));
 }
 await page.evaluate(()=>renderMediaDownloads({...__settledBase,enabled:false,statusRevision:906},{currentSnapshot:true}));
 check((await page.locator('#media-download-label').textContent()).includes('后台下载已关闭'),'user disabled state keeps precedence over completion');
 await page.evaluate(()=>{
  window.__startupPaused={...__settledBase,phase:'paused',resourcePauseReason:'startup-protection',unavailable:3,statusRevision:907};
  window.__startupCommandBase=__downloadCommands.length;
  renderMediaDownloads(__startupPaused,{currentSnapshot:true});
 });
 const startupLabel=await page.locator('#media-download-label').textContent();
 check(startupLabel==='本轮已处理 · 71 / 71 · 3 个已跳过，不影响其他游戏','settled status ignores startup protection even when the native phase is paused; actual: '+startupLabel);
 await page.clock.runFor(60001);
 check(await page.evaluate(()=>!__downloadCommands.slice(__startupCommandBase).some(x=>x.command.startsWith('mediaDownload'))),'settled paused status does not start polling or downloading');
 await page.evaluate(()=>renderMediaDownloads({...__startupPaused,completed:70,remaining:1,nextDueAt:Date.now()+60000,statusRevision:908},{currentSnapshot:true}));
 const pendingStartupLabel=await page.locator('#media-download-label').textContent();
 check(pendingStartupLabel.includes('启动保护')&&!pendingStartupLabel.includes('本轮已处理'),'startup protection still displays when a job is scheduled to retry');
 await page.evaluate(()=>{
  window.__manualFocused=true;document.hasFocus=()=>__manualFocused;document.querySelector('#library-view').classList.remove('hidden');
  window.__manualRevision=1000;window.__manualBase={enabled:true,phase:'pending',manualState:'requested',manualUntil:Date.now()+60000,inventoryRequestId:'manual-current',inventoryReadyId:'old',inventoryAwaitUntil:Date.now()+10000,total:49,completed:49,remaining:0,workerPid:0,activeCount:0,statusRevision:++__manualRevision};
  renderMediaDownloads({...__manualBase,manualUntil:0,statusRevision:++__manualRevision},{currentSnapshot:true});
  window.__state={...__manualBase,statusRevision:++__manualRevision};
 });
 await page.locator('#media-download-now').click();await page.evaluate(()=>Promise.resolve());
 check((await page.locator('#media-download-now').textContent()).includes('停止本轮补齐'),'accepted manual request changes the actual button to stop');
 check((await page.locator('#media-download-label').textContent()).includes('手动补齐中'),'accepted manual request shows preparation instead of the previous completion');
 for(const reason of ['cpu-busy','cpu-unknown','cpu-recovering']){
  await page.evaluate(reason=>renderMediaDownloads({...__manualBase,resourcePauseReason:reason,pauseReason:'game-running',statusRevision:++__manualRevision},{currentSnapshot:true}),reason);
  const label=await page.locator('#media-download-label').textContent();
  check(label.includes('手动补齐中')&&!label.includes('已暂停'),'manual library feedback ignores stale soft CPU pause: '+reason);
 }
 check(!(await page.locator('#media-download-label').textContent()).includes('49 / 49'),'manual preparation never borrows previous completed counters');
 check(await page.locator('#media-download-bar').getAttribute('value')===null,'manual preparation clears a previous full progress bar');
 for(const [reason,copy] of [['on-battery','电池模式'],['power-unknown','供电'],['startup-protection','启动保护']]){
  await page.evaluate(reason=>renderMediaDownloads({...__manualBase,resourcePauseReason:reason,statusRevision:++__manualRevision},{currentSnapshot:true}),reason);
  const label=await page.locator('#media-download-label').textContent();check(label.includes(copy)&&!label.includes('手动补齐中'),'manual library feedback retains hard resource protection: '+reason);
 }
 for(const reason of ['battery-saver','low-battery','media-storage-unavailable','media-disk-space-low']){
  await page.evaluate(reason=>renderMediaDownloads({...__manualBase,pauseReason:reason,statusRevision:++__manualRevision},{currentSnapshot:true}),reason);
  check(!(await page.locator('#media-download-label').textContent()).includes('手动补齐中'),'manual library feedback retains hard policy pause: '+reason);
 }
 await page.evaluate(()=>renderMediaDownloads({...__manualBase,manualUntil:0,remaining:1,resourcePauseReason:'cpu-busy',statusRevision:++__manualRevision},{currentSnapshot:true}));
 check((await page.locator('#media-download-label').textContent()).includes('CPU 占用'),'automatic library work still displays CPU pause');
 const oldLabel=await page.locator('#media-download-label').textContent();
 await page.evaluate(()=>{__manualFocused=false;window.dispatchEvent(new Event('blur'));renderMediaDownloads({...__manualBase,resourcePauseReason:'cpu-busy',statusRevision:++__manualRevision},{currentSnapshot:true});});
 check(await page.locator('#media-download-label').textContent()===oldLabel,'background status accepts state without painting manual feedback');
 await page.evaluate(()=>{__manualFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(20);
 check((await page.locator('#media-download-label').textContent()).includes('手动补齐中'),'foreground status paints the retained manual acknowledgement');
 check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadNow').length===1&&!__downloadCommands.some(x=>['mediaDownloadCancel','commit','saveEditor','mediaEdit','mediaSelectVideo','identifyOne','scan','prepare'].includes(x.command))),'manual feedback rendering never submits additional business work');
 const rows=[];
 for(const mode of ['retired-page-start','retired-page-stop','replaced-root','current-error']){
  await page.reload();await page.waitForFunction(()=>typeof renderMediaDownloads==='function');await page.clock.runFor(251);
  rows.push(await page.evaluate(async mode=>{
   const refresh=async()=>{for(let i=0;i<4;i++)await Promise.resolve();};
   document.hasFocus=()=>true;document.querySelector('#library-view').classList.remove('hidden');
   __dropManual=true;__dropStatus=true;__holdState=true;
   const idle={enabled:true,phase:'completed',completed:4,total:4,remaining:0,statusRevision:2000};
   const root='G:/FixtureLibrary/Current',button=document.querySelector('#media-download-now');
   snapshot.dataRoot=root;
   renderMediaDownloads({...idle,manualUntil:mode==='retired-page-stop'?Date.now()+60000:0},{currentSnapshot:true});
   const start=__downloadCommands.length;button.click();
   const writes=()=>__downloadCommands.slice(start).filter(x=>['mediaDownloadNow','mediaDownloadCancel'].includes(x.command));
   const command=writes()[0],expected=mode==='retired-page-stop'?'mediaDownloadCancel':'mediaDownloadNow';
   if(!button.disabled||writes().length!==1||command.command!==expected)throw Error('Manual lifecycle did not reach the requested action: '+mode);
   if(mode.startsWith('retired-page')){
    window.dispatchEvent(new PageTransitionEvent('pagehide'));
    window.dispatchEvent(new PageTransitionEvent('pageshow'));
   }else if(mode==='replaced-root')snapshot.dataRoot='G:/FixtureLibrary/Other';
   renderMediaDownloads({...idle,statusRevision:2001},{currentSnapshot:true});clearNotice(libraryNotice);
   button.dispatchEvent(new Event('click')); // Even a queued controller event must not duplicate a write.
   const error='Manual fixture result: '+mode;
   if(mode==='retired-page-start')__respond(command,{...idle,statusRevision:2002,manualUntil:Date.now()+60000});
   else __send({kind:'response',id:command.id,ok:false,error});
   await refresh();
   const result={mode,writes:writes().map(item=>item.command),statusReads:__downloadCommands.slice(start).filter(x=>x.command==='mediaDownloadStatus').length,revision:snapshot.mediaDownloads.statusRevision,disabled:button.disabled,errorVisible:!toastNotice.classList.contains('hidden')&&toastNotice.textContent.includes(error)};
   return result;
  },mode));
 }
 run.lifecycleRows=rows;
 check(rows.every(row=>row.writes.join() === (row.mode==='retired-page-stop'?'mediaDownloadCancel':'mediaDownloadNow')&&!row.disabled&&row.revision===2001&&row.statusReads===Number(row.mode==='current-error')&&row.errorVisible===(row.mode==='current-error')),'retired manual download reply affected the current page or triggered a status read');
 run.checks=checks-run.startCheck+1;run.status='PASS';
}
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
 if(selectedOrderingCase==='manual-status'){await runManualStatusCase();}else if(selectedOrderingCase){await runMediaOrderingCase(selectedOrderingCase);}else{
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
  window.__realRenderEntry=renderEntry;window.__realRenderLibrary=renderLibrary;
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
 await page.evaluate(()=>window.__retryCommandBase=__downloadCommands.length);
 await page.clock.runFor(15001);check(await page.evaluate(()=>__heldState.length===6),'failed snapshot read does not immediately retry or create a read storm');
 await page.clock.runFor(6001);check(await page.evaluate(()=>__heldState.length===7),'visible demand retries a lost snapshot without a focus change');
 await page.evaluate(()=>__respond(__heldState[6],{config:{},library:{games:[]},refreshProbe:'recovered'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='recovered'),'snapshot recovery publishes current data');
 await page.clock.runFor(60001);check(await page.evaluate(()=>__heldState.length===7),'healthy snapshot recovery stops its periodic reads');
 // The original timer must preserve fault delay through notification/focus bursts.
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>__send({kind:'response',id:__heldState[7].id,ok:false,error:'owned temporary snapshot failure'}));await page.evaluate(()=>Promise.resolve());
 await page.evaluate(()=>{for(let n=0;n<20;n++){__refreshFocused=false;window.dispatchEvent(new Event('blur'));__refreshFocused=true;window.dispatchEvent(new Event('focus'));__send({event:'game-media-changed'});}});
 await page.clock.runFor(15001);check(await page.evaluate(()=>__heldState.length===8),'focus and event bursts cannot shorten the snapshot fault delay');
 await page.evaluate(()=>{__refreshFocused=false;window.dispatchEvent(new Event('blur'));for(let n=0;n<100;n++)__send({event:'game-media-changed'});});
 await page.clock.runFor(120001);check(await page.evaluate(()=>__heldState.length===8),'background snapshot fault with repeated changes performs no native reads');
 await page.evaluate(()=>{__refreshFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===9),'restored visible demand gets one coalesced read after the retained delay expires');
 await page.evaluate(()=>__respond(__heldState[8],{config:{},library:{games:[]},refreshProbe:'background-fault-recovered'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='background-fault-recovered'),'background recovery publishes the current retained result');
 // Invalid/continuing native replies remain a read fault, never empty the cards.
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>__respond(__heldState[9],null));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.refreshProbe==='background-fault-recovered'),'invalid native snapshot retains the last usable card data');
 await page.clock.runFor(15001);check(await page.evaluate(()=>__heldState.length===10),'invalid native snapshot does not start an immediate read loop');
 await page.clock.runFor(6001);check(await page.evaluate(()=>__heldState.length===11),'invalid native snapshot receives a delayed current read');
 await page.evaluate(()=>__send({kind:'response',id:__heldState[10].id,ok:false,error:'owned continuing snapshot failure'}));await page.evaluate(()=>Promise.resolve());
 await page.clock.runFor(15001);check(await page.evaluate(()=>__heldState.length===11),'continuing fault retains one delayed owner');
 await page.clock.runFor(6001);check(await page.evaluate(()=>__heldState.length===12&&Array.from(pending.values()).filter(x=>x.command==='state').length===1),'continuing fault schedules exactly one new read with no overlap');
 await page.evaluate(()=>__respond(__heldState[11],{config:{},library:{games:[]},refreshProbe:'persistent-fault-recovered'}));await page.evaluate(()=>Promise.resolve());
 await page.clock.runFor(120001);check(await page.evaluate(()=>__heldState.length===12),'success after continuing faults returns the snapshot owner to idle');
 await page.evaluate(()=>scheduleSilentRefresh());await page.clock.runFor(251);
 await page.evaluate(()=>__send({kind:'response',id:__heldState[12].id,ok:false,error:'owned teardown fault'}));await page.evaluate(()=>Promise.resolve());
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pagehide')));await page.clock.runFor(60001);
 check(await page.evaluate(()=>__heldState.length===13),'page teardown cancels a pending fault-recovery timer');
 await page.evaluate(()=>window.dispatchEvent(new PageTransitionEvent('pageshow')));await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===14),'new visible lifecycle receives one current read');
 await page.evaluate(()=>__respond(__heldState[13],{config:{},library:{games:[]},refreshProbe:'fault-lifecycle-recovered'}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>!__downloadCommands.slice(__retryCommandBase).some(x=>['commit','saveEditor','mediaEdit','mediaImport','mediaCreate','mediaSelectVideo','mediaDownloadNow','mediaDownloadCancel','identifyOne','scan','prepare','addRoot','removeRoot'].includes(x.command))),'snapshot recovery never replays edits, manual actions or Steam writes');
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
 // Media-progress advisories negotiate independently from older library notices.
 await page.evaluate(()=>{renderEntry=window.__realRenderEntry;renderLibrary=window.__realRenderLibrary;});
 check(await page.evaluate(()=>__downloadCommands.find(x=>x.command==='startup')?.arguments?.advisoryMediaProgressProtocol===1),'startup declares support for compact media progress');
 await page.evaluate(()=>{window.__state={enabled:true,completed:48,total:48,remaining:0,statusRevision:70};renderMediaDownloads(__state);window.__mediaBase=__heldState.length;__refreshFocused=false;window.dispatchEvent(new Event('blur'));for(let n=0;n<100;n++)__send({kind:'event',event:'media-download-progress',refreshRequested:true,mediaProgressProtocol:1});});
 await page.clock.runFor(120001);
 check(await page.evaluate(()=>__heldState.length===__mediaBase),'background media advisory burst performs no state reads');
 check(await page.evaluate(()=>snapshot.mediaDownloads.completed===48),'media advisory never replaces data with an empty payload');
 await page.evaluate(()=>{__refreshFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===__mediaBase+1),'focus coalesces media advisories into one current read');
 await page.evaluate(()=>__respond(__heldState.at(-1),{config:{},library:{games:[]},mediaDownloads:{enabled:true,completed:50,total:50,remaining:0,statusRevision:71}}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.mediaDownloads.completed===50),'fresh state restores the current downloaded count');
 check((await page.locator('#media-download-label').textContent()).includes('50 / 50'),'current progress is painted after returning to foreground');
 await page.clock.runFor(60001);check(await page.evaluate(()=>__heldState.length===__mediaBase+1),'completed media advisory recovery returns to idle');
 await page.evaluate(()=>{__refreshFocused=false;window.dispatchEvent(new Event('blur'));__send({event:'media-download-progress',mediaDownloads:{enabled:true,completed:50,total:50,remaining:0,statusRevision:72}});});
 check(await page.evaluate(()=>snapshot.mediaDownloads.statusRevision===72),'old Host full media progress remains compatible in background');
 await page.evaluate(()=>{__refreshFocused=true;window.dispatchEvent(new Event('focus'));__send({event:'media-download-progress',refreshRequested:true,mediaProgressProtocol:2});});await page.clock.runFor(1001);
 check(await page.evaluate(()=>__heldState.length===__mediaBase+1),'unknown media advisory protocol does not start a read');
 await page.evaluate(()=>{for(let n=0;n<20;n++)__send({event:'media-download-progress',refreshRequested:true,mediaProgressProtocol:1});});await page.clock.runFor(251);
 check(await page.evaluate(()=>__heldState.length===__mediaBase+2),'foreground media advisory burst shares one current read');
 await page.evaluate(()=>__respond(__heldState.at(-1),{config:{},library:{games:[]},mediaDownloads:{enabled:true,completed:52,total:52,remaining:0,statusRevision:73}}));await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>snapshot.mediaDownloads.completed===52),'foreground advisory publishes the latest progress');

 // An unversioned unavailable receipt is an error overlay, never user OFF or
 // a new authority for replacing the last valid progress revision.
 await page.evaluate(()=>{
  window.__readFailure={enabled:false,recoveryRequired:true,reason:'queue-recovery-required',recoveryReason:'media-checkpoint-unreadable',phase:'deferred',completed:0,total:0,remaining:0,nextDueAt:-1};
  window.__failureStatusBase=__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length;window.__failureCommandBase=__downloadCommands.length;
  renderMediaDownloads(__readFailure);
 });
 check((await page.locator('#media-download-label').textContent()).includes('暂不可读'),'unversioned native unavailable state is visible above retained progress');
 check(await page.evaluate(()=>lastMediaDownloadDisplay.completed===52&&lastMediaDownloadDisplay.statusRevision===73),'unavailable receipt preserves the last valid progress revision');
 check(await page.evaluate(()=>document.querySelector('#media-download-bar').value===52&&document.querySelector('#media-download-bar').max===52),'unavailable receipt preserves the last valid progress bar');
 await page.evaluate(()=>renderMediaDownloads({enabled:true,completed:49,total:52,remaining:3,statusRevision:72}));
 check((await page.locator('#media-download-label').textContent()).includes('暂不可读'),'stale healthy receipt cannot clear a current read error');
 await page.evaluate(()=>{__state={enabled:true,completed:52,total:52,remaining:0,statusRevision:74};__dropStatus=false;});
 await page.clock.runFor(20001);await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__failureStatusBase+1),'visible unavailable state reuses one slow status check');
 check(await page.evaluate(()=>snapshot.mediaDownloads.completed===52&&lastMediaDownloadDisplay.statusRevision===74),'successful status check restores real progress');
 check(!(await page.locator('#media-download-label').textContent()).includes('暂不可读'),'successful status check clears the read error');
 await page.clock.runFor(60001);check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__failureStatusBase+1),'successful unavailable-state recovery returns to idle');
 await page.evaluate(()=>{renderMediaDownloads(__readFailure);__refreshFocused=false;window.dispatchEvent(new Event('blur'));});await page.clock.runFor(60001);
 check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__failureStatusBase+1),'background unavailable state performs no status reads');
 await page.evaluate(()=>{__state={...__state,statusRevision:75};__refreshFocused=true;window.dispatchEvent(new Event('focus'));});await page.clock.runFor(20001);await page.evaluate(()=>Promise.resolve());
 check(await page.evaluate(()=>__downloadCommands.filter(x=>x.command==='mediaDownloadStatus').length===__failureStatusBase+2&&snapshot.mediaDownloads.statusRevision===75),'foreground recovery resumes the original slow read owner');
 await page.evaluate(()=>{lastMediaDownloadDisplay=null;renderMediaDownloads(__readFailure);});
 check((await page.locator('#media-download-label').textContent()).includes('暂不可读')&&!(await page.locator('#media-download-label').textContent()).includes('已关闭'),'cold unavailable queue is not presented as the user disabling downloads');
 await page.evaluate(()=>renderMediaDownloads({enabled:true,completed:52,total:52,remaining:0,statusRevision:76}));
 check(await page.evaluate(()=>!__downloadCommands.slice(__failureCommandBase).some(x=>['commit','saveEditor','mediaEdit','mediaImport','mediaCreate','mediaSelectVideo','mediaDownloadNow','mediaDownloadCancel','identifyOne','scan','prepare','addRoot','removeRoot'].includes(x.command))),'unavailable-state recovery never replays a write or manual download');
 await page.evaluate(()=>{window.dispatchEvent(new PageTransitionEvent('pagehide'));for(let n=0;n<20;n++)__send({event:'media-download-progress',refreshRequested:true,mediaProgressProtocol:1});});await page.clock.runFor(60001);
 check(await page.evaluate(()=>__heldState.length===__mediaBase+2),'teardown cancels media advisory reads');
 await runMediaOrderingCase('snapshot-order');await runMediaOrderingCase('status-lifecycle');
 await runManualStatusCase();
 }
 check(errors.length===0,'actual library script has no browser exceptions');
 await page.screenshot({path:out+'/download-recovery.png'});const result={ok:true,checks,assertions,errors,selectedOrderingCase,orderingCaseRuns,manualStatusCaseRuns,scope:'actual production library HTML/CSS/JS; bounded native transport fixture and browser clock, no installed cache writes'};fs.writeFileSync(out+'/results.json',JSON.stringify(result,null,2));console.log(JSON.stringify(result));
}catch(error){await page.screenshot({path:out+'/failure.png'});fs.writeFileSync(out+'/failure.json',JSON.stringify({error:String(error),checks,assertions,errors,selectedOrderingCase,orderingCaseRuns,manualStatusCaseRuns,commands:await page.evaluate(()=>window.__downloadCommands),refreshMetrics:await page.evaluate(()=>({held:window.__heldState?.length,renders:window.__refreshMetrics}))},null,2));throw error;}finally{await browser.close();}
