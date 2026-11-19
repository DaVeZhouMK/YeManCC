import assert from 'node:assert/strict';
import {APP_VERSION} from '../../../../src/version';
import {installUpdaterFixtureEnvironment} from '../../../lib/updater_fixture_environment';
const mode=process.argv[2]||'unknown-post';
if(!['unknown-post','idle-resync','activity-rejected','version-fence','power-admission'].includes(mode))throw Error('UNKNOWN_UPDATE_MEMORY_MODE');
const vPower=(n:number)=>{const a=APP_VERSION.split('.').map(Number);return `${a[0]}.${a[1]}.${a[2]+n}`;};let power:any={phase:'ready',generation:61,known:true,inputReady:true,hardwareWritesAllowed:true,notificationPending:false,publicationRevision:1,observedNotificationSequence:1},holdActivity=false;const activities:any[]=[];
const p=APP_VERSION.split('.').map(Number),newer=`${p[0]}.${p[1]}.${p[2]+1}`,manifest={version:newer,sha256:'C'.repeat(64)};
let now=1e9,timerId=0,receive:(event:any)=>void=()=>{};
const timers=new Map<number,{at:number;fn:()=>unknown}>(),calls:any[]=[],checks:any[]=[];
let native:any=mode==='version-fence'?{phase:'downloaded',version:APP_VERSION,sha256:manifest.sha256,downloadActive:false}:{phase:'idle',downloadActive:false};let pending:any=null;
let holdManifest=false,failManifest=false;const manifestRequests:any[]=[];
const game={known:true,game:null,listeners:new Set<()=>void>()};(globalThis as any).__updaterGameFixture=game;
const realSet=globalThis.setTimeout,realClear=globalThis.clearTimeout,realNow=Date.now;
(globalThis as any).setTimeout=(fn:any,ms:number)=>{timers.set(++timerId,{at:now+ms,fn});return timerId;};(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);Date.now=()=>now;
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{onLine:true}});
(globalThis as any).CustomEvent=class extends Event {detail:any;constructor(name:string,init:any){super(name);this.detail=init.detail;}};
const win:any=new EventTarget();const policy={enabled:true,bootId:'fixture-boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:0};
win.chrome={webview:{addEventListener(_type:string,listener:any){receive=listener;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{const ok=(result:any)=>receive({data:{id:req.id,result}});switch(req.cmd){
case 'power.lifecycle':ok({...power});break;
case 'app.updateState':ok({...native});break;
case 'app.checkUpdate':if(holdManifest&&req.args.background){manifestRequests.push(req);break;}if(failManifest){receive({data:{id:req.id,error:'previous manifest check failed'}});break;}ok(mode==='version-fence'?{...manifest,version:APP_VERSION}:manifest);break;
case 'app.downloadUpdate':if(mode==='power-admission'&&req.args.cacheOnly){receive({data:{id:req.id,error:'Verified cached update package is unavailable'}});break;}pending=req;native={...req.args,phase:'downloading',stage:'download',downloadActive:true,activeDownloadOperation:req.args.operationId};break;
case 'app.retireUpdateDownload':ok({retired:false,active:true,activeOperation:native.activeDownloadOperation||''});break;
case 'app.cancelUpdateDownload':if(mode==='unknown-post')return;ok(true);if(mode==='power-admission'&&pending?.args.background){const old=pending;pending=null;native={...native,phase:'interrupted',downloadActive:false,activeDownloadOperation:''};receive({data:{id:old.id,error:'Background download paused'}});}break;
case 'app.updatePredownloadActivity':if(mode==='power-admission'&&holdActivity&&req.args.idle){activities.push(req);break;}ok(mode==='activity-rejected'?false:true);break;
case 'app.updatePredownloadRead':ok({...policy});break;
case 'app.updatePredownloadSchedule':Object.assign(policy,req.args);ok(true);break;
case 'app.updatePredownloadNotified':ok(true);break;
default:throw Error('FORBIDDEN_NATIVE:'+req.cmd);
}});}}};(globalThis as any).window=win;
{const rawPost=win.chrome.webview.postMessage;installUpdaterFixtureEnvironment(win,event=>receive(event));win.chrome.webview.postMessage=rawPost;}
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
async function advance(ms:number){now+=ms;for(let i=0;i<100;i++){const due=[...timers].filter(([,value])=>value.at<=now);if(!due.length)break;for(const[id,timer]of due){timers.delete(id);void timer.fn();}await flush();}}
function check(id:string,input:any,state:any){checks.push({id,status:'PASS',inputConsumerStateFeedback:{source:'source-derived-fixture',input,consumer:'formal update service/scheduler/provider/manager/yeman/WebView IPC',state,feedback:{calls:[...calls],contractSatisfied:true}}});}
let service:any;
try{const manager=await import('../../../../src/bridge/updateManager');
 if(mode==='idle-resync'){
  const {startUpdatePredownload}=await import('../../../../src/bridge/updatePredownloadService');service=startUpdatePredownload();await flush();await advance(15000);await advance(1000);assert.ok(pending);const prior=calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length;win.dispatchEvent(new Event('online'));await flush();assert.equal(calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length,prior,'unchanged confirmed idle resync must not cancel a healthy transfer');check('service.idle-resync-preserves-transfer',{onlineRecoveryWhileIdle:true},{cancelRequestsBefore:prior,cancelRequestsAfter:calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length});
 }else if(mode==='power-admission'){
  const {startUpdatePredownload}=await import('../../../../src/bridge/updatePredownloadService');
  const count=(cmd:string)=>calls.filter(x=>x.cmd===cmd).length;
  const publish=(patch:any)=>{power={...power,...patch,publicationRevision:power.publicationRevision+1,observedNotificationSequence:power.observedNotificationSequence+1};receive({data:{event:'power.lifecycleChanged',data:{...power}}});};
  failManifest=true;assert.equal(await manager.checkForUpdate(APP_VERSION),false);failManifest=false;
  const priorError=manager.updateSnapshot.error,priorChecks=count('app.checkUpdate');
  assert.equal(manager.updateSnapshot.phase,'failed');assert.match(priorError,/previous manifest check failed/);
  holdManifest=true;service=startUpdatePredownload();await flush();await advance(15000);await advance(1000);
  assert.equal(manifestRequests.length,1,'initial Ready must admit one background manifest read');
  const oldManifest=manifestRequests.shift(),retiredVersion=vPower(9);
  publish({notificationPending:true});await flush();await advance(1000);
  assert.equal(count('app.downloadUpdate'),0,'pending power must not start a download');
  publish({notificationPending:false});await flush();await advance(1000);
  assert.equal(count('app.checkUpdate'),priorChecks+1,'recovery must not overlap the original check');
  holdManifest=false;receive({data:{id:oldManifest.id,result:{...manifest,version:retiredVersion}}});await flush();
  assert.notEqual(manager.updateInfo.value?.version,retiredVersion,'sleep-retired manifest changed the current target after Ready');
  assert.equal(count('app.downloadUpdate'),0,'sleep-retired manifest started a download');
  assert.equal(manager.updateSnapshot.error,priorError,'retirement did not restore the previous failed display');
  assert.equal(policy.failureCount,0,'retired manifest consumed a failure retry');
  holdManifest=true;await advance(30000);assert.equal(manifestRequests.length,1,'same-generation Ready must recheck once');
  const previousGenerationManifest=manifestRequests.shift();
  publish({generation:power.generation+1});await flush();await advance(1000);
  assert.equal(count('app.checkUpdate'),priorChecks+2,'new generation must not overlap the original check');
  holdManifest=false;receive({data:{id:previousGenerationManifest.id,error:'retired generation manifest error'}});await flush();
  assert.equal(manager.updateSnapshot.error,priorError,'older generation error replaced the current display');
  assert.equal(policy.failureCount,0,'older generation error consumed a failure retry');
  await advance(30000);assert.ok(pending,'current Ready must start a fresh admitted transfer');
  assert.equal(count('app.checkUpdate'),priorChecks+3,'recovery must run exactly one fresh check per interruption');
  assert.equal(pending.args.version,manifest.version,'recovery must select the freshly observed version');
  check('service.power-retired-manifest-rechecks-current',{sameGeneration:true,oldManifestReturnedAfterReady:true,newGenerationWithoutPending:true},{version:pending.args.version,checks:count('app.checkUpdate')});
  const initialDownloads=count('app.downloadUpdate');publish({notificationPending:true});await flush();
  assert.equal(manager.updateSnapshot.backgroundPaused,true,'same-generation pending did not pause the actual background manager');assert.equal(pending,null,'pending Native fact did not cancel the current transfer');check('service.power-pending-cancels-transfer',{sameGeneration:true,notificationPending:true},{snapshot:{...manager.updateSnapshot},downloads:initialDownloads});
  const denied=count('app.checkUpdate')+count('app.downloadUpdate');await advance(60000);assert.equal(count('app.checkUpdate')+count('app.downloadUpdate'),denied,'pending power performed network work');check('service.power-pending-keeps-network-dormant',{logicalMs:60000},{networkRequests:denied});
  holdActivity=true;publish({notificationPending:false});await flush();assert.equal(activities.length,1,'new Ready did not ask for current native activity');const delayed=activities.shift();publish({hardwareWritesAllowed:false});await flush();receive({data:{id:delayed.id,result:true}});await flush();await advance(1000);
  assert.equal(manager.updateSnapshot.backgroundPaused,true,'old idle ACK reopened the manager after native write-gate closure');assert.equal(count('app.checkUpdate')+count('app.downloadUpdate'),denied,'old idle ACK triggered work after a newer power denial');check('service.power-late-activity-ack-cannot-reopen',{oldAck:true,hardwareWritesAllowed:false},{snapshot:{...manager.updateSnapshot}});
  holdActivity=false;native={phase:'idle',downloadActive:false};manifest.version=vPower(2);publish({hardwareWritesAllowed:true,inputReady:false});await flush();await advance(1000);assert.ok(pending,'current same-generation Native Ready did not resume background work');assert.equal(pending.args.version,vPower(2),'recovered service did not select current manifest');check('service.power-same-generation-current-ready-recovers',{inputReady:false},{version:pending.args.version,downloads:count('app.downloadUpdate')});
  const cancels=count('app.cancelUpdateDownload'),network=count('app.checkUpdate')+count('app.downloadUpdate'),activity=count('app.updatePredownloadActivity');publish({inputReady:true});publish({inputReady:true});await flush();await advance(1000);assert.equal(count('app.cancelUpdateDownload'),cancels,'metadata-only Ready cancelled healthy transfer');assert.equal(count('app.checkUpdate')+count('app.downloadUpdate'),network,'metadata-only Ready duplicated business');assert.equal(count('app.updatePredownloadActivity'),activity,'input-only Ready repeated activity transaction');check('service.power-metadata-does-not-repeat',{metadataAndInputOnly:true},{cancels,network,activity});
  const previousSequence=power.observedNotificationSequence;publish({notificationPending:true});await flush();const heldNetwork=count('app.checkUpdate')+count('app.downloadUpdate');receive({data:{event:'power.lifecycleChanged',data:{...power,notificationPending:false,publicationRevision:power.publicationRevision+1,observedNotificationSequence:previousSequence}}});receive({data:{event:'power.resumed',data:{generation:power.generation}}});await flush();await advance(1000);assert.equal(count('app.checkUpdate')+count('app.downloadUpdate'),heldNetwork);assert.equal(manager.updateSnapshot.backgroundPaused,true,'out-of-order Native or legacy Ready reopened service');check('service.power-stale-native-and-legacy-ready-stay-denied',{olderSequence:true,legacyReady:true},{networkRequests:heldNetwork});
  native={phase:'idle',downloadActive:false};publish({notificationPending:false});await flush();await advance(1000);assert.ok(pending,'fresh Native Ready could not recover after stale notifications');check('service.power-current-ready-after-stale-recovers',{sameGeneration:true},{version:pending.args.version});
  const stopped=service;service=null;stopped.stop();await flush();const stoppedCalls=calls.length;publish({hardwareWritesAllowed:false});publish({hardwareWritesAllowed:true});await advance(60000);assert.equal(calls.length,stoppedCalls,'stopped service resurrected after power return');assert.equal(timers.size,0,'service stop retained a timer');assert.equal(game.listeners.size,0,'service stop retained game subscription');check('service.power-stop-releases-and-does-not-restart',{stopped:true},{timers:timers.size,calls:stoppedCalls});
 }else if(mode==='activity-rejected'){
  const {startUpdatePredownload}=await import('../../../../src/bridge/updatePredownloadService');const {updatePredownloadState}=await import('../../../../src/bridge/updatePredownload');
  service=startUpdatePredownload();await flush();await advance(15000);await advance(1000);
  assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate'||x.cmd==='app.downloadUpdate').length,0,'false activity admission cannot authorize network work');
  check('service.rejected-activity-ack',{nativeActivityReply:false},{provider:{...updatePredownloadState},networkRequests:0});
 }else if(mode==='version-fence'){
  const yeman=await import('../../../../src/bridge/yeman');await manager.ensureUpdateManager();assert.equal(manager.updateInfo.value,null);await manager.checkForUpdate(APP_VERSION,true);assert.equal(manager.updateSnapshot.phase,'latest');assert.equal(await manager.prepareUpdate({background:true}),false);
  assert.equal(calls.some(x=>x.cmd==='app.downloadUpdate'||x.cmd==='app.installUpdate'),false);
  assert.throws(()=>yeman.validateUpdateManifest({version:'../bad',sha256:manifest.sha256}));assert.throws(()=>yeman.validateUpdateManifest({version:newer,sha256:'bad'}));
  assert.match(yeman.updatePackageUrl(newer),new RegExp('/v'+newer.replaceAll('.','\\.')+'/YeManCC.zip$'));
  check('manager.export-version-fence',{APP_VERSION,savedVersion:APP_VERSION,invalidManifest:true},{snapshot:{...manager.updateSnapshot},noDownload:true});
 }else{
  await manager.ensureUpdateManager();await manager.checkForUpdate(APP_VERSION,true);const first=manager.prepareUpdate({background:true}),second=manager.prepareUpdate({background:true});assert.equal(first,second);await flush();assert.ok(pending);
  let settled=false;void first.then(()=>{settled=true;});for(let i=0;i<8;i++)await advance(15000);
  assert.equal(settled,false);assert.equal(calls.filter(x=>x.cmd==='app.downloadUpdate').length,1);await manager.checkForUpdate(APP_VERSION,true);assert.equal(calls.filter(x=>x.cmd==='app.checkUpdate').length,1);
  check('manager.unknown-post-owner',{nativeDownloadPost:'no completion',wallClockAdvanced:120000},{settled,downloadRequests:1,snapshot:{...manager.updateSnapshot}});
  const cancel=manager.cancelBackgroundUpdate().catch(error=>String(error));await flush();await advance(5000);const result=await cancel;assert.match(String(result),/timed out/);assert.equal(manager.prepareUpdate({background:true}),first);assert.equal(calls.filter(x=>x.cmd==='app.downloadUpdate').length,1);
  check('manager.cancel-timeout-no-replay',{cancelReply:'absent',timeoutMs:5000},{cancelError:result,downloadRequests:calls.filter(x=>x.cmd==='app.downloadUpdate').length});
  native={...native,phase:'downloaded',downloadActive:false,activeDownloadOperation:'',percent:100};await advance(15000);assert.equal(await first,true);
  receive({data:{id:pending.id,error:'late unknown transport failure'}});await flush();assert.equal(manager.updateSnapshot.phase,'downloaded');assert.equal(calls.some(x=>x.cmd==='app.installUpdate'),false);
  check('manager.native-proof-recovers-unknown-post',{matchingOperationNativeState:'downloaded',lateOriginalFailure:true},{snapshot:{...manager.updateSnapshot},downloadRequests:1,installed:false});
 }
}catch(error:any){checks.push({id:'memory.'+mode,status:'FAIL',inputConsumerStateFeedback:{source:'source-derived-fixture',input:{mode},consumer:'formal manager/IPC',state:'assertion-failed',feedback:{code:error.code??error.message,message:error.message,exceptionType:error.name,calls}}});}
finally{service?.stop();await flush();globalThis.setTimeout=realSet;globalThis.clearTimeout=realClear;Date.now=realNow;}
console.log('UPDATE_DOWNLOAD_RECEIPT '+JSON.stringify({schemaVersion:1,status:checks.every(x=>x.status==='PASS')?'PASS':'FAIL',checks,executedProductModules:['src/bridge/updateManager.ts','src/bridge/yeman.ts','src/bridge/ipc.ts','src/version.ts',...(['activity-rejected','idle-resync','power-admission'].includes(mode)?['src/bridge/updatePredownloadService.ts','src/bridge/updatePredownloadScheduler.ts','src/bridge/updatePredownload.ts']:[])],safety:{realNetwork:false,realNative:false,userConfigurationRead:false,installed:false}}));process.exitCode=checks.every(x=>x.status==='PASS')?0:1;

