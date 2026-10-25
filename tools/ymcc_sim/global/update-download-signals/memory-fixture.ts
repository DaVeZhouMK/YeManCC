import assert from 'node:assert/strict';
import {APP_VERSION} from '../../../../src/version';
const mode=process.argv[2]||'unknown-post';
const p=APP_VERSION.split('.').map(Number),newer=`${p[0]}.${p[1]}.${p[2]+1}`,manifest={version:newer,sha256:'C'.repeat(64)};
let now=1e9,timerId=0,receive:(event:any)=>void=()=>{};
const timers=new Map<number,{at:number;fn:()=>unknown}>(),calls:any[]=[],checks:any[]=[];
let native:any=mode==='version-fence'?{phase:'downloaded',version:APP_VERSION,sha256:manifest.sha256,downloadActive:false}:{phase:'idle',downloadActive:false};let pending:any=null;
const game={known:true,game:null,listeners:new Set<()=>void>()};(globalThis as any).__updaterGameFixture=game;
const realSet=globalThis.setTimeout,realClear=globalThis.clearTimeout,realNow=Date.now;
(globalThis as any).setTimeout=(fn:any,ms:number)=>{timers.set(++timerId,{at:now+ms,fn});return timerId;};(globalThis as any).clearTimeout=(id:number)=>timers.delete(id);Date.now=()=>now;
Object.defineProperty(globalThis,'navigator',{configurable:true,value:{onLine:true}});
(globalThis as any).CustomEvent=class extends Event {detail:any;constructor(name:string,init:any){super(name);this.detail=init.detail;}};
const win:any=new EventTarget();const policy={enabled:true,bootId:'fixture-boot',lastNotifiedBoot:'',failureCount:0,nextCheckAt:0};
win.chrome={webview:{addEventListener(_type:string,listener:any){receive=listener;},postMessage(req:any){if(typeof req.id!=='number')return;calls.push(req);queueMicrotask(()=>{const ok=(result:any)=>receive({data:{id:req.id,result}});switch(req.cmd){
case 'app.updateState':ok({...native});break;
case 'app.checkUpdate':ok(mode==='version-fence'?{...manifest,version:APP_VERSION}:manifest);break;
case 'app.downloadUpdate':pending=req;native={...req.args,phase:'downloading',stage:'download',downloadActive:true,activeDownloadOperation:req.args.operationId};break;
case 'app.retireUpdateDownload':ok({retired:false,active:true,activeOperation:native.activeDownloadOperation||''});break;
case 'app.cancelUpdateDownload':if(mode==='unknown-post')return;ok(true);break;
case 'app.updatePredownloadActivity':ok(mode==='activity-rejected'?false:true);break;
case 'app.updatePredownloadRead':ok({...policy});break;
case 'app.updatePredownloadSchedule':Object.assign(policy,req.args);ok(true);break;
case 'app.updatePredownloadNotified':ok(true);break;
default:throw Error('FORBIDDEN_NATIVE:'+req.cmd);
}});}}};(globalThis as any).window=win;
const flush=async()=>{for(let i=0;i<100;i++)await Promise.resolve();};
async function advance(ms:number){now+=ms;for(let i=0;i<100;i++){const due=[...timers].filter(([,value])=>value.at<=now);if(!due.length)break;for(const[id,timer]of due){timers.delete(id);void timer.fn();}await flush();}}
function check(id:string,input:any,state:any){checks.push({id,status:'PASS',inputConsumerStateFeedback:{source:'source-derived-fixture',input,consumer:'formal update service/scheduler/provider/manager/yeman/WebView IPC',state,feedback:{calls:[...calls],contractSatisfied:true}}});}
let service:any;
try{const manager=await import('../../../../src/bridge/updateManager');
 if(mode==='idle-resync'){
  const {startUpdatePredownload}=await import('../../../../src/bridge/updatePredownloadService');service=startUpdatePredownload();await flush();await advance(15000);await advance(1000);assert.ok(pending);const prior=calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length;win.dispatchEvent(new Event('online'));await flush();assert.equal(calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length,prior,'unchanged confirmed idle resync must not cancel a healthy transfer');check('service.idle-resync-preserves-transfer',{onlineRecoveryWhileIdle:true},{cancelRequestsBefore:prior,cancelRequestsAfter:calls.filter(x=>x.cmd==='app.cancelUpdateDownload').length});
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
}catch(error:any){checks.push({id:'memory.'+mode,status:'FAIL',inputConsumerStateFeedback:{source:'source-derived-fixture',input:{mode},consumer:'formal manager/IPC',state:'assertion-failed',feedback:{code:error.code??error.message,exceptionType:error.name,calls}}});}
finally{service?.stop();await flush();globalThis.setTimeout=realSet;globalThis.clearTimeout=realClear;Date.now=realNow;}
console.log('UPDATE_DOWNLOAD_RECEIPT '+JSON.stringify({schemaVersion:1,status:checks.every(x=>x.status==='PASS')?'PASS':'FAIL',checks,executedProductModules:['src/bridge/updateManager.ts','src/bridge/yeman.ts','src/bridge/ipc.ts','src/version.ts',...(['activity-rejected','idle-resync'].includes(mode)?['src/bridge/updatePredownloadService.ts','src/bridge/updatePredownloadScheduler.ts','src/bridge/updatePredownload.ts']:[])],safety:{realNetwork:false,realNative:false,userConfigurationRead:false,installed:false}}));process.exitCode=checks.every(x=>x.status==='PASS')?0:1;

