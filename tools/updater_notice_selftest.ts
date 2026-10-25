import assert from 'node:assert/strict';
let receive:(event:{data:any})=>void=()=>{};
const calls:{cmd:string;args:any}[]=[];let frameAccepted=true,id=0;
// Existing native lease protocol, as used by monitor-notice-lifecycle/memory-boundary.mjs.
let leaseId=0,leaseNonce='',leaseRetired=true;
let owner:{id:number;leaseId:number;nonce:string}|null=null;
class FixtureEvent extends Event { detail:unknown;constructor(type:string,init:{detail?:unknown}){super(type);this.detail=init.detail;} }
(globalThis as any).CustomEvent=FixtureEvent;
(globalThis as any).document={documentElement:{dataset:{theme:'blue-black'}}};
const win=new EventTarget() as any;win.chrome={webview:{addEventListener(_name:string,handler:typeof receive){receive=handler;},postMessage(req:any){
 if(typeof req.id!=='number')return;calls.push(req);
 queueMicrotask(()=>{
  let result:any=true;
  if(req.cmd==='diagnostics.frontendTrace'){
   assert.equal(req.args?.where,'monitor-notice-lifecycle');const diagnostic=JSON.parse(req.args.message);
   assert.equal(typeof diagnostic.stage,'string');assert.equal(typeof diagnostic.hostId,'number');
   for(const secret of ['nonce','notification','token','error'])assert.equal(Object.hasOwn(diagnostic,secret),false);
   result={ok:true};
  }
  else if(req.cmd==='monitorNotice.status')result={id:owner?.id??0,leaseProtocol:1,available:true,ready:owner!==null,...(owner??{})};
  else if(req.cmd==='monitorNotice.reserve'){
   assert.equal(owner,null);assert.equal(leaseRetired,true);assert.equal(typeof req.args.nonce,'string');
   leaseId++;leaseNonce=req.args.nonce;leaseRetired=false;result={leaseId,nonce:leaseNonce};
  }
  else if(req.cmd==='monitorNotice.create') {
   assert.equal(leaseRetired,false);assert.equal(req.args.leaseId,leaseId);assert.equal(req.args.nonce,leaseNonce);assert.equal(owner,null);
   owner={id:++id,leaseId,nonce:leaseNonce};receive({data:{event:'monitorNotice.ready',data:{id,nonce:leaseNonce}}});result={id,nonce:leaseNonce};
  }
  else if(req.cmd==='monitorNotice.retire'){
   assert.equal(req.args.leaseId,leaseId);assert.equal(req.args.nonce,leaseNonce);
   if(owner){assert.equal(owner.leaseId,leaseId);assert.equal(owner.nonce,leaseNonce);}owner=null;leaseRetired=true;
   result={leaseRetired:true,resourcesDestroyed:true,ownedOnly:true};
  }
  else if(req.cmd==='monitorNotice.close'){
   assert.equal(req.args.hudId,owner?.id);assert.equal(req.args.nonce,owner?.nonce);const closed=owner!;owner=null;leaseRetired=true;
   receive({data:{event:'monitorNotice.closed',data:{id:closed.id,nonce:closed.nonce}}});result=true;
  }
  else if(req.cmd==='monitorNotice.frame'||req.cmd==='monitorNotice.hide'){
   assert.equal(req.args.hudId,owner?.id);assert.equal(req.args.nonce,owner?.nonce);result=req.cmd==='monitorNotice.frame'?frameAccepted:true;
  }
  else throw Error('FORBIDDEN '+req.cmd);
  receive({data:{id:req.id,result}});
 });
}}};(globalThis as any).window=win;
const {startMonitorNoticeHost}=await import('../src/bridge/monitorNoticeHost');
const {presentUpdateReady}=await import('../src/bridge/updateReadyNotice');
const {normalizeShortcutActionFeedback}=await import('../src/bridge/shortcutActionFeedback');
let checks=0;function check(label:string,fn:()=>void){fn();checks++;console.log('PASS '+label);}
const settle=async()=>{for(let i=0;i<20;i++)await Promise.resolve();};
check('no presenter cannot falsely consume a boot notice',()=>{});assert.equal(await presentUpdateReady('99.0.1'),false);
const host=startMonitorNoticeHost(),prefs={popupEnabled:true,ymcc:{opacity:.8}} as any;
host.ready(prefs);await settle();
assert.equal(await presentUpdateReady('99.0.1'),true);
check('ready notice uses the existing inert monitor carrier with the early-ready race handled',()=>{const frame=calls.find(x=>x.cmd==='monitorNotice.frame')!.args.frame;assert.equal(frame.feedback.origin,'updater');assert.equal(frame.feedback.actionId,'app.updateReady');assert.match(frame.feedback.message,/99.0.1/);assert.equal(frame.feedback.expiresAtUnixMs-frame.feedback.occurredAtUnixMs,8000);assert.ok(normalizeShortcutActionFeedback(frame.feedback));});
assert.equal(await presentUpdateReady('99.0.1'),false);check('busy popup does not replace an existing receipt',()=>assert.equal(calls.filter(x=>x.cmd==='monitorNotice.frame').length,1));
receive({data:{event:'power.resumed',data:{}}});frameAccepted=false;
assert.equal(await presentUpdateReady('99.0.1'),false);check('rejected native presentation never acknowledges or strands the popup',()=>assert.equal(host.inspect().state.current,null));
frameAccepted=true;assert.equal(await presentUpdateReady('99.0.1'),true);
check('carrier recovery permits a later successful notice',()=>assert.ok(host.inspect().state.current));
host.update({...prefs,popupEnabled:false});await settle();assert.equal(await presentUpdateReady('99.0.1'),false);
check('user popup preference is honored without enabling the HUD',()=>assert.equal(host.inspect().enabled,false));
const previous=calls.find(x=>x.cmd==='monitorNotice.frame')!.args.frame.feedback;
check('updater origin cannot masquerade as a hardware shortcut result',()=>assert.equal(normalizeShortcutActionFeedback({...previous,actionId:'tdp.apply',state:'applied'}),null));
host.stop();await settle();assert.equal(await presentUpdateReady('99.0.1'),false);
check('host disposal releases the notification seam and timers',()=>{assert.equal(host.inspect().state.timerPending,false);assert.equal(host.inspect().cleanup.pending,0);assert.equal(host.inspect().cleanup.retirementFailures,0);assert.equal(owner,null);assert.equal(leaseRetired,true);assert.equal(host.inspect().diagnostics.failedDeliveries,0);});
console.log('UPDATER_NOTICE_SELFTEST_OK checks='+checks+' (mock IPC only; no actual popup)');
